// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "N2CMcpResourceTypes.generated.h"

/**
 * Represents a resource definition in the MCP protocol
 */
USTRUCT()
struct NODETOCODE_API FMcpResourceDefinition
{
	GENERATED_BODY()

	/** The URI of the resource */
	UPROPERTY()
	FString Uri;

	/** Human-readable name for the resource */
	UPROPERTY()
	FString Name;

	/** Description of what this resource provides */
	UPROPERTY()
	FString Description;

	/** MIME type of the resource content */
	UPROPERTY()
	FString MimeType;

	/** Optional annotations for the resource */
	TSharedPtr<FJsonObject> Annotations;

	/**
	 * Converts this resource definition to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;

	/**
	 * Creates a resource definition from JSON
	 */
	static FMcpResourceDefinition FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * Represents the contents of a resource
 */
USTRUCT()
struct NODETOCODE_API FMcpResourceContents
{
	GENERATED_BODY()

	/** The URI of this resource */
	UPROPERTY()
	FString Uri;

	/** MIME type of the content */
	UPROPERTY()
	FString MimeType;

	/** Text content (for text-based resources) */
	UPROPERTY()
	FString Text;

	/** Binary data (for non-text resources) */
	UPROPERTY()
	TArray<uint8> BlobData;

	/**
	 * Set when the read failed: the JSON-RPC error code resources/read answers with (JsonRpcErrorCodes::ResourceNotFound
	 * for an unknown uri, InternalError for a handler that timed out), and its message. 0 when the read succeeded.
	 */
	int32 ErrorCode = 0;
	FString ErrorMessage;

	/** True when the read failed (ErrorCode is set) */
	bool IsError() const { return ErrorCode != 0; }

	/**
	 * Marks this as a failed read: resources/read answers with Code and Message (and the uri in data). Text becomes
	 * the error as JSON {error, uri}, for callers that embed the contents (the code-generation prompts).
	 */
	void SetError(int32 Code, const FString& Message);

	/**
	 * Checks if this resource contains text data
	 */
	bool IsText() const { return !Text.IsEmpty(); }

	/**
	 * Gets the binary data as a base64-encoded string
	 */
	FString GetBase64Blob() const;

	/**
	 * Converts this resource contents to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Template for dynamic resources
 */
USTRUCT()
struct NODETOCODE_API FMcpResourceTemplate
{
	GENERATED_BODY()

	/** URI pattern with {placeholder} variables */
	UPROPERTY()
	FString UriTemplate;

	/** Human-readable name for this type of resource */
	UPROPERTY()
	FString Name;

	/** Description of what these resources provide */
	UPROPERTY()
	FString Description;

	/** MIME type of resources matching this template */
	UPROPERTY()
	FString MimeType;

	/** Optional annotations */
	TSharedPtr<FJsonObject> Annotations;

	/**
	 * Converts this template to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Delegate for handling resource read requests
 */
DECLARE_DELEGATE_RetVal_OneParam(FMcpResourceContents, FMcpResourceReadDelegate, const FString& /* Uri */);

/**
 * Delegate for handling dynamic resource template requests
 */
DECLARE_DELEGATE_RetVal_OneParam(FMcpResourceContents, FMcpResourceTemplateHandler, const FString& /* Uri */);

/**
 * Interface for implementing MCP resources
 */
class NODETOCODE_API IN2CMcpResource
{
public:
	virtual ~IN2CMcpResource() = default;

	/**
	 * Gets the resource definition
	 */
	virtual FMcpResourceDefinition GetDefinition() const = 0;

	/**
	 * Reads the resource contents
	 */
	virtual FMcpResourceContents Read(const FString& Uri) = 0;

	/**
	 * Checks if this resource supports subscriptions
	 */
	virtual bool CanSubscribe() const { return false; }

	/**
	 * Checks if this resource requires Game Thread execution
	 */
	virtual bool RequiresGameThread() const { return false; }
};