// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CMcpResourceTypes.h"

/**
 * Entry for a static resource with its handler
 */
struct FMcpResourceEntry
{
	FMcpResourceDefinition Definition;
	FMcpResourceReadDelegate Handler;
	bool bRequiresGameThread = false;
};

/**
 * Entry for a dynamic resource template
 */
struct FMcpResourceTemplateEntry
{
	FMcpResourceTemplate Template;
	FMcpResourceTemplateHandler Handler;
	bool bRequiresGameThread = false;
};

/**
 * Manages MCP resources for the NodeToCode plugin
 * Singleton pattern for global resource management
 */
class NODETOCODE_API FN2CMcpResourceManager
{
public:
	/**
	 * Gets the singleton instance
	 */
	static FN2CMcpResourceManager& Get();

	/**
	 * Registers a static resource
	 * @param Definition The resource definition
	 * @param Handler The handler delegate for reading the resource
	 * @param bRequiresGameThread Whether the handler must run on game thread
	 * @return true if successfully registered
	 */
	bool RegisterStaticResource(const FMcpResourceDefinition& Definition, 
	                           const FMcpResourceReadDelegate& Handler,
	                           bool bRequiresGameThread = false);

	/**
	 * Registers a dynamic resource template
	 * @param Template The resource template
	 * @param Handler The handler for resources matching this template
	 * @param bRequiresGameThread Whether the handler must run on game thread
	 * @return true if successfully registered
	 */
	bool RegisterDynamicResource(const FMcpResourceTemplate& Template,
	                            const FMcpResourceTemplateHandler& Handler,
	                            bool bRequiresGameThread = false);

	/**
	 * Unregisters a static resource
	 * @param Uri The URI of the resource to unregister
	 * @return true if successfully unregistered
	 */
	bool UnregisterStaticResource(const FString& Uri);

	/**
	 * Unregisters every dynamic resource template with this URI template
	 * @return true if one was registered
	 */
	bool UnregisterDynamicResource(const FString& UriTemplate);

	/**
	 * Lists all available resources
	 * @param Cursor Optional cursor for pagination (not implemented in MVP)
	 * @return Array of resource definitions
	 */
	TArray<FMcpResourceDefinition> ListResources(const FString& Cursor = TEXT("")) const;

	/**
	 * Lists all resource templates
	 * @return Array of resource templates
	 */
	TArray<FMcpResourceTemplate> ListResourceTemplates() const;

	/**
	 * Reads a resource by URI
	 * @param Uri The URI of the resource to read
	 * @return The resource contents
	 */
	FMcpResourceContents ReadResource(const FString& Uri);

	/**
	 * Checks if a resource exists
	 * @param Uri The URI to check
	 * @return true if the resource exists
	 */
	bool IsResourceRegistered(const FString& Uri) const;

	/**
	 * Subscribe to resource updates (Phase 3)
	 * @param Uri The resource URI
	 * @param SubscriptionId The subscription identifier
	 * @return true if successfully subscribed
	 */
	bool SubscribeToResource(const FString& Uri, const FString& SubscriptionId);

	/**
	 * Unsubscribe from resource updates
	 * @param Uri The resource URI
	 * @param SubscriptionId The subscription identifier
	 * @return true if successfully unsubscribed
	 */
	bool UnsubscribeFromResource(const FString& Uri, const FString& SubscriptionId);

	/**
	 * Notify that a resource has been updated
	 * @param Uri The URI of the updated resource
	 */
	void NotifyResourceUpdated(const FString& Uri);

	/**
	 * Clears all registered resources
	 */
	void ClearAllResources();

private:
	// Private constructor for singleton
	FN2CMcpResourceManager() = default;

	// Non-copyable
	FN2CMcpResourceManager(const FN2CMcpResourceManager&) = delete;
	FN2CMcpResourceManager& operator=(const FN2CMcpResourceManager&) = delete;

	/**
	 * Checks if a URI matches a template pattern: the whole URI, {var} as one non-empty segment and {+var} as the rest
	 * of the path, values percent-decoded (FN2CMcpUriTemplate)
	 * @param Uri The URI to check
	 * @param Template The template pattern
	 * @param OutParams Extracted parameters from the template
	 * @return true if the URI matches the template
	 */
	bool MatchesTemplate(const FString& Uri, const FString& Template, TMap<FString, FString>& OutParams) const;

	/** Static resources mapped by URI */
	TMap<FString, FMcpResourceEntry> StaticResources;

	/** Dynamic resource templates */
	TArray<FMcpResourceTemplateEntry> DynamicResources;

	/** Resource subscriptions - URI -> SubscriptionIds */
	TMultiMap<FString, FString> ResourceSubscriptions;

	/** Thread safety */
	mutable FCriticalSection ResourceLock;
};