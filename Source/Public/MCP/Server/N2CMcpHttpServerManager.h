// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HttpServerModule.h"
#include "HttpRouteHandle.h"
#include "Containers/Ticker.h"
#include "IHttpRouter.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"

class FN2CMcpProtocol;

/**
 * The HTTP transport of the MCP server: binds /mcp and /mcp/health on the engine's HTTPServer, applies the
 * local-request policy (Origin, Host, and Content-Type for POST) and hands every /mcp request (POST, DELETE, and GET,
 * PUT or PATCH, which get 405) to the transport-neutral protocol core (FN2CMcpProtocol,
 * Source/Private/MCP/Server/N2CMcpProtocol.h) with an HTTPServer responder. Sessions are per client (Mcp-Session-Id).
 *
 * Ports: StartServer takes the first free port from Port to Port + PortRange - 1. A port another process (or another
 * plugin in this editor) already holds is skipped; when the server ends up on another port than the one asked for,
 * or on none, the log and an editor notification say so and name the Server Port setting. The HTTPServer router is
 * shared with the rest of the editor (Remote Control, UE 5.8's Unreal MCP plugin), so the server only binds and
 * unbinds its own routes and never stops HTTPServer listeners. While it runs, a discovery file
 * (<Project>/Saved/NodeToCode/mcp-server.json and <UserSettingsDir>/NodeToCode/servers/<pid>.json) names the port,
 * project, process and versions, so a client can find the right editor.
 */
class NODETOCODE_API FN2CMcpHttpServerManager
{
public:
	/** Default number of ports StartServer tries (Server Port and the next 9) */
	static constexpr int32 DefaultPortRange = 10;

	/** Highest port the server takes: the port+1 notification server needs the next one */
	static constexpr int32 MaxServerPort = 65534;

	/**
	 * Gets the singleton instance of the MCP HTTP server manager.
	 */
	static FN2CMcpHttpServerManager& Get();

	~FN2CMcpHttpServerManager();

	/**
	 * Starts the HTTP server on the first free port from Port to Port + PortRange - 1 (never above MaxServerPort).
	 * Logs a warning and shows a notification when it is not on Port; logs an error and shows a notification naming
	 * the ports and the Server Port setting when none is free.
	 * @return true if the server is running (on GetServerPort()), false otherwise
	 */
	bool StartServer(int32 Port = 27000, int32 PortRange = DefaultPortRange);

	/**
	 * Unbinds the server's routes and removes its discovery file. HTTPServer listeners keep running, since other
	 * plugins share them; the listener on the old port answers 404 for /mcp until the editor exits.
	 */
	void StopServer();

	/**
	 * Starts the HTTP server and the port+1 notification server from the settings (McpServerPort,
	 * McpServerPortRange). Called once by the module; later port changes restart through HandlePortSettingsChanged.
	 * @return true if the HTTP server is running
	 */
	bool StartConfiguredServers();

	/** Stops the notification server and the HTTP server (module shutdown) */
	void StopConfiguredServers();

	/**
	 * Restarts both servers on the current McpServerPort and McpServerPortRange, on the next tick, when the module
	 * started them (not when the server is off or skipped in a commandlet). On failure StartServer's error
	 * notification says what to change.
	 */
	void HandlePortSettingsChanged();

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

	/** The port the last StartServer asked for (the setting), or -1 before the first start */
	int32 GetRequestedPort() const { return RequestedPort; }

	/** One line on where the server listens or why it is not running (shown as Server Status in the settings) */
	const FString& GetStatusText() const { return StatusText; }

	/** Path of this project's discovery file (<Project>/Saved/NodeToCode/mcp-server.json unless overridden) */
	FString GetDiscoveryFilePath() const;

	/** Path of this process's entry in the per-user discovery index (<UserSettingsDir>/NodeToCode/servers/<pid>.json) */
	FString GetDiscoveryIndexFilePath() const;

	/** Tests: write the discovery files to ProjectFile and IndexDir instead (empty strings restore the defaults) */
	void SetDiscoveryPathsForTesting(const FString& ProjectFile, const FString& IndexDir);

	/** Tests: receive (message, bIsError) instead of showing editor notifications (nullptr restores them) */
	void SetNotificationHandlerForTesting(TFunction<void(const FString&, bool)> Handler);

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
	 * Binds the routes on Port when nothing else holds it. On failure leaves no route bound and sets OutReason.
	 */
	bool TryStartOnPort(int32 Port, FString& OutReason);

	/** Unbinds the routes this server bound and releases the router */
	void UnbindRoutes();

	/** Writes the discovery file and the per-user index entry for the running server */
	void WriteDiscoveryFiles() const;

	/** Removes this process's discovery files (the project file only while it still names this process) */
	void RemoveDiscoveryFiles() const;

	/** Logs Message (an error when bIsError, else a warning or, with !bLogAsWarning, info) and shows it as an editor notification */
	void Notify(const FString& Message, bool bIsError, bool bLogAsWarning = true);

	/** HandlePortSettingsChanged's deferred part */
	void RestartConfiguredServers();

	/** Sets the status text and mirrors it into the settings page's Server Status */
	void SetStatus(const FString& InStatus);

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

	/** Whether the module started the servers from the settings (a port change then restarts them) */
	bool bConfiguredServersStarted = false;

	/** A restart from HandlePortSettingsChanged is queued for the next tick */
	bool bRestartPending = false;

	/** The queued restart's ticker */
	FTSTicker::FDelegateHandle RestartTickerHandle;

	/** The port the server is listening on */
	int32 ServerPort = -1;

	/** The port the last StartServer asked for */
	int32 RequestedPort = -1;

	/** See GetStatusText */
	FString StatusText = TEXT("Not started");

	/** Ports whose HTTPServer listener this server started earlier in this process (still bound after a stop) */
	TSet<int32> OwnedListenerPorts;

	/** HTTP server module reference */
	FHttpServerModule* HttpServerModule = nullptr;

	/** HTTP router for handling requests */
	TSharedPtr<IHttpRouter> HttpRouter;

	/** The routes bound on HttpRouter (/mcp and /mcp/health), unbound in StopServer */
	TArray<FHttpRouteHandle> RouteHandles;

	/** Test overrides (see SetDiscoveryPathsForTesting and SetNotificationHandlerForTesting) */
	FString DiscoveryFileOverride;
	FString DiscoveryIndexDirOverride;
	TFunction<void(const FString&, bool)> NotificationHandlerOverride;

	/** The protocol core every /mcp request goes to (sessions, dispatch, long-running calls) */
	TSharedPtr<FN2CMcpProtocol> Protocol;
};
