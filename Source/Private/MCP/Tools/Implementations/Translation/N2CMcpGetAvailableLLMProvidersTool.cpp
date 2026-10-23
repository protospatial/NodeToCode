// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetAvailableLLMProvidersTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSecrets.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CLLMModelRegistry.h"
#include "LLM/N2COllamaConfig.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/EnumProperty.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetAvailableLLMProvidersTool)

FMcpToolDefinition FN2CMcpGetAvailableLLMProvidersTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("get-available-llm-providers"),
        TEXT("Returns the list of configured LLM providers available for Blueprint translation, including which have valid API keys."),
        TEXT("Translation")
    );
    
    // This tool takes no input parameters
    Definition.InputSchema = BuildEmptyObjectSchema();
    
    // Add read-only annotation
    AddReadOnlyAnnotation(Definition);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpGetAvailableLLMProvidersTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // Since this tool requires Game Thread execution, use the base class helper
    return ExecuteOnGameThread([this]() -> FMcpToolCallResult
    {
        FN2CLogger::Get().Log(TEXT("Executing get-available-llm-providers tool"), EN2CLogSeverity::Debug);
        
        // Get settings to determine current provider
        const UN2CSettings* Settings = GetDefault<UN2CSettings>();
        if (!Settings)
        {
            FN2CLogger::Get().LogError(TEXT("Failed to get NodeToCode settings"));
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to retrieve plugin settings"));
        }
        
        // Build the response JSON
        TSharedPtr<FJsonObject> ResponseObject = MakeShareable(new FJsonObject);
        TArray<TSharedPtr<FJsonValue>> ProvidersArray;
        
        // Get the enum for provider iteration
        const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>();
        if (!ProviderEnum)
        {
            FN2CLogger::Get().LogError(TEXT("Failed to get EN2CLLMProvider enum type"));
            return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to retrieve provider enum information"));
        }
        
        // Iterate through all provider enum values
        for (int32 EnumIndex = 0; EnumIndex < ProviderEnum->NumEnums() - 1; ++EnumIndex) // -1 to exclude MAX value
        {
            EN2CLLMProvider Provider = static_cast<EN2CLLMProvider>(ProviderEnum->GetValueByIndex(EnumIndex));
            
            // Skip providers that aren't configured
            if (!IsProviderConfigured(Provider))
            {
                continue;
            }
            
            // Build provider info object
            TSharedPtr<FJsonObject> ProviderInfo = BuildProviderInfo(Provider);
            if (ProviderInfo.IsValid())
            {
                ProvidersArray.Add(MakeShareable(new FJsonValueObject(ProviderInfo)));
            }
        }
        
        ResponseObject->SetArrayField(TEXT("providers"), ProvidersArray);
        
        // Add current provider
        FString CurrentProviderId = ProviderEnum->GetNameStringByValue(static_cast<int64>(Settings->Provider));
        if (CurrentProviderId.Contains(TEXT("::")))
        {
            CurrentProviderId = CurrentProviderId.RightChop(CurrentProviderId.Find(TEXT("::")) + 2);
        }
        ResponseObject->SetStringField(TEXT("currentProvider"), CurrentProviderId.ToLower());
        
        // Add provider count
        ResponseObject->SetNumberField(TEXT("configuredProviderCount"), ProvidersArray.Num());
        
        FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully retrieved %d configured LLM providers"), ProvidersArray.Num()), EN2CLogSeverity::Info);
        
        // Convert JSON to string for the result
        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
        
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}

bool FN2CMcpGetAvailableLLMProvidersTool::IsProviderConfigured(EN2CLLMProvider Provider) const
{
    // Local providers are always available
    if (Provider == EN2CLLMProvider::Ollama || Provider == EN2CLLMProvider::LMStudio)
    {
        return true;
    }
    
    // Get settings to access user secrets
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings || !Settings->UserSecrets)
    {
        return false;
    }
    
    // Check API key for cloud providers
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            return !Settings->UserSecrets->OpenAI_API_Key.IsEmpty();
        case EN2CLLMProvider::Anthropic:
            return !Settings->UserSecrets->Anthropic_API_Key.IsEmpty();
        case EN2CLLMProvider::Gemini:
            return !Settings->UserSecrets->Gemini_API_Key.IsEmpty();
        case EN2CLLMProvider::DeepSeek:
            return !Settings->UserSecrets->DeepSeek_API_Key.IsEmpty();
        default:
            return false;
    }
}

FString FN2CMcpGetAvailableLLMProvidersTool::GetProviderDisplayName(EN2CLLMProvider Provider) const
{
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            return TEXT("OpenAI");
        case EN2CLLMProvider::Anthropic:
            return TEXT("Anthropic");
        case EN2CLLMProvider::Gemini:
            return TEXT("Google Gemini");
        case EN2CLLMProvider::DeepSeek:
            return TEXT("DeepSeek");
        case EN2CLLMProvider::Ollama:
            return TEXT("Ollama (Local)");
        case EN2CLLMProvider::LMStudio:
            return TEXT("LM Studio (Local)");
        default:
            return TEXT("Unknown Provider");
    }
}

TSharedPtr<FJsonObject> FN2CMcpGetAvailableLLMProvidersTool::BuildProviderInfo(EN2CLLMProvider Provider) const
{
    TSharedPtr<FJsonObject> ProviderObject = MakeShareable(new FJsonObject);
    
    // Get provider ID from enum
    const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>();
    FString ProviderId = ProviderEnum->GetNameStringByValue(static_cast<int64>(Provider));
    if (ProviderId.Contains(TEXT("::")))
    {
        ProviderId = ProviderId.RightChop(ProviderId.Find(TEXT("::")) + 2);
    }
    ProviderObject->SetStringField(TEXT("id"), ProviderId.ToLower());
    
    // Set display name
    ProviderObject->SetStringField(TEXT("displayName"), GetProviderDisplayName(Provider));
    
    // Set configured status (always true since we filtered unconfigured providers)
    ProviderObject->SetBoolField(TEXT("configured"), true);
    
    // Set local vs cloud
    bool bIsLocal = (Provider == EN2CLLMProvider::Ollama || Provider == EN2CLLMProvider::LMStudio);
    ProviderObject->SetBoolField(TEXT("isLocal"), bIsLocal);
    
    // Set current model
    FString CurrentModel = GetCurrentModel(Provider);
    if (!CurrentModel.IsEmpty())
    {
        ProviderObject->SetStringField(TEXT("currentModel"), CurrentModel);
    }
    
    // Add available models for cloud providers
    if (!bIsLocal)
    {
        TArray<TSharedPtr<FJsonValue>> Models = GetAvailableModels(Provider);
        if (Models.Num() > 0)
        {
            ProviderObject->SetArrayField(TEXT("availableModels"), Models);
        }
    }
    
    // Add endpoint for local providers
    if (bIsLocal)
    {
        FString Endpoint = GetProviderEndpoint(Provider);
        if (!Endpoint.IsEmpty())
        {
            ProviderObject->SetStringField(TEXT("endpoint"), Endpoint);
        }
    }
    
    // Add structured output support flag
    ProviderObject->SetBoolField(TEXT("supportsStructuredOutput"), SupportsStructuredOutput(Provider));
    
    // Add system prompt support flag
    bool bSupportsSystemPrompts = true;
    if (Provider == EN2CLLMProvider::OpenAI)
    {
        // Check if current model supports system prompts
        const UN2CSettings* Settings = GetDefault<UN2CSettings>();
        if (Settings)
        {
            bSupportsSystemPrompts = FN2CLLMModelUtils::SupportsSystemPrompts(Settings->OpenAI_Model);
        }
    }
    ProviderObject->SetBoolField(TEXT("supportsSystemPrompts"), bSupportsSystemPrompts);
    
    return ProviderObject;
}

TArray<TSharedPtr<FJsonValue>> FN2CMcpGetAvailableLLMProvidersTool::GetAvailableModels(EN2CLLMProvider Provider) const
{
    TArray<TSharedPtr<FJsonValue>> Models;
    
    // Get the appropriate model enum for each provider
    const UEnum* ModelEnum = nullptr;
    
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            ModelEnum = StaticEnum<EN2COpenAIModel>();
            break;
        case EN2CLLMProvider::Anthropic:
            ModelEnum = StaticEnum<EN2CAnthropicModel>();
            break;
        case EN2CLLMProvider::Gemini:
            ModelEnum = StaticEnum<EN2CGeminiModel>();
            break;
        case EN2CLLMProvider::DeepSeek:
            ModelEnum = StaticEnum<EN2CDeepSeekModel>();
            break;
        default:
            return Models; // Empty for local providers or unknown
    }
    
    if (ModelEnum)
    {
        const FN2CLLMModelRegistry& Registry = FN2CLLMModelRegistry::Get();
        TSet<FString> OfferedIds;

        // Iterate through enum values (skip _MAX)
        for (int32 i = 0; i < ModelEnum->NumEnums() - 1; ++i)
        {
            // Models the provider has shut down stay in the enum for saved settings but are not offered
            if (ModelEnum->HasMetaData(TEXT("Hidden"), i))
            {
                continue;
            }

            const int64 Value = ModelEnum->GetValueByIndex(i);
            const FN2CModelMetadata* Meta = nullptr;
            switch (Provider)
            {
                case EN2CLLMProvider::OpenAI:
                    Meta = Registry.GetModelMetadata(static_cast<EN2COpenAIModel>(Value));
                    break;
                case EN2CLLMProvider::Anthropic:
                    Meta = Registry.GetModelMetadata(static_cast<EN2CAnthropicModel>(Value));
                    break;
                case EN2CLLMProvider::Gemini:
                    Meta = Registry.GetModelMetadata(static_cast<EN2CGeminiModel>(Value));
                    break;
                case EN2CLLMProvider::DeepSeek:
                    Meta = Registry.GetModelMetadata(static_cast<EN2CDeepSeekModel>(Value));
                    break;
                default:
                    break;
            }
            if (!Meta || Meta->bIsRetired)
            {
                continue;
            }

            // Entries that share an ID differ only in thinking mode (DeepSeek). A model override is just an ID: it
            // gets the selected entry's mode when it names the selected model, otherwise the first entry's (the
            // provider default), so the ID is offered once, with the first entry's mode.
            if (OfferedIds.Contains(Meta->ModelId))
            {
                continue;
            }
            OfferedIds.Add(Meta->ModelId);

            TSharedPtr<FJsonObject> ModelObj = MakeShareable(new FJsonObject);
            ModelObj->SetStringField(TEXT("id"), Meta->ModelId);
            ModelObj->SetStringField(TEXT("name"), ModelEnum->GetDisplayNameTextByIndex(i).ToString());

            // Deprecated but still served: say when it stops working
            if (Meta->bIsDeprecated)
            {
                ModelObj->SetBoolField(TEXT("deprecated"), true);
                if (!Meta->ShutdownDate.IsEmpty())
                {
                    ModelObj->SetStringField(TEXT("shutdownDate"), Meta->ShutdownDate);
                }
            }

            // Add system prompt support for OpenAI models
            if (Provider == EN2CLLMProvider::OpenAI)
            {
                ModelObj->SetBoolField(TEXT("supportsSystemPrompts"), Meta->bSupportsSystemPrompts);
            }

            // Thinking mode a request for this ID sends ("none" = thinking off)
            if (!Meta->ReasoningEffort.IsEmpty())
            {
                ModelObj->SetStringField(TEXT("reasoningEffort"), Meta->ReasoningEffort);
            }

            Models.Add(MakeShareable(new FJsonValueObject(ModelObj)));
        }
    }
    
    return Models;
}

FString FN2CMcpGetAvailableLLMProvidersTool::GetCurrentModel(EN2CLLMProvider Provider) const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        return FString();
    }
    
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
            return FN2CLLMModelUtils::GetOpenAIModelValue(Settings->OpenAI_Model);
        case EN2CLLMProvider::Anthropic:
            return FN2CLLMModelUtils::GetAnthropicModelValue(Settings->AnthropicModel);
        case EN2CLLMProvider::Gemini:
            return FN2CLLMModelUtils::GetGeminiModelValue(Settings->Gemini_Model);
        case EN2CLLMProvider::DeepSeek:
            return FN2CLLMModelUtils::GetDeepSeekModelValue(Settings->DeepSeekModel);
        case EN2CLLMProvider::Ollama:
            return Settings->OllamaModel;
        case EN2CLLMProvider::LMStudio:
            return Settings->LMStudioModel;
        default:
            return FString();
    }
}

bool FN2CMcpGetAvailableLLMProvidersTool::SupportsStructuredOutput(EN2CLLMProvider Provider) const
{
    switch (Provider)
    {
        case EN2CLLMProvider::OpenAI:
        case EN2CLLMProvider::LMStudio: // LM Studio uses OpenAI-compatible API
        case EN2CLLMProvider::Gemini:
        case EN2CLLMProvider::DeepSeek:
        case EN2CLLMProvider::Ollama:
            return true;
        case EN2CLLMProvider::Anthropic:
            return false; // Anthropic doesn't have native structured output yet
        default:
            return false;
    }
}

FString FN2CMcpGetAvailableLLMProvidersTool::GetProviderEndpoint(EN2CLLMProvider Provider) const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        return FString();
    }
    
    switch (Provider)
    {
        case EN2CLLMProvider::Ollama:
            return Settings->OllamaConfig.OllamaEndpoint;
        case EN2CLLMProvider::LMStudio:
            return Settings->LMStudioEndpoint;
        default:
            return FString();
    }
}