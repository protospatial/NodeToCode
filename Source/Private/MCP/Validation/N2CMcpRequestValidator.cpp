// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Validation/N2CMcpRequestValidator.h"
#include "Utils/N2CLogger.h"

bool FN2CMcpRequestValidator::ValidateToolsCallRequest(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	if (!Params.IsValid())
	{
		OutError = TEXT("Params object is null");
		return false;
	}

	// Validate required 'name' field
	FString ToolName;
	if (!ValidateRequiredString(Params, TEXT("name"), ToolName, OutError))
	{
		return false;
	}

	// Validate optional 'arguments' field
	TSharedPtr<FJsonObject> Arguments;
	if (!ValidateOptionalObject(Params, TEXT("arguments"), Arguments))
	{
		OutError = TEXT("'arguments' field must be an object if present");
		return false;
	}

	return true;
}

bool FN2CMcpRequestValidator::ValidateResourcesReadRequest(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	if (!Params.IsValid())
	{
		OutError = TEXT("Params object is null");
		return false;
	}

	// Validate required 'uri' field
	FString Uri;
	if (!ValidateRequiredString(Params, TEXT("uri"), Uri, OutError))
	{
		return false;
	}

	return true;
}

bool FN2CMcpRequestValidator::ValidatePromptsGetRequest(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	if (!Params.IsValid())
	{
		OutError = TEXT("Params object is null");
		return false;
	}

	// Validate required 'name' field
	FString PromptName;
	if (!ValidateRequiredString(Params, TEXT("name"), PromptName, OutError))
	{
		return false;
	}

	// Validate optional 'arguments' field
	TSharedPtr<FJsonObject> Arguments;
	if (!ValidateOptionalObject(Params, TEXT("arguments"), Arguments))
	{
		OutError = TEXT("'arguments' field must be an object if present");
		return false;
	}

	return true;
}

bool FN2CMcpRequestValidator::ValidateRequiredString(const TSharedPtr<FJsonObject>& Object, 
                                                     const FString& FieldName, 
                                                     FString& OutValue, 
                                                     FString& OutError)
{
	if (!Object.IsValid())
	{
		OutError = TEXT("Object is null");
		return false;
	}

	if (!Object->HasField(FieldName))
	{
		OutError = FString::Printf(TEXT("Missing required field: %s"), *FieldName);
		return false;
	}

	if (!Object->TryGetStringField(FieldName, OutValue))
	{
		OutError = FString::Printf(TEXT("Field '%s' must be a string"), *FieldName);
		return false;
	}

	if (OutValue.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Field '%s' cannot be empty"), *FieldName);
		return false;
	}

	return true;
}

bool FN2CMcpRequestValidator::ValidateOptionalObject(const TSharedPtr<FJsonObject>& Object,
                                                     const FString& FieldName,
                                                     TSharedPtr<FJsonObject>& OutValue)
{
	OutValue = nullptr;
	
	if (!Object.IsValid() || !Object->HasField(FieldName))
	{
		// Field is optional, so missing is OK
		return true;
	}

	// Field exists, must be a valid object
	const TSharedPtr<FJsonObject>* ObjectField = nullptr;
	if (Object->TryGetObjectField(FieldName, ObjectField) && ObjectField->IsValid())
	{
		OutValue = *ObjectField;
		return true;
	}

	// Field exists but is not an object
	return false;
}

bool FN2CMcpRequestValidator::ValidateParamsNotNull(const TSharedPtr<FJsonValue>& Params, FString& OutError)
{
	if (!Params.IsValid() || Params->IsNull())
	{
		OutError = TEXT("Missing or null params");
		return false;
	}

	return true;
}

bool FN2CMcpRequestValidator::ValidateParamsIsObject(const TSharedPtr<FJsonValue>& Params,
                                                     TSharedPtr<FJsonObject>& OutObject,
                                                     FString& OutError)
{
	if (!ValidateParamsNotNull(Params, OutError))
	{
		return false;
	}

	if (Params->Type != EJson::Object)
	{
		OutError = TEXT("Params must be an object");
		return false;
	}

	OutObject = Params->AsObject();
	return true;
}