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
#include "Runtime/Launch/Resources/Version.h"

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

FN2CMcpHttpServerManager& FN2CMcpHttpServerManager::Get()
{
	static FN2CMcpHttpServerManager Instance;
	return Instance;
}

bool FN2CMcpHttpServerManager::StartServer(int32 Port)
{
	if (bIsServerRunning)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("MCP HTTP server is already running on port %d"), ServerPort));
		return true;
	}

	// Get the HTTP server module
	HttpServerModule = &FModuleManager::LoadModuleChecked<FHttpServerModule>(FName("HTTPServer"));
	if (!HttpServerModule)
	{
		FN2CLogger::Get().LogError(TEXT("Failed to load HTTPServer module"));
		return false;
	}

	// Create HTTP router
	HttpRouter = HttpServerModule->GetHttpRouter(Port);
	if (!HttpRouter.IsValid())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to create HTTP router for port %d"), Port));
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
	HttpRouter->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET
		| EHttpServerRequestVerbs::VERB_DELETE | EHttpServerRequestVerbs::VERB_PUT | EHttpServerRequestVerbs::VERB_PATCH, McpHandler);

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
	HttpRouter->BindRoute(FHttpPath(TEXT("/mcp/health")), EHttpServerRequestVerbs::VERB_GET, HealthHandler);

	// Start listening
	HttpServerModule->StartAllListeners();

	bIsServerRunning = true;
	ServerPort = Port;

	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP HTTP server started on port %d (HTTPServer listener; loopback unless [HTTPServer.Listeners] overrides it)"), Port), EN2CLogSeverity::Info);
	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP endpoint available at: http://localhost:%d/mcp"), Port), EN2CLogSeverity::Info);
	FN2CLogger::Get().Log(FString::Printf(TEXT("Health check available at: http://localhost:%d/mcp/health"), Port), EN2CLogSeverity::Info);

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

	if (HttpServerModule)
	{
		HttpServerModule->StopAllListeners();
		HttpRouter.Reset();
	}

	bIsServerRunning = false;
	ServerPort = -1;

	FN2CLogger::Get().Log(TEXT("MCP HTTP server stopped"), EN2CLogSeverity::Info);
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
