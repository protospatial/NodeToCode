// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/Providers/N2COpenAIService.h"

#include "LLM/N2CLLMModels.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Utils/N2CLogger.h"

UN2CResponseParserBase* UN2COpenAIService::CreateResponseParser()
{
    UN2COpenAIResponseParser* Parser = NewObject<UN2COpenAIResponseParser>(this);
    return Parser;
}

FString UN2COpenAIService::GetDefaultEndpoint() const
{
    // Return appropriate endpoint based on model
    if (RequiresResponsesAPI())
    {
        return ResponsesAPIEndpoint;
    }
    return ChatCompletionsEndpoint;
}

bool UN2COpenAIService::RequiresResponsesAPI() const
{
    return FN2CLLMModelRegistry::Get().RequiresResponsesAPI(Config.Model);
}

void UN2COpenAIService::GetConfiguration(
    FString& OutEndpoint,
    FString& OutAuthToken,
    bool& OutSupportsSystemPrompts)
{
    OutEndpoint = Config.ApiEndpoint;
    OutAuthToken = Config.ApiKey;
    
    // System prompt support comes from the registry by model ID; IDs it does not know (for example an MCP
    // model override) are assumed to accept developer/system messages
    const FN2CModelMetadata* ModelMeta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Config.Model);
    OutSupportsSystemPrompts = ModelMeta ? ModelMeta->bSupportsSystemPrompts : true;
}

void UN2COpenAIService::GetProviderHeaders(TMap<FString, FString>& OutHeaders) const
{
    OutHeaders.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Config.ApiKey));
    OutHeaders.Add(TEXT("Content-Type"), TEXT("application/json"));
    
    // Add organization header if available
    if (!OrganizationId.IsEmpty())
    {
        OutHeaders.Add(TEXT("OpenAI-Organization"), OrganizationId);
    }
}

FString UN2COpenAIService::FormatRequestPayload(const FString& UserMessage, const FString& SystemMessage) const
{
    // Check if model supports system prompts using registry
    const FN2CModelMetadata* ModelMeta = FN2CLLMModelRegistry::Get().GetModelMetadataById(Config.Model);
    bool bSupportsSystemPrompts = ModelMeta ? ModelMeta->bSupportsSystemPrompts : true;

    // Use appropriate payload format based on API requirements
    if (RequiresResponsesAPI())
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Using Responses API for model: %s"), *Config.Model),
            EN2CLogSeverity::Info,
            TEXT("OpenAIService")
        );
        return FormatResponsesAPIPayload(UserMessage, SystemMessage, bSupportsSystemPrompts);
    }

    return FormatChatCompletionsPayload(UserMessage, SystemMessage, bSupportsSystemPrompts);
}

FString UN2COpenAIService::FormatChatCompletionsPayload(const FString& UserMessage, const FString& SystemMessage, bool bSupportsSystemPrompts) const
{
    // Create and configure payload builder
    UN2CLLMPayloadBuilder* PayloadBuilder = NewObject<UN2CLLMPayloadBuilder>();
    PayloadBuilder->Initialize(Config.Model);
    PayloadBuilder->ConfigureForOpenAI();

    // Max tokens (max_tokens or max_completion_tokens per the registry). Temperature is applied by
    // ConfigureForOpenAI only for models that accept it; reasoning and unregistered models get none.
    PayloadBuilder->SetMaxTokens(16000);

    // JSON response format: the payload builder picks strict json_schema or none from the registry
    PayloadBuilder->SetJsonResponseFormat(UN2CLLMPayloadBuilder::GetN2CResponseSchema());

    // Determine final content based on system prompt support
    FString FinalContent = UserMessage;

    // Try prepending source files to the user message
    PromptManager->PrependSourceFilesToUserMessage(FinalContent);

    // Add messages
    if (bSupportsSystemPrompts)
    {
        PayloadBuilder->AddSystemMessage(SystemMessage);
        PayloadBuilder->AddUserMessage(FinalContent);
    }
    else
    {
        // Merge system and user prompts if model doesn't support system prompts
        FString MergedContent = PromptManager->MergePrompts(SystemMessage, FinalContent);
        PayloadBuilder->AddUserMessage(MergedContent);
    }

    // Build and return the payload
    return PayloadBuilder->Build();
}

FString UN2COpenAIService::FormatResponsesAPIPayload(const FString& UserMessage, const FString& SystemMessage, bool bSupportsSystemPrompts) const
{
    // Responses API uses a different structure than Chat Completions
    // Format: { "model": "...", "input": [...], "text": {...} }

    TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());

    // Set model
    RootObject->SetStringField(TEXT("model"), Config.Model);

    // Build input array
    TArray<TSharedPtr<FJsonValue>> InputArray;

    // Prepare final user content
    FString FinalContent = UserMessage;
    PromptManager->PrependSourceFilesToUserMessage(FinalContent);

    if (bSupportsSystemPrompts && !SystemMessage.IsEmpty())
    {
        // Add developer/system message
        TSharedPtr<FJsonObject> DeveloperMessage = MakeShareable(new FJsonObject());
        DeveloperMessage->SetStringField(TEXT("role"), TEXT("developer"));
        DeveloperMessage->SetStringField(TEXT("content"), SystemMessage);
        InputArray.Add(MakeShareable(new FJsonValueObject(DeveloperMessage)));
    }
    else if (!bSupportsSystemPrompts)
    {
        // Merge system and user prompts if model doesn't support system prompts
        FinalContent = PromptManager->MergePrompts(SystemMessage, FinalContent);
    }

    // Add user message
    TSharedPtr<FJsonObject> UserMessageObj = MakeShareable(new FJsonObject());
    UserMessageObj->SetStringField(TEXT("role"), TEXT("user"));
    UserMessageObj->SetStringField(TEXT("content"), FinalContent);
    InputArray.Add(MakeShareable(new FJsonValueObject(UserMessageObj)));

    RootObject->SetArrayField(TEXT("input"), InputArray);

    // Configure text output format with JSON schema
    TSharedPtr<FJsonObject> TextConfig = MakeShareable(new FJsonObject());

    // Format configuration for structured JSON output
    TSharedPtr<FJsonObject> FormatConfig = MakeShareable(new FJsonObject());
    FormatConfig->SetStringField(TEXT("type"), TEXT("json_schema"));
    FormatConfig->SetStringField(TEXT("name"), TEXT("n2c_response"));
    FormatConfig->SetBoolField(TEXT("strict"), true);

    // Get the N2C response schema
    TSharedPtr<FJsonObject> Schema = UN2CLLMPayloadBuilder::GetN2CResponseSchema();
    if (Schema.IsValid())
    {
        FormatConfig->SetObjectField(TEXT("schema"), Schema);
    }

    TextConfig->SetObjectField(TEXT("format"), FormatConfig);
    RootObject->SetObjectField(TEXT("text"), TextConfig);

    // Set max output tokens (Responses API uses max_output_tokens)
    RootObject->SetNumberField(TEXT("max_output_tokens"), 16000);

    // Serialize to JSON string
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

    return OutputString;
}
