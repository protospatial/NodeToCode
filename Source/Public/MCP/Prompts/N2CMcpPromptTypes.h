// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MCP/Resources/N2CMcpResourceTypes.h"
#include "N2CMcpPromptTypes.generated.h"

/**
 * Represents an argument for a prompt
 */
USTRUCT()
struct NODETOCODE_API FMcpPromptArgument
{
	GENERATED_BODY()

	/** Name of the argument */
	UPROPERTY()
	FString Name;

	/** Human-readable name for display (MCP "title"; omitted when empty) */
	UPROPERTY()
	FString Title;

	/** Description of what this argument is for */
	UPROPERTY()
	FString Description;

	/** Whether this argument is required */
	UPROPERTY()
	bool bRequired = false;

	/**
	 * Converts this argument to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Represents a prompt definition in the MCP protocol
 */
USTRUCT()
struct NODETOCODE_API FMcpPromptDefinition
{
	GENERATED_BODY()

	/** Unique name for this prompt */
	UPROPERTY()
	FString Name;

	/** Human-readable name for display (MCP "title"; omitted when empty) */
	UPROPERTY()
	FString Title;

	/** Description of what this prompt does */
	UPROPERTY()
	FString Description;

	/** Arguments this prompt accepts */
	UPROPERTY()
	TArray<FMcpPromptArgument> Arguments;

	/**
	 * Converts this prompt definition to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Content item in a prompt message
 */
USTRUCT()
struct NODETOCODE_API FMcpPromptContent
{
	GENERATED_BODY()

	/** Type of content (text, image, resource) */
	UPROPERTY()
	FString Type;

	/** Text content (if type is text) */
	UPROPERTY()
	FString Text;

	/** Resource content (if type is resource) */
	FMcpResourceContents Resource;

	/** Optional MCP annotations ("audience", "priority"), sent as "annotations" when set */
	TSharedPtr<FJsonObject> Annotations;

	/**
	 * Converts this content to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Message in a prompt result
 */
USTRUCT()
struct NODETOCODE_API FMcpPromptMessage
{
	GENERATED_BODY()

	/** Role of the message sender (user, assistant, system) */
	UPROPERTY()
	FString Role;

	/** Content of the message */
	UPROPERTY()
	FMcpPromptContent Content;

	/**
	 * Converts this message to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Result of getting a prompt
 */
USTRUCT()
struct NODETOCODE_API FMcpPromptResult
{
	GENERATED_BODY()

	/** Description of what this prompt instance does */
	UPROPERTY()
	FString Description;

	/** Messages that make up this prompt */
	UPROPERTY()
	TArray<FMcpPromptMessage> Messages;

	/**
	 * Set when getting the prompt failed: the JSON-RPC error code prompts/get answers with (InvalidParams for an
	 * unknown prompt or a missing required argument, InternalError for a handler that timed out), and its message.
	 * 0 when the prompt was produced.
	 */
	int32 ErrorCode = 0;
	FString ErrorMessage;

	/** True when getting the prompt failed (ErrorCode is set) */
	bool IsError() const { return ErrorCode != 0; }

	/**
	 * Converts this result to JSON format
	 */
	TSharedPtr<FJsonObject> ToJson() const;
};

/**
 * Type alias for prompt arguments map
 */
using FMcpPromptArguments = TMap<FString, FString>;

/**
 * Delegate for handling prompt get requests
 */
DECLARE_DELEGATE_RetVal_OneParam(FMcpPromptResult, FMcpPromptGetDelegate, const FMcpPromptArguments& /* Arguments */);

/**
 * Interface for implementing MCP prompts
 */
class NODETOCODE_API IN2CMcpPrompt
{
public:
	virtual ~IN2CMcpPrompt() = default;

	/**
	 * Gets the prompt definition
	 */
	virtual FMcpPromptDefinition GetDefinition() const = 0;

	/**
	 * Gets the prompt with the provided arguments
	 */
	virtual FMcpPromptResult GetPrompt(const FMcpPromptArguments& Arguments) = 0;

	/**
	 * Checks if this prompt requires Game Thread execution
	 */
	virtual bool RequiresGameThread() const { return false; }
};