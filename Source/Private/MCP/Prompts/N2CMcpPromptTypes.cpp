// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Prompts/N2CMcpPromptTypes.h"
#include "Serialization/JsonSerializer.h"

TSharedPtr<FJsonObject> FMcpPromptArgument::ToJson() const
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
	
	JsonObject->SetBoolField(TEXT("required"), bRequired);
	
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptDefinition::ToJson() const
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
	
	if (Arguments.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ArgumentsArray;
		for (const FMcpPromptArgument& Arg : Arguments)
		{
			TSharedPtr<FJsonObject> ArgJson = Arg.ToJson();
			if (ArgJson.IsValid())
			{
				ArgumentsArray.Add(MakeShareable(new FJsonValueObject(ArgJson)));
			}
		}
		JsonObject->SetArrayField(TEXT("arguments"), ArgumentsArray);
	}
	
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptContent::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("type"), Type);
	
	if (Type == TEXT("text"))
	{
		JsonObject->SetStringField(TEXT("text"), Text);
	}
	else if (Type == TEXT("resource"))
	{
		TSharedPtr<FJsonObject> ResourceJson = Resource.ToJson();
		if (ResourceJson.IsValid())
		{
			JsonObject->SetObjectField(TEXT("resource"), ResourceJson);
		}
	}

	if (Annotations.IsValid())
	{
		JsonObject->SetObjectField(TEXT("annotations"), Annotations);
	}
	
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptMessage::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	JsonObject->SetStringField(TEXT("role"), Role);
	
	TSharedPtr<FJsonObject> ContentJson = Content.ToJson();
	if (ContentJson.IsValid())
	{
		JsonObject->SetObjectField(TEXT("content"), ContentJson);
	}
	
	return JsonObject;
}

TSharedPtr<FJsonObject> FMcpPromptResult::ToJson() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	
	if (!Description.IsEmpty())
	{
		JsonObject->SetStringField(TEXT("description"), Description);
	}
	
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FMcpPromptMessage& Message : Messages)
	{
		TSharedPtr<FJsonObject> MessageJson = Message.ToJson();
		if (MessageJson.IsValid())
		{
			MessagesArray.Add(MakeShareable(new FJsonValueObject(MessageJson)));
		}
	}
	JsonObject->SetArrayField(TEXT("messages"), MessagesArray);
	
	return JsonObject;
}