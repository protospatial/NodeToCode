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
	/** The request verb: POST carries JSON-RPC messages, DELETE ends a session; any other verb is answered with 405. */
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
	/**
	 * Transport status (HTTP status code): 200; 202 for a message that needs no response; 204 for an ended session;
	 * 400 for a bad message, a missing Mcp-Session-Id or an unsupported MCP-Protocol-Version; 404 for an unknown or
	 * ended session; 405 for a verb the endpoint does not take
	 */
	int32 Status = 200;

	/** The JSON-RPC message or batch (condensed JSON), or empty for no body (202, 204) */
	FString Body;

	/** The Mcp-Session-Id to send with the answer, or empty for none */
	FString SessionId;

	/** Further transport headers (name, value), for example Allow on a 405 */
	TArray<TPair<FString, FString>> Headers;
};

/**
 * One client's MCP session: created by a successful initialize, named by a new random Mcp-Session-Id, ended by DELETE
 * (or dropped as the least recently used when the table is full). Every request after initialize names it.
 */
struct FN2CMcpSession
{
	/** The Mcp-Session-Id (a random GUID, visible ASCII) */
	FString Id;

	/** The protocol version initialize negotiated */
	FString ProtocolVersion;

	/** clientInfo and capabilities as the client sent them in initialize (null when absent) */
	TSharedPtr<FJsonObject> ClientInfo;
	TSharedPtr<FJsonObject> ClientCapabilities;

	/** True once the client sent notifications/initialized */
	bool bInitialized = false;

	/** FPlatformTime::Seconds() of initialize and of the last request that named the session */
	double CreatedSeconds = 0.0;
	double LastUsedSeconds = 0.0;
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
 * per-client session table. Transports build an FN2CMcpInbound and an IN2CMcpResponder and call Handle (after their
 * own Origin and Host checks).
 *
 * Sessions and versions (MCP 2025-11-25, basic/transports and basic/lifecycle): initialize negotiates 2025-11-25,
 * 2025-06-18, 2025-03-26 or 2024-11-05 (the requested one when supported, else the newest) and creates a session with
 * a new random Mcp-Session-Id. Every other POST must name a live session: none gives 400, an unknown or ended one 404.
 * An MCP-Protocol-Version header with an unsupported value gives 400; a missing one is accepted (2025-03-26 clients do
 * not send it) and the session's negotiated version applies. GET and other verbs give 405 (Allow: POST, DELETE);
 * DELETE ends the session and cancels its running calls. Client responses and notifications get 202 with no body.
 * JSON-RPC batches are accepted only on sessions negotiated at 2025-03-26 (batching was added there and removed in
 * 2025-06-18) and get 400 with -32600 otherwise.
 *
 * Capabilities and errors (card 0110): initialize advertises tools (listChanged), resources and prompts, not logging,
 * and returns instructions for the agent. resources/templates/list lists the resource templates. An unknown tool or
 * prompt, or a missing required prompt argument, is -32602; an unknown resource uri is -32002 with the uri in data; a
 * resource or prompt handler that timed out is -32603; -32601 is only for an unknown method.
 *
 * Cancellation (basic/utilities/cancellation): notifications/cancelled {requestId} cancels the running long-running
 * call with that request id in the same session through its call context; no response is sent for that request (a
 * stream closes without it, a held request ends with 202 and no body). Unknown or finished ids are ignored. The
 * custom nodetocode/cancelTask {progressToken} still works, is deprecated, and its call answers with the cancelled
 * result.
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
	 * branch, sessions, the MCP-Protocol-Version check and the batch-per-version rule (a batch is always answered).
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

	/** The live session with this id, or null */
	const FN2CMcpSession* FindSession(const FString& SessionId) const { return Sessions.Find(SessionId); }

	/** How many sessions are live */
	int32 GetSessionCount() const { return Sessions.Num(); }

	/** The protocol version negotiated by a session's initialize, or empty */
	FString GetSessionProtocolVersion(const FString& SessionId) const;

	/**
	 * Ends a session: later requests naming it get 404, and its running long-running calls are cancelled (each still
	 * answers its own request, with the cancelled result).
	 * @return true when the session was live
	 */
	bool EndSession(const FString& SessionId, const FString& Reason);

	/** Supported protocol versions, newest first */
	static const TArray<FString>& GetSupportedProtocolVersions();

	/** InitializeResult.instructions: which tools an agent starts with */
	static const FString& GetServerInstructions();

	/** The most sessions kept at once; initialize beyond it drops the least recently used one */
	static constexpr int32 MaxSessions = 64;

private:
	/** Answers DELETE: ends the named session (204), or 400 without Mcp-Session-Id and 404 for an unknown one */
	void HandleDelete(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& Responder);

	/** Creates the session a successful initialize asked for and returns its id */
	FString CreateSession();

	/** True when ToolName is a registered tool marked bIsLongRunning */
	bool IsLongRunningTool(const FString& ToolName) const;

	/** Starts a long-running tool's call and answers the request from it (see Handle) */
	void HandleLongRunningToolCall(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& Responder,
		const TSharedPtr<FJsonObject>& RequestObject, const TSharedPtr<FJsonObject>& Params, const FString& ToolName,
		const FString& SessionId);

	bool ProcessBatchRequest(const TArray<TSharedPtr<FJsonValue>>& BatchArray, FString& OutResponseBody, int32& OutStatusCode);
	bool ProcessRequest(const FJsonRpcRequest& Request, FJsonRpcResponse& OutResponse);
	bool ProcessNotification(const FJsonRpcNotification& Notification);

	/** notifications/cancelled: cancels the session's running call with that requestId (no response is sent for it) */
	void HandleCancelledNotification(const FJsonRpcNotification& Notification);
	bool DispatchMethod(const FString& Method, const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);

	bool HandleInitialize(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleToolsList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleToolsCall(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleResourcesList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleResourceTemplatesList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleResourcesRead(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandlePromptsList(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandlePromptsGet(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);
	bool HandleCancelTask(const TSharedPtr<FJsonValue>& Params, const TSharedPtr<FJsonValue>& Id, FJsonRpcResponse& OutResponse);

	FN2CMcpCatalogs Catalogs;

	/** Live sessions by Mcp-Session-Id */
	TMap<FString, FN2CMcpSession> Sessions;

	/** The session of the message Handle is processing (empty outside Handle): where notifications/cancelled looks */
	FString ActiveSessionId;

	/** What the initialize being handled negotiated: set by HandleInitialize on success, read by Handle */
	FString LastNegotiatedVersion;
	TSharedPtr<FJsonObject> LastClientInfo;
	TSharedPtr<FJsonObject> LastClientCapabilities;

	FBroadcastSink BroadcastSink;

	/** Subscription to the tool manager's list-changed event */
	FDelegateHandle ToolListChangedHandle;
};
