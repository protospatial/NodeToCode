// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Server/N2CMcpProtocol.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Interfaces/IPluginManager.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Resources/N2CMcpResourceManager.h"
#include "MCP/Prompts/N2CMcpPromptManager.h"
#include "MCP/Validation/N2CMcpRequestValidator.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Server/N2CSseServer.h"

// ----------------------------------------------------------------------------------------------------------------
// Inbound message
// ----------------------------------------------------------------------------------------------------------------

const FString* FN2CMcpInbound::FindHeader(const TCHAR* Name) const
{
	for (const TPair<FString, TArray<FString>>& Pair : Headers)
	{
		if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
		{
			return &Pair.Value[0];
		}
	}
	return nullptr;
}

bool FN2CMcpInbound::AcceptsEventStream() const
{
	for (const TPair<FString, TArray<FString>>& Pair : Headers)
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

FN2CMcpCatalogs FN2CMcpCatalogs::Default()
{
	FN2CMcpCatalogs Catalogs;
	Catalogs.Tools = &FN2CMcpToolManager::Get();
	Catalogs.Resources = &FN2CMcpResourceManager::Get();
	Catalogs.Prompts = &FN2CMcpPromptManager::Get();
	Catalogs.Tasks = &FN2CToolAsyncTaskManager::Get();
	return Catalogs;
}

// ----------------------------------------------------------------------------------------------------------------
// Core
// ----------------------------------------------------------------------------------------------------------------

const TArray<FString>& FN2CMcpProtocol::GetSupportedProtocolVersions()
{
	// In order of preference (newest first)
	static const TArray<FString> Versions = {
		TEXT("2025-03-26"),
		TEXT("2024-11-05")
	};
	return Versions;
}

FN2CMcpProtocol::FN2CMcpProtocol(const FN2CMcpCatalogs& InCatalogs)
	: Catalogs(InCatalogs)
{
	check(Catalogs.Tools && Catalogs.Resources && Catalogs.Prompts && Catalogs.Tasks);

	// The tool list changes when assess-needed-tools activates categories: tell clients through the broadcast sink
	ToolListChangedHandle = Catalogs.Tools->OnToolListChanged().AddLambda([this]()
	{
		FJsonRpcNotification Notification;
		Notification.Method = TEXT("notifications/tools/list_changed");
		Notification.Params = MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
		BroadcastNotification(Notification);
	});
}

FN2CMcpProtocol::~FN2CMcpProtocol()
{
	Catalogs.Tools->OnToolListChanged().Remove(ToolListChangedHandle);
}

void FN2CMcpProtocol::BroadcastNotification(const FJsonRpcNotification& Notification)
{
	const FString NotificationJson = FJsonRpcUtils::SerializeNotification(Notification);
	if (NotificationJson.IsEmpty())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to serialize notification: %s"), *Notification.Method));
		return;
	}
	if (!BroadcastSink)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("No notification channel; '%s' not sent"), *Notification.Method), EN2CLogSeverity::Debug);
		return;
	}
	BroadcastSink(NotificationJson);
	FN2CLogger::Get().Log(FString::Printf(TEXT("Broadcast notification '%s'"), *Notification.Method), EN2CLogSeverity::Debug);
}

FString FN2CMcpProtocol::GetSessionProtocolVersion(const FString& SessionId) const
{
	const FString* Version = SessionProtocolVersions.Find(SessionId);
	return Version ? *Version : FString();
}

void FN2CMcpProtocol::Handle(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& Responder)
{
	if (!Inbound.Verb.Equals(TEXT("POST"), ESearchCase::IgnoreCase))
	{
		FN2CMcpReply Reply;
		Reply.Status = 405;
		Reply.Body = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(nullptr, JsonRpcErrorCodes::InvalidRequest,
			FString::Printf(TEXT("Method %s not allowed; send JSON-RPC messages with POST"), *Inbound.Verb)));
		Responder->Reply(Reply);
		return;
	}

	// Parse the body once, over exactly the bytes received. A batch parses to an array, and trailing bytes after the
	// root value are a parse error. The parsed value is used for the long-running check and dispatch; null means
	// -32700 Parse error.
	TSharedPtr<FJsonValue> JsonValue;
	ParseBody(Inbound.Body, JsonValue);
	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP request: %d body bytes, %s"), Inbound.Body.Num(),
		JsonValue.IsValid() ? (JsonValue->Type == EJson::Array ? TEXT("batch") : TEXT("single message")) : TEXT("not valid JSON")),
		EN2CLogSeverity::Debug);

	const FString* SessionHeader = Inbound.FindHeader(TEXT("Mcp-Session-Id"));
	const FString ClientSentSessionId = SessionHeader ? *SessionHeader : FString();

	// A single tools/call of a long-running tool runs as a call answered on this same request
	if (JsonValue.IsValid() && JsonValue->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> RequestObject = JsonValue->AsObject();
		FString Method;
		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		FString ToolName;
		if (FJsonRpcUtils::GetMessageType(RequestObject) == FJsonRpcUtils::EJsonRpcMessageType::Request
			&& RequestObject->TryGetStringField(TEXT("method"), Method) && Method == TEXT("tools/call")
			&& RequestObject->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj && ParamsObj->IsValid()
			&& (*ParamsObj)->TryGetStringField(TEXT("name"), ToolName) && IsLongRunningTool(ToolName))
		{
			HandleLongRunningToolCall(Inbound, Responder, RequestObject, *ParamsObj, ToolName, ClientSentSessionId);
			return;
		}
	}

	// Everything else is answered at once
	FN2CMcpReply Reply;
	bool bWasInitializeCall = false;
	LastNegotiatedVersion.Reset();
	ProcessMessage(JsonValue, Reply.Body, Reply.Status, bWasInitializeCall);
	const bool bSuccessfulInitialize = bWasInitializeCall && Reply.Status == 200;
	Reply.SessionId = ResolveResponseSessionId(bSuccessfulInitialize, ClientSentSessionId);
	if (bSuccessfulInitialize && !Reply.SessionId.IsEmpty() && !LastNegotiatedVersion.IsEmpty())
	{
		SessionProtocolVersions.Add(Reply.SessionId, LastNegotiatedVersion);
	}
	Responder->Reply(Reply);
}

FString FN2CMcpProtocol::ResolveResponseSessionId(bool bSuccessfulInitialize, const FString& ClientSentSessionId)
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

bool FN2CMcpProtocol::IsLongRunningTool(const FString& ToolName) const
{
	if (ToolName.IsEmpty())
	{
		return false;
	}
	for (const FMcpToolDefinition& Def : Catalogs.Tools->GetAllToolDefinitions())
	{
		if (Def.Name == ToolName)
		{
			return Def.bIsLongRunning;
		}
	}
	return false;
}

void FN2CMcpProtocol::HandleLongRunningToolCall(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& Responder,
	const TSharedPtr<FJsonObject>& RequestObject, const TSharedPtr<FJsonObject>& Params, const FString& ToolName,
	const FString& ClientSentSessionId)
{
	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	const TSharedPtr<FJsonObject> ToolArguments = Params->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid()
		? *ArgsObj : MakeShared<FJsonObject>();

	const TSharedPtr<FJsonValue> RequestId = RequestObject->TryGetField(TEXT("id"));

	// _meta.progressToken is a string or an integer (MCP). It is kept as sent, so notifications echo it with its
	// type; its string form is the key nodetocode/cancelTask finds the call by.
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
		// No client token: a generated key still lets the call be found (logs, nodetocode/cancelTask)
		ProgressTokenKey = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}

	const FString ResponseSessionId = ResolveResponseSessionId(/*bSuccessfulInitialize=*/false, ClientSentSessionId);

	// Stream when the transport can stream, the client can read a stream (Accept lists text/event-stream) and wants
	// progress (sent a token); otherwise answer with one JSON reply when the call finishes.
	const bool bStream = Responder->CanStream() && ClientProgressToken.IsValid() && Inbound.AcceptsEventStream();

	FN2CAsyncTaskResponder CallResponder;
	if (bStream)
	{
		CallResponder.SendProgress = [Responder, ResponseSessionId](const FString& NotificationJson)
		{
			check(IsInGameThread());
			Responder->Stream(NotificationJson, ResponseSessionId);
		};
		CallResponder.SendResponse = [Responder, ResponseSessionId](const FString& ResponseJson)
		{
			check(IsInGameThread());
			Responder->Stream(ResponseJson, ResponseSessionId);
			Responder->Close();
		};
	}
	else
	{
		// The transport holds the request open and answers it once, when the call reports
		CallResponder.SendResponse = [Responder, ResponseSessionId](const FString& ResponseJson)
		{
			check(IsInGameThread());
			FN2CMcpReply Reply;
			Reply.Body = ResponseJson;
			Reply.SessionId = ResponseSessionId;
			Responder->Reply(Reply);
		};
	}

	// A call that fails inside InvokeAsync (no Blueprint focused, no key) writes its response before LaunchTask returns
	const FGuid TaskId = FGuid::NewGuid();
	const FGuid LaunchedTaskId = Catalogs.Tasks->LaunchTask(TaskId, ToolName, ToolArguments,
		ProgressTokenKey, bStream ? ClientProgressToken : nullptr, CurrentSessionId, RequestId, CallResponder);

	if (!LaunchedTaskId.IsValid())
	{
		// Nothing was started and nothing written: answer with a JSON-RPC error
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to launch async task for tool: %s"), *ToolName));
		FN2CMcpReply Reply;
		Reply.Body = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(RequestId,
			JsonRpcErrorCodes::InternalError, FString::Printf(TEXT("Failed to start tool '%s'"), *ToolName)));
		Reply.SessionId = ResponseSessionId;
		Responder->Reply(Reply);
		return;
	}

	// Tests/mcp_probe/live_*.py parse this line: keep "Started task ... answering ..." and "text/event-stream" (the
	// stream a transport writes is an SSE stream today)
	FN2CLogger::Get().Log(FString::Printf(TEXT("Started task %s for tool %s (progress token %s), answering %s"),
		*TaskId.ToString(), *ToolName, *ProgressTokenKey,
		bStream ? TEXT("as text/event-stream on the same request") : TEXT("with one JSON response when it finishes")),
		EN2CLogSeverity::Info);
}

bool FN2CMcpProtocol::ParseBody(const TArray<uint8>& Body, TSharedPtr<FJsonValue>& OutJsonValue)
{
	OutJsonValue.Reset();
	if (Body.Num() == 0)
	{
		return false;
	}

	// Convert exactly the bytes received (HTTPServer sizes Body to Content-Length). The JSON reader then rejects
	// anything after the root value except whitespace, so trailing garbage is a parse error.
	const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Body.GetData()), Body.Num());
	const FString BodyString(Converter.Length(), Converter.Get());

	TSharedPtr<FJsonValue> JsonValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
	if (!FJsonSerializer::Deserialize(Reader, JsonValue) || !JsonValue.IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to parse JSON-RPC request (%s): %s"),
			*Reader->GetErrorMessage(), *BodyString.Left(500)));
		return false;
	}

	OutJsonValue = JsonValue;
	return true;
}

bool FN2CMcpProtocol::ProcessMessage(const TSharedPtr<FJsonValue>& JsonValue, FString& OutResponseBody, int32& OutStatusCode, bool& OutWasInitializeCall)
{
	// Default to success
	OutStatusCode = 200;
	OutWasInitializeCall = false;

	// The body was parsed once, by ParseRequestBody; a null value means it was not valid JSON
	if (!JsonValue.IsValid())
	{
		// Create parse error response
		FJsonRpcResponse ErrorResponse = FJsonRpcUtils::CreateErrorResponse(nullptr, JsonRpcErrorCodes::ParseError, TEXT("Parse error"));
		OutResponseBody = FJsonRpcUtils::SerializeResponse(ErrorResponse);
		
		OutStatusCode = 400; // Bad Request
		return false;
	}

	// Handle batch requests (array) or single requests (object)
	if (JsonValue->Type == EJson::Array)
	{
		// Batch request - process array of requests
		const TArray<TSharedPtr<FJsonValue>>& BatchArray = JsonValue->AsArray();
		
		if (BatchArray.Num() == 0)
		{
			FN2CLogger::Get().LogWarning(TEXT("Empty batch request received"));
			FJsonRpcResponse ErrorResponse = FJsonRpcUtils::CreateErrorResponse(nullptr, JsonRpcErrorCodes::InvalidRequest, TEXT("Batch request cannot be empty"));
			OutResponseBody = FJsonRpcUtils::SerializeResponse(ErrorResponse);
			OutStatusCode = 400; // Bad Request
			return false;
		}
		
		return ProcessBatchRequest(BatchArray, OutResponseBody, OutStatusCode);
	}
	else if (JsonValue->Type == EJson::Object)
	{
		// Single request
		TSharedPtr<FJsonObject> RequestObject = JsonValue->AsObject();
		
		// Determine message type
		FJsonRpcUtils::EJsonRpcMessageType MessageType = FJsonRpcUtils::GetMessageType(RequestObject);
		
		// Log the message type for debugging
		FN2CLogger::Get().Log(FString::Printf(TEXT("Message type detected: %d"), (int32)MessageType), EN2CLogSeverity::Debug);
		
		if (MessageType == FJsonRpcUtils::EJsonRpcMessageType::Request)
		{
			FJsonRpcRequest Request(RequestObject);
			FJsonRpcResponse Response;
			
			// Check if this is an initialize request
			if (Request.Method == TEXT("initialize"))
			{
				OutWasInitializeCall = true;
			}
			
			if (ProcessRequest(Request, Response))
			{
				OutResponseBody = FJsonRpcUtils::SerializeResponse(Response);
				return true;
			}
			else
			{
				OutStatusCode = 500; // Internal Server Error
				return false;
			}
		}
		else if (MessageType == FJsonRpcUtils::EJsonRpcMessageType::Notification)
		{
			FJsonRpcNotification Notification(RequestObject);
			
			if (ProcessNotification(Notification))
			{
				// Notification - no response body
				OutResponseBody = TEXT("");
				OutStatusCode = 202; // Accepted
				return true;
			}
			else
			{
				OutStatusCode = 500; // Internal Server Error
				return false;
			}
		}
		else
		{
			// Invalid JSON-RPC message
			FN2CLogger::Get().LogWarning(TEXT("Invalid JSON-RPC message format"));
			
			FJsonRpcResponse ErrorResponse = FJsonRpcUtils::CreateErrorResponse(nullptr, JsonRpcErrorCodes::InvalidRequest, TEXT("Invalid Request"));
			OutResponseBody = FJsonRpcUtils::SerializeResponse(ErrorResponse);
			
			OutStatusCode = 400; // Bad Request
			return false;
		}
	}
	else
	{
		// Invalid JSON structure
		FN2CLogger::Get().LogWarning(TEXT("Invalid JSON-RPC request structure"));
		
		FJsonRpcResponse ErrorResponse = FJsonRpcUtils::CreateErrorResponse(nullptr, JsonRpcErrorCodes::InvalidRequest, TEXT("Invalid Request"));
		OutResponseBody = FJsonRpcUtils::SerializeResponse(ErrorResponse);
		
		OutStatusCode = 400; // Bad Request
		return false;
	}
}

bool FN2CMcpProtocol::ProcessRequest(const FJsonRpcRequest& Request, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(FString::Printf(TEXT("Processing JSON-RPC request: %s"), *Request.Method), EN2CLogSeverity::Debug);

	// Dispatch the method
	return DispatchMethod(Request.Method, Request.Params, Request.Id, OutResponse);
}

bool FN2CMcpProtocol::ProcessNotification(const FJsonRpcNotification& Notification)
{
	FN2CLogger::Get().Log(FString::Printf(TEXT("Received JSON-RPC notification: %s"), *Notification.Method), EN2CLogSeverity::Info);
	
	// Handle specific notifications
	if (Notification.Method == TEXT("notifications/initialized"))
	{
		FN2CLogger::Get().Log(TEXT("MCP connection fully established - client sent initialized notification"), EN2CLogSeverity::Info);
		// TODO: Could trigger any post-initialization setup here if needed
		return true;
	}
	
	// For other notifications, just log them
	FN2CLogger::Get().Log(FString::Printf(TEXT("Unhandled notification: %s"), *Notification.Method), EN2CLogSeverity::Debug);
	return true;
}

bool FN2CMcpProtocol::DispatchMethod(const FString& Method, const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(FString::Printf(TEXT("Dispatching JSON-RPC method: %s"), *Method), EN2CLogSeverity::Debug);

	// Handle method calls that expect responses
	if (Method == TEXT("initialize"))
	{
		return HandleInitialize(Params, Id, OutResponse);
	}
	else if (Method == TEXT("ping"))
	{
		// Simple ping response
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(Result)));
		return true;
	}
	else if (Method == TEXT("tools/list"))
	{
		return HandleToolsList(Params, Id, OutResponse);
	}
	else if (Method == TEXT("tools/call"))
	{
		return HandleToolsCall(Params, Id, OutResponse);
	}
	else if (Method == TEXT("resources/list"))
	{
		return HandleResourcesList(Params, Id, OutResponse);
	}
	else if (Method == TEXT("resources/read"))
	{
		return HandleResourcesRead(Params, Id, OutResponse);
	}
	else if (Method == TEXT("prompts/list"))
	{
		return HandlePromptsList(Params, Id, OutResponse);
	}
	else if (Method == TEXT("prompts/get"))
	{
		return HandlePromptsGet(Params, Id, OutResponse);
	}
	else if (Method == TEXT("nodetocode/cancelTask"))
	{
		return HandleCancelTask(Params, Id, OutResponse);
	}
	else
	{
		// Method not found
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("JSON-RPC method not found: %s"), *Method));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::MethodNotFound, FString::Printf(TEXT("Method '%s' not found"), *Method));
		return true;
	}
}

bool FN2CMcpProtocol::HandleInitialize(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing MCP initialize request"), EN2CLogSeverity::Info);

	// Parse InitializeClientRequestParams from the params
	if (!Params.IsValid() || Params->IsNull())
	{
		FN2CLogger::Get().LogWarning(TEXT("Initialize request missing or null params"));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, TEXT("Missing or null params for initialize"));
		return true;
	}
	
	if (Params->Type != EJson::Object)
	{
		FN2CLogger::Get().LogWarning(TEXT("Initialize request params is not an object"));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, TEXT("Params must be an object for initialize"));
		return true;
	}

	TSharedPtr<FJsonObject> ParamsObject = Params->AsObject();
	
	// Extract protocol version
	FString ClientProtocolVersion;
	if (!ParamsObject->TryGetStringField(TEXT("protocolVersion"), ClientProtocolVersion))
	{
		FN2CLogger::Get().LogWarning(TEXT("Initialize request missing protocolVersion"));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, TEXT("Missing required field: protocolVersion"));
		return true;
	}

	// Log client info if provided
	const TSharedPtr<FJsonObject>* ClientInfo = nullptr;
	if (ParamsObject->TryGetObjectField(TEXT("clientInfo"), ClientInfo) && ClientInfo->IsValid())
	{
		FString ClientName, ClientVersion;
		(*ClientInfo)->TryGetStringField(TEXT("name"), ClientName);
		(*ClientInfo)->TryGetStringField(TEXT("version"), ClientVersion);
		FN2CLogger::Get().Log(FString::Printf(TEXT("MCP Client: %s v%s"), *ClientName, *ClientVersion), EN2CLogSeverity::Info);
	}

	// Log client capabilities if provided
	const TSharedPtr<FJsonObject>* ClientCapabilities = nullptr;
	if (ParamsObject->TryGetObjectField(TEXT("capabilities"), ClientCapabilities) && ClientCapabilities->IsValid())
	{
		FN2CLogger::Get().Log(TEXT("Client capabilities received"), EN2CLogSeverity::Debug);
		// For MVP, we just acknowledge capabilities without specific handling
	}

	// Protocol version negotiation
	FString NegotiatedProtocolVersion;
	
	// First, check if we support the exact version requested
	if (GetSupportedProtocolVersions().Contains(ClientProtocolVersion))
	{
		NegotiatedProtocolVersion = ClientProtocolVersion;
		FN2CLogger::Get().Log(
			FString::Printf(TEXT("Using client's requested protocol version: %s"), 
			*NegotiatedProtocolVersion), EN2CLogSeverity::Info);
	}
	else
	{
		// We don't support their version - use our latest
		NegotiatedProtocolVersion = GetSupportedProtocolVersions()[0];
		FN2CLogger::Get().LogWarning(
			FString::Printf(TEXT("Client requested unsupported version '%s'. Negotiating to '%s'"), 
			*ClientProtocolVersion, *NegotiatedProtocolVersion));
		
		// Log all supported versions for debugging
		FString SupportedVersionsList = FString::Join(GetSupportedProtocolVersions(), TEXT(", "));
		FN2CLogger::Get().Log(
			FString::Printf(TEXT("Server supports protocol versions: %s"), 
			*SupportedVersionsList), EN2CLogSeverity::Debug);
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Protocol version negotiated: %s"), *NegotiatedProtocolVersion), EN2CLogSeverity::Info);
	LastNegotiatedVersion = NegotiatedProtocolVersion;

	// Build InitializeServerResult
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	// Set protocol version
	Result->SetStringField(TEXT("protocolVersion"), NegotiatedProtocolVersion);
	
	// Set server capabilities
	TSharedPtr<FJsonObject> ServerCapabilities = MakeShareable(new FJsonObject);
	
	// Tools capability
	TSharedPtr<FJsonObject> ToolsCapability = MakeShareable(new FJsonObject);
	ToolsCapability->SetBoolField(TEXT("listChanged"), true); // We support the tools/list_changed notification
	ServerCapabilities->SetObjectField(TEXT("tools"), ToolsCapability);
	
	// Resources capability
	TSharedPtr<FJsonObject> ResourcesCapability = MakeShareable(new FJsonObject);
	ResourcesCapability->SetBoolField(TEXT("subscribe"), false); // Subscriptions not implemented yet
	ResourcesCapability->SetBoolField(TEXT("listChanged"), false); // List change notifications not implemented yet
	ServerCapabilities->SetObjectField(TEXT("resources"), ResourcesCapability);
	
	// Prompts capability
	TSharedPtr<FJsonObject> PromptsCapability = MakeShareable(new FJsonObject);
	PromptsCapability->SetBoolField(TEXT("listChanged"), false); // List change notifications not implemented yet
	ServerCapabilities->SetObjectField(TEXT("prompts"), PromptsCapability);
	
	// Logging capability (for future use)
	TSharedPtr<FJsonObject> LoggingCapability = MakeShareable(new FJsonObject);
	ServerCapabilities->SetObjectField(TEXT("logging"), LoggingCapability);
	
	Result->SetObjectField(TEXT("capabilities"), ServerCapabilities);
	
	// Set server info
	TSharedPtr<FJsonObject> ServerInfo = MakeShareable(new FJsonObject);
	ServerInfo->SetStringField(TEXT("name"), TEXT("NodeToCodeMCPServer"));
	
	// Get plugin version dynamically
	FString PluginVersion = TEXT("Unknown");
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode")))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}
	ServerInfo->SetStringField(TEXT("version"), PluginVersion);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

	// Add _meta field with notificationUrl
	int32 SsePort = NodeToCodeSseServer::GetSseServerPort();
	if (SsePort > 0)
	{
		TSharedPtr<FJsonObject> MetaObject = MakeShareable(new FJsonObject);
		FString NotificationUrl = FString::Printf(TEXT("http://localhost:%d/mcp/notifications"), SsePort);
		MetaObject->SetStringField(TEXT("notificationUrl"), NotificationUrl);
		Result->SetObjectField(TEXT("_meta"), MetaObject);
		FN2CLogger::Get().Log(FString::Printf(TEXT("Providing notificationUrl in initialize response: %s"), *NotificationUrl), EN2CLogSeverity::Info);
	}

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(Result)));
	
	FN2CLogger::Get().Log(TEXT("MCP connection initialized successfully"), EN2CLogSeverity::Info);
	
	return true;
}

bool FN2CMcpProtocol::HandleToolsList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing MCP tools/list request"), EN2CLogSeverity::Info);

	// For MVP, we ignore pagination params (cursor) and return all tools
	// TODO: In the future, implement pagination if tool list becomes large

	// Get all registered tools from the manager
	TArray<FMcpToolDefinition> Tools = Catalogs.Tools->GetAllToolDefinitions();

	// Build the result object
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Create tools array
	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	for (const FMcpToolDefinition& Tool : Tools)
	{
		TSharedPtr<FJsonObject> ToolJson = Tool.ToJson();
		if (ToolJson.IsValid())
		{
			ToolsArray.Add(MakeShareable(new FJsonValueObject(ToolJson)));
		}
	}

	Result->SetArrayField(TEXT("tools"), ToolsArray);

	// For MVP, no pagination so no nextCursor field

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(Result)));

	FN2CLogger::Get().Log(FString::Printf(TEXT("Returned %d tools in tools/list response"), Tools.Num()), EN2CLogSeverity::Info);

	return true;
}

bool FN2CMcpProtocol::HandleToolsCall(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing MCP tools/call request"), EN2CLogSeverity::Info);

	// Validate params using the validation framework
	TSharedPtr<FJsonObject> ParamsObject;
	FString ValidationError;
	if (!FN2CMcpRequestValidator::ValidateParamsIsObject(Params, ParamsObject, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("tools/call validation failed: %s"), *ValidationError));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, ValidationError);
		return true;
	}

	// Validate tools/call specific requirements
	if (!FN2CMcpRequestValidator::ValidateToolsCallRequest(ParamsObject, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("tools/call validation failed: %s"), *ValidationError));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, ValidationError);
		return true;
	}

	// Extract validated fields
	FString ToolName;
	ParamsObject->TryGetStringField(TEXT("name"), ToolName);  // Already validated as required

	// Extract arguments (optional, already validated)
	const TSharedPtr<FJsonObject>* ArgumentsObj = nullptr;
	TSharedPtr<FJsonObject> Arguments;
	if (ParamsObject->TryGetObjectField(TEXT("arguments"), ArgumentsObj) && ArgumentsObj->IsValid())
	{
		Arguments = *ArgumentsObj;
	}
	else
	{
		// If no arguments provided, use empty object
		Arguments = MakeShareable(new FJsonObject);
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Calling tool: %s"), *ToolName), EN2CLogSeverity::Info);

	// Check if tool exists
	if (!Catalogs.Tools->IsToolRegistered(ToolName))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Tool not found: %s"), *ToolName));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::MethodNotFound, FString::Printf(TEXT("Tool not found: %s"), *ToolName));
		return true;
	}

	// TODO: Validate arguments against tool's input schema (for MVP, skip validation)

	// Execute the tool
	FMcpToolCallResult ToolResult = Catalogs.Tools->ExecuteTool(ToolName, Arguments);

	// Convert tool result to JSON
	TSharedPtr<FJsonObject> ResultJson = ToolResult.ToJson();

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(ResultJson)));

	return true;
}

bool FN2CMcpProtocol::ProcessBatchRequest(const TArray<TSharedPtr<FJsonValue>>& BatchArray, FString& OutResponseBody, int32& OutStatusCode)
{
	FN2CLogger::Get().Log(FString::Printf(TEXT("Processing JSON-RPC batch request with %d items"), BatchArray.Num()), EN2CLogSeverity::Info);
	
	TArray<TSharedPtr<FJsonValue>> ResponseArray;
	bool bHasResponses = false;
	bool bHasErrors = false;
	
	// Process each item in the batch
	for (const TSharedPtr<FJsonValue>& Item : BatchArray)
	{
		if (!Item.IsValid() || Item->Type != EJson::Object)
		{
			// Invalid item in batch - add error response
			FJsonRpcResponse ErrorResponse = FJsonRpcUtils::CreateErrorResponse(nullptr, JsonRpcErrorCodes::InvalidRequest, TEXT("Invalid item in batch"));
			ResponseArray.Add(MakeShareable(new FJsonValueObject(ErrorResponse.ToJson())));
			bHasResponses = true;
			bHasErrors = true;
			continue;
		}
		
		TSharedPtr<FJsonObject> RequestObject = Item->AsObject();
		FJsonRpcUtils::EJsonRpcMessageType MessageType = FJsonRpcUtils::GetMessageType(RequestObject);
		
		if (MessageType == FJsonRpcUtils::EJsonRpcMessageType::Request)
		{
			FJsonRpcRequest Request(RequestObject);
			FJsonRpcResponse Response;
			
			// Process the request
			if (ProcessRequest(Request, Response))
			{
				ResponseArray.Add(MakeShareable(new FJsonValueObject(Response.ToJson())));
				bHasResponses = true;
				
				// Check if this is an error response
				if (Response.Error.IsValid())
				{
					bHasErrors = true;
				}
			}
			else
			{
				// Failed to process request - add internal error
				FJsonRpcResponse ErrorResponse = FJsonRpcUtils::CreateErrorResponse(Request.Id, JsonRpcErrorCodes::InternalError, TEXT("Failed to process request"));
				ResponseArray.Add(MakeShareable(new FJsonValueObject(ErrorResponse.ToJson())));
				bHasResponses = true;
				bHasErrors = true;
			}
		}
		else if (MessageType == FJsonRpcUtils::EJsonRpcMessageType::Notification)
		{
			FJsonRpcNotification Notification(RequestObject);
			
			// Process the notification - no response added
			if (!ProcessNotification(Notification))
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to process notification in batch: %s"), *Notification.Method));
			}
			// Notifications don't contribute to the response array
		}
		else
		{
			// Invalid JSON-RPC message
			FN2CLogger::Get().LogWarning(TEXT("Invalid JSON-RPC message in batch"));
			FJsonRpcResponse ErrorResponse = FJsonRpcUtils::CreateErrorResponse(nullptr, JsonRpcErrorCodes::InvalidRequest, TEXT("Invalid JSON-RPC message"));
			ResponseArray.Add(MakeShareable(new FJsonValueObject(ErrorResponse.ToJson())));
			bHasResponses = true;
			bHasErrors = true;
		}
	}
	
	// Determine response based on what we processed
	if (bHasResponses)
	{
		// Serialize the response array
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutResponseBody);
		if (!FJsonSerializer::Serialize(ResponseArray, Writer))
		{
			FN2CLogger::Get().LogError(TEXT("Failed to serialize batch response array"));
			OutStatusCode = 500; // Internal Server Error
			return false;
		}
		
		// Use 200 for successful batch processing, even if individual requests had errors
		OutStatusCode = 200;
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("Batch request processed: %d responses, has errors: %s"), 
			ResponseArray.Num(), bHasErrors ? TEXT("true") : TEXT("false")), EN2CLogSeverity::Info);
	}
	else
	{
		// All were notifications - return 202 Accepted with no body
		OutResponseBody = TEXT("");
		OutStatusCode = 202;
		
		FN2CLogger::Get().Log(TEXT("Batch request contained only notifications - returning 202"), EN2CLogSeverity::Info);
	}
	
	return true;
}

bool FN2CMcpProtocol::HandleResourcesList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing MCP resources/list request"), EN2CLogSeverity::Info);

	// For MVP, we ignore pagination params (cursor) and return all resources
	// TODO: In the future, implement pagination if resource list becomes large

	// Get all registered resources from the manager
	TArray<FMcpResourceDefinition> Resources = Catalogs.Resources->ListResources();

	// Build the result object
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Create resources array
	TArray<TSharedPtr<FJsonValue>> ResourcesArray;
	for (const FMcpResourceDefinition& Resource : Resources)
	{
		TSharedPtr<FJsonObject> ResourceJson = Resource.ToJson();
		if (ResourceJson.IsValid())
		{
			ResourcesArray.Add(MakeShareable(new FJsonValueObject(ResourceJson)));
		}
	}

	Result->SetArrayField(TEXT("resources"), ResourcesArray);

	// Also include resource templates  
	TArray<FMcpResourceTemplate> Templates = Catalogs.Resources->ListResourceTemplates();
	if (Templates.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TemplatesArray;
		for (const FMcpResourceTemplate& Template : Templates)
		{
			TSharedPtr<FJsonObject> TemplateJson = Template.ToJson();
			if (TemplateJson.IsValid())
			{
				TemplatesArray.Add(MakeShareable(new FJsonValueObject(TemplateJson)));
			}
		}
		Result->SetArrayField(TEXT("resourceTemplates"), TemplatesArray);
	}

	// For MVP, no pagination so no nextCursor field

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(Result)));

	FN2CLogger::Get().Log(FString::Printf(TEXT("Returned %d resources and %d templates in resources/list response"), 
		Resources.Num(), Templates.Num()), EN2CLogSeverity::Info);

	return true;
}

bool FN2CMcpProtocol::HandleResourcesRead(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing MCP resources/read request"), EN2CLogSeverity::Info);

	// Validate params using the validation framework
	TSharedPtr<FJsonObject> ParamsObject;
	FString ValidationError;
	if (!FN2CMcpRequestValidator::ValidateParamsIsObject(Params, ParamsObject, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("resources/read validation failed: %s"), *ValidationError));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, ValidationError);
		return true;
	}

	// Validate resources/read specific requirements
	if (!FN2CMcpRequestValidator::ValidateResourcesReadRequest(ParamsObject, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("resources/read validation failed: %s"), *ValidationError));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, ValidationError);
		return true;
	}

	// Extract validated fields
	FString ResourceUri;
	ParamsObject->TryGetStringField(TEXT("uri"), ResourceUri);  // Already validated as required

	FN2CLogger::Get().Log(FString::Printf(TEXT("Reading resource: %s"), *ResourceUri), EN2CLogSeverity::Info);

	// Check if resource exists
	if (!Catalogs.Resources->IsResourceRegistered(ResourceUri))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Resource not found: %s"), *ResourceUri));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::MethodNotFound, FString::Printf(TEXT("Resource not found: %s"), *ResourceUri));
		return true;
	}

	// Read the resource
	FMcpResourceContents ResourceContents = Catalogs.Resources->ReadResource(ResourceUri);

	// Build result object
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	// Create contents array (MCP expects an array of content items)
	TArray<TSharedPtr<FJsonValue>> ContentsArray;
	
	TSharedPtr<FJsonObject> ContentItem = ResourceContents.ToJson();
	if (ContentItem.IsValid())
	{
		ContentsArray.Add(MakeShareable(new FJsonValueObject(ContentItem)));
	}
	
	Result->SetArrayField(TEXT("contents"), ContentsArray);

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(Result)));

	return true;
}

bool FN2CMcpProtocol::HandlePromptsList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing MCP prompts/list request"), EN2CLogSeverity::Info);

	// For MVP, we ignore pagination params (cursor) and return all prompts
	// TODO: In the future, implement pagination if prompt list becomes large

	// Get all registered prompts from the manager
	TArray<FMcpPromptDefinition> Prompts = Catalogs.Prompts->ListPrompts();

	// Build the result object
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

	// Create prompts array
	TArray<TSharedPtr<FJsonValue>> PromptsArray;
	for (const FMcpPromptDefinition& Prompt : Prompts)
	{
		TSharedPtr<FJsonObject> PromptJson = Prompt.ToJson();
		if (PromptJson.IsValid())
		{
			PromptsArray.Add(MakeShareable(new FJsonValueObject(PromptJson)));
		}
	}

	Result->SetArrayField(TEXT("prompts"), PromptsArray);

	// For MVP, no pagination so no nextCursor field

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(Result)));

	FN2CLogger::Get().Log(FString::Printf(TEXT("Returned %d prompts in prompts/list response"), Prompts.Num()), EN2CLogSeverity::Info);

	return true;
}

bool FN2CMcpProtocol::HandlePromptsGet(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing MCP prompts/get request"), EN2CLogSeverity::Info);

	// Validate params using the validation framework
	TSharedPtr<FJsonObject> ParamsObject;
	FString ValidationError;
	if (!FN2CMcpRequestValidator::ValidateParamsIsObject(Params, ParamsObject, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("prompts/get validation failed: %s"), *ValidationError));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, ValidationError);
		return true;
	}

	// Validate prompts/get specific requirements
	if (!FN2CMcpRequestValidator::ValidatePromptsGetRequest(ParamsObject, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("prompts/get validation failed: %s"), *ValidationError));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, ValidationError);
		return true;
	}

	// Extract validated fields
	FString PromptName;
	ParamsObject->TryGetStringField(TEXT("name"), PromptName);  // Already validated as required

	// Check if prompt exists
	if (!Catalogs.Prompts->IsPromptRegistered(PromptName))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Prompt not found: %s"), *PromptName));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::MethodNotFound, FString::Printf(TEXT("Prompt not found: %s"), *PromptName));
		return true;
	}

	// Extract arguments (optional). MCP prompt arguments are strings; anything else is a client error.
	FMcpPromptArguments Arguments;
	const TSharedPtr<FJsonObject>* ArgumentsObj = nullptr;
	if (ParamsObject->TryGetObjectField(TEXT("arguments"), ArgumentsObj) && ArgumentsObj->IsValid())
	{
		for (const auto& Pair : (*ArgumentsObj)->Values)
		{
			if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::String)
			{
				OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams,
					FString::Printf(TEXT("Prompt argument '%s' must be a string"), *FString(*Pair.Key)));
				return true;
			}
			Arguments.Add(FString(*Pair.Key), Pair.Value->AsString());
		}
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Getting prompt: %s with %d arguments"), *PromptName, Arguments.Num()), EN2CLogSeverity::Info);

	// Get the prompt
	FMcpPromptResult PromptResult = Catalogs.Prompts->GetPrompt(PromptName, Arguments);

	// Convert result to JSON
	TSharedPtr<FJsonObject> ResultJson = PromptResult.ToJson();

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(ResultJson)));

	return true;
}

bool FN2CMcpProtocol::HandleCancelTask(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse)
{
	FN2CLogger::Get().Log(TEXT("Processing nodetocode/cancelTask request"), EN2CLogSeverity::Info);

	// Validate params using the validation framework
	TSharedPtr<FJsonObject> ParamsObject;
	FString ValidationError;
	if (!FN2CMcpRequestValidator::ValidateParamsIsObject(Params, ParamsObject, ValidationError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("nodetocode/cancelTask validation failed: %s"), *ValidationError));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, ValidationError);
		return true;
	}

	// Extract progress token
	FString ProgressToken;
	if (!ParamsObject->TryGetStringField(TEXT("progressToken"), ProgressToken))
	{
		FN2CLogger::Get().LogWarning(TEXT("nodetocode/cancelTask missing required parameter: progressToken"));
		OutResponse = FJsonRpcUtils::CreateErrorResponse(Id, JsonRpcErrorCodes::InvalidParams, TEXT("Missing required parameter: progressToken"));
		return true;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Attempting to cancel task with progress token: %s"), *ProgressToken), EN2CLogSeverity::Info);

	// Attempt to cancel the task
	bool bCancelled = Catalogs.Tasks->CancelTaskByProgressToken(ProgressToken);

	// Create result object
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	
	if (bCancelled)
	{
		ResultObject->SetStringField(TEXT("status"), TEXT("cancellation_initiated"));
		FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully initiated cancellation for task with progress token: %s"), *ProgressToken), EN2CLogSeverity::Info);
	}
	else
	{
		// Task not found or already completed. A task stops being tracked once it has reported, so a finished task
		// is recognised by its remembered progress token.
		auto TaskContext = Catalogs.Tasks->GetTaskContextByProgressToken(ProgressToken);
		if (!TaskContext.IsValid() && Catalogs.Tasks->WasProgressTokenCompleted(ProgressToken))
		{
			ResultObject->SetStringField(TEXT("status"), TEXT("task_already_completed"));
			FN2CLogger::Get().Log(FString::Printf(TEXT("Task already completed for progress token: %s"), *ProgressToken), EN2CLogSeverity::Info);
		}
		else if (!TaskContext.IsValid())
		{
			ResultObject->SetStringField(TEXT("status"), TEXT("task_not_found"));
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Task not found for progress token: %s"), *ProgressToken));
		}
		else if (!Catalogs.Tasks->IsTaskRunning(TaskContext->TaskId))
		{
			ResultObject->SetStringField(TEXT("status"), TEXT("task_already_completed"));
			FN2CLogger::Get().Log(FString::Printf(TEXT("Task already completed for progress token: %s"), *ProgressToken), EN2CLogSeverity::Info);
		}
		else
		{
			ResultObject->SetStringField(TEXT("status"), TEXT("cancellation_not_supported"));
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Task does not support cancellation: %s"), *ProgressToken));
		}
	}

	// Create success response
	OutResponse = FJsonRpcUtils::CreateSuccessResponse(Id, MakeShareable(new FJsonValueObject(ResultObject)));

	return true;
}
