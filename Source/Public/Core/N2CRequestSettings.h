// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LLM/N2CLLMTypes.h"
#include "N2CRequestSettings.generated.h"

/** Controls how model-specific instructions interact with global custom instructions. */
UENUM(BlueprintType)
enum class EN2CModelInstructionMode : uint8
{
    SendWithGlobalInstructions UMETA(DisplayName = "Send With Global Instructions"),
    ReplaceGlobalInstructions UMETA(DisplayName = "Replace Global Instructions")
};

/** Custom instructions associated with one exact provider/model pair. */
USTRUCT(BlueprintType)
struct FN2CModelCustomInstructions
{
    GENERATED_BODY()

    /** Disable an entry without deleting it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Model Instructions")
    bool bEnabled = true;

    /** Provider that owns the model identifier below. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Model Instructions")
    EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;

    /**
     * Exact API model identifier used by the provider (for example, qwen3:32b or MiniMax-M2.7).
     * Matching is case-insensitive and is evaluated against the actual model used for the request.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Model Instructions")
    FString Model;

    /** Whether these instructions supplement or replace the enabled global instructions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Model Instructions")
    EN2CModelInstructionMode Mode = EN2CModelInstructionMode::SendWithGlobalInstructions;

    /** Instructions sent whenever the matching provider/model is used. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Model Instructions", meta = (MultiLine = true))
    FString Instructions;
};

/**
 * Shared request-level settings inherited by the main Node to Code developer settings object.
 * Abstract prevents this base class from registering a second Project Settings page.
 */
UCLASS(Abstract, Config = NodeToCode, DefaultConfig)
class NODETOCODE_API UN2CRequestSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    /** Enable the global custom instructions for every LLM request. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Custom Instructions",
        meta = (DisplayName = "Enable Global Custom Instructions"))
    bool bEnableGlobalCustomInstructions = false;

    /** Instructions appended to the built-in Node to Code system prompt for every provider. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Custom Instructions",
        meta = (DisplayName = "Global Custom Instructions", MultiLine = true,
                EditCondition = "bEnableGlobalCustomInstructions"))
    FString GlobalCustomInstructions;

    /**
     * Optional per-model instruction overrides. Later matching entries take precedence if the same
     * provider/model pair appears more than once.
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Custom Instructions",
        meta = (DisplayName = "Model Specific Custom Instructions", TitleProperty = "Model"))
    TArray<FN2CModelCustomInstructions> ModelCustomInstructions;

    /** Compose the custom instructions for the provider/model used by a request. */
    FString GetEffectiveCustomInstructions(EN2CLLMProvider Provider, const FString& Model) const;
};

/** Fully resolved provider choice for one translation operation. */
struct FN2CResolvedRequestProvider
{
    EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;
    FString CustomProviderName;
    FString ApiKey;
    FString Model;
};

/** Runtime helpers for transient per-request provider selection and ad-hoc context. */
class NODETOCODE_API FN2CRequestRuntime
{
public:
    /**
     * Resolve the provider used by the next translation operation. This always displays the modal
     * provider picker. Returning false means the user cancelled or no provider was usable.
     */
    static bool ResolveProviderForRequest(
        EN2CLLMProvider DefaultProvider,
        const TArray<EN2CLLMProvider>& AvailableProviders,
        FN2CResolvedRequestProvider& OutProvider);

    /** Consume the named custom provider selected for this request, if any. */
    static FString ConsumeSelectedCustomProviderName();

    /** Ad hoc instructions entered in the provider picker for the current translation operation. */
    static const FString& GetAdHocInstructions()
    {
        return AdHocInstructions;
    }

    /** Files attached in the provider picker for the current translation operation. */
    static const TArray<FString>& GetAdditionalContextFilePaths()
    {
        return AdditionalContextFilePaths;
    }

private:
    static bool ResolveProviderConfig(
        EN2CLLMProvider Provider,
        const FString& CustomProviderName,
        FN2CResolvedRequestProvider& OutProvider);

    static FString SelectedCustomProviderName;
    static FString AdHocInstructions;
    static TArray<FString> AdditionalContextFilePaths;
};
