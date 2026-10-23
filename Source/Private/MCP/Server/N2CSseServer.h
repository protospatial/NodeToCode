// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>

// Forward declare httplib types to avoid including httplib.h in this header
namespace httplib {
    class Server;
    struct Request;
    struct Response;
    class DataSink;
}

namespace NodeToCodeSseServer
{
    struct SseClientConnection; // Forward declaration

    /**
     * Prepares an SSE stream context for a given TaskId.
     * This should be called when an async task is launched and an SSE connection is expected.
     * @param TaskId The Task ID for which to prepare the stream.
     */
    void PrepareSseStreamForTask(const FGuid& TaskId);

    /**
     * How long a finished task's stream (its queued result) waits for a client that never connected before it is
     * dropped. A client that connects in time reads the result and the stream is released when it closes.
     */
    constexpr double CompletedStreamTtlSeconds = 600.0;

    /**
     * Drops the stream contexts of tasks that finished at least MaxAgeSeconds ago and that no client is reading.
     * Runs on every PrepareSseStreamForTask, so streams nobody connects to do not accumulate.
     * @param MaxAgeSeconds Minimum time since the task signalled completion.
     * @return The number of streams dropped.
     */
    int32 ExpireCompletedStreams(double MaxAgeSeconds = CompletedStreamTtlSeconds);

    /**
     * Removes the stream context of a task that never started (for example its launch failed), unless a client is
     * already reading it.
     * @param TaskId The Task ID passed to PrepareSseStreamForTask.
     */
    void DiscardStreamForTask(const FGuid& TaskId);

    /**
     * Checks whether a stream context exists for the task (prepared and not yet released, discarded or expired).
     * @param TaskId The Task ID.
     */
    bool HasStreamForTask(const FGuid& TaskId);

    /**
     * Starts the SSE HTTP server on a separate thread.
     * @param Port The port number for the SSE server to listen on.
     * @return True if the server started successfully, false otherwise.
     */
    bool StartSseServer(int32 Port);

    /**
     * Stops the SSE HTTP server.
     */
    void StopSseServer();

    /**
     * Checks if the SSE server is currently running.
     * @return True if the server is running.
     */
    bool IsSseServerRunning();

    /**
     * Gets the port the SSE server is running on.
     * @return The port number, or -1 if not running.
     */
    int32 GetSseServerPort();

    /**
     * Pushes an SSE-formatted event string to a specific client's queue.
     * @param TaskId The Task ID identifying the client connection.
     * @param SseMessage The fully formatted SSE message string (including "event:", "data:", and newlines).
     */
    void PushFormattedSseEventToClient(const FGuid& TaskId, const std::string& SseMessage);

    /**
     * Signals an SSE client connection that its task is complete and the stream should be closed.
     * @param TaskId The Task ID identifying the client connection.
     */
    void SignalSseClientCompletion(const FGuid& TaskId);
    
    /**
    * Formats an event type and JSON data into an SSE message string.
    * This can be called from UE code to prepare messages for PushFormattedSseEventToClient.
    * @param EventType The type of the SSE event (e.g., "progress", "response"). Can be empty for default "message" events or comments.
    * @param JsonData The JSON string data for the event.
    * @return A fully formatted SSE message string.
    */
    std::string FormatSseMessage(const FString& EventType, const FString& JsonData);

    /**
     * Pushes a notification event to all connected general notification clients.
     * @param SseMessage The fully formatted SSE message string.
     */
    void PushNotificationToAllClients(const std::string& SseMessage);

} // namespace NodeToCodeSseServer
