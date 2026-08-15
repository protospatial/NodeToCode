// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LLM/N2CLLMTypes.h"
#include "N2CRequestSettings.generated.h"

/** Controls whether translation commands send immediately or ask for a provider first. */
UENUM(BlueprintType)
enum class EN2CRequestDispatchMode : uint8
{
    SendImmediatelyToDefaultProvider UMETA(DisplayName = "Send Request Immediately to Default Provider"),
    SelectProviderBeforeSending UMETA(DisplayName = "Select Provider Before Sending Request")
};

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
 * Request-level Node to Code settings that apply uniformly to every LLM provider.
 * These live in the same NodeToCode config file while remaining independent of provider credentials.
 */
UCLASS(Config = NodeToCode, DefaultConfig, meta = (DisplayName = "Node to Code - Requests"))
class NODETOCODE_API UN2CRequestSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
    virtual FText GetSectionText() const override
    {
        return NSLOCTEXT("NodeToCode", "RequestSettingsSection", "Node to Code - Requests");
    }

    /** Behavior used by Translate Blueprint Graph to Code and Translate Entire Blueprint. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Request Dispatch",
        meta = (DisplayName = "Translation Request Behavior"))
    EN2CRequestDispatchMode RequestDispatchMode =
        EN2CRequestDispatchMode::SendImmediatelyToDefaultProvider;

    /** Enable the global custom instructions for every LLM request. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Custom Instructions",
        meta = (DisplayName = "Enable Global Custom Instructions"))
    bool bEnableGlobalCustomInstructions = false;

    /** Instructions appended to the built-in Node to Code system prompt for every provider. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Custom Instructions",
        meta = (DisplayName = "Global Custom Instructions", MultiLine = true,
                EditCondition = "bEnableGlobalCustomInstructions"))
    FString GlobalCustomInstructions;

    /**
     * Optional per-model instruction overrides. Later matching entries take precedence if the same
     * provider/model pair appears more than once.
     */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Custom Instructions",
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

/** Runtime helpers for transient per-request provider selection. */
class NODETOCODE_API FN2CRequestRuntime
{
public:
    /**
     * Resolve the provider used by the next translation operation. In selection mode this displays
     * a modal provider picker. Returning false means the user cancelled or no provider was usable.
     */
    static bool ResolveProviderForRequest(
        EN2CLLMProvider DefaultProvider,
        const TArray<EN2CLLMProvider>& AvailableProviders,
        FN2CResolvedRequestProvider& OutProvider);

    /** Consume the named custom provider selected for this request, if any. */
    static FString ConsumeSelectedCustomProviderName();

private:
    static bool ResolveProviderConfig(
        EN2CLLMProvider Provider,
        const FString& CustomProviderName,
        FN2CResolvedRequestProvider& OutProvider);

    static FString SelectedCustomProviderName;
};
