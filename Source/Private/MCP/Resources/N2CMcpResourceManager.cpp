// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Resources/N2CMcpResourceManager.h"
#include "MCP/Resources/N2CMcpUriTemplate.h"
#include "Utils/N2CLogger.h"
#include "Async/Async.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace N2CMcpResourceManagerPrivate
{
	/** Marks Contents as a failed read of Uri (FMcpResourceContents::SetError) */
	static void SetReadError(FMcpResourceContents& Contents, const FString& Uri, int32 Code, const FString& Message)
	{
		Contents.Uri = Uri;
		Contents.SetError(Code, Message);
	}
}

FN2CMcpResourceManager& FN2CMcpResourceManager::Get()
{
	static FN2CMcpResourceManager Instance;
	return Instance;
}

bool FN2CMcpResourceManager::RegisterStaticResource(const FMcpResourceDefinition& Definition, 
                                                   const FMcpResourceReadDelegate& Handler,
                                                   bool bRequiresGameThread)
{
	if (Definition.Uri.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register resource with empty URI"));
		return false;
	}

	if (!Handler.IsBound())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register resource with unbound handler"));
		return false;
	}

	FScopeLock Lock(&ResourceLock);

	if (StaticResources.Contains(Definition.Uri))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Resource already registered: %s"), *Definition.Uri));
		return false;
	}

	FMcpResourceEntry Entry;
	Entry.Definition = Definition;
	Entry.Handler = Handler;
	Entry.bRequiresGameThread = bRequiresGameThread;

	StaticResources.Add(Definition.Uri, Entry);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Registered static resource: %s"), *Definition.Uri), EN2CLogSeverity::Info);
	
	// TODO: In Phase 3, notify subscribers about new resource
	
	return true;
}

bool FN2CMcpResourceManager::RegisterDynamicResource(const FMcpResourceTemplate& Template,
                                                    const FMcpResourceTemplateHandler& Handler,
                                                    bool bRequiresGameThread)
{
	if (Template.UriTemplate.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register resource template with empty URI template"));
		return false;
	}

	if (!Handler.IsBound())
	{
		FN2CLogger::Get().LogWarning(TEXT("Cannot register resource template with unbound handler"));
		return false;
	}

	if (!FN2CMcpUriTemplate(Template.UriTemplate).IsValid())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Cannot register resource template with an unsupported URI template: %s"), *Template.UriTemplate));
		return false;
	}

	FScopeLock Lock(&ResourceLock);

	FMcpResourceTemplateEntry Entry;
	Entry.Template = Template;
	Entry.Handler = Handler;
	Entry.bRequiresGameThread = bRequiresGameThread;

	DynamicResources.Add(Entry);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Registered dynamic resource template: %s"), *Template.UriTemplate), EN2CLogSeverity::Info);
	
	return true;
}

bool FN2CMcpResourceManager::UnregisterStaticResource(const FString& Uri)
{
	FScopeLock Lock(&ResourceLock);

	if (StaticResources.Remove(Uri) > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Unregistered static resource: %s"), *Uri), EN2CLogSeverity::Info);
		return true;
	}

	return false;
}

bool FN2CMcpResourceManager::UnregisterDynamicResource(const FString& UriTemplate)
{
	FScopeLock Lock(&ResourceLock);

	const int32 Removed = DynamicResources.RemoveAll([&UriTemplate](const FMcpResourceTemplateEntry& Entry)
	{
		return Entry.Template.UriTemplate == UriTemplate;
	});
	if (Removed > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Unregistered dynamic resource template: %s"), *UriTemplate), EN2CLogSeverity::Info);
	}
	return Removed > 0;
}

TArray<FMcpResourceDefinition> FN2CMcpResourceManager::ListResources(const FString& Cursor) const
{
	FScopeLock Lock(&ResourceLock);

	TArray<FMcpResourceDefinition> Resources;
	
	// Add all static resources
	for (const auto& Pair : StaticResources)
	{
		Resources.Add(Pair.Value.Definition);
	}

	// TODO: In a full implementation, we might also list instances of dynamic resources
	// For now, we'll just return static resources
	
	// TODO: Implement cursor-based pagination if needed
	
	return Resources;
}

TArray<FMcpResourceTemplate> FN2CMcpResourceManager::ListResourceTemplates() const
{
	FScopeLock Lock(&ResourceLock);

	TArray<FMcpResourceTemplate> Templates;
	
	for (const FMcpResourceTemplateEntry& Entry : DynamicResources)
	{
		Templates.Add(Entry.Template);
	}
	
	return Templates;
}

FMcpResourceContents FN2CMcpResourceManager::ReadResource(const FString& Uri)
{
	FMcpResourceContents Contents;
	Contents.Uri = Uri;

	// First check static resources
	{
		FScopeLock Lock(&ResourceLock);
		
		if (const FMcpResourceEntry* Entry = StaticResources.Find(Uri))
		{
			if (Entry->Handler.IsBound())
			{
				if (Entry->bRequiresGameThread && !IsInGameThread())
				{
					// Execute on game thread and wait for result
					TPromise<FMcpResourceContents> Promise;
					TFuture<FMcpResourceContents> Future = Promise.GetFuture();
					
					AsyncTask(ENamedThreads::GameThread, [Handler = Entry->Handler, Uri, Promise = MoveTemp(Promise)]() mutable
					{
						Promise.SetValue(Handler.Execute(Uri));
					});
					
					// Wait for result with timeout
					if (Future.WaitFor(FTimespan::FromSeconds(5.0)))
					{
						Contents = Future.Get();
					}
					else
					{
						FN2CLogger::Get().LogError(FString::Printf(TEXT("Timeout reading resource on game thread: %s"), *Uri));
						N2CMcpResourceManagerPrivate::SetReadError(Contents, Uri, JsonRpcErrorCodes::InternalError, TEXT("Timed out reading the resource on the game thread"));
					}
				}
				else
				{
					Contents = Entry->Handler.Execute(Uri);
				}
				
				// Ensure MIME type is set
				if (Contents.MimeType.IsEmpty() && !Entry->Definition.MimeType.IsEmpty())
				{
					Contents.MimeType = Entry->Definition.MimeType;
				}
				
				return Contents;
			}
		}
	}

	// Check dynamic resources
	TMap<FString, FString> Params;
	{
		FScopeLock Lock(&ResourceLock);
		
		for (const FMcpResourceTemplateEntry& Entry : DynamicResources)
		{
			if (MatchesTemplate(Uri, Entry.Template.UriTemplate, Params))
			{
				if (Entry.Handler.IsBound())
				{
					if (Entry.bRequiresGameThread && !IsInGameThread())
					{
						// Execute on game thread and wait for result
						TPromise<FMcpResourceContents> Promise;
						TFuture<FMcpResourceContents> Future = Promise.GetFuture();
						
						AsyncTask(ENamedThreads::GameThread, [Handler = Entry.Handler, Uri, Promise = MoveTemp(Promise)]() mutable
						{
							Promise.SetValue(Handler.Execute(Uri));
						});
						
						// Wait for result with timeout
						if (Future.WaitFor(FTimespan::FromSeconds(5.0)))
						{
							Contents = Future.Get();
						}
						else
						{
							FN2CLogger::Get().LogError(FString::Printf(TEXT("Timeout reading dynamic resource on game thread: %s"), *Uri));
							N2CMcpResourceManagerPrivate::SetReadError(Contents, Uri, JsonRpcErrorCodes::InternalError, TEXT("Timed out reading the resource on the game thread"));
						}
					}
					else
					{
						Contents = Entry.Handler.Execute(Uri);
					}
					
					// Ensure MIME type is set
					if (Contents.MimeType.IsEmpty() && !Entry.Template.MimeType.IsEmpty())
					{
						Contents.MimeType = Entry.Template.MimeType;
					}
					
					return Contents;
				}
			}
		}
	}

	// Resource not found
	FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Resource not found: %s"), *Uri));
	N2CMcpResourceManagerPrivate::SetReadError(Contents, Uri, JsonRpcErrorCodes::ResourceNotFound, TEXT("Resource not found"));
	
	return Contents;
}

bool FN2CMcpResourceManager::IsResourceRegistered(const FString& Uri) const
{
	FScopeLock Lock(&ResourceLock);
	
	// Check static resources
	if (StaticResources.Contains(Uri))
	{
		return true;
	}
	
	// Check if URI matches any dynamic template
	TMap<FString, FString> Params;
	for (const FMcpResourceTemplateEntry& Entry : DynamicResources)
	{
		if (MatchesTemplate(Uri, Entry.Template.UriTemplate, Params))
		{
			return true;
		}
	}
	
	return false;
}

bool FN2CMcpResourceManager::SubscribeToResource(const FString& Uri, const FString& SubscriptionId)
{
	if (Uri.IsEmpty() || SubscriptionId.IsEmpty())
	{
		return false;
	}

	FScopeLock Lock(&ResourceLock);
	
	// Check if resource exists
	if (!IsResourceRegistered(Uri))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Cannot subscribe to non-existent resource: %s"), *Uri));
		return false;
	}
	
	ResourceSubscriptions.AddUnique(Uri, SubscriptionId);
	
	FN2CLogger::Get().Log(FString::Printf(TEXT("Subscribed to resource %s with ID %s"), *Uri, *SubscriptionId), EN2CLogSeverity::Debug);
	
	return true;
}

bool FN2CMcpResourceManager::UnsubscribeFromResource(const FString& Uri, const FString& SubscriptionId)
{
	FScopeLock Lock(&ResourceLock);
	
	return ResourceSubscriptions.RemoveSingle(Uri, SubscriptionId) > 0;
}

void FN2CMcpResourceManager::NotifyResourceUpdated(const FString& Uri)
{
	TArray<FString> Subscribers;
	{
		FScopeLock Lock(&ResourceLock);
		ResourceSubscriptions.MultiFind(Uri, Subscribers);
	}
	
	if (Subscribers.Num() > 0)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Resource updated: %s (notifying %d subscribers)"), *Uri, Subscribers.Num()), EN2CLogSeverity::Debug);
		
		// TODO: In Phase 3, implement actual notification sending via MCP server
	}
}

void FN2CMcpResourceManager::ClearAllResources()
{
	FScopeLock Lock(&ResourceLock);
	
	StaticResources.Empty();
	DynamicResources.Empty();
	ResourceSubscriptions.Empty();
	
	FN2CLogger::Get().Log(TEXT("Cleared all registered resources"), EN2CLogSeverity::Info);
}

bool FN2CMcpResourceManager::MatchesTemplate(const FString& Uri, const FString& Template, TMap<FString, FString>& OutParams) const
{
	// A full match: {var} is one non-empty segment, {+var} the rest of the path, values percent-decoded (card 0208)
	return FN2CMcpUriTemplate(Template).Match(Uri, OutParams);
}
