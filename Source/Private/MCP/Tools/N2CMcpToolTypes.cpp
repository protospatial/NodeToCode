// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpToolTypesPrivate
{
	void SetHint(const TSharedRef<FJsonObject>& Json, const TCHAR* Field, const TOptional<bool>& Hint)
	{
		if (Hint.IsSet())
		{
			Json->SetBoolField(Field, Hint.GetValue());
		}
	}

	TOptional<bool> GetHint(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		bool bValue = false;
		return Json->TryGetBoolField(Field, bValue) ? TOptional<bool>(bValue) : TOptional<bool>();
	}
}

TSharedPtr<FJsonObject> FMcpToolAnnotations::ToJson() const
{
	using namespace N2CMcpToolTypesPrivate;

	if (!IsSet())
	{
		return nullptr;
	}
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	SetHint(Json, TEXT("readOnlyHint"), ReadOnlyHint);
	SetHint(Json, TEXT("destructiveHint"), DestructiveHint);
	SetHint(Json, TEXT("idempotentHint"), IdempotentHint);
	SetHint(Json, TEXT("openWorldHint"), OpenWorldHint);
	return Json;
}

FMcpToolAnnotations FMcpToolAnnotations::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	using namespace N2CMcpToolTypesPrivate;

	FMcpToolAnnotations Annotations;
	if (JsonObject.IsValid())
	{
		Annotations.ReadOnlyHint = GetHint(JsonObject, TEXT("readOnlyHint"));
		Annotations.DestructiveHint = GetHint(JsonObject, TEXT("destructiveHint"));
		Annotations.IdempotentHint = GetHint(JsonObject, TEXT("idempotentHint"));
		Annotations.OpenWorldHint = GetHint(JsonObject, TEXT("openWorldHint"));
	}
	return Annotations;
}

TSharedPtr<FJsonObject> FMcpToolDefinition::BuildMeta() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	if (Meta.IsValid())
	{
		Json->Values = Meta->Values;
	}
	if (!Category.IsEmpty())
	{
		Json->SetStringField(N2CMcpToolMetaKeys::Category, Category);
	}
	if (MaxResultSizeChars.IsSet())
	{
		Json->SetNumberField(N2CMcpToolMetaKeys::MaxResultSizeChars, MaxResultSizeChars.GetValue());
	}
	if (bAlwaysLoad)
	{
		Json->SetBoolField(N2CMcpToolMetaKeys::AlwaysLoad, true);
	}
	return Json->Values.Num() > 0 ? TSharedPtr<FJsonObject>(Json) : nullptr;
}

TSharedPtr<FJsonObject> FMcpToolDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);

	JsonObject->SetStringField(TEXT("name"), Name);

	if (!Title.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("title"), Title);
	}

	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}

	if (InputSchema.IsValid())
	{
		JsonObject->SetObjectField(TEXT("inputSchema"), InputSchema);
	}

	if (OutputSchema.IsValid())
	{
		JsonObject->SetObjectField(TEXT("outputSchema"), OutputSchema);
	}

	if (const TSharedPtr<FJsonObject> AnnotationsJson = Annotations.ToJson())
	{
		JsonObject->SetObjectField(TEXT("annotations"), AnnotationsJson);
	}

	// Category is not a Tool field in MCP: it travels in _meta under a prefixed key (MCP 2025-11-25, _meta)
	if (const TSharedPtr<FJsonObject> MetaJson = BuildMeta())
	{
		JsonObject->SetObjectField(TEXT("_meta"), MetaJson);
	}

	return JsonObject;
}

FMcpToolDefinition FMcpToolDefinition::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FMcpToolDefinition Definition;

	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("name"), Definition.Name);
		JsonObject->TryGetStringField(TEXT("title"), Definition.Title);
		JsonObject->TryGetStringField(TEXT("description"), Definition.Description);

		const TSharedPtr<FJsonObject>* InputSchemaObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("inputSchema"), InputSchemaObj))
		{
			Definition.InputSchema = *InputSchemaObj;
		}

		const TSharedPtr<FJsonObject>* OutputSchemaObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("outputSchema"), OutputSchemaObj))
		{
			Definition.OutputSchema = *OutputSchemaObj;
		}

		const TSharedPtr<FJsonObject>* AnnotationsObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("annotations"), AnnotationsObj))
		{
			Definition.Annotations = FMcpToolAnnotations::FromJson(*AnnotationsObj);
		}

		// Older NodeToCode servers sent the category as a top-level field
		JsonObject->TryGetStringField(TEXT("category"), Definition.Category);

		const TSharedPtr<FJsonObject>* MetaObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("_meta"), MetaObj))
		{
			const TSharedRef<FJsonObject> Rest = MakeShared<FJsonObject>();
			Rest->Values = (*MetaObj)->Values;

			FString MetaCategory;
			if (Rest->TryGetStringField(N2CMcpToolMetaKeys::Category, MetaCategory))
			{
				Definition.Category = MetaCategory;
				Rest->RemoveField(N2CMcpToolMetaKeys::Category);
			}
			int32 MaxChars = 0;
			if (Rest->TryGetNumberField(N2CMcpToolMetaKeys::MaxResultSizeChars, MaxChars))
			{
				Definition.MaxResultSizeChars = MaxChars;
				Rest->RemoveField(N2CMcpToolMetaKeys::MaxResultSizeChars);
			}
			bool bAlwaysLoad = false;
			if (Rest->TryGetBoolField(N2CMcpToolMetaKeys::AlwaysLoad, bAlwaysLoad))
			{
				Definition.bAlwaysLoad = bAlwaysLoad;
				Rest->RemoveField(N2CMcpToolMetaKeys::AlwaysLoad);
			}
			if (Rest->Values.Num() > 0)
			{
				Definition.Meta = Rest;
			}
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

	if (StructuredContent.IsValid())
	{
		JsonObject->SetObjectField(TEXT("structuredContent"), StructuredContent);
	}

	if (bIsError)
	{
		JsonObject->SetBoolField(TEXT("isError"), true);
	}

	return JsonObject;
}
