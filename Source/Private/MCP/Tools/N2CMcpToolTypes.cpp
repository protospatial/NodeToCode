// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Serialization/JsonSerializer.h"

TSharedPtr<FJsonObject> FMcpToolDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("name"), Name);
	
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}

	if (!Category.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("category"), Category);
	}
	
	if (InputSchema.IsValid())
	{
		JsonObject->SetObjectField(TEXT("inputSchema"), InputSchema);
	}
	
	if (Annotations.IsValid())
	{
		JsonObject->SetObjectField(TEXT("annotations"), Annotations);
	}
	
	return JsonObject;
}

FMcpToolDefinition FMcpToolDefinition::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FMcpToolDefinition Definition;
	
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("name"), Definition.Name);
		JsonObject->TryGetStringField(TEXT("description"), Definition.Description);
		JsonObject->TryGetStringField(TEXT("category"), Definition.Category);
		
		const TSharedPtr<FJsonObject>* InputSchemaObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("inputSchema"), InputSchemaObj))
		{
			Definition.InputSchema = *InputSchemaObj;
		}
		
		const TSharedPtr<FJsonObject>* AnnotationsObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("annotations"), AnnotationsObj))
		{
			Definition.Annotations = *AnnotationsObj;
		}
	}
	
	return Definition;
}

FMcpToolCallResult FMcpToolCallResult::CreateTextResult(const FString& Text, bool bInIsError)
{
	FMcpToolCallResult Result;
	Result.bIsError = bInIsError;
	
	TSharedPtr<FJsonObject> TextContent = MakeShareable(new FJsonObject);
	TextContent->SetStringField(TEXT("type"), TEXT("text"));
	TextContent->SetStringField(TEXT("text"), Text);
	
	Result.Content.Add(TextContent);
	return Result;
}

FMcpToolCallResult FMcpToolCallResult::CreateErrorResult(const FString& ErrorMessage)
{
	FMcpToolCallResult Result;
	Result.bIsError = true;
	
	TSharedPtr<FJsonObject> ErrorContent = MakeShareable(new FJsonObject);
	ErrorContent->SetStringField(TEXT("type"), TEXT("text"));
	ErrorContent->SetStringField(TEXT("text"), ErrorMessage);
	
	Result.Content.Add(ErrorContent);
	return Result;
}

TSharedPtr<FJsonObject> FMcpToolCallResult::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	// Create content array
	TArray<TSharedPtr<FJsonValue>> ContentArray;
	for (const TSharedPtr<FJsonObject>& ContentItem : Content)
	{
		if (ContentItem.IsValid())
		{
			ContentArray.Add(MakeShareable(new FJsonValueObject(ContentItem)));
		}
	}
	
	JsonObject->SetArrayField(TEXT("content"), ContentArray);
	
	if (bIsError)
	{
		JsonObject->SetBoolField(TEXT("isError"), true);
	}
	
	return JsonObject;
}