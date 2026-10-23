// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Server/N2CSseServer.h"
#include "Utils/N2CLogger.h"
#include "HAL/PlatformProcess.h"
#include "HAL/CriticalSection.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"

// Include httplib.h here in the .cpp file
// httplib.h includes Windows headers which define macros that conflict with UE methods
// We must use Unreal's platform type guards to prevent macro pollution
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#pragma warning(push)
// Disable specific warnings httplib.h might trigger
#pragma warning(disable : 4267) // size_t to int truncation
#pragma warning(disable : 4244) // conversion from 'X' to 'Y', possible loss of data
#endif

#include "httplib.h"

#if PLATFORM_WINDOWS
#pragma warning(pop)
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace NodeToCodeSseServer
{
    // Structure to hold connection-specific data for each SSE client
    struct SseClientConnection
    {
        std::queue<std::string> event_queue;
        std::mutex queue_mutex;
        std::condition_variable cv;
        std::atomic<bool> stop_requested{false};
        FGuid taskId;
        std::atomic<bool> is_http_stream_active{false}; // True when httplib is actively using this for a response stream
    };

    // Map for active SSE clients
    TMap<FGuid, TSharedPtr<SseClientConnection>> GActiveSseClients;
    FCriticalSection GActiveSseClientsMutex; // UE's critical section

    // Map for general notification clients
    TMap<FString, TSharedPtr<SseClientConnection>> GNotificationClients;
    FCriticalSection GNotificationClientsMutex;

    // httplib server instance and thread
    TSharedPtr<httplib::Server> GSseHttpServer;
    std::unique_ptr<std::thread> GSseServerThreadPtr; // Use unique_ptr for better management
    std::atomic<bool> GbSseServerIsRunning{false};
    int32 GSseServerPort = -1;

    void PrepareSseStreamForTask(const FGuid& TaskId)
    {
        FScopeLock Lock(&GActiveSseClientsMutex);
        if (GActiveSseClients.Contains(TaskId))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: PrepareSseStreamForTask called for already existing TaskId %s."), *TaskId.ToString()));
            // Potentially retrieve and return existing, or just log and proceed if behavior is idempotent.
            // For now, just ensure it exists.
            return;
        }
        auto NewClientConn = MakeShared<SseClientConnection>();
        NewClientConn->taskId = TaskId;
        // is_http_stream_active defaults to false
        GActiveSseClients.Add(TaskId, NewClientConn);
        FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Prepared stream context for TaskId %s. Client connection pending."), *TaskId.ToString()), EN2CLogSeverity::Info);
    }

    void CleanupStreamForCompletedTask(const FGuid& TaskId)
    {
        FScopeLock Lock(&GActiveSseClientsMutex);
        TSharedPtr<SseClientConnection>* FoundConnPtr = GActiveSseClients.Find(TaskId);
        if (FoundConnPtr && FoundConnPtr->IsValid())
        {
            // If the task is marked to stop (completed/cancelled) AND httplib is not actively streaming for it,
            // then it's safe to clean up. This handles cases where client never connected.
            if ((*FoundConnPtr)->stop_requested.load() && !(*FoundConnPtr)->is_http_stream_active.load()) {
                GActiveSseClients.Remove(TaskId);
                FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Cleaned up orphaned/completed stream for TaskId %s."), *TaskId.ToString()), EN2CLogSeverity::Info);
            }
            // If is_http_stream_active is true, the releaser lambda will handle cleanup when the stream ends.
            // If stop_requested is false, the task isn't fully done from SSE perspective, so don't remove yet.
        }
    }

    std::string FormatSseMessage(const FString& EventType, const FString& JsonData)
    {
        std::string sse_message;
        if (!EventType.IsEmpty())
        {
            sse_message += "event: " + std::string(TCHAR_TO_UTF8(*EventType)) + "\n";
        }
        
        TArray<FString> Lines;
        // Ensure JsonData is parsed correctly as lines, even if it contains newlines itself
        FString TempData = JsonData;
        TempData = TempData.Replace(TEXT("\r\n"), TEXT("\n")); // Normalize newlines
        TempData.ParseIntoArrayLines(Lines, false); // false to keep empty lines if any

        for (const FString& Line : Lines)
        {
            sse_message += "data: " + std::string(TCHAR_TO_UTF8(*Line)) + "\n";
        }
        sse_message += "\n"; // Double newline to end the event
        return sse_message;
    }

    void PushNotificationToAllClients(const std::string& SseMessage)
    {
        FScopeLock Lock(&GNotificationClientsMutex);
        for (auto& Pair : GNotificationClients)
        {
            if (Pair.Value)
            {
                {
                    std::lock_guard<std::mutex> q_lock(Pair.Value->queue_mutex);
                    if (Pair.Value->stop_requested.load()) {
                        continue; // Skip stopped clients
                    }
                    Pair.Value->event_queue.push(SseMessage);
                }
                Pair.Value->cv.notify_one();
            }
        }
        FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Pushed notification to %d clients. Message: %s"), GNotificationClients.Num(), UTF8_TO_TCHAR(SseMessage.substr(0, 100).c_str())), EN2CLogSeverity::Debug);
    }

    void PushFormattedSseEventToClient(const FGuid& TaskId, const std::string& SseMessage)
    {
        TSharedPtr<SseClientConnection> ClientConn;
        {
            FScopeLock Lock(&GActiveSseClientsMutex);
            TSharedPtr<SseClientConnection>* FoundConn = GActiveSseClients.Find(TaskId);
            if (FoundConn)
            {
                ClientConn = *FoundConn;
            }
        }

        if (ClientConn)
        {
            {
                std::lock_guard<std::mutex> q_lock(ClientConn->queue_mutex);
                if (ClientConn->stop_requested.load()) {
                     FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Client %s is stopping, not queuing event."), *TaskId.ToString()), EN2CLogSeverity::Debug);
                    return;
                }
                ClientConn->event_queue.push(SseMessage);
            }
            ClientConn->cv.notify_one();
            FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Queued event for TaskId %s. Message: %s"), *TaskId.ToString(), UTF8_TO_TCHAR(SseMessage.substr(0, 100).c_str())), EN2CLogSeverity::Debug);
        }
        else
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: No active client connection found for TaskId %s to push event."), *TaskId.ToString()));
        }
    }

    void SignalSseClientCompletion(const FGuid& TaskId)
    {
        TSharedPtr<SseClientConnection> ClientConn;
        {
            FScopeLock Lock(&GActiveSseClientsMutex);
            TSharedPtr<SseClientConnection>* FoundConn = GActiveSseClients.Find(TaskId);
            if (FoundConn)
            {
                ClientConn = *FoundConn;
            }
        }

        if (ClientConn)
        {
            {
                std::lock_guard<std::mutex> q_lock(ClientConn->queue_mutex);
                ClientConn->stop_requested.store(true);
            }
            ClientConn->cv.notify_one();
            FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Signaled completion for TaskId %s."), *TaskId.ToString()), EN2CLogSeverity::Info);
        }
         else
        {
             FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: No active client connection found for TaskId %s to signal completion."), *TaskId.ToString()));
        }
    }

    bool StartSseServer(int32 Port)
    {
        if (GbSseServerIsRunning.load()) {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: Server already running on port %d."), GSseServerPort));
            return true;
        }

        GSseServerPort = Port;
        GSseHttpServer = MakeShareable(new httplib::Server());

        if (GSseHttpServer.IsValid()) {
            // Same policy as the /mcp listener: only loopback Origins and Hosts, so a web page or a
            // DNS-rebinding attack cannot open the notification streams.
            GSseHttpServer->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
                auto IsLoopback = [](std::string HostPort) {
                    if (!HostPort.empty() && HostPort[0] == '[') {
                        const size_t Close = HostPort.find(']');
                        HostPort = Close == std::string::npos ? HostPort : HostPort.substr(0, Close + 1);
                    } else {
                        const size_t Colon = HostPort.find(':');
                        if (Colon != std::string::npos) HostPort = HostPort.substr(0, Colon);
                    }
                    for (char& C : HostPort) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
                    return HostPort == "localhost" || HostPort == "127.0.0.1" || HostPort == "[::1]";
                };
                const std::string Origin = req.get_header_value("Origin");
                if (!Origin.empty()) {
                    const size_t Scheme = Origin.find("://");
                    if (Scheme == std::string::npos || !IsLoopback(Origin.substr(Scheme + 3))) {
                        res.status = 403;
                        return httplib::Server::HandlerResponse::Handled;
                    }
                }
                const std::string Host = req.get_header_value("Host");
                if (!Host.empty() && !IsLoopback(Host)) {
                    res.status = 403;
                    return httplib::Server::HandlerResponse::Handled;
                }
                return httplib::Server::HandlerResponse::Unhandled;
            });
        }

        if (!GSseHttpServer.IsValid()) {
            FN2CLogger::Get().LogError(TEXT("SSE: Failed to create httplib::Server instance."));
            return false;
        }
        
        // Route for SSE events: /mcp/events/{taskIdGUID}
        // Regex: TaskId is a GUID (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)
        // Example: /mcp/events/123e4567-e89b-12d3-a456-426614174000
        GSseHttpServer->Get(R"(/mcp/events/([0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}))",
            [](const httplib::Request& req, httplib::Response& res) {

            FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: HTTP GET request received for path: %s"), UTF8_TO_TCHAR(req.path.c_str())), EN2CLogSeverity::Info);
            if (req.matches.size() > 1) {
                FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Matched TaskId from URL: %s"), UTF8_TO_TCHAR(req.matches[1].str().c_str())), EN2CLogSeverity::Info);
            } else {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: Path %s matched regex route, but no TaskId captured. Matches.size: %d"), UTF8_TO_TCHAR(req.path.c_str()), req.matches.size()));
                // This indicates a problem with the regex or httplib's matching.
            }
            
            FGuid TaskId;
            FString TaskIdStr = UTF8_TO_TCHAR(req.matches[1].str().c_str());
            
            if (!FGuid::Parse(TaskIdStr, TaskId) || !TaskId.IsValid())
            {
                res.status = 400; // Bad Request
                res.set_content("Invalid Task ID format in URL path.", "text/plain");
                FN2CLogger::Get().LogError(FString::Printf(TEXT("SSE: Received request with invalid TaskId URL format: %s"), *TaskIdStr));
                return;
            }

            TSharedPtr<SseClientConnection> ClientConn;
            {
                FScopeLock Lock(&GActiveSseClientsMutex);
                TSharedPtr<SseClientConnection>* FoundConnPtr = GActiveSseClients.Find(TaskId);
                if (FoundConnPtr && FoundConnPtr->IsValid()) {
                    ClientConn = *FoundConnPtr;
                    if (ClientConn->is_http_stream_active.load()) {
                        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: TaskId %s already has an active HTTP stream. Rejecting new connection."), *TaskIdStr));
                        res.status = 409; // Conflict
                        res.set_content("Task ID already has an active SSE stream.", "text/plain");
                        return;
                    }
                    if (ClientConn->stop_requested.load() && ClientConn->event_queue.empty()) {
                         FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: Client connected for TaskId %s, but task was already completed/cancelled and queue is empty."), *TaskIdStr));
                         res.status = 410; // Gone
                         res.set_content("Task already completed or cancelled.", "text/plain");
                         // The releaser lambda won't be called if we return early, so clean up here.
                         GActiveSseClients.Remove(TaskId);
                         return;
                    }
                }
            }

            if (!ClientConn) {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("SSE: Client connected for TaskId %s, but no SseClientConnection was pre-registered. This indicates an internal error."), *TaskIdStr));
                res.status = 500; // Internal Server Error
                res.set_content("Internal server error: SSE stream context not found.", "text/plain");
                return;
            }

            FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Client connected for TaskId: %s. Activating stream."), *TaskIdStr), EN2CLogSeverity::Info);
            ClientConn->is_http_stream_active.store(true); // Mark as active for httplib streaming

            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
            
            // Send an initial comment to establish the connection immediately.
            std::string InitialComment = FormatSseMessage(TEXT(""), TEXT(": SSE connection established for task ") + TaskIdStr);
            // We push this to the queue, the content_provider will send it.
            {
                 std::lock_guard<std::mutex> q_lock(ClientConn->queue_mutex);
                 ClientConn->event_queue.push(InitialComment);
            }
            // No cv.notify_one() here, the content_provider loop will pick it up on its first run.

            res.set_content_provider(
                "text/event-stream", 
                [ClientConn](size_t offset, httplib::DataSink &sink) -> bool {
                    std::unique_lock<std::mutex> q_lock(ClientConn->queue_mutex);
                    
                    // Wait until there's something in the queue or stop is requested
                    ClientConn->cv.wait(q_lock, [&ClientConn] {
                        return !ClientConn->event_queue.empty() || ClientConn->stop_requested.load();
                    });

                    bool bStopAfterThisWrite = false;
                    if (ClientConn->stop_requested.load() && ClientConn->event_queue.empty()) {
                        q_lock.unlock(); // Release before calling sink.done()
                        sink.done(); // Signal end of stream
                        FN2CLogger::Get().Log(FString::Printf(TEXT("SSE Provider: TaskId %s stop_requested and queue empty. Sink done."), *ClientConn->taskId.ToString()), EN2CLogSeverity::Debug);
                        return false; // Stop the provider loop
                    }

                    if (!ClientConn->event_queue.empty()) {
                        std::string event_data_to_send = ClientConn->event_queue.front();
                        ClientConn->event_queue.pop();

                        // Check again if this is the last event after popping
                        if (ClientConn->stop_requested.load() && ClientConn->event_queue.empty()) {
                             bStopAfterThisWrite = true;
                        }
                        q_lock.unlock(); // Release mutex before writing to sink
                        
                        if (!sink.write(event_data_to_send.c_str(), event_data_to_send.length())) {
                            FN2CLogger::Get().LogError(FString::Printf(TEXT("SSE Provider: sink.write failed for TaskId %s."), *ClientConn->taskId.ToString()));
                            ClientConn->stop_requested.store(true); // Ensure cleanup on write failure
                            // Do not call sink.done() here, let the releaser handle it or httplib closes it
                            return false; // Stop the provider loop on write failure
                        }

                        if (bStopAfterThisWrite) {
                            sink.done();
                            FN2CLogger::Get().Log(FString::Printf(TEXT("SSE Provider: TaskId %s sent last event and called sink.done()."), *ClientConn->taskId.ToString()), EN2CLogSeverity::Debug);
                            return false; // Stop after sending the final event and calling done
                        }
                    } else {
                         q_lock.unlock(); // Should not happen if stop_requested is false due to cv.wait condition
                    }
                    
                    return true; // Continue providing data
                },
                [ClientConn, TaskId, TaskIdStr](bool success) { // Releaser lambda, capture ClientConn
                    ClientConn->is_http_stream_active.store(false); // Mark as no longer active
                    FScopeLock Lock(&GActiveSseClientsMutex);
                    // Only remove if it's still the same instance and hasn't been replaced or already removed.
                    TSharedPtr<SseClientConnection>* CurrentEntry = GActiveSseClients.Find(TaskId);
                    if (CurrentEntry && CurrentEntry->Get() == ClientConn.Get()) {
                         GActiveSseClients.Remove(TaskId);
                    }
                    FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: Connection for TaskId %s (Path: %s) released by httplib. Success: %d"), *TaskId.ToString(), *TaskIdStr, success), EN2CLogSeverity::Info);
                }
            );
        });
        
        GSseHttpServer->Get("/mcp/notifications", [](const httplib::Request& req, httplib::Response& res) {
            FString ConnectionId = FGuid::NewGuid().ToString();
            auto ClientConn = MakeShared<SseClientConnection>();
            
            {
                FScopeLock Lock(&GNotificationClientsMutex);
                GNotificationClients.Add(ConnectionId, ClientConn);
            }
        
            FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: General notification client connected. ConnectionId: %s"), *ConnectionId), EN2CLogSeverity::Info);
        
            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");
        
            res.set_content_provider(
                "text/event-stream",
                [ClientConn](size_t offset, httplib::DataSink &sink) -> bool {
                    std::unique_lock<std::mutex> q_lock(ClientConn->queue_mutex);
                    
                    ClientConn->cv.wait(q_lock, [&ClientConn] {
                        return !ClientConn->event_queue.empty() || ClientConn->stop_requested.load();
                    });
        
                    if (ClientConn->stop_requested.load()) {
                        q_lock.unlock();
                        sink.done();
                        return false;
                    }
        
                    if (!ClientConn->event_queue.empty()) {
                        std::string event_data_to_send = ClientConn->event_queue.front();
                        ClientConn->event_queue.pop();
                        q_lock.unlock();
                        
                        if (!sink.write(event_data_to_send.c_str(), event_data_to_send.length())) {
                            ClientConn->stop_requested.store(true);
                            return false;
                        }
                    } else {
                        q_lock.unlock();
                    }
                    
                    return true;
                },
                [ConnectionId](bool success) {
                    FScopeLock Lock(&GNotificationClientsMutex);
                    GNotificationClients.Remove(ConnectionId);
                    FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: General notification client disconnected. ConnectionId: %s. Success: %d"), *ConnectionId, success), EN2CLogSeverity::Info);
                }
            );
        });
        
        GbSseServerIsRunning.store(false); // Ensure it's false before starting thread
        GSseServerThreadPtr = std::make_unique<std::thread>([Port]() {
            FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: httplib server thread starting, attempting to listen on 127.0.0.1:%d"), Port), EN2CLogSeverity::Info);
            // GbSseServerIsRunning.store(true); // MOVED: Set this after listen() call or based on is_running()
            
            bool bListenSuccess = GSseHttpServer->listen("127.0.0.1", Port); // Loopback only. This is blocking
            
            // After listen() returns (i.e., server stopped or failed to start)
            GbSseServerIsRunning.store(false); 

            if (!bListenSuccess) {
                // This log might appear if stop() was called, or if listen failed.
                // httplib's listen() returns true if it started successfully and then was stopped.
                // It returns false if it failed to bind/listen initially.
                // However, the documentation is a bit sparse on return values for listen().
                // We'll rely more on is_running() for status.
                FN2CLogger::Get().LogError(FString::Printf(TEXT("SSE: httplib server listen() call returned. This may indicate a failure to bind or that the server was stopped. Port: %d"), Port));
            } else {
                 FN2CLogger::Get().Log(TEXT("SSE: httplib server finished listening (listen() returned true)."), EN2CLogSeverity::Info);
            }
        });

        // Wait for the server to actually start running
        int MaxWaitAttempts = 20; // e.g., 20 * 100ms = 2 seconds
        int Attempts = 0;
        while (Attempts < MaxWaitAttempts)
        {
            FPlatformProcess::Sleep(0.1f); // Wait 100ms
            if (GSseHttpServer->is_running()) {
                GbSseServerIsRunning.store(true);
                FN2CLogger::Get().Log(FString::Printf(TEXT("SSE: httplib server confirmed running on port %d after %d attempts."), Port, Attempts + 1), EN2CLogSeverity::Info);
                break;
            }
            Attempts++;
        }

        if (!GbSseServerIsRunning.load()) {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("SSE: httplib server failed to start running on port %d after %d attempts."), Port, MaxWaitAttempts));
            // If the server thread didn't start, try to join it to clean up
            if (GSseServerThreadPtr && GSseServerThreadPtr->joinable()) {
                GSseHttpServer->stop(); // Ensure listen call can exit
                GSseServerThreadPtr->join();
            }
            GSseServerThreadPtr.reset();
            return false;
        }

        return GbSseServerIsRunning.load();
    }

    void StopSseServer()
    {
        if (!GSseHttpServer.IsValid() && !GbSseServerIsRunning.load()) {
            FN2CLogger::Get().Log(TEXT("SSE: Server already stopped or not initialized."), EN2CLogSeverity::Debug);
            return;
        }

        FN2CLogger::Get().Log(TEXT("SSE: Initiating server stop..."), EN2CLogSeverity::Info);
        
        // Signal all active client connections to stop
        {
            FScopeLock Lock(&GActiveSseClientsMutex);
            for (auto& Pair : GActiveSseClients)
            {
                if (Pair.Value)
                {
                    std::lock_guard<std::mutex> q_lock(Pair.Value->queue_mutex);
                    Pair.Value->stop_requested.store(true);
                    Pair.Value->cv.notify_all(); 
                }
            }
            // Don't clear GActiveSseClients here, let the releaser lambdas do it
        }
        {
            FScopeLock Lock(&GNotificationClientsMutex);
            for (auto& Pair : GNotificationClients)
            {
                if (Pair.Value)
                {
                    std::lock_guard<std::mutex> q_lock(Pair.Value->queue_mutex);
                    Pair.Value->stop_requested.store(true);
                    Pair.Value->cv.notify_all();
                }
            }
        }

        if (GSseHttpServer.IsValid()) {
            GSseHttpServer->stop(); // This is thread-safe and signals listen() to return
        }
        
        if (GSseServerThreadPtr && GSseServerThreadPtr->joinable())
        {
            FN2CLogger::Get().Log(TEXT("SSE: Waiting for server thread to join..."), EN2CLogSeverity::Debug);
            GSseServerThreadPtr->join();
            GSseServerThreadPtr.reset();
            FN2CLogger::Get().Log(TEXT("SSE: Server thread joined."), EN2CLogSeverity::Info);
        }
        
        GSseHttpServer.Reset(); // Release the server instance
        GbSseServerIsRunning.store(false);
        GSseServerPort = -1;
        
        // Final cleanup of any remaining clients, just in case
        {
             FScopeLock Lock(&GActiveSseClientsMutex);
             if(GActiveSseClients.Num() > 0)
             {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: %d client connections remained after server stop. Clearing now."), GActiveSseClients.Num()));
                GActiveSseClients.Empty();
             }
        }
        {
             FScopeLock Lock(&GNotificationClientsMutex);
             if(GNotificationClients.Num() > 0)
             {
                FN2CLogger::Get().LogWarning(FString::Printf(TEXT("SSE: %d notification clients remained after server stop. Clearing now."), GNotificationClients.Num()));
                GNotificationClients.Empty();
             }
        }
        FN2CLogger::Get().Log(TEXT("SSE: Server fully stopped."), EN2CLogSeverity::Info);
    }

    bool IsSseServerRunning()
    {
        return GbSseServerIsRunning.load() && GSseHttpServer.IsValid() && GSseHttpServer->is_running();
    }

    int32 GetSseServerPort()
    {
        return IsSseServerRunning() ? GSseServerPort : -1;
    }

} // namespace NodeToCodeSseServer
