// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Delegates/IDelegateInstance.h"
#include "HAL/CriticalSection.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"

class FN2CMcpToolManager;
class FN2CMcpResourceManager;
class FN2CMcpPromptManager;
class FN2CToolAsyncTaskManager;

/**
 * One message a transport received for the MCP endpoint: the verb, the headers and the body bytes exactly as they
 * arrived. The HTTP transport copies FHttpServerRequest into it; another transport (stdio, a second HTTP stack, a
 * test) fills it the same way.
 */
struct FN2CMcpInbound
{
	/** The request verb. The endpoint takes POST; any other verb is answered with 405. */
	FString Verb = TEXT("POST");

	/** Header names as sent (matched case-insensitively) and their values */
	TMap<FString, TArray<FString>> Headers;

	/** The body: exactly the bytes received (UTF-8 JSON) */
	TArray<uint8> Body;

	/** The first value of a header (case-insensitive name), or null */
	const FString* FindHeader(const TCHAR* Name) const;

	/** True when an Accept header lists text/event-stream (the client can read a stream on its request) */
	bool AcceptsEventStream() const;
};

/** One complete answer to an inbound message */
struct FN2CMcpReply
{
	/** Transport status (HTTP status code): 200, 202 for a message that needs no response, 400 for a bad message */
	int32 Status = 200;

	/** The JSON-RPC message or batch (condensed JSON), or empty for no body (202) */
	FString Body;

	/** The Mcp-Session-Id to send with the answer, or empty for none */
	FString SessionId;
};

/**
 * Where the protocol core writes its answer to one inbound message. A transport implements it (the HTTPServer
 * responder in FN2CMcpHttpServerManager, an in-memory one in the tests). The core calls either Reply once, or Stream
 * one or more times and then Close; all calls come on the game thread. It holds the responder until the answer is
 * written, so a long-running tools/call answers through it after Handle has returned.
 */
class IN2CMcpResponder
{
public:
	virtual ~IN2CMcpResponder() = default;

	/** True when this transport can answer the request with a stream of messages (text/event-stream) */
	virtual bool CanStream() const = 0;

	/** Answers with one body (or none, for 202). Called at most once, and never after Stream. */
	virtual void Reply(const FN2CMcpReply& Reply) = 0;

	/**
	 * Writes one JSON-RPC message (a notification, or the final response) on the request's stream. The first call
	 * opens the stream (200, text/event-stream, SessionId when not empty). Only called when CanStream() is true.
	 */
	virtual void Stream(const FString& MessageJson, const FString& SessionId) = 0;

	/** Ends a stream opened by Stream. */
	virtual void Close() = 0;
};

/**
 * The catalogs the protocol core dispatches to. They are passed in at construction instead of reached as globals, so
 * a later tool catalog (E17) or a test can supply its own. Default() is the plugin's singletons.
 */
struct FN2CMcpCatalogs
{
	FN2CMcpToolManager* Tools = nullptr;
	FN2CMcpResourceManager* Resources = nullptr;
	FN2CMcpPromptManager* Prompts = nullptr;
	FN2CToolAsyncTaskManager* Tasks = nullptr;

	static FN2CMcpCatalogs Default();
};

/**
 * The MCP protocol core, independent of any transport. It owns body parsing, JSON-RPC classification, batches, method
 * dispatch, the long-running tools/call branch (answered on the same request, streamed when the responder can) and the
 * session and protocol-version table. Transports build an FN2CMcpInbound and an IN2CMcpResponder and call Handle.
 *
 * Server-initiated notifications (notifications/tools/list_changed) flow from the core to the sink a transport sets
 * with SetBroadcastSink; tools never call a transport.
 *
 * Game thread only.
 */
class FN2CMcpProtocol
{
public:
	explicit FN2CMcpProtocol(const FN2CMcpCatalogs& InCatalogs = FN2CMcpCatalogs::Default());
	~FN2CMcpProtocol();

	FN2CMcpProtocol(const FN2CMcpProtocol&) = delete;
	FN2CMcpProtocol& operator=(const FN2CMcpProtocol&) = delete;

	/**
	 * Handles one inbound message and answers it through Responder: at once for most messages; for a single
	 * tools/call of a long-running tool, when its call finishes (as a stream of notifications/progress and then the
	 * response when the responder can stream, the client accepts text/event-stream and sent _meta.progressToken;
	 * otherwise as one JSON reply).
	 */
	void Handle(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& Responder);

	/**
	 * Parses a body as one JSON value: exactly Body.Num() bytes of UTF-8, nothing cut off and nothing ignored. A
	 * JSON-RPC batch parses to an array; trailing bytes after the root value fail the parse.
	 * @param OutJsonValue The parsed value, or null when the body is not valid JSON
	 * @return true if the body is a single valid JSON value
	 */
	static bool ParseBody(const TArray<uint8>& Body, TSharedPtr<FJsonValue>& OutJsonValue);

	/**
	 * Answers an already parsed message (or batch) synchronously: everything Handle does except the long-running
	 * branch and sessions.
	 * @param JsonValue The parsed body (ParseBody); null gives a -32700 Parse error
	 * @param OutResponseBody The JSON-RPC response (condensed, one line), or empty for 202
	 * @param OutStatusCode The transport status
	 * @param OutWasInitializeCall True when the message was an initialize request
	 * @return true if the message was processed (false for a parse error, an invalid message or an empty batch)
	 */
	bool ProcessMessage(const TSharedPtr<FJsonValue>& JsonValue, FString& OutResponseBody, int32& OutStatusCode, bool& OutWasInitializeCall);

	/** Sends a server notification to the broadcast sink (logged and dropped when no sink is set) */
	void BroadcastNotification(const FJsonRpcNotification& Notification);

	/** Where server notifications go: receives one serialized JSON-RPC notification. Unset: none are sent. */
	using FBroadcastSink = TFunction<void(const FString& /*NotificationJson*/)>;
	void SetBroadcastSink(FBroadcastSink InSink) { BroadcastSink = MoveTemp(InSink); }

	/** The current session id (empty before the first successful initialize) */
	const FString& GetCurrentSessionId() const { return CurrentSessionId; }

	/** The protocol version negotiated by a session's initialize, or empty */
	FString GetSessionProtocolVersion(const FString& SessionId) const;

	/** Supported protocol versions, newest first */
	static const TArray<FString>& GetSupportedProtocolVersions();

private:
	/** The value the response's Mcp-Session-Id carries (empty for none); a successful initialize creates the session */
	FString ResolveResponseSessionId(bool bSuccessfulInitialize, const FString& ClientSentSessionId);

	/** True when ToolName is a registered tool marked bIsLongRunning */
	bool IsLongRunningTool(const FString& ToolName) const;

	/** Starts a long-running tool's call and answers the request from it (see Handle) */
	void HandleLongRunningToolCall(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& Responder,
		const TSharedPtr<FJsonObject>& RequestObject, const TSharedPtr<FJsonObject>& Params, const FString& ToolName,
		const FString& ClientSentSessionId);

	bool ProcessBatchRequest(const TArray<TSharedPtr<FJsonValue>>& BatchArray, FString& OutResponseBody, int32& OutStatusCode);
	bool ProcessRequest(const FJsonRpcRequest& Request, FJsonRpcResponse& OutResponse);
	bool ProcessNotification(const FJsonRpcNotification& Notification);
	bool DispatchMethod(const FString& Method, const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);

	bool HandleInitialize(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleToolsList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleToolsCall(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleResourcesList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleResourcesRead(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandlePromptsList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandlePromptsGet(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleCancelTask(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);

	FN2CMcpCatalogs Catalogs;

	/** The current session (one for all clients today; card 0109 makes sessions per client) */
	FString CurrentSessionId;

	/** Protocol version negotiated per session id */
	TMap<FString, FString> SessionProtocolVersions;

	/** Version negotiated by the last initialize, recorded against the session it creates or keeps */
	FString LastNegotiatedVersion;

	FBroadcastSink BroadcastSink;

	/** Subscription to the tool manager's list-changed event */
	FDelegateHandle ToolListChangedHandle;
};
