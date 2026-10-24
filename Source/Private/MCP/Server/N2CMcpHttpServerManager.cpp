// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CSseServer.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "Utils/N2CLogger.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "HttpPath.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Resources/N2CMcpResourceManager.h"
#include "MCP/Resources/Implementations/N2CMcpBlueprintResource.h"
#include "MCP/Prompts/N2CMcpPromptManager.h"
#include "MCP/Prompts/Implementations/N2CMcpCodeGenerationPrompt.h"
#include "Core/N2CSettings.h"
#include "Containers/Queue.h"
#include "Containers/Ticker.h"
#include "Runtime/Launch/Resources/Version.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "IPAddress.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

/** UE 5.8's HTTPServer can stream one response body (FHttpServerResponse::StreamingBodyQueue); 5.6 and 5.7 cannot */
#define N2C_MCP_HTTP_CAN_STREAM (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))

namespace N2CMcpRequestPolicy
{
	/** True when Host (an Origin's or a Host header's host part, port stripped) is a loopback name. */
	static bool IsLoopbackHost(const FString& HostAndPort)
	{
		FString Host = HostAndPort;
		if (Host.StartsWith(TEXT("[")))
		{
			int32 CloseBracket = INDEX_NONE;
			if (Host.FindChar(TEXT(']'), CloseBracket))
			{
				Host = Host.Left(CloseBracket + 1);
			}
		}
		else
		{
			FString Left, Right;
			if (Host.Split(TEXT(":"), &Left, &Right))
			{
				Host = Left;
			}
		}
		return Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase) || Host == TEXT("127.0.0.1") || Host == TEXT("[::1]");
	}

	static const FString* FirstHeader(const FHttpServerRequest& Request, const TCHAR* Name)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
			{
				return &Pair.Value[0];
			}
		}
		return nullptr;
	}

	static void Reject(const FHttpResultCallback& OnComplete, EHttpServerResponseCodes Code, const FString& Reason)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP request rejected (%d): %s"), static_cast<int32>(Code), *Reason));
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(
			FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32600,\"message\":\"%s\"}}"), *Reason),
			TEXT("application/json"));
		Response->Code = Code;
		OnComplete(MoveTemp(Response));
	}

	/**
	 * Allows only requests a local, non-browser client (or a page served from localhost) can make:
	 * Origin absent or loopback, Host loopback, and for POST a JSON body. Prevents drive-by requests from
	 * web pages and DNS rebinding. Returns false after answering the request when it is rejected.
	 */
	bool ValidateLocalRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete, bool bRequireJsonBody)
	{
		if (const FString* Origin = FirstHeader(Request, TEXT("Origin")))
		{
			FString Scheme, HostPart;
			if (!Origin->Split(TEXT("://"), &Scheme, &HostPart) || !IsLoopbackHost(HostPart))
			{
				Reject(OnComplete, EHttpServerResponseCodes::Forbidden, TEXT("Origin not allowed"));
				return false;
			}
		}
		if (const FString* Host = FirstHeader(Request, TEXT("Host")))
		{
			if (!IsLoopbackHost(*Host))
			{
				Reject(OnComplete, EHttpServerResponseCodes::Forbidden, TEXT("Host not allowed"));
				return false;
			}
		}
		if (bRequireJsonBody)
		{
			const FString* ContentType = FirstHeader(Request, TEXT("Content-Type"));
			FString MediaType = ContentType ? *ContentType : FString();
			int32 Semicolon = INDEX_NONE;
			if (MediaType.FindChar(TEXT(';'), Semicolon))
			{
				MediaType = MediaType.Left(Semicolon);
			}
			MediaType.TrimStartAndEndInline();
			if (!MediaType.Equals(TEXT("application/json"), ESearchCase::IgnoreCase))
			{
				Reject(OnComplete, EHttpServerResponseCodes::UnsupportedMedia, TEXT("Content-Type must be application/json"));
				return false;
			}
		}
		return true;
	}
}

namespace N2CMcpPorts
{
	static const TCHAR* ListenersSection = TEXT("HTTPServer.Listeners");

	/**
	 * The address HTTPServer binds a listener on Port to: [HTTPServer.Listeners] DefaultBindAddress, or the BindAddress
	 * of a ListenerOverrides entry for Port, else localhost (FHttpServerConfig::GetListenerConfig, which is private).
	 */
	static TSharedRef<FInternetAddr> ListenerAddress(ISocketSubsystem& Sockets, int32 Port)
	{
		FString BindAddress = TEXT("localhost");
		if (GConfig)
		{
			GConfig->GetString(ListenersSection, TEXT("DefaultBindAddress"), BindAddress, GEngineIni);
			TArray<FString> Overrides;
			GConfig->GetArray(ListenersSection, TEXT("ListenerOverrides"), Overrides, GEngineIni);
			for (FString Entry : Overrides)
			{
				Entry.ReplaceInline(TEXT("("), TEXT(""));
				Entry.ReplaceInline(TEXT(")"), TEXT(""));
				uint32 EntryPort = 0;
				if (FParse::Value(*Entry, TEXT("Port="), EntryPort) && EntryPort == static_cast<uint32>(Port))
				{
					FParse::Value(*Entry, TEXT("BindAddress="), BindAddress);
					break;
				}
			}
		}

		TSharedRef<FInternetAddr> Address = Sockets.CreateInternetAddr();
		if (BindAddress.Equals(TEXT("any"), ESearchCase::IgnoreCase))
		{
			Address->SetAnyAddress();
		}
		else if (BindAddress.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
		{
			Address->SetLoopbackAddress();
		}
		else
		{
			bool bValid = false;
			Address->SetIp(*BindAddress, bValid);
			if (!bValid)
			{
				Address->SetLoopbackAddress();
			}
		}
		Address->SetPort(Port);
		return Address;
	}

	/**
	 * True when nothing (in any process, this editor included) holds Port on the listener's address: a bind there,
	 * without address reuse, succeeds. True as well when no socket can be made, so HTTPServer gets to try.
	 */
	static bool IsPortFree(int32 Port)
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!Sockets)
		{
			return true;
		}
		FUniqueSocket Socket = Sockets->CreateUniqueSocket(NAME_Stream, TEXT("NodeToCodeMcpPortCheck"));
		if (!Socket)
		{
			return true;
		}
		return Socket->Bind(*ListenerAddress(*Sockets, Port));
	}

	static FString SettingPath()
	{
		return TEXT("Project Settings > Plugins > Node to Code > MCP Server > Server Port");
	}
}

FN2CMcpHttpServerManager& FN2CMcpHttpServerManager::Get()
{
	static FN2CMcpHttpServerManager Instance;
	return Instance;
}

bool FN2CMcpHttpServerManager::TryStartOnPort(int32 Port, FString& OutReason)
{
	// Checked with a bind of our own first, so a port another process holds is never handed to HTTPServer (which
	// would log an error and keep a dead listener for it). A port whose listener this server started earlier is held
	// by this editor and reused.
	if (!OwnedListenerPorts.Contains(Port) && !N2CMcpPorts::IsPortFree(Port))
	{
		OutReason = TEXT("in use by another program or plugin");
		return false;
	}

	HttpRouter = HttpServerModule->GetHttpRouter(Port, /*bFailOnBindFailure=*/true);
	if (!HttpRouter.IsValid())
	{
		OutReason = TEXT("in use (HTTPServer could not bind it)");
		return false;
	}

	// Register the MCP endpoint: POST carries messages, DELETE ends a session, and GET, PUT and PATCH get 405 from the
	// protocol core (an unbound verb would get the router's 404). The Origin and Host checks run first for every verb;
	// only POST needs a JSON body.
	FHttpRequestHandler McpHandler;
	McpHandler.BindLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
	{
		const bool bPost = Request.Verb == EHttpServerRequestVerbs::VERB_POST;
		if (!N2CMcpRequestPolicy::ValidateLocalRequest(Request, OnComplete, /*bRequireJsonBody=*/bPost))
		{
			return true;
		}
		return HandleMcpRequest(Request, OnComplete);
	});
	RouteHandles.Add(HttpRouter->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET
		| EHttpServerRequestVerbs::VERB_DELETE | EHttpServerRequestVerbs::VERB_PUT | EHttpServerRequestVerbs::VERB_PATCH, McpHandler));

	// Register health check endpoint
	FHttpRequestHandler HealthHandler;
	HealthHandler.BindLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
	{
		if (!N2CMcpRequestPolicy::ValidateLocalRequest(Request, OnComplete, /*bRequireJsonBody=*/false))
		{
			return true;
		}
		return HandleHealthRequest(Request, OnComplete);
	});
	RouteHandles.Add(HttpRouter->BindRoute(FHttpPath(TEXT("/mcp/health")), EHttpServerRequestVerbs::VERB_GET, HealthHandler));

	// BindRoute returns no handle when another plugin already bound the path on this port's router
	if (RouteHandles.ContainsByPredicate([](const FHttpRouteHandle& Handle) { return !Handle.IsValid(); }))
	{
		UnbindRoutes();
		OutReason = TEXT("used by another plugin in this editor (its /mcp route is already bound)");
		return false;
	}

	// HTTPServer starts with its listeners disabled until someone calls StartAllListeners, and has no per-listener
	// start: then GetHttpRouter returns a router that is not listening yet. Only in that case start them, as Remote
	// Control and Unreal MCP do; that changes nothing for listeners that are already up.
	if (N2CMcpPorts::IsPortFree(Port))
	{
		HttpServerModule->StartAllListeners();
		if (N2CMcpPorts::IsPortFree(Port))
		{
			UnbindRoutes();
			OutReason = TEXT("HTTPServer did not start listening on it");
			return false;
		}
	}

	OwnedListenerPorts.Add(Port);
	return true;
}

void FN2CMcpHttpServerManager::UnbindRoutes()
{
	if (HttpRouter.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			if (Handle.IsValid())
			{
				HttpRouter->UnbindRoute(Handle);
			}
		}
	}
	RouteHandles.Reset();
	HttpRouter.Reset();
}

bool FN2CMcpHttpServerManager::StartServer(int32 Port, int32 PortRange)
{
	if (bIsServerRunning)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP HTTP server is already running on port %d"), ServerPort));
		return true;
	}

	RequestedPort = Port;

	// Get the HTTP server module
	HttpServerModule = &FModuleManager::LoadModuleChecked<FHttpServerModule>(FName("HTTPServer"));
	if (!HttpServerModule)
	{
		FN2CLogger::Get().LogError(TEXT("Failed to load HTTPServer module"));
		SetStatus(TEXT("Not running: the HTTPServer module did not load"));
		return false;
	}

	const int32 FirstPort = FMath::Clamp(Port, 1, MaxServerPort);
	const int32 LastPort = FMath::Clamp(FirstPort + FMath::Max(PortRange, 1) - 1, FirstPort, MaxServerPort);
	TArray<FString> Skipped;
	for (int32 Candidate = FirstPort; Candidate <= LastPort; ++Candidate)
	{
		FString Reason;
		if (TryStartOnPort(Candidate, Reason))
		{
			ServerPort = Candidate;
			break;
		}
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP port %d is in use: %s"), Candidate, *Reason));
		Skipped.Add(FString::Printf(TEXT("%d"), Candidate));
	}

	if (ServerPort == -1)
	{
		const FString Ports = FirstPort == LastPort ? FString::Printf(TEXT("port %d is"), FirstPort)
			: FString::Printf(TEXT("ports %d-%d are all"), FirstPort, LastPort);
		Notify(FString::Printf(TEXT("NodeToCode MCP server did not start: %s in use by another program (another editor?). ")
			TEXT("Set a free port in %s, or close the program using it."), *Ports, *N2CMcpPorts::SettingPath()), /*bIsError=*/true);
		SetStatus(FString::Printf(TEXT("Not running: %s in use"), *Ports));
		return false;
	}

	bIsServerRunning = true;

	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP HTTP server started on port %d (HTTPServer listener; loopback unless [HTTPServer.Listeners] overrides it)"), ServerPort), EN2CLogSeverity::Info);
	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP endpoint available at: http://localhost:%d/mcp"), ServerPort), EN2CLogSeverity::Info);
	FN2CLogger::Get().Log(FString::Printf(TEXT("Health check available at: http://localhost:%d/mcp/health"), ServerPort), EN2CLogSeverity::Info);
	if (ServerPort != FirstPort)
	{
		Notify(FString::Printf(TEXT("NodeToCode MCP: port %d is in use (another editor?), so the MCP server is on port %d: ")
			TEXT("http://127.0.0.1:%d/mcp. Point your MCP client there, or set a free port in %s."),
			FirstPort, ServerPort, ServerPort, *N2CMcpPorts::SettingPath()), /*bIsError=*/false);
		SetStatus(FString::Printf(TEXT("Running on http://127.0.0.1:%d/mcp (port %d was in use)"), ServerPort, FirstPort));
	}
	else
	{
		SetStatus(FString::Printf(TEXT("Running on http://127.0.0.1:%d/mcp"), ServerPort));
	}

	WriteDiscoveryFiles();

	// Register tools
	RegisterMcpTools();

	// Register resources
	RegisterMcpResources();

	// Register prompts
	RegisterMcpPrompts();

	return true;
}

void FN2CMcpHttpServerManager::StopServer()
{
	if (!bIsServerRunning)
	{
		return;
	}

	// Only this server's routes: HTTPServer's listeners are shared with other plugins (Remote Control, Unreal MCP)
	UnbindRoutes();
	RemoveDiscoveryFiles();

	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP HTTP server stopped (routes on port %d unbound)"), ServerPort), EN2CLogSeverity::Info);

	bIsServerRunning = false;
	ServerPort = -1;
	SetStatus(TEXT("Stopped"));
}

bool FN2CMcpHttpServerManager::StartConfiguredServers()
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	const int32 Port = Settings ? Settings->McpServerPort : 27000;
	const int32 Range = Settings ? Settings->McpServerPortRange : DefaultPortRange;
	bConfiguredServersStarted = true;

	const bool bStarted = StartServer(Port, Range);
	if (bStarted)
	{
		FN2CLogger::Get().Log(TEXT("MCP HTTP server initialized successfully"), EN2CLogSeverity::Info);

		// Notification stream for long-running operations, on the port after the one the HTTP server got
		const int32 SsePort = ServerPort + 1;
		if (NodeToCodeSseServer::StartSseServer(SsePort))
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("SSE server started on port %d"), SsePort), EN2CLogSeverity::Info);
		}
		else
		{
			FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to start SSE server on port %d"), SsePort));
		}
	}
	else
	{
		FN2CLogger::Get().LogError(TEXT("Failed to start MCP HTTP server"), TEXT("NodeToCode"));
	}
	return bStarted;
}

void FN2CMcpHttpServerManager::StopConfiguredServers()
{
	// A restart queued by a port change must not run after the module is gone
	if (RestartTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RestartTickerHandle);
		RestartTickerHandle.Reset();
	}
	bRestartPending = false;
	NodeToCodeSseServer::StopSseServer();
	FN2CLogger::Get().Log(TEXT("SSE server stopped"), EN2CLogSeverity::Info);
	StopServer();
	bConfiguredServersStarted = false;
}

void FN2CMcpHttpServerManager::HandlePortSettingsChanged()
{
	if (!bConfiguredServersStarted)
	{
		FN2CLogger::Get().Log(TEXT("MCP server port changed; it applies the next time the server starts"), EN2CLogSeverity::Info);
		return;
	}
	if (bRestartPending)
	{
		return;
	}

	// On the next tick: the change may come from inside an MCP request (run-python setting the property), whose
	// route must not be unbound while its handler runs
	bRestartPending = true;
	RestartTickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("NodeToCodeMcpRestart"), 0.0f, [this](float)
	{
		bRestartPending = false;
		RestartTickerHandle.Reset();
		RestartConfiguredServers();
		return false;
	});
}

void FN2CMcpHttpServerManager::RestartConfiguredServers()
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	if (!bConfiguredServersStarted || !Settings)
	{
		return;
	}
	if (bIsServerRunning && RequestedPort == Settings->McpServerPort && ServerPort == Settings->McpServerPort)
	{
		return;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP server port changed to %d: restarting the MCP server"), Settings->McpServerPort), EN2CLogSeverity::Info);
	StopConfiguredServers();
	if (StartConfiguredServers() && ServerPort == Settings->McpServerPort)
	{
		// Not on the asked port, or not running: StartServer has already said so
		Notify(FString::Printf(TEXT("NodeToCode MCP server restarted on http://127.0.0.1:%d/mcp. Update your MCP client's URL."), ServerPort),
			/*bIsError=*/false, /*bLogAsWarning=*/false);
	}
}

FString FN2CMcpHttpServerManager::GetDiscoveryFilePath() const
{
	if (!DiscoveryFileOverride.IsEmpty())
	{
		return DiscoveryFileOverride;
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode"), TEXT("mcp-server.json")));
}

FString FN2CMcpHttpServerManager::GetDiscoveryIndexFilePath() const
{
	const FString Dir = !DiscoveryIndexDirOverride.IsEmpty() ? DiscoveryIndexDirOverride
		: FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("NodeToCode"), TEXT("servers"));
	return FPaths::Combine(Dir, FString::Printf(TEXT("%u.json"), FPlatformProcess::GetCurrentProcessId()));
}

void FN2CMcpHttpServerManager::SetDiscoveryPathsForTesting(const FString& ProjectFile, const FString& IndexDir)
{
	DiscoveryFileOverride = ProjectFile;
	DiscoveryIndexDirOverride = IndexDir;
}

void FN2CMcpHttpServerManager::SetNotificationHandlerForTesting(TFunction<void(const FString&, bool)> Handler)
{
	NotificationHandlerOverride = MoveTemp(Handler);
}

void FN2CMcpHttpServerManager::WriteDiscoveryFiles() const
{
	const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("server"), TEXT("NodeToCode"));
	Root->SetNumberField(TEXT("port"), ServerPort);
	Root->SetNumberField(TEXT("requestedPort"), RequestedPort);
	Root->SetStringField(TEXT("url"), FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), ServerPort));
	Root->SetNumberField(TEXT("pid"), Pid);
	Root->SetStringField(TEXT("projectFile"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	Root->SetStringField(TEXT("projectName"), FApp::GetProjectName());
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	FString PluginVersion;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode")))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}
	Root->SetStringField(TEXT("pluginVersion"), PluginVersion);
	TArray<TSharedPtr<FJsonValue>> Versions;
	for (const FString& Version : FN2CMcpProtocol::GetSupportedProtocolVersions())
	{
		Versions.Add(MakeShared<FJsonValueString>(Version));
	}
	Root->SetArrayField(TEXT("protocolVersions"), Versions);
	Root->SetStringField(TEXT("startedAt"), FDateTime::UtcNow().ToIso8601());

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ProjectFile = GetDiscoveryFilePath();
	const FString IndexFile = GetDiscoveryIndexFilePath();
	for (const FString& Path : { ProjectFile, IndexFile })
	{
		if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Could not write the MCP discovery file %s"), *Path));
		}
	}

	// Drop index entries of editors that are gone (crashed without removing theirs)
	TArray<FString> Entries;
	const FString IndexDir = FPaths::GetPath(IndexFile);
	IFileManager::Get().FindFiles(Entries, *FPaths::Combine(IndexDir, TEXT("*.json")), /*Files=*/true, /*Directories=*/false);
	for (const FString& Entry : Entries)
	{
		const FString Base = FPaths::GetBaseFilename(Entry);
		if (!Base.IsNumeric())
		{
			continue;
		}
		const uint32 EntryPid = static_cast<uint32>(FCString::Atoi64(*Base));
		if (EntryPid != Pid && !FPlatformProcess::IsApplicationRunning(EntryPid))
		{
			IFileManager::Get().Delete(*FPaths::Combine(IndexDir, Entry), /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
		}
	}
}

void FN2CMcpHttpServerManager::RemoveDiscoveryFiles() const
{
	IFileManager::Get().Delete(*GetDiscoveryIndexFilePath(), /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);

	// Another editor on the same project may have written the project file since; leave its file alone
	const FString ProjectFile = GetDiscoveryFilePath();
	FString Json;
	if (FFileHelper::LoadFileToString(Json, *ProjectFile))
	{
		TSharedPtr<FJsonObject> Root;
		double Pid = 0;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid()
			&& Root->TryGetNumberField(TEXT("pid"), Pid) && static_cast<uint32>(Pid) != FPlatformProcess::GetCurrentProcessId())
		{
			return;
		}
		IFileManager::Get().Delete(*ProjectFile, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
	}
}

void FN2CMcpHttpServerManager::Notify(const FString& Message, bool bIsError, bool bLogAsWarning)
{
	if (bIsError)
	{
		FN2CLogger::Get().LogError(Message, TEXT("MCP"));
	}
	else if (bLogAsWarning)
	{
		FN2CLogger::Get().LogWarning(Message, TEXT("MCP"));
	}
	else
	{
		FN2CLogger::Get().Log(Message, EN2CLogSeverity::Info, TEXT("MCP"));
	}

	if (NotificationHandlerOverride)
	{
		NotificationHandlerOverride(Message, bIsError);
		return;
	}
	if (!FSlateApplication::IsInitialized() || IsRunningCommandlet() || IsEngineExitRequested())
	{
		return;
	}
	FNotificationInfo Info(FText::FromString(Message));
	Info.bFireAndForget = true;
	Info.ExpireDuration = bIsError ? 30.0f : 15.0f;
	Info.bUseLargeFont = false;
	Info.Image = FCoreStyle::Get().GetBrush(bIsError ? TEXT("MessageLog.Error") : bLogAsWarning ? TEXT("MessageLog.Warning") : TEXT("MessageLog.Note"));
	Info.HyperlinkText = FText::FromString(TEXT("Open Node to Code settings"));
	Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
	{
		FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("N2CSettings"));
	});
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bIsError ? SNotificationItem::CS_Fail : SNotificationItem::CS_None);
	}
}

void FN2CMcpHttpServerManager::SetStatus(const FString& InStatus)
{
	StatusText = InStatus;
	if (!GExitPurge && UObjectInitialized())
	{
		if (UN2CSettings* Settings = GetMutableDefault<UN2CSettings>())
		{
			Settings->McpServerStatus = InStatus;
		}
	}
}

namespace N2CMcpHttpTransport
{
	/**
	 * Answers one /mcp request for the protocol core through HTTPServer's completion callback. Reply sends one
	 * application/json response (no body for 202). On UE 5.8 Stream answers with one text/event-stream response whose
	 * body the connection drains from a queue: its headers go out with the first event, and the connection closes once
	 * Close marks the queue complete (the body has no Content-Length, so the close ends it). The 5.6 and 5.7
	 * HTTPServer writes one framed body per request, so there CanStream() is false and the core always replies once
	 * (holding the request open while a long-running call runs: the HTTP server sets no timeout while a handler is
	 * pending). Everything runs on the game thread, where the HTTP server also drains the queue.
	 */
	class FHttpServerResponder : public IN2CMcpResponder
	{
	public:
		explicit FHttpServerResponder(const FHttpResultCallback& InOnComplete) : OnComplete(InOnComplete) {}

		virtual bool CanStream() const override { return FN2CMcpHttpServerManager::CanStreamToolProgress(); }

		virtual void Reply(const FN2CMcpReply& Reply) override
		{
			check(IsInGameThread());
			if (bAnswered)
			{
				return;
			}
			bAnswered = true;
			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Reply.Body, TEXT("application/json"));
			Response->Code = static_cast<EHttpServerResponseCodes>(Reply.Status);
			if (!Reply.SessionId.IsEmpty())
			{
				Response->Headers.Add(TEXT("Mcp-Session-Id"), { Reply.SessionId });
			}
			for (const TPair<FString, FString>& Header : Reply.Headers)
			{
				Response->Headers.FindOrAdd(Header.Key).Add(Header.Value);
			}
			OnComplete(MoveTemp(Response));
		}

		virtual void Stream(const FString& MessageJson, const FString& SessionId) override
		{
			check(IsInGameThread());
#if N2C_MCP_HTTP_CAN_STREAM
			const FString Event = FString::Printf(TEXT("event: message\r\ndata: %s\r\n\r\n"), *MessageJson);
			const FTCHARToUTF8 Utf8(*Event);
			TArray<uint8> Bytes;
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			if (BodyQueue.IsValid())
			{
				BodyQueue->Enqueue(MoveTemp(Bytes));
				return;
			}
			if (bAnswered)
			{
				return;
			}
			// First event: open the stream with it already queued, so the connection writes the headers at once
			bAnswered = true;
			BodyQueue = MakeShared<FBodyQueue>();
			BodyComplete = MakeShared<TAtomic<bool>>(false);
			BodyQueue->Enqueue(MoveTemp(Bytes));

			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), TEXT("text/event-stream"));
			Response->Code = EHttpServerResponseCodes::Ok;
			Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
			if (!SessionId.IsEmpty())
			{
				Response->Headers.Add(TEXT("Mcp-Session-Id"), { SessionId });
			}
			Response->StreamingBodyQueue = BodyQueue;
			Response->StreamingBodyComplete = BodyComplete;
			Response->Flags = EHttpServerResponseFlags::CloseAfterWrite;
			OnComplete(MoveTemp(Response));
#else
			ensureMsgf(false, TEXT("MCP: this engine's HTTPServer cannot stream a response"));
#endif
		}

		virtual void Close() override
		{
			check(IsInGameThread());
#if N2C_MCP_HTTP_CAN_STREAM
			if (BodyComplete.IsValid())
			{
				BodyComplete->Store(true);
			}
#endif
		}

	private:
		FHttpResultCallback OnComplete;
		bool bAnswered = false;
#if N2C_MCP_HTTP_CAN_STREAM
		using FBodyQueue = TQueue<TArray<uint8>, EQueueMode::Spsc>;
		TSharedPtr<FBodyQueue> BodyQueue;
		TSharedPtr<TAtomic<bool>> BodyComplete;
#endif
	};
}

FN2CMcpHttpServerManager::FN2CMcpHttpServerManager()
	: Protocol(MakeShared<FN2CMcpProtocol>())
{
	// Server notifications (tools/list_changed) go to the clients of the port+1 notification stream
	Protocol->SetBroadcastSink([](const FString& NotificationJson)
	{
		NodeToCodeSseServer::PushNotificationToAllClients(NodeToCodeSseServer::FormatSseMessage(TEXT("notification"), NotificationJson));
	});
}

FN2CMcpHttpServerManager::~FN2CMcpHttpServerManager() = default;

bool FN2CMcpHttpServerManager::HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FN2CMcpInbound Inbound;
	switch (Request.Verb)
	{
	case EHttpServerRequestVerbs::VERB_POST:   Inbound.Verb = TEXT("POST"); break;
	case EHttpServerRequestVerbs::VERB_GET:    Inbound.Verb = TEXT("GET"); break;
	case EHttpServerRequestVerbs::VERB_DELETE: Inbound.Verb = TEXT("DELETE"); break;
	case EHttpServerRequestVerbs::VERB_PUT:    Inbound.Verb = TEXT("PUT"); break;
	case EHttpServerRequestVerbs::VERB_PATCH:  Inbound.Verb = TEXT("PATCH"); break;
	default:                                   Inbound.Verb = TEXT("OTHER"); break;
	}
	Inbound.Headers = Request.Headers;
	Inbound.Body = Request.Body;
	Protocol->Handle(Inbound, MakeShared<N2CMcpHttpTransport::FHttpServerResponder>(OnComplete));
	return true;
}

bool FN2CMcpHttpServerManager::CanStreamToolProgress()
{
	// Transport choice per engine (decision card 0085, still open; this follows the review's recommendation and the
	// spec of card 0087). UE 5.8's HTTPServer can stream one response body (FHttpServerResponse::StreamingBodyQueue),
	// so progress and the result go out as text/event-stream on the same POST. The 5.6 and 5.7 HTTPServer writes one
	// framed body per request, so there the call holds one application/json response until the task finishes and
	// sends no progress. Change this function (and FHttpServerResponder) if 0085 decides otherwise; the protocol core
	// only asks the responder whether it can stream.
#if N2C_MCP_HTTP_CAN_STREAM
	return true;
#else
	return false;
#endif
}

bool FN2CMcpHttpServerManager::HandleHealthRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Create simple health check response
	FString HealthResponse = TEXT("{\"status\":\"ok\",\"service\":\"NodeToCode MCP Server\"}");
	
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(HealthResponse, TEXT("application/json"));
	Response->Code = EHttpServerResponseCodes::Ok;

	OnComplete(MoveTemp(Response));

	FN2CLogger::Get().Log(TEXT("Health check request processed"), EN2CLogSeverity::Debug);

	return true;
}

void FN2CMcpHttpServerManager::RegisterMcpTools()
{
	FN2CLogger::Get().Log(TEXT("Registering NodeToCode MCP tools"), EN2CLogSeverity::Info);

	const UN2CSettings* Settings = GetDefault<UN2CSettings>();

	// Check Python script-only mode first (takes priority)
	if (Settings && Settings->bEnablePythonScriptOnlyMode)
	{
		// Register only essential tools for Python script-only mode
		// This mode focuses on Python scripting for UE automation - no translation tools
		TArray<FString> EssentialTools = {
			TEXT("run-python"),
			// Script management tools for building a reusable script library
			TEXT("list-python-scripts"),
			TEXT("search-python-scripts"),
			TEXT("get-python-script"),
			TEXT("get-script-functions"),  // Token-efficient function signature discovery for composition
			TEXT("save-python-script"),
			TEXT("delete-python-script")
		};

		FN2CMcpToolManager::Get().RegisterToolsByName(EssentialTools);
		FN2CLogger::Get().Log(TEXT("Python script-only mode enabled - registered essential tools only"), EN2CLogSeverity::Info);
	}
	// Check the dynamic tool discovery setting
	else if (Settings && Settings->bEnableDynamicToolDiscovery)
	{
		// Set the default toolset (only assess-needed-tools)
		FN2CMcpToolManager::Get().SetDefaultToolSet();
		FN2CLogger::Get().Log(TEXT("Dynamic tool discovery enabled - registered assess-needed-tools only"), EN2CLogSeverity::Info);
	}
	else
	{
		// Register all tools except assess-needed-tools
		FN2CMcpToolManager::Get().RegisterAllToolsExceptAssess();
		FN2CLogger::Get().Log(TEXT("Dynamic tool discovery disabled - registered all tools"), EN2CLogSeverity::Info);
	}

	FN2CLogger::Get().Log(TEXT("MCP tools registered successfully"), EN2CLogSeverity::Info);
}

void FN2CMcpHttpServerManager::RegisterMcpResources()
{
	FN2CLogger::Get().Log(TEXT("Registering NodeToCode MCP resources"), EN2CLogSeverity::Info);
	
	// Clear any existing resources first
	FN2CMcpResourceManager::Get().ClearAllResources();
	
	// Register current blueprint resource
	{
		FMcpResourceDefinition CurrentBlueprintDef;
		CurrentBlueprintDef.Uri = TEXT("nodetocode://blueprint/current");
		CurrentBlueprintDef.Name = TEXT("Current Blueprint");
		CurrentBlueprintDef.Description = TEXT("The currently focused Blueprint in N2CJSON format");
		CurrentBlueprintDef.MimeType = TEXT("application/json");
		
		auto Handler = FMcpResourceReadDelegate::CreateLambda([](const FString& Uri) -> FMcpResourceContents
		{
			FN2CMcpCurrentBlueprintResource Resource;
			return Resource.Read(Uri);
		});
		
		FN2CMcpResourceManager::Get().RegisterStaticResource(CurrentBlueprintDef, Handler, true);
	}
	
	// Register all blueprints resource
	{
		FMcpResourceDefinition AllBlueprintsDef;
		AllBlueprintsDef.Uri = TEXT("nodetocode://blueprints/all");
		AllBlueprintsDef.Name = TEXT("All Open Blueprints");
		AllBlueprintsDef.Description = TEXT("List of all currently open Blueprints");
		AllBlueprintsDef.MimeType = TEXT("application/json");
		
		auto Handler = FMcpResourceReadDelegate::CreateLambda([](const FString& Uri) -> FMcpResourceContents
		{
			FN2CMcpAllBlueprintsResource Resource;
			return Resource.Read(Uri);
		});
		
		FN2CMcpResourceManager::Get().RegisterStaticResource(AllBlueprintsDef, Handler, true);
	}
	
	// Register blueprint by name template
	{
		FMcpResourceTemplate BlueprintByNameTemplate = FN2CMcpBlueprintByNameResource::GetResourceTemplate();
		
		auto Handler = FMcpResourceTemplateHandler::CreateLambda([](const FString& Uri) -> FMcpResourceContents
		{
			FN2CMcpBlueprintByNameResource Resource;
			return Resource.Read(Uri);
		});
		
		FN2CMcpResourceManager::Get().RegisterDynamicResource(BlueprintByNameTemplate, Handler, true);
	}
	
	FN2CLogger::Get().Log(TEXT("MCP resources registered successfully"), EN2CLogSeverity::Info);
}

void FN2CMcpHttpServerManager::RegisterMcpPrompts()
{
	FN2CLogger::Get().Log(TEXT("Registering NodeToCode MCP prompts"), EN2CLogSeverity::Info);
	
	// Clear any existing prompts first
	FN2CMcpPromptManager::Get().ClearAllPrompts();
	
	// Register generate-code prompt
	{
		FN2CMcpCodeGenerationPrompt TempPrompt;
		FMcpPromptDefinition CodeGenDef = TempPrompt.GetDefinition();
		
		auto Handler = FMcpPromptGetDelegate::CreateLambda([](const FMcpPromptArguments& Arguments) -> FMcpPromptResult
		{
			FN2CMcpCodeGenerationPrompt Prompt;
			return Prompt.GetPrompt(Arguments);
		});
		
		FN2CMcpPromptManager::Get().RegisterPrompt(CodeGenDef, Handler, true);
	}
	
	// Register analyze-blueprint prompt
	{
		FN2CMcpBlueprintAnalysisPrompt TempPrompt;
		FMcpPromptDefinition AnalysisDef = TempPrompt.GetDefinition();
		
		auto Handler = FMcpPromptGetDelegate::CreateLambda([](const FMcpPromptArguments& Arguments) -> FMcpPromptResult
		{
			FN2CMcpBlueprintAnalysisPrompt Prompt;
			return Prompt.GetPrompt(Arguments);
		});
		
		FN2CMcpPromptManager::Get().RegisterPrompt(AnalysisDef, Handler, true);
	}
	
	// Register refactor-blueprint prompt
	{
		FN2CMcpRefactorPrompt TempPrompt;
		FMcpPromptDefinition RefactorDef = TempPrompt.GetDefinition();

		auto Handler = FMcpPromptGetDelegate::CreateLambda([](const FMcpPromptArguments& Arguments) -> FMcpPromptResult
		{
			FN2CMcpRefactorPrompt Prompt;
			return Prompt.GetPrompt(Arguments);
		});

		FN2CMcpPromptManager::Get().RegisterPrompt(RefactorDef, Handler, true);
	}

	// Register python-scripting prompt (Context7 + Script Management)
	{
		FN2CMcpPythonScriptingPrompt TempPrompt;
		FMcpPromptDefinition PythonScriptingDef = TempPrompt.GetDefinition();

		auto Handler = FMcpPromptGetDelegate::CreateLambda([](const FMcpPromptArguments& Arguments) -> FMcpPromptResult
		{
			FN2CMcpPythonScriptingPrompt Prompt;
			return Prompt.GetPrompt(Arguments);
		});

		FN2CMcpPromptManager::Get().RegisterPrompt(PythonScriptingDef, Handler, false);
	}

	FN2CLogger::Get().Log(TEXT("MCP prompts registered successfully"), EN2CLogSeverity::Info);
}

void FN2CMcpHttpServerManager::BroadcastNotification(const FJsonRpcNotification& Notification)
{
	Protocol->BroadcastNotification(Notification);
}
