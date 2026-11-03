// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/Dialects/N2COpenAIChatDialect.h"

#include "LLM/N2CModelCatalog.h"
#include "LLM/N2CResponseParserBase.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

const FN2COpenAIChatDialect& FN2COpenAIChatDialect::Get()
{
	static const FN2COpenAIChatDialect Instance;
	return Instance;
}

void FN2COpenAIChatDialect::MergeInto(const TSharedRef<FJsonObject>& Target, const TSharedPtr<FJsonObject>& Fragment)
{
	if (!Fragment.IsValid())
	{
		return;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Fragment->Values)
	{
		const TSharedPtr<FJsonValue> Existing = Target->TryGetField(Field.Key);
		if (Field.Value.IsValid() && Field.Value->Type == EJson::Object && Existing.IsValid() && Existing->Type == EJson::Object)
		{
			// Copy the target's object first, so a fragment never changes a profile's own JSON
			TSharedRef<FJsonObject> Merged = MakeShared<FJsonObject>(*Existing->AsObject());
			MergeInto(Merged, Field.Value->AsObject());
			Target->SetObjectField(Field.Key, Merged);
		}
		else if (Field.Value.IsValid() && Field.Value->Type == EJson::Object)
		{
			TSharedRef<FJsonObject> Copy = MakeShared<FJsonObject>();
			MergeInto(Copy, Field.Value->AsObject());
			Target->SetObjectField(Field.Key, Copy);
		}
		else
		{
			Target->SetField(Field.Key, Field.Value);
		}
	}
}

TSharedRef<FJsonObject> FN2COpenAIChatDialect::BuildBody(const FN2CLLMRequest& Request, const FN2CProviderProfile& Profile)
{
	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Request.Model.IsEmpty() ? Profile.DefaultModel : Request.Model);

	// Always-on extras first (LM Studio: stream false), so a request field below wins over them
	MergeInto(Body, Profile.ExtraBody);

	// Thinking: the request's level, through the profile's fragments
	const FString Effort = Request.ReasoningEffort.TrimStartAndEnd();
	if (!Effort.IsEmpty())
	{
		const bool bThinking = !Effort.Equals(TEXT("none"), ESearchCase::IgnoreCase);
		MergeInto(Body, bThinking ? Profile.ThinkingOn : Profile.ThinkingOff);
		if (bThinking && !Profile.ReasoningEffortField.IsEmpty())
		{
			Body->SetStringField(Profile.ReasoningEffortField, Effort);
		}
	}

	if (Request.Temperature.IsSet())
	{
		Body->SetNumberField(TEXT("temperature"), Request.Temperature.GetValue());
	}

	TArray<TSharedPtr<FJsonValue>> Messages;
	if (!Request.SystemPrompt.IsEmpty())
	{
		TSharedRef<FJsonObject> System = MakeShared<FJsonObject>();
		System->SetStringField(TEXT("role"), TEXT("system"));
		System->SetStringField(TEXT("content"), Request.SystemPrompt);
		Messages.Add(MakeShared<FJsonValueObject>(System));
	}
	TSharedRef<FJsonObject> User = MakeShared<FJsonObject>();
	User->SetStringField(TEXT("role"), TEXT("user"));
	User->SetStringField(TEXT("content"), Request.UserMessage);
	Messages.Add(MakeShared<FJsonValueObject>(User));
	Body->SetArrayField(TEXT("messages"), Messages);

	if (Request.MaxOutputTokens > 0)
	{
		Body->SetNumberField(Profile.MaxTokensField == EN2CMaxTokensField::MaxCompletionTokens ? TEXT("max_completion_tokens") : TEXT("max_tokens"),
			Request.MaxOutputTokens);
	}

	if (Request.bAllowStructuredOutput && Request.ResponseSchema.IsValid())
	{
		TSharedRef<FJsonObject> Format = MakeShared<FJsonObject>();
		switch (Profile.StructuredOutput)
		{
		case EN2CStructuredOutputMode::JsonSchemaStrict:
		case EN2CStructuredOutputMode::JsonSchema:
			{
				Format->SetStringField(TEXT("type"), TEXT("json_schema"));
				TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
				Schema->SetStringField(TEXT("name"), Request.SchemaName);
				if (Profile.StructuredOutput == EN2CStructuredOutputMode::JsonSchemaStrict)
				{
					// A JSON boolean: some hosts reject the string "true"
					Schema->SetBoolField(TEXT("strict"), true);
				}
				Schema->SetObjectField(TEXT("schema"), Request.ResponseSchema);
				Format->SetObjectField(TEXT("json_schema"), Schema);
				Body->SetObjectField(TEXT("response_format"), Format);
			}
			break;
		case EN2CStructuredOutputMode::JsonObject:
			Format->SetStringField(TEXT("type"), TEXT("json_object"));
			Body->SetObjectField(TEXT("response_format"), Format);
			break;
		case EN2CStructuredOutputMode::PromptOnly:
			break;
		}
	}
	return Body;
}

bool FN2COpenAIChatDialect::EncodeRequest(const FN2CLLMRequest& Request, const FN2CProviderProfile& Profile, FN2CHttpRequestSpec& Out,
	FString& OutError) const
{
	if (Request.Model.TrimStartAndEnd().IsEmpty() && Profile.DefaultModel.IsEmpty())
	{
		OutError = FString::Printf(TEXT("No model for %s: set Model in the OpenAI-Compatible settings (the model ID the server serves)"),
			*Profile.DisplayName);
		return false;
	}
	Out.Url = Profile.GetEndpoint();
	Out.Headers.Reset();
	Out.Headers.Emplace(TEXT("Content-Type"), TEXT("application/json"));
	for (const TPair<FString, FString>& Header : Profile.ExtraHeaders)
	{
		Out.Headers.Add(Header);
	}
	Out.Body.Reset();
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out.Body);
	FJsonSerializer::Serialize(BuildBody(Request, Profile), Writer);
	return true;
}

FN2CLLMResult FN2COpenAIChatDialect::DecodeResponse(const FString& Body, const FN2CProviderProfile& Profile, const FString& OutputBudgetHint) const
{
	FN2CLLMResult Result;
	FN2CTranslationResponse Scratch;

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UN2CResponseParserBase::SetParseError(Scratch, TEXT("The provider response is not valid JSON"));
		Result.Error = Scratch.Error;
		return Result;
	}

	// The host's error (the HTTP handler adds the HTTP status to the body)
	if (UN2CResponseParserBase::HandleCommonErrorResponse(Root, TEXT("error"), Scratch))
	{
		Result.Error = Scratch.Error;
		return Result;
	}

	// Usage first: a truncated answer is billed too
	const TSharedPtr<FJsonObject>* Usage = nullptr;
	if (Root->TryGetObjectField(TEXT("usage"), Usage) && Usage && Usage->IsValid())
	{
		UN2CResponseParserBase::ReadOpenAICompatibleUsage(*Usage, Result.Usage);
	}

	// finish_reason, refusal, then content; reasoning is collected, never taken as the answer
	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (Root->TryGetArrayField(TEXT("choices"), Choices) && Choices && Choices->Num() > 0 && (*Choices)[0].IsValid()
		&& (*Choices)[0]->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject();
		Result.FinishReason = UN2CResponseParserBase::ReadTextField(Choice, TEXT("finish_reason"));
		const TSharedPtr<FJsonObject>* Message = nullptr;
		if (Choice->TryGetObjectField(TEXT("message"), Message) && Message && Message->IsValid())
		{
			for (const FString& Field : Profile.ReasoningFields)
			{
				const FString Text = UN2CResponseParserBase::ReadTextField(*Message, Field);
				if (!Text.IsEmpty())
				{
					Result.Reasoning = Text;
					break;
				}
			}
		}
	}

	FString Content;
	if (!UN2CResponseParserBase::ExtractChatCompletionContent(Root, OutputBudgetHint, Content, Scratch))
	{
		Result.Error = Scratch.Error;
		return Result;
	}
	Result.Content = MoveTemp(Content);
	return Result;
}
