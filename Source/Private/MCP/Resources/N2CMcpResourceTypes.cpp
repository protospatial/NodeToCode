// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Resources/N2CMcpResourceTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

TSharedPtr<FJsonObject> FMcpResourceDefinition::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("uri"), Uri);
	JsonObject->SetStringField(TEXT("name"), Name);
	
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}
	
	if (!MimeType.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("mimeType"), MimeType);
	}
	
	if (Annotations.IsValid())
	{
		JsonObject->SetObjectField(TEXT("annotations"), Annotations);
	}
	
	return JsonObject;
}

FMcpResourceDefinition FMcpResourceDefinition::FromJson(const TSharedPtr<FJsonObject>& JsonObject)
{
	FMcpResourceDefinition Definition;
	
	if (JsonObject.IsValid())
	{
		JsonObject->TryGetStringField(TEXT("uri"), Definition.Uri);
		JsonObject->TryGetStringField(TEXT("name"), Definition.Name);
		JsonObject->TryGetStringField(TEXT("description"), Definition.Description);
		JsonObject->TryGetStringField(TEXT("mimeType"), Definition.MimeType);
		
		const TSharedPtr<FJsonObject>* AnnotationsObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("annotations"), AnnotationsObj))
		{
			Definition.Annotations = *AnnotationsObj;
		}
	}
	
	return Definition;
}

TSharedPtr<FJsonObject> FMcpResourceContents::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("uri"), Uri);
	
	if (!MimeType.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("mimeType"), MimeType);
	}
	
	if (IsText())
	{
		// Text content
		JsonObject->SetStringField(TEXT("text"), Text);
	}
	else if (BlobData.Num() > 0)
	{
		// Binary content - encode as base64
		JsonObject->SetStringField(TEXT("blob"), GetBase64Blob());
	}
	
	return JsonObject;
}

FString FMcpResourceContents::GetBase64Blob() const
{
	if (BlobData.Num() > 0)
	{
		return FBase64::Encode(BlobData);
	}
	return FString();
}

TSharedPtr<FJsonObject> FMcpResourceTemplate::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("uriTemplate"), UriTemplate);
	JsonObject->SetStringField(TEXT("name"), Name);
	
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}
	
	if (!MimeType.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("mimeType"), MimeType);
	}
	
	if (Annotations.IsValid())
	{
		JsonObject->SetObjectField(TEXT("annotations"), Annotations);
	}
	
	return JsonObject;
}

void FMcpResourceContents::SetError(int32 Code, const FString& Message)
{
	ErrorCode = Code;
	ErrorMessage = Message;
	MimeType = TEXT("application/json");
	BlobData.Reset();
	// Built with FJsonObject so the uri is escaped
	const TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
	Error->SetStringField(TEXT("error"), Message);
	Error->SetStringField(TEXT("uri"), Uri);
	Text.Reset();
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
	FJsonSerializer::Serialize(Error, Writer);
}
