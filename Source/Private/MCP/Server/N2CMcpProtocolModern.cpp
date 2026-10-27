// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

// The 2026-07-28 (modern, stateless) half of FN2CMcpProtocol (card 0210). Legacy initialize and session traffic is
// answered by N2CMcpProtocol.cpp exactly as before; a message whose params._meta names a protocol version (or whose
// MCP-Protocol-Version header is a modern one) comes here. Spec: modelcontextprotocol.io/specification/2026-07-28
// (basic/index "_meta", basic/versioning, basic/transports/streamable-http, server/discover,
// basic/patterns/subscriptions, basic/patterns/cancellation).

#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Base64.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CMcpModernPrivate
{
	const TCHAR* const MetaProtocolVersion = TEXT("io.modelcontextprotocol/protocolVersion");
	const TCHAR* const MetaClientCapabilities = TEXT("io.modelcontextprotocol/clientCapabilities");
	const TCHAR* const MetaServerInfo = TEXT("io.modelcontextprotocol/serverInfo");
	const TCHAR* const MetaSubscriptionId = TEXT("io.modelcontextprotocol/subscriptionId");

	/** basic/index, error codes reserved for the MCP specification */
	constexpr int32 HeaderMismatch = -32020;
	constexpr int32 UnsupportedProtocolVersion = -32022;

	/** The methods a 2026-07-28 request may name here; anything else is 404 with -32601 (ping and logging/setLevel are gone) */
	bool IsModernMethod(const FString& Method)
	{
		return Method == TEXT("server/discover") || Method == TEXT("subscriptions/listen")
			|| Method == TEXT("tools/list") || Method == TEXT("tools/call")
			|| Method == TEXT("resources/list") || Method == TEXT("resources/templates/list") || Method == TEXT("resources/read")
			|| Method == TEXT("prompts/list") || Method == TEXT("prompts/get");
	}

	FString Condensed(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	/** A client-sent value safe to echo in a message (bounded, printable ASCII only) */
	FString Safe(const FString& Value)
	{
		FString Out;
		for (const TCHAR Ch : Value.Left(100))
		{
			Out.AppendChar(Ch >= 0x20 && Ch <= 0x7E ? Ch : TEXT('?'));
		}
		return Out;
	}

	/** True when Value has only what an HTTP header value may carry: visible ASCII, space and tab */
	bool IsHeaderSafe(const FString& Value)
	{
		for (const TCHAR Ch : Value)
		{
			if (!((Ch >= 0x20 && Ch <= 0x7E) || Ch == TEXT('\t')))
			{
				return false;
			}
		}
		return true;
	}
}

bool FN2CMcpProtocol::IsModernMessage(const FN2CMcpInbound& Inbound, const TSharedPtr<FJsonObject>& Single)
{
	using namespace N2CMcpModernPrivate;
	if (Single.IsValid())
	{
		const TSharedPtr<FJsonObject>* Params = nullptr;
		const TSharedPtr<FJsonObject>* Meta = nullptr;
		if (Single->TryGetObjectField(TEXT("params"), Params) && Params && Params->IsValid()
			&& (*Params)->TryGetObjectField(TEXT("_meta"), Meta) && Meta && Meta->IsValid()
			&& (*Meta)->HasField(MetaProtocolVersion))
		{
			return true;
		}
	}
	const FString* VersionHeader = Inbound.FindHeader(TEXT("MCP-Protocol-Version"));
	return VersionHeader && GetModernProtocolVersions().Contains(VersionHeader->TrimStartAndEnd());
}

TOptional<FString> FN2CMcpProtocol::DecodeHeaderValue(const FString& Value)
{
	using namespace N2CMcpModernPrivate;
	if (!IsHeaderSafe(Value))
	{
		return {};
	}
	// streamable-http, "Value Encoding": =?base64?<Base64 of the UTF-8 text>?= (markers case-sensitive)
	if (Value.StartsWith(TEXT("=?base64?"), ESearchCase::CaseSensitive) && Value.EndsWith(TEXT("?="), ESearchCase::CaseSensitive)
		&& Value.Len() >= 11)
	{
		TArray<uint8> Bytes;
		if (!FBase64::Decode(Value.Mid(9, Value.Len() - 11), Bytes))
		{
			return {};
		}
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Converter.Length(), Converter.Get());
	}
	return Value;
}

TSharedRef<FJsonObject> FN2CMcpProtocol::BuildServerCapabilities()
{
	const TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();

	// false: tools/list is fixed while the server runs (tool search reaches every tool through list_toolsets,
	// describe_toolset and call_tool, card 0165). It changes only when the server is configured again.
	const TSharedRef<FJsonObject> Tools = MakeShared<FJsonObject>();
	Tools->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("tools"), Tools);

	const TSharedRef<FJsonObject> Resources = MakeShared<FJsonObject>();
	Resources->SetBoolField(TEXT("subscribe"), false);
	Resources->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("resources"), Resources);

	const TSharedRef<FJsonObject> Prompts = MakeShared<FJsonObject>();
	Prompts->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("prompts"), Prompts);

	// No logging capability: the server sends no notifications/message and has no logging/setLevel, and both parties
	// may "Only use capabilities that were successfully negotiated" (logging is deprecated in 2026-07-28)
	return Capabilities;
}

TSharedRef<FJsonObject> FN2CMcpProtocol::BuildServerInfo()
{
	const TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), TEXT("NodeToCodeMCPServer"));
	FString PluginVersion = TEXT("Unknown");
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode")))
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
	}
	ServerInfo->SetStringField(TEXT("version"), PluginVersion);
	return ServerInfo;
}

void FN2CMcpProtocol::DecorateModernResult(const FString& Method, const TSharedPtr<FJsonObject>& Result)
{
	using namespace N2CMcpModernPrivate;
	if (!Result.IsValid())
	{
		return;
	}
	// basic/index: every result carries resultType; "complete" for an ordinary result
	if (!Result->HasField(TEXT("resultType")))
	{
		Result->SetStringField(TEXT("resultType"), TEXT("complete"));
	}

	// Servers SHOULD identify themselves in each result's _meta
	const TSharedPtr<FJsonObject>* ExistingMeta = nullptr;
	const TSharedPtr<FJsonObject> Meta = Result->TryGetObjectField(TEXT("_meta"), ExistingMeta) && ExistingMeta && ExistingMeta->IsValid()
		? *ExistingMeta : MakeShared<FJsonObject>();
	Meta->SetObjectField(MetaServerInfo, BuildServerInfo());
	Result->SetObjectField(TEXT("_meta"), Meta);

	// CacheableResult (server/utilities/caching): required on these five. Lists of tools, prompts and resource
	// templates are fixed while the server runs; resources and their contents follow the editor, so they are not
	// cached. "private": the answer is this user's editor, never shared through an intermediary.
	const bool bFixedList = Method == TEXT("tools/list") || Method == TEXT("prompts/list") || Method == TEXT("resources/templates/list");
	const bool bLive = Method == TEXT("resources/list") || Method == TEXT("resources/read");
	if (bFixedList || bLive)
	{
		Result->SetNumberField(TEXT("ttlMs"), bFixedList ? ListTtlMs : 0);
		Result->SetStringField(TEXT("cacheScope"), TEXT("private"));
	}
}

FString FN2CMcpProtocol::DecorateModernResponseJson(const FString& Method, const FString& ResponseJson)
{
	TSharedPtr<FJsonObject> Response;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ResponseJson), Response) || !Response.IsValid())
	{
		return ResponseJson;
	}
	const TSharedPtr<FJsonObject>* Result = nullptr;
	if (!Response->TryGetObjectField(TEXT("result"), Result) || !Result || !Result->IsValid())
	{
		return ResponseJson;
	}
	DecorateModernResult(Method, *Result);
	return N2CMcpModernPrivate::Condensed(Response.ToSharedRef());
}

void FN2CMcpProtocol::HandleModern(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& Responder,
	const TSharedPtr<FJsonValue>& JsonValue)
{
	using namespace N2CMcpModernPrivate;

	auto ReplyError = [&Responder](int32 Status, const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message,
		const TSharedPtr<FJsonValue>& Data = nullptr)
	{
		FN2CMcpReply Reply;
		Reply.Status = Status;
		Reply.Body = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateErrorResponse(Id, Code, Message, Data));
		Responder->Reply(Reply);
	};

	if (!JsonValue.IsValid())
	{
		ReplyError(400, nullptr, JsonRpcErrorCodes::ParseError, TEXT("Parse error"));
		return;
	}
	if (JsonValue->Type != EJson::Object)
	{
		ReplyError(400, nullptr, JsonRpcErrorCodes::InvalidRequest,
			TEXT("Invalid Request: protocol version 2026-07-28 takes one JSON-RPC message per POST, not a batch"));
		return;
	}
	const TSharedPtr<FJsonObject> Object = JsonValue->AsObject();
	const FJsonRpcUtils::EJsonRpcMessageType Type = FJsonRpcUtils::GetMessageType(Object);
	if (Type == FJsonRpcUtils::EJsonRpcMessageType::Notification || Type == FJsonRpcUtils::EJsonRpcMessageType::Response)
	{
		// 2026-07-28 defines no client notification on Streamable HTTP (closing a stream is the cancellation); accept
		// and ignore, as the transport requires for a notification the server takes
		FString Method;
		Object->TryGetStringField(TEXT("method"), Method);
		FN2CLogger::Get().Log(FString::Printf(TEXT("2026-07-28 %s '%s' accepted (202) and ignored"),
			Type == FJsonRpcUtils::EJsonRpcMessageType::Notification ? TEXT("notification") : TEXT("response"), *Safe(Method)),
			EN2CLogSeverity::Debug);
		FN2CMcpReply Reply;
		Reply.Status = 202;
		Responder->Reply(Reply);
		return;
	}
	if (Type != FJsonRpcUtils::EJsonRpcMessageType::Request)
	{
		ReplyError(400, nullptr, JsonRpcErrorCodes::InvalidRequest, TEXT("Invalid Request"));
		return;
	}

	const FJsonRpcRequest Request(Object);
	const TSharedPtr<FJsonValue>& Id = Request.Id;
	const TSharedPtr<FJsonObject> Params = Request.Params.IsValid() && Request.Params->Type == EJson::Object ? Request.Params->AsObject() : nullptr;

	// basic/index, per-request protocol fields: protocolVersion and clientCapabilities are required on every request;
	// "A request missing any required field is malformed": -32602 with 400
	const TSharedPtr<FJsonObject>* MetaPtr = nullptr;
	const TSharedPtr<FJsonObject> Meta = Params.IsValid() && Params->TryGetObjectField(TEXT("_meta"), MetaPtr) && MetaPtr ? *MetaPtr : nullptr;
	FString Version;
	const TSharedPtr<FJsonObject>* Capabilities = nullptr;
	if (!Meta.IsValid() || !Meta->TryGetStringField(MetaProtocolVersion, Version)
		|| !Meta->TryGetObjectField(MetaClientCapabilities, Capabilities) || !Capabilities || !Capabilities->IsValid())
	{
		ReplyError(400, Id, JsonRpcErrorCodes::InvalidParams, FString::Printf(
			TEXT("Invalid params: a 2026-07-28 request carries params._meta[\"%s\"] (a string) and params._meta[\"%s\"] (an object); ")
			TEXT("a client of an earlier protocol version sends initialize first"), MetaProtocolVersion, MetaClientCapabilities));
		return;
	}

	// streamable-http, protocol version header: required on every POST and equal to the _meta version
	const FString* VersionHeader = Inbound.FindHeader(TEXT("MCP-Protocol-Version"));
	if (!VersionHeader || VersionHeader->TrimStartAndEnd() != Version)
	{
		ReplyError(400, Id, HeaderMismatch, VersionHeader
			? FString::Printf(TEXT("Header mismatch: MCP-Protocol-Version header '%s' does not match _meta value '%s'"), *Safe(*VersionHeader), *Safe(Version))
			: FString(TEXT("Header mismatch: the MCP-Protocol-Version header is required")));
		return;
	}

	// basic/versioning: an unsupported version gets UnsupportedProtocolVersionError listing what the server supports
	if (!GetModernProtocolVersions().Contains(Version))
	{
		const TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Supported;
		for (const FString& Each : GetModernProtocolVersions())
		{
			Supported.Add(MakeShared<FJsonValueString>(Each));
		}
		Data->SetArrayField(TEXT("supported"), Supported);
		Data->SetStringField(TEXT("requested"), Version);
		FN2CLogger::Get().Log(FString::Printf(TEXT("MCP request for unsupported protocol version '%s' answered 400"), *Safe(Version)), EN2CLogSeverity::Info);
		ReplyError(400, Id, UnsupportedProtocolVersion, TEXT("Unsupported protocol version"), MakeShared<FJsonValueObject>(Data));
		return;
	}

	// streamable-http, standard request headers: Mcp-Method on every request, Mcp-Name on tools/call, resources/read
	// and prompts/get; both must match the body (Mcp-Name after Base64 decoding)
	const FString& Method = Request.Method;
	const FString* MethodHeader = Inbound.FindHeader(TEXT("Mcp-Method"));
	if (!MethodHeader || !MethodHeader->Equals(Method, ESearchCase::CaseSensitive))
	{
		ReplyError(400, Id, HeaderMismatch, MethodHeader
			? FString::Printf(TEXT("Header mismatch: Mcp-Method header '%s' does not match body value '%s'"), *Safe(*MethodHeader), *Safe(Method))
			: FString(TEXT("Header mismatch: the Mcp-Method header is required")));
		return;
	}
	const TCHAR* NameField = Method == TEXT("tools/call") || Method == TEXT("prompts/get") ? TEXT("name")
		: Method == TEXT("resources/read") ? TEXT("uri") : nullptr;
	if (NameField)
	{
		FString BodyName;
		const bool bHasBodyName = Params.IsValid() && Params->TryGetStringField(NameField, BodyName);
		const FString* NameHeader = Inbound.FindHeader(TEXT("Mcp-Name"));
		const TOptional<FString> Decoded = NameHeader ? DecodeHeaderValue(*NameHeader) : TOptional<FString>();
		if (NameHeader && !Decoded.IsSet())
		{
			ReplyError(400, Id, HeaderMismatch, TEXT("Header mismatch: the Mcp-Name header has invalid characters or invalid Base64"));
			return;
		}
		if (bHasBodyName && (!Decoded.IsSet() || !Decoded->Equals(BodyName, ESearchCase::CaseSensitive)))
		{
			ReplyError(400, Id, HeaderMismatch, Decoded.IsSet()
				? FString::Printf(TEXT("Header mismatch: Mcp-Name header value '%s' does not match body value '%s'"), *Safe(*Decoded), *Safe(BodyName))
				: FString::Printf(TEXT("Header mismatch: the Mcp-Name header is required for %s"), *Method));
			return;
		}
	}

	// streamable-http: "If the server does not implement the requested RPC method, it MUST respond with 404 Not Found
	// and a JSON-RPC error with code -32601"
	if (!IsModernMethod(Method))
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("2026-07-28 request for unknown method '%s' answered 404"), *Safe(Method)), EN2CLogSeverity::Info);
		ReplyError(404, Id, JsonRpcErrorCodes::MethodNotFound, FString::Printf(TEXT("Method not found: '%s'"), *Safe(Method)));
		return;
	}

	if (Method == TEXT("server/discover"))
	{
		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Versions;
		for (const FString& Each : GetModernProtocolVersions())
		{
			Versions.Add(MakeShared<FJsonValueString>(Each));
		}
		Result->SetArrayField(TEXT("supportedVersions"), Versions);
		Result->SetObjectField(TEXT("capabilities"), BuildServerCapabilities());
		Result->SetStringField(TEXT("instructions"), GetServerInstructions());
		DecorateModernResult(Method, Result);
		Result->SetNumberField(TEXT("ttlMs"), ListTtlMs);
		Result->SetStringField(TEXT("cacheScope"), TEXT("private"));

		FString ClientName;
		const TSharedPtr<FJsonObject>* ClientInfo = nullptr;
		if (Meta->TryGetObjectField(TEXT("io.modelcontextprotocol/clientInfo"), ClientInfo) && ClientInfo && ClientInfo->IsValid())
		{
			(*ClientInfo)->TryGetStringField(TEXT("name"), ClientName);
		}
		FN2CLogger::Get().Log(FString::Printf(TEXT("MCP server/discover from %s (protocol %s)"),
			*Safe(ClientName.IsEmpty() ? FString(TEXT("an unnamed client")) : ClientName), *Version), EN2CLogSeverity::Info);

		FN2CMcpReply Reply;
		Reply.Body = FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateSuccessResponse(Id, MakeShared<FJsonValueObject>(Result)));
		Responder->Reply(Reply);
		return;
	}

	if (Method == TEXT("subscriptions/listen"))
	{
		if (!Responder->CanStream() || !Inbound.AcceptsEventStream())
		{
			// UE 5.6 and 5.7: the engine's HTTP server writes one framed body per request, so it cannot keep a stream
			// open (decision 0085 (b)); tools/list never changes while the server runs, so nothing is lost
			ReplyError(404, Id, JsonRpcErrorCodes::MethodNotFound, Responder->CanStream()
				? FString(TEXT("Method not found: subscriptions/listen answers on a text/event-stream response; send Accept: text/event-stream"))
				: FString(TEXT("Method not found: subscriptions/listen needs a stream this engine's HTTP server cannot keep open (UE 5.8 and later)")));
			return;
		}
		HandleSubscriptionsListen(Responder, Params, Id);
		return;
	}

	// A long-running tools/call runs as a call answered on this request, cancelled when the client closes its stream
	if (Method == TEXT("tools/call") && TryHandleLongRunningToolCall(Inbound, Responder, Object, FString(), /*bModern=*/true))
	{
		return;
	}

	FJsonRpcResponse Response;
	DispatchMethod(Method, Request.Params, Id, Response);
	if (Response.IsError())
	{
		// 2026-07-28 replaced resource-not-found -32002 with -32602 (Invalid params)
		if (Method == TEXT("resources/read") && Response.Error->GetIntegerField(TEXT("code")) == JsonRpcErrorCodes::ResourceNotFound)
		{
			Response.Error->SetNumberField(TEXT("code"), JsonRpcErrorCodes::InvalidParams);
		}
	}
	else if (Response.Result.IsValid() && Response.Result->Type == EJson::Object)
	{
		DecorateModernResult(Method, Response.Result->AsObject());
	}
	FN2CMcpReply Reply;
	Reply.Body = FJsonRpcUtils::SerializeResponse(Response);
	Responder->Reply(Reply);
}

void FN2CMcpProtocol::HandleSubscriptionsListen(const TSharedRef<IN2CMcpResponder>& Responder, const TSharedPtr<FJsonObject>& Params,
	const TSharedPtr<FJsonValue>& Id)
{
	using namespace N2CMcpModernPrivate;

	// basic/patterns/subscriptions: the server honours what it can and acknowledges that subset first. The only change
	// notification this server sends is notifications/tools/list_changed; prompts, resources and resource updates never
	// change while it runs, so those types are left out of the acknowledgement.
	bool bToolsListChanged = false;
	const TSharedPtr<FJsonObject>* Filter = nullptr;
	if (Params.IsValid() && Params->TryGetObjectField(TEXT("notifications"), Filter) && Filter && Filter->IsValid())
	{
		(*Filter)->TryGetBoolField(TEXT("toolsListChanged"), bToolsListChanged);
	}

	const TSharedRef<FJsonObject> Accepted = MakeShared<FJsonObject>();
	if (bToolsListChanged)
	{
		Accepted->SetBoolField(TEXT("toolsListChanged"), true);
	}
	const TSharedRef<FJsonObject> AckMeta = MakeShared<FJsonObject>();
	AckMeta->SetField(MetaSubscriptionId, Id);
	const TSharedRef<FJsonObject> AckParams = MakeShared<FJsonObject>();
	AckParams->SetObjectField(TEXT("_meta"), AckMeta);
	AckParams->SetObjectField(TEXT("notifications"), Accepted);
	FJsonRpcNotification Ack;
	Ack.Method = TEXT("notifications/subscriptions/acknowledged");
	Ack.Params = MakeShared<FJsonValueObject>(AckParams);
	Responder->Stream(FJsonRpcUtils::SerializeNotification(Ack), FString());

	FWatchedStream Watched;
	Watched.Responder = Responder;
	Watched.SubscriptionId = Id;
	Watched.bToolsListChanged = bToolsListChanged;
	Watched.bDone = MakeShared<bool>(false);
	Watched.bOpened = MakeShared<bool>(true);
	WatchedStreams.Add(MoveTemp(Watched));
	EnsureStreamTicker();
	FN2CLogger::Get().Log(FString::Printf(TEXT("MCP subscriptions/listen opened (toolsListChanged %s), %d open"),
		bToolsListChanged ? TEXT("on") : TEXT("off"), GetSubscriptionCount()), EN2CLogSeverity::Info);
}

int32 FN2CMcpProtocol::GetSubscriptionCount() const
{
	int32 Count = 0;
	for (const FWatchedStream& Watched : WatchedStreams)
	{
		Count += Watched.SubscriptionId.IsValid() && !*Watched.bDone ? 1 : 0;
	}
	return Count;
}

void FN2CMcpProtocol::CloseSubscriptions()
{
	using namespace N2CMcpModernPrivate;
	for (int32 Index = WatchedStreams.Num() - 1; Index >= 0; --Index)
	{
		FWatchedStream& Watched = WatchedStreams[Index];
		if (!Watched.SubscriptionId.IsValid())
		{
			continue;
		}
		if (!*Watched.bDone && !Watched.Responder->IsClosedByClient())
		{
			// Graceful closure: the response to subscriptions/listen, then the stream ends
			const TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
			Meta->SetField(MetaSubscriptionId, Watched.SubscriptionId);
			const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("resultType"), TEXT("complete"));
			Result->SetObjectField(TEXT("_meta"), Meta);
			Watched.Responder->Stream(FJsonRpcUtils::SerializeResponse(FJsonRpcUtils::CreateSuccessResponse(Watched.SubscriptionId,
				MakeShared<FJsonValueObject>(Result))), FString());
			Watched.Responder->Close();
		}
		*Watched.bDone = true;
		WatchedStreams.RemoveAt(Index);
	}
}

void FN2CMcpProtocol::EnsureStreamTicker()
{
	if (StreamTickerHandle.IsValid())
	{
		return;
	}
	StreamTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float DeltaTime)
	{
		const bool bKeep = TickStreams(DeltaTime);
		if (!bKeep)
		{
			StreamTickerHandle.Reset();
		}
		return bKeep;
	}), StreamKeepAliveSeconds);
}

bool FN2CMcpProtocol::TickStreams(float DeltaTime)
{
	for (int32 Index = WatchedStreams.Num() - 1; Index >= 0; --Index)
	{
		FWatchedStream& Watched = WatchedStreams[Index];
		if (*Watched.bDone)
		{
			WatchedStreams.RemoveAt(Index);
			continue;
		}
		if (Watched.Responder->IsClosedByClient())
		{
			// streamable-http, cancellation: "Closing the SSE response stream MUST be treated by the server as
			// cancellation of that request"
			*Watched.bDone = true;
			if (Watched.TaskId.IsValid())
			{
				FN2CLogger::Get().Log(FString::Printf(TEXT("The client closed the stream of call %s: cancelling it"), *Watched.TaskId.ToString()),
					EN2CLogSeverity::Info);
				Catalogs.Tasks->CancelTask(Watched.TaskId, TEXT("The client closed the request's stream"));
			}
			else
			{
				FN2CLogger::Get().Log(TEXT("The client closed a subscriptions/listen stream"), EN2CLogSeverity::Info);
				Watched.Responder->Close();
			}
			WatchedStreams.RemoveAt(Index);
			continue;
		}
		Watched.Responder->StreamKeepAlive(FString());
		if (Watched.bOpened.IsValid())
		{
			*Watched.bOpened = true;
		}
	}
	return WatchedStreams.Num() > 0;
}
