// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Server/N2CMcpSSEResponseManager.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FN2CMcpSSEResponseManager& FN2CMcpSSEResponseManager::Get()
{
	static FN2CMcpSSEResponseManager Instance;
	return Instance;
}

FString FN2CMcpSSEResponseManager::CreateSSEConnection(const TSharedPtr<FHttpServerRequest>& Request, const FString& SessionId,
	const TSharedPtr<FJsonValue>& OriginalRequestId, const FString& ProgressToken, const FGuid& TaskId)
{
	if (!Request.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("Cannot create SSE connection with invalid request"));
		return FString();
	}

	// Generate connection ID
	FString ConnectionId = FGuid::NewGuid().ToString();

	// Create SSE response with appropriate headers
	auto Response = MakeShared<FHttpServerResponse>();
	Response->Code = EHttpServerResponseCodes::Ok;
	Response->Headers.Add(TEXT("Content-Type"), { TEXT("text/event-stream") });
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
	Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
	
	// Add MCP session ID header
	if (!SessionId.IsEmpty())
	{
		Response->Headers.Add(TEXT("Mcp-Session-Id"), { SessionId });
	}

	// Create connection context
	auto Connection = MakeShared<FSSEConnection>();
	Connection->Request = Request;
	Connection->Response = Response;
	Connection->SessionId = SessionId;
	Connection->OriginalRequestId = OriginalRequestId;
	Connection->ProgressToken = ProgressToken;
	Connection->TaskId = TaskId;
	Connection->bIsActive = true;
	Connection->ConnectionTime = FDateTime::Now();
	Connection->ConnectionId = ConnectionId; // Store the generated ConnectionId

	// Store connection
	{
		FScopeLock Lock(&ConnectionMapLock);
		ActiveConnections.Add(ConnectionId, Connection);
	}

	// Send initial SSE comment to establish connection
	// The FormatSSEEvent will now correctly format this as an SSE comment line.
	WriteSSEEvent(Connection, TEXT(""), TEXT("comment: Connection established"));

	// Send initial task started notification
	FJsonRpcNotification TaskStartedNotification;
	TaskStartedNotification.Method = TEXT("nodetocode/taskStarted");
	
	auto ParamsObject = MakeShared<FJsonObject>();
	ParamsObject->SetStringField(TEXT("taskId"), TaskId.ToString());
	ParamsObject->SetStringField(TEXT("progressToken"), ProgressToken);
	TaskStartedNotification.Params = MakeShared<FJsonValueObject>(ParamsObject);

	// Serialize and send the notification
	FString NotificationJson = FJsonRpcUtils::SerializeNotification(TaskStartedNotification);
	WriteSSEEvent(Connection, TEXT("notification"), NotificationJson);

	FN2CLogger::Get().Log(FString::Printf(TEXT("Created SSE connection %s for task %s"), *ConnectionId, *TaskId.ToString()), EN2CLogSeverity::Info);

	return ConnectionId;
}

TSharedPtr<FHttpServerResponse> FN2CMcpSSEResponseManager::CreateSSEConnectionAndGetResponse(
    const TSharedPtr<FHttpServerRequest>& Request, 
    const FString& SessionId,
    const TSharedPtr<FJsonValue>& OriginalRequestId, 
    const FString& ProgressToken, 
    const FGuid& TaskId)
{
    if (!Request.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Cannot create SSE connection with invalid FHttpServerRequest object."));
        return nullptr;
    }

    FString InternalConnectionId = FGuid::NewGuid().ToString(); // Internal server-side ID for the connection context

    auto SseHttpResponse = MakeShared<FHttpServerResponse>();
    SseHttpResponse->Code = EHttpServerResponseCodes::Ok;
    SseHttpResponse->Headers.Add(TEXT("Content-Type"), { TEXT("text/event-stream") });
    SseHttpResponse->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache") });
    SseHttpResponse->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
    
    if (!SessionId.IsEmpty())
    {
        SseHttpResponse->Headers.Add(TEXT("Mcp-Session-Id"), { SessionId });
    }

    auto ConnectionContext = MakeShared<FSSEConnection>();
    ConnectionContext->Request = Request; 
    ConnectionContext->Response = SseHttpResponse; // Store the response object
    ConnectionContext->SessionId = SessionId;
    ConnectionContext->OriginalRequestId = OriginalRequestId;
    ConnectionContext->ProgressToken = ProgressToken;
    ConnectionContext->TaskId = TaskId;
    ConnectionContext->bIsActive = true;
    ConnectionContext->ConnectionTime = FDateTime::Now();
    ConnectionContext->ConnectionId = InternalConnectionId;

    {
        FScopeLock Lock(&ConnectionMapLock);
        ActiveConnections.Add(InternalConnectionId, ConnectionContext);
    }

    // Prepare initial SSE events to be part of the first response body chunk
    FString InitialEventsBody;
    // FormatSSEEvent will correctly format this as an SSE comment (e.g. ":comment: SSE connection established\n\n")
    InitialEventsBody += FormatSSEEvent(TEXT(""), TEXT("comment: SSE connection established")); 

    FJsonRpcNotification TaskStartedNotification;
    TaskStartedNotification.Method = TEXT("nodetocode/taskStarted");
    auto ParamsObject = MakeShared<FJsonObject>();
    ParamsObject->SetStringField(TEXT("taskId"), TaskId.ToString());
    ParamsObject->SetStringField(TEXT("progressToken"), ProgressToken);
    TaskStartedNotification.Params = MakeShared<FJsonValueObject>(ParamsObject);
    InitialEventsBody += FormatSSEEvent(TEXT("notification"), FJsonRpcUtils::SerializeNotification(TaskStartedNotification));

    // Append initial events to the response body
    FTCHARToUTF8 Converter(*InitialEventsBody);
    SseHttpResponse->Body.Append((const uint8*)Converter.Get(), Converter.Length());

    FN2CLogger::Get().Log(FString::Printf(TEXT("Created SSE connection context %s for task %s. Initial HTTP response prepared."), *InternalConnectionId, *TaskId.ToString()), EN2CLogSeverity::Info);

    return SseHttpResponse;
}

bool FN2CMcpSSEResponseManager::SendProgressNotification(const FString& ConnectionId, const FJsonRpcNotification& ProgressNotification)
{
	FScopeLock Lock(&ConnectionMapLock);
	
	auto Connection = ActiveConnections.Find(ConnectionId);
	if (!Connection || !Connection->IsValid() || !(*Connection)->bIsActive)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Cannot send progress to inactive connection: %s"), *ConnectionId));
		return false;
	}

	// Serialize the notification
	FString NotificationJson = FJsonRpcUtils::SerializeNotification(ProgressNotification);
	
	// Send as SSE event
	return WriteSSEEvent(*Connection, TEXT("progress"), NotificationJson);
}

bool FN2CMcpSSEResponseManager::SendFinalResponse(const FString& ConnectionId, const FJsonRpcResponse& Response)
{
	FScopeLock Lock(&ConnectionMapLock);
	
	auto Connection = ActiveConnections.Find(ConnectionId);
	if (!Connection || !Connection->IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Cannot send final response to missing connection: %s"), *ConnectionId));
		return false;
	}

	// Serialize the response
	FString ResponseJson = FJsonRpcUtils::SerializeResponse(Response);
	
	// Send as final SSE event
	bool bSuccess = WriteSSEEvent(*Connection, TEXT("response"), ResponseJson);

	// Mark connection as inactive
	(*Connection)->bIsActive = false;

	// Complete the HTTP response
	if ((*Connection)->Request.IsValid() && (*Connection)->Response.IsValid())
	{
		// The response should be completed after sending the final event
		// Note: In actual implementation, we need to properly integrate with UE's HTTP server streaming API
		FN2CLogger::Get().Log(FString::Printf(TEXT("Completed SSE connection %s"), *ConnectionId), EN2CLogSeverity::Info);
	}

	return bSuccess;
}

FString FN2CMcpSSEResponseManager::FindConnectionByTaskId(const FGuid& TaskId) const
{
	FScopeLock Lock(&ConnectionMapLock);
	
	for (const auto& Pair : ActiveConnections)
	{
		if (Pair.Value.IsValid() && Pair.Value->TaskId == TaskId)
		{
			return Pair.Key;
		}
	}
	
	return FString();
}

FString FN2CMcpSSEResponseManager::FindConnectionByProgressToken(const FString& ProgressToken) const
{
	FScopeLock Lock(&ConnectionMapLock);
	
	for (const auto& Pair : ActiveConnections)
	{
		if (Pair.Value.IsValid() && Pair.Value->ProgressToken == ProgressToken)
		{
			return Pair.Key;
		}
	}
	
	return FString();
}

void FN2CMcpSSEResponseManager::CloseConnection(const FString& ConnectionId)
{
	FScopeLock Lock(&ConnectionMapLock);
	
	auto Connection = ActiveConnections.Find(ConnectionId);
	if (Connection && Connection->IsValid())
	{
		(*Connection)->bIsActive = false;
		
		// Send connection close event
		WriteSSEEvent(*Connection, TEXT(""), TEXT("Connection closing"));
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("Closed SSE connection %s"), *ConnectionId), EN2CLogSeverity::Info);
	}
	
	ActiveConnections.Remove(ConnectionId);
}

void FN2CMcpSSEResponseManager::CloseAllConnections()
{
	FScopeLock Lock(&ConnectionMapLock);
	
	for (auto& Pair : ActiveConnections)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->bIsActive = false;
		}
	}
	
	int32 Count = ActiveConnections.Num();
	ActiveConnections.Empty();
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Closed all %d SSE connections"), Count), EN2CLogSeverity::Info);
}

int32 FN2CMcpSSEResponseManager::GetActiveConnectionCount() const
{
	FScopeLock Lock(&ConnectionMapLock);
	
	int32 ActiveCount = 0;
	for (const auto& Pair : ActiveConnections)
	{
		if (Pair.Value.IsValid() && Pair.Value->bIsActive)
		{
			ActiveCount++;
		}
	}
	
	return ActiveCount;
}

void FN2CMcpSSEResponseManager::CleanupInactiveConnections()
{
	FScopeLock Lock(&ConnectionMapLock);
	
	TArray<FString> ConnectionsToRemove;
	FDateTime Now = FDateTime::Now();
	
	for (const auto& Pair : ActiveConnections)
	{
		if (!Pair.Value.IsValid() || !Pair.Value->bIsActive ||
			(Now - Pair.Value->ConnectionTime).GetTotalSeconds() > CONNECTION_TIMEOUT_SECONDS)
		{
			ConnectionsToRemove.Add(Pair.Key);
		}
	}
	
	for (const FString& ConnectionId : ConnectionsToRemove)
	{
		ActiveConnections.Remove(ConnectionId);
	}
	
	if (ConnectionsToRemove.Num() > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Cleaned up %d inactive SSE connections"), ConnectionsToRemove.Num()), EN2CLogSeverity::Debug);
	}
}

bool FN2CMcpSSEResponseManager::WriteSSEEvent(const TSharedPtr<FSSEConnection>& Connection, const FString& EventType, const FString& Data)
{
	if (!Connection.IsValid() || !Connection->bIsActive)
	{
		return false;
	}

	// Format as SSE event
	FString SSEEvent = FormatSSEEvent(EventType, Data);
	
	// Convert to UTF-8 bytes
	FTCHARToUTF8 Converter(*SSEEvent);
	TArray<uint8> EventBytes;
	EventBytes.Append((const uint8*)Converter.Get(), Converter.Length());

	// Write to response stream
	// Note: In actual implementation, we need to use the proper HTTP server streaming API
	// For now, we're accumulating in the response body
	if (Connection->Response.IsValid())
	{
		Connection->Response->Body.Append(EventBytes);
		
		// In a real streaming implementation, we would flush the response here
		// Connection->Response->Flush(); // This doesn't exist in the current API
		
		return true;
	}

	return false;
}

FString FN2CMcpSSEResponseManager::FormatSSEEvent(const FString& EventType, const FString& Data) const
{
	FString SSEEvent;

	// Handle SSE comments
	// If EventType is empty and Data starts with "comment:", treat as an SSE comment.
	if (EventType.IsEmpty() && Data.StartsWith(TEXT("comment:")))
	{
		// SSE comments start with a colon.
		// Example: ": This is a comment"
		// The "comment:" prefix is a convention for our internal Data string, 
		// the actual SSE comment line starts with ':'
		SSEEvent += FString::Printf(TEXT(":%s\n\n"), *Data); // Ensure double newline to terminate the comment/event
	}
	else
	{
		// Add event type if specified
		if (!EventType.IsEmpty())
		{
			SSEEvent += FString::Printf(TEXT("event: %s\n"), *EventType);
		}
		
		// Split data into lines and format each line
		TArray<FString> Lines;
		Data.ParseIntoArray(Lines, TEXT("\n"), false);
		
		for (const FString& Line : Lines)
		{
			SSEEvent += FString::Printf(TEXT("data: %s\n"), *Line);
		}
		
		// End event with double newline
		SSEEvent += TEXT("\n");
	}
	
	return SSEEvent;
}
