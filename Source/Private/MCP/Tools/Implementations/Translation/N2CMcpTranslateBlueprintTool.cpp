// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpTranslateBlueprintTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h" // For REGISTER_MCP_TOOL
#include "Utils/N2CLogger.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"

REGISTER_MCP_TOOL(FN2CMcpTranslateBlueprintTool)

FMcpToolDefinition FN2CMcpTranslateBlueprintTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("translate-focused-blueprint"),
        TEXT("Translates the currently focused Blueprint graph using an LLM provider and returns the translation as this call's result. ")
        TEXT("It can take minutes: send params._meta.progressToken to receive notifications/progress while it runs. ")
        TEXT("Nothing is saved to the translation output directory."),
        TEXT("Translation")
    );

    Definition.bIsLongRunning = true; // Mark as long-running

    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

    TSharedPtr<FJsonObject> ProviderProp = MakeShareable(new FJsonObject);
    ProviderProp->SetStringField(TEXT("type"), TEXT("string"));
    ProviderProp->SetStringField(TEXT("description"), TEXT("Optional: LLM provider ID as get-available-llm-providers lists it (e.g., 'openai', 'anthropic', 'ollama'; case-insensitive). Uses settings default if empty; an unknown ID is an error."));
    Properties->SetObjectField(TEXT("provider"), ProviderProp);

    TSharedPtr<FJsonObject> ModelProp = MakeShareable(new FJsonObject);
    ModelProp->SetStringField(TEXT("type"), TEXT("string"));
    ModelProp->SetStringField(TEXT("description"), TEXT("Optional: Specific model ID. Uses provider's default from settings if empty. An ID NodeToCode does not list for a cloud provider is still sent, with a note in the result's warnings array."));
    Properties->SetObjectField(TEXT("model"), ModelProp);

    TSharedPtr<FJsonObject> LanguageProp = MakeShareable(new FJsonObject);
    LanguageProp->SetStringField(TEXT("type"), TEXT("string"));
    LanguageProp->SetStringField(TEXT("description"), TEXT("Optional: Target language ID as get-available-translation-targets lists it (e.g., 'cpp', 'python'; case-insensitive). Uses settings default if empty; an unknown ID is an error."));
    Properties->SetObjectField(TEXT("language"), LanguageProp);
    
    TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    Schema->SetObjectField(TEXT("properties"), Properties);
    // No required fields, all are optional and will use defaults from settings.
    
    Definition.InputSchema = Schema;

    // Results carry Blueprint text (code an LLM wrote from the Blueprint's text; FN2CTranslateBlueprintCall marks it): marked as untrusted data (card 0124)
    Definition.bReturnsUntrustedContent = true;

    return Definition;
}

namespace N2CMcpTranslateBlueprintToolPrivate
{
    static FN2CTranslateBlueprintCall::FRequestStarter& RequestStarterForTesting()
    {
        static FN2CTranslateBlueprintCall::FRequestStarter Starter;
        return Starter;
    }
}

void FN2CMcpTranslateBlueprintTool::SetRequestStarterForTesting(FN2CTranslateBlueprintCall::FRequestStarter InStarter)
{
    N2CMcpTranslateBlueprintToolPrivate::RequestStarterForTesting() = MoveTemp(InStarter);
}

TFuture<FMcpToolCallResult> FN2CMcpTranslateBlueprintTool::InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments)
{
    check(IsInGameThread());

    FN2CMcpArgumentParser ArgParser(Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>());
    const FString ProviderId = ArgParser.GetOptionalString(TEXT("provider"));
    const FString ModelId = ArgParser.GetOptionalString(TEXT("model"));
    const FString LanguageId = ArgParser.GetOptionalString(TEXT("language"));

    // An unknown provider or language is the agent's mistake: say so, with the valid IDs, instead of translating
    // with the defaults
    FN2CTranslationOverrides Overrides;
    FString OverrideError;
    if (!FN2CEditorIntegration::ResolveTranslationOverrides(ProviderId, ModelId, LanguageId, Overrides, OverrideError))
    {
        FN2CLogger::Get().LogWarning(FString::Printf(TEXT("translate-focused-blueprint: %s"), *OverrideError));
        return MakeFulfilledPromise<FMcpToolCallResult>(FMcpToolCallResult::CreateErrorResult(OverrideError)).GetFuture();
    }

    const TSharedRef<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe> Call =
        MakeShared<FN2CTranslateBlueprintCall, ESPMode::ThreadSafe>(Context, ProviderId, ModelId, LanguageId, Overrides.Warnings);
    if (const FN2CTranslateBlueprintCall::FRequestStarter& Starter = N2CMcpTranslateBlueprintToolPrivate::RequestStarterForTesting())
    {
        Call->SetRequestStarter(Starter);
    }
    return Call->Start();
}
