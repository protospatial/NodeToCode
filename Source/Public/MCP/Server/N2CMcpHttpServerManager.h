// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"

class FN2CMcpProtocol;

/**
 * The HTTP transport of the MCP server: binds /mcp and /mcp/health on the engine's HTTPServer, applies the
 * local-request policy (Origin, Host, and Content-Type for POST) and hands every /mcp request (POST, DELETE, and GET,
 * PUT or PATCH, which get 405) to the transport-neutral protocol core (FN2CMcpProtocol,
 * Source/Private/MCP/Server/N2CMcpProtocol.h) with an HTTPServer responder. Sessions are per client (Mcp-Session-Id).
 */
class NODETOCODE_API FN2CMcpHttpServerManager
{
public:
	/**
	 * Gets the singleton instance of the MCP HTTP server manager.
	 */
	static FN2CMcpHttpServerManager& Get();

	~FN2CMcpHttpServerManager();

	/**
	 * Initializes and starts the HTTP server on the specified port.
	 * @param Port The port to listen on (default: 27000)
	 * @return true if the server started successfully, false otherwise
	 */
	bool StartServer(int32 Port = 27000);

	/**
	 * Stops the HTTP server and cleans up resources.
	 */
	void StopServer();

	/**
	 * Checks if the server is currently running.
	 * @return true if the server is running, false otherwise
	 */
	bool IsServerRunning() const { return bIsServerRunning; }

	/**
	 * Gets the port the server is listening on.
	 * @return The server port, or -1 if not running
	 */
	int32 GetServerPort() const { return ServerPort; }

	/**
	 * Broadcasts a server notification through the protocol core to the connected notification clients (the
	 * port+1 notification stream).
	 * @param Notification The notification to broadcast
	 */
	void BroadcastNotification(const FJsonRpcNotification& Notification);

	/**
	 * Handles a request to /mcp (the route handler; public so tests can drive it without a socket): copies the request
	 * into an FN2CMcpInbound and lets the protocol core answer it through an HTTPServer responder. Most requests are
	 * answered before it returns. A single tools/call of a long-running tool (translate-focused-blueprint) is answered
	 * on the same request when its call finishes: as text/event-stream with notifications/progress and then the
	 * JSON-RPC response when CanStreamToolProgress(), the Accept header lists text/event-stream and
	 * params._meta.progressToken is set; otherwise as one application/json response.
	 */
	bool HandleMcpRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** True on engines whose HTTPServer can stream a response (UE 5.8 and later); see the .cpp for 5.6 and 5.7 */
	static bool CanStreamToolProgress();

private:
	// Private constructor for singleton
	FN2CMcpHttpServerManager();

	// Non-copyable
	FN2CMcpHttpServerManager(const FN2CMcpHttpServerManager&) = delete;
	FN2CMcpHttpServerManager& operator=(const FN2CMcpHttpServerManager&) = delete;

	/**
	 * Handles health check requests.
	 */
	bool HandleHealthRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/**
	 * Registers MCP tools with the tool manager.
	 */
	void RegisterMcpTools();

	/**
	 * Registers MCP resources with the resource manager.
	 */
	void RegisterMcpResources();

	/**
	 * Registers MCP prompts with the prompt manager.
	 */
	void RegisterMcpPrompts();

	/** Whether the server is currently running */
	bool bIsServerRunning = false;

	/** The port the server is listening on */
	int32 ServerPort = -1;

	/** HTTP server module reference */
	FHttpServerModule* HttpServerModule = nullptr;

	/** HTTP router for handling requests */
	TSharedPtr<IHttpRouter> HttpRouter;

	/** The protocol core every /mcp request goes to (sessions, dispatch, long-running calls) */
	TSharedPtr<FN2CMcpProtocol> Protocol;
};
