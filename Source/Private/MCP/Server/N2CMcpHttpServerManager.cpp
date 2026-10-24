// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "MCP/Server/N2CMcpHttpRequestHandler.h"
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
#include "MCP/Server/N2CMcpSSEResponseManager.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
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

	/** True when an Accept header lists text/event-stream (the client can read an SSE response to its POST) */
	static bool AcceptsEventStream(const FHttpServerRequest& Request)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(TEXT("Accept"), ESearchCase::IgnoreCase))
			{
				for (const FString& Value : Pair.Value)
				{
					if (Value.Contains(TEXT("text/event-stream"), ESearchCase::IgnoreCase))
					{
						return true;
					}
				}
			}
		}
		return false;
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

	// Register MCP endpoint handler
	FHttpRequestHandler McpHandler;
	McpHandler.BindLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
	{
		if (!N2CMcpRequestPolicy::ValidateLocalRequest(Request, OnComplete, /*bRequireJsonBody=*/true))
		{
			return true;
		}
		return HandleMcpRequest(Request, OnComplete);
	});
	HttpRouter->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_POST, McpHandler);

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

	// Register a dummy SSE endpoint to prevent 404s from clients that auto-probe for a default SSE stream.
	// The official notification stream is advertised via the 'initialize' response's _meta.notificationUrl field.
	FHttpRequestHandler SseDummyHandler;
	SseDummyHandler.BindLambda([](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
	{
		if (!N2CMcpRequestPolicy::ValidateLocalRequest(Request, OnComplete, /*bRequireJsonBody=*/false))
		{
			return true;
		}
		FN2CLogger::Get().Log(TEXT("Received probe for default SSE endpoint at /mcp/sse. Responding with 204 No Content."), EN2CLogSeverity::Debug);
		auto Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
		Response->Code = EHttpServerResponseCodes::NoContent;

		OnComplete(MoveTemp(Response));
		return true;
	});
	HttpRouter->BindRoute(FHttpPath(TEXT("/mcp/sse")), EHttpServerRequestVerbs::VERB_GET, SseDummyHandler);

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

bool FN2CMcpHttpServerManager::HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse the body once, over exactly the bytes received (HTTPServer sizes Body to Content-Length). A batch
	// parses to an array, and trailing bytes after the root value are a parse error. The parsed value is used
	// below for the long-running check and handed to ProcessMcpRequest; null means -32700 Parse error.
	TSharedPtr<FJsonValue> JsonValue;
	FN2CMcpHttpRequestHandler::ParseRequestBody(Request.Body, JsonValue);
	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP HTTP request: %d body bytes, %s"), Request.Body.Num(),
		JsonValue.IsValid() ? (JsonValue->Type == EJson::Array ? TEXT("batch") : TEXT("single message")) : TEXT("not valid JSON")),
		EN2CLogSeverity::Debug);

	FString ClientSentSessionId;
	if (const FString* SessionIdHeader = N2CMcpRequestPolicy::FirstHeader(Request, TEXT("Mcp-Session-Id")))
	{
		ClientSentSessionId = *SessionIdHeader;
	}

	// A single tools/call of a long-running tool runs as an async task answered on this same request
	if (JsonValue.IsValid() && JsonValue->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> RequestObject = JsonValue->AsObject();
		FString Method;
		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		FString ToolName;
		if (RequestObject->TryGetStringField(TEXT("method"), Method) && Method == TEXT("tools/call")
			&& RequestObject->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj && ParamsObj->IsValid()
			&& (*ParamsObj)->TryGetStringField(TEXT("name"), ToolName) && IsLongRunningTool(ToolName))
		{
			HandleLongRunningToolCall(Request, OnComplete, RequestObject, *ParamsObj, ToolName, ClientSentSessionId);
			return true;
		}
	}

	// Everything else is answered at once
	FString ResponseBody;
	int32 StatusCode = 200;
	bool bWasInitializeCall = false;
	FN2CMcpHttpRequestHandler::ProcessMcpRequest(JsonValue, ResponseBody, StatusCode, bWasInitializeCall);

	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseBody, TEXT("application/json"));
	if (!Response.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("Failed to create FHttpServerResponse object in HandleMcpRequest."));
		return false;
	}
	Response->Code = static_cast<EHttpServerResponseCodes>(StatusCode);

	const FString ResponseSessionId = ResolveResponseSessionId(bWasInitializeCall && StatusCode == 200, ClientSentSessionId);
	if (!ResponseSessionId.IsEmpty())
	{
		Response->Headers.Add(TEXT("Mcp-Session-Id"), { ResponseSessionId });
	}

	OnComplete(MoveTemp(Response));
	return true;
}

FString FN2CMcpHttpServerManager::ResolveResponseSessionId(bool bSuccessfulInitialize, const FString& ClientSentSessionId)
{
	// Session ID handling for the HTTP transport (MCP spec 2025-03-26)
	if (bSuccessfulInitialize)
	{
		// Successful initialize - generate or reuse session ID
		if (CurrentSessionId.IsEmpty())
		{
			CurrentSessionId = FGuid::NewGuid().ToString();
		}
		FN2CLogger::Get().Log(FString::Printf(TEXT("MCP Initialize successful. Responding with Mcp-Session-Id: %s"), *CurrentSessionId), EN2CLogSeverity::Info);
		return CurrentSessionId;
	}
	if (!ClientSentSessionId.IsEmpty())
	{
		// For non-initialize requests, if client sent a session ID, echo it back if it matches our current one
		if (ClientSentSessionId == CurrentSessionId || CurrentSessionId.IsEmpty())
		{
			return ClientSentSessionId;
		}
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Mcp-Session-Id mismatch. Client: %s, Server: %s. Not adding Mcp-Session-Id to response."), *ClientSentSessionId, *CurrentSessionId));
		return FString();
	}
	if (!CurrentSessionId.IsEmpty())
	{
		// Client did not send session ID for an ongoing session
		FN2CLogger::Get().LogWarning(TEXT("Client did not send Mcp-Session-Id for an ongoing session. Responded with current session ID."));
		return CurrentSessionId;
	}
	return FString();
}

bool FN2CMcpHttpServerManager::IsLongRunningTool(const FString& ToolName)
{
	if (ToolName.IsEmpty())
	{
		return false;
	}
	for (const FMcpToolDefinition& Def : FN2CMcpToolManager::Get().GetAllToolDefinitions())
	{
		if (Def.Name == ToolName)
		{
			return Def.bIsLongRunning;
		}
	}
	return false;
}

bool FN2CMcpHttpServerManager::CanStreamToolProgress()
{
	// Transport choice per engine (decision card 0085, still open; this follows the review's recommendation and the
	// spec of card 0087). UE 5.8's HTTPServer can stream one response body (FHttpServerResponse::StreamingBodyQueue),
	// so progress and the result go out as text/event-stream on the same POST. The 5.6 and 5.7 HTTPServer writes one
	// framed body per request, so there the call holds one application/json response until the task finishes and
	// sends no progress. Change this function (and the #if in HandleLongRunningToolCall) if 0085 decides otherwise.
#if N2C_MCP_HTTP_CAN_STREAM
	return true;
#else
	return false;
#endif
}

void FN2CMcpHttpServerManager::HandleLongRunningToolCall(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete,
	const TSharedPtr<FJsonObject>& RequestObject, const TSharedPtr<FJsonObject>& Params, const FString& ToolName,
	const FString& ClientSentSessionId)
{
	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	const TSharedPtr<FJsonObject> ToolArguments = Params->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid()
		? *ArgsObj : MakeShared<FJsonObject>();

	TSharedPtr<FJsonValue> RequestId = RequestObject->TryGetField(TEXT("id"));
	if (!RequestId.IsValid())
	{
		RequestId = MakeShared<FJsonValueNull>();
	}

	// _meta.progressToken is a string or an integer (MCP). It is kept as sent, so notifications echo it with its
	// type; its string form is the key nodetocode/cancelTask finds the task by.
	TSharedPtr<FJsonValue> ClientProgressToken;
	FString ProgressTokenKey;
	const TSharedPtr<FJsonObject>* MetaObj = nullptr;
	if (Params->TryGetObjectField(TEXT("_meta"), MetaObj) && MetaObj && MetaObj->IsValid())
	{
		const TSharedPtr<FJsonValue> Token = (*MetaObj)->TryGetField(TEXT("progressToken"));
		if (Token.IsValid() && (Token->Type == EJson::String || Token->Type == EJson::Number)
			&& Token->TryGetString(ProgressTokenKey) && !ProgressTokenKey.IsEmpty())
		{
			ClientProgressToken = Token;
		}
		else if (Token.IsValid())
		{
			ProgressTokenKey.Reset();
			FN2CLogger::Get().LogWarning(TEXT("_meta.progressToken is neither a non-empty string nor a number; the call runs without progress notifications"), TEXT("MCP"));
		}
	}
	if (ProgressTokenKey.IsEmpty())
	{
		// No client token: a generated key still lets the task be found (logs, nodetocode/cancelTask)
		ProgressTokenKey = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	const FString ResponseSessionId = ResolveResponseSessionId(/*bSuccessfulInitialize=*/false, ClientSentSessionId);

	// Stream when the client can read a stream (Accept lists text/event-stream), wants progress (sent a token) and the
	// engine can stream; otherwise answer with one JSON response when the task finishes.
	const bool bStream = CanStreamToolProgress() && ClientProgressToken.IsValid() && N2CMcpRequestPolicy::AcceptsEventStream(Request);

	FN2CAsyncTaskResponder Responder;
	TUniquePtr<FHttpServerResponse> StreamResponse;
#if N2C_MCP_HTTP_CAN_STREAM
	if (bStream)
	{
		// One response whose body the connection drains from a queue: its headers go out with the first event, and
		// the connection closes once the queue is marked complete (the body has no Content-Length, so the close ends
		// it). Everything is enqueued on the game thread, where the HTTP server also drains it.
		using FBodyQueue = TQueue<TArray<uint8>, EQueueMode::Spsc>;
		const TSharedRef<FBodyQueue> BodyQueue = MakeShared<FBodyQueue>();
		const TSharedRef<TAtomic<bool>> BodyComplete = MakeShared<TAtomic<bool>>(false);

		StreamResponse = FHttpServerResponse::Create(FString(), TEXT("text/event-stream"));
		StreamResponse->Code = EHttpServerResponseCodes::Ok;
		StreamResponse->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
		if (!ResponseSessionId.IsEmpty())
		{
			StreamResponse->Headers.Add(TEXT("Mcp-Session-Id"), { ResponseSessionId });
		}
		StreamResponse->StreamingBodyQueue = BodyQueue;
		StreamResponse->StreamingBodyComplete = BodyComplete;
		StreamResponse->Flags = EHttpServerResponseFlags::CloseAfterWrite;

		auto Enqueue = [BodyQueue](const FString& Json)
		{
			const FString Event = FString::Printf(TEXT("event: message\r\ndata: %s\r\n\r\n"), *Json);
			const FTCHARToUTF8 Utf8(*Event);
			TArray<uint8> Bytes;
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			BodyQueue->Enqueue(MoveTemp(Bytes));
		};
		Responder.SendProgress = [Enqueue](const FString& NotificationJson)
		{
			check(IsInGameThread());
			Enqueue(NotificationJson);
		};
		Responder.SendResponse = [Enqueue, BodyComplete](const FString& ResponseJson)
		{
			check(IsInGameThread());
			Enqueue(ResponseJson);
			BodyComplete->Store(true);
		};
	}
	else
#endif
	{
		// Hold this request open and answer it once, when the task reports. The HTTP server sets no timeout while a
		// handler is pending, so the connection waits for as long as the task runs.
		const TSharedRef<bool> bAnswered = MakeShared<bool>(false);
		Responder.SendResponse = [OnComplete, ResponseSessionId, bAnswered](const FString& ResponseJson)
		{
			check(IsInGameThread());
			if (*bAnswered)
			{
				return;
			}
			*bAnswered = true;
			TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseJson, TEXT("application/json"));
			Response->Code = EHttpServerResponseCodes::Ok;
			if (!ResponseSessionId.IsEmpty())
			{
				Response->Headers.Add(TEXT("Mcp-Session-Id"), { ResponseSessionId });
			}
			OnComplete(MoveTemp(Response));
		};
	}

	// A task that fails inside Execute() (no Blueprint focused, no key) writes its response before LaunchTask returns:
	// into the stream's queue, or straight to OnComplete.
	const FGuid TaskId = FGuid::NewGuid();
	const FGuid LaunchedTaskId = FN2CToolAsyncTaskManager::Get().LaunchTask(TaskId, ToolName, ToolArguments,
		ProgressTokenKey, bStream ? ClientProgressToken : nullptr, CurrentSessionId, RequestId, Responder);

	if (!LaunchedTaskId.IsValid())
	{
		// Nothing was started and nothing written: answer with a JSON-RPC error
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to launch async task for tool: %s"), *ToolName));
		const FString ErrorBody = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(RequestId,
			JsonRpcErrorCodes::InternalError, FString::Printf(TEXT("Failed to start tool '%s'"), *ToolName)));
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ErrorBody, TEXT("application/json"));
		Response->Code = EHttpServerResponseCodes::Ok;
		if (!ResponseSessionId.IsEmpty())
		{
			Response->Headers.Add(TEXT("Mcp-Session-Id"), { ResponseSessionId });
		}
		OnComplete(MoveTemp(Response));
		return;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Started task %s for tool %s (progress token %s), answering %s"),
		*TaskId.ToString(), *ToolName, *ProgressTokenKey,
		bStream ? TEXT("as text/event-stream on the same request") : TEXT("with one JSON response when it finishes")),
		EN2CLogSeverity::Info);

	if (StreamResponse.IsValid())
	{
		OnComplete(MoveTemp(StreamResponse));
	}
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

void FN2CMcpHttpServerManager::RegisterClient(const FString& SessionId, TSharedPtr<IN2CMcpNotificationChannel> Channel)
{
	if (!Channel.IsValid())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register null notification channel"));
		return;
	}

	FScopeLock Lock(&ClientChannelLock);
	
	// Unregister any existing channel for this session
	if (ClientChannels.Contains(SessionId))
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Replacing existing notification channel for session: %s"), *SessionId), EN2CLogSeverity::Debug);
	}
	
	ClientChannels.Add(SessionId, Channel);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Registered notification channel for session: %s"), *SessionId), EN2CLogSeverity::Info);
}

void FN2CMcpHttpServerManager::UnregisterClient(const FString& SessionId)
{
	FScopeLock Lock(&ClientChannelLock);
	
	if (ClientChannels.Remove(SessionId) > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Unregistered notification channel for session: %s"), *SessionId), EN2CLogSeverity::Info);
	}
	
	// Also remove protocol version info
	SessionProtocolVersions.Remove(SessionId);
}

void FN2CMcpHttpServerManager::BroadcastNotification(const FJsonRpcNotification& Notification)
{
	// Serialize the notification to JSON
	FString NotificationJson = FJsonRpcUtils::SerializeNotification(Notification);
	if (NotificationJson.IsEmpty())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to serialize notification: %s"), *Notification.Method));
		return;
	}

	// Format as an SSE message with event type "notification"
	std::string SseMessage = NodeToCodeSseServer::FormatSseMessage(TEXT("notification"), NotificationJson);

	// Push to all connected notification clients
	NodeToCodeSseServer::PushNotificationToAllClients(SseMessage);

	FN2CLogger::Get().Log(FString::Printf(TEXT("Broadcast notification '%s' via SSE"), *Notification.Method), EN2CLogSeverity::Debug);
}

void FN2CMcpHttpServerManager::SendNotificationToClient(const FString& SessionId, const FJsonRpcNotification& Notification)
{
	FScopeLock Lock(&ClientChannelLock);
	
	TSharedPtr<IN2CMcpNotificationChannel>* ChannelPtr = ClientChannels.Find(SessionId);
	if (!ChannelPtr || !ChannelPtr->IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("No active notification channel for session: %s"), *SessionId));
		return;
	}
	
	TSharedPtr<IN2CMcpNotificationChannel>& Channel = *ChannelPtr;
	if (Channel->IsActive())
	{
		if (!Channel->SendNotification(Notification))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to send notification to session: %s"), *SessionId));
		}
		else
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("Sent notification '%s' to session: %s"), 
				*Notification.Method, *SessionId), EN2CLogSeverity::Debug);
		}
	}
	else
	{
		// Remove inactive channel
		ClientChannels.Remove(SessionId);
		SessionProtocolVersions.Remove(SessionId);
		FN2CLogger::Get().Log(FString::Printf(TEXT("Removed inactive notification channel for session: %s"), *SessionId), EN2CLogSeverity::Debug);
	}
}

void FN2CMcpHttpServerManager::SetSessionProtocolVersion(const FString& SessionId, const FString& ProtocolVersion)
{
	FScopeLock Lock(&ClientChannelLock);
	SessionProtocolVersions.Add(SessionId, ProtocolVersion);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Set protocol version '%s' for session: %s"), 
		*ProtocolVersion, *SessionId), EN2CLogSeverity::Debug);
}

void FN2CMcpHttpServerManager::SendAsyncTaskProgress(const FString& SessionId, const FJsonRpcNotification& ProgressNotification)
{
	// Find the SSE connection for this progress notification
	if (ProgressNotification.Params.IsValid() && ProgressNotification.Params->Type == EJson::Object)
	{
		TSharedPtr<FJsonObject> ParamsObj = ProgressNotification.Params->AsObject();
		FString ProgressToken;
		if (ParamsObj->TryGetStringField(TEXT("progressToken"), ProgressToken))
		{
			FString ConnectionId = FN2CMcpSSEResponseManager::Get().FindConnectionByProgressToken(ProgressToken);
			if (!ConnectionId.IsEmpty())
			{
				FN2CMcpSSEResponseManager::Get().SendProgressNotification(ConnectionId, ProgressNotification);
			}
			else
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("No SSE connection found for progress token: %s"), *ProgressToken));
			}
		}
	}
}

void FN2CMcpHttpServerManager::SendAsyncTaskResponse(const FGuid& TaskId, const TSharedPtr<FJsonValue>& OriginalRequestId, const FMcpToolCallResult& Result)
{
	// Find the SSE connection using the TaskId
	FString ConnectionId = FN2CMcpSSEResponseManager::Get().FindConnectionByTaskId(TaskId);
	if (ConnectionId.IsEmpty())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("No SSE connection found for task: %s"), *TaskId.ToString()));
		return;
	}

	// Create the JSON-RPC response
	FJsonRpcResponse Response;
	Response.Id = OriginalRequestId;
	
	if (Result.bIsError)
	{
		// Create error response
		FString ErrorMessage = TEXT("Tool execution failed");
		
		// Extract error message from the result content
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), ErrorMessage);
		}
		
		// Create error object
		FJsonRpcError ErrorObj(-32603, ErrorMessage);
		Response.Error = ErrorObj.ToJson();
	}
	else
	{
		// Create success response with the tool result
		Response.Result = MakeShared<FJsonValueObject>(Result.ToJson());
	}

	// Send the final response and close the SSE connection
	FN2CMcpSSEResponseManager::Get().SendFinalResponse(ConnectionId, Response);
}
