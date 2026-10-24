// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Validates incoming MCP requests against their expected schemas.
 * Provides both specific validation methods for known request types
 * and generic validation helpers for common patterns.
 */
class NODETOCODE_API FN2CMcpRequestValidator
{
public:
	/**
	 * Validates a tools/call request.
	 * @param Params The request parameters to validate
	 * @param OutError Error message if validation fails
	 * @return true if the request is valid
	 */
	static bool ValidateToolsCallRequest(const TSharedPtr<FJsonObject>& Params, FString& OutError);

	/**
	 * Validates a resources/read request.
	 * @param Params The request parameters to validate
	 * @param OutError Error message if validation fails
	 * @return true if the request is valid
	 */
	static bool ValidateResourcesReadRequest(const TSharedPtr<FJsonObject>& Params, FString& OutError);

	/**
	 * Validates a prompts/get request.
	 * @param Params The request parameters to validate
	 * @param OutError Error message if validation fails
	 * @return true if the request is valid
	 */
	static bool ValidatePromptsGetRequest(const TSharedPtr<FJsonObject>& Params, FString& OutError);

	// Generic validation helpers

	/**
	 * Validates that a required string field exists and extracts its value.
	 * @param Object The JSON object to validate
	 * @param FieldName The name of the field to check
	 * @param OutValue The extracted string value
	 * @param OutError Error message if validation fails
	 * @return true if the field exists and is a valid string
	 */
	static bool ValidateRequiredString(const TSharedPtr<FJsonObject>& Object, 
	                                  const FString& FieldName, 
	                                  FString& OutValue, 
	                                  FString& OutError);

	/**
	 * Validates an optional object field and extracts its value if present.
	 * @param Object The JSON object to validate
	 * @param FieldName The name of the field to check
	 * @param OutValue The extracted object value (null if not present)
	 * @return true if the field is not present or is a valid object
	 */
	static bool ValidateOptionalObject(const TSharedPtr<FJsonObject>& Object,
	                                  const FString& FieldName,
	                                  TSharedPtr<FJsonObject>& OutValue);

	/**
	 * Validates that request parameters are not null.
	 * @param Params The parameters to validate
	 * @param OutError Error message if validation fails
	 * @return true if params are valid
	 */
	static bool ValidateParamsNotNull(const TSharedPtr<FJsonValue>& Params, FString& OutError);

	/**
	 * Validates that request parameters are a JSON object.
	 * @param Params The parameters to validate
	 * @param OutObject The extracted JSON object
	 * @param OutError Error message if validation fails
	 * @return true if params are a valid object
	 */
	static bool ValidateParamsIsObject(const TSharedPtr<FJsonValue>& Params,
	                                  TSharedPtr<FJsonObject>& OutObject,
	                                  FString& OutError);
};