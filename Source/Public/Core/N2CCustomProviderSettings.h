// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"
#include "UObject/Object.h"
#include "N2CCustomProviderSettings.generated.h"

UENUM(BlueprintType)
enum class EN2CCustomProviderApiType : uint8
{
    OpenAI UMETA(DisplayName = "OpenAI")
};

/** Determines whether a saved profile owns custom connection settings or references a built-in provider. */
UENUM(BlueprintType)
enum class EN2CCustomProviderProfileSource : uint8
{
    CustomEndpoint UMETA(DisplayName = "Custom Endpoint"),
    BuiltInProvider UMETA(DisplayName = "Built-in Provider Reference")
};

USTRUCT(BlueprintType)
struct FN2CCustomProviderDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    FString Name;

    /** Existing profiles default to CustomEndpoint for backward compatibility. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    EN2CCustomProviderProfileSource ProfileSource = EN2CCustomProviderProfileSource::CustomEndpoint;

    /** Built-in provider whose authentication/endpoint/config are shared by this profile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    EN2CLLMProvider BuiltInProvider = EN2CLLMProvider::OpenAI;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    EN2CCustomProviderApiType ApiType = EN2CCustomProviderApiType::OpenAI;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    FString Endpoint;

    /** Independent model selection for both custom endpoints and built-in provider profiles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    FString Model;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    bool bUseSystemPrompts = true;

    /**
     * Maximum completion budget sent to OpenAI-compatible custom providers. Reasoning models may
     * spend part of this budget in a separate reasoning_content field before producing final JSON,
     * so the previous 8K default was too small for full Blueprint translation/consolidation.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider",
        meta = (ClampMin = "1024", ClampMax = "131072", UIMin = "1024", UIMax = "131072"))
    int32 MaxOutputTokens = 32768;
};

UCLASS(Config = NodeToCode, DefaultConfig)
class NODETOCODE_API UN2CCustomProviderSettings : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    TArray<FN2CCustomProviderDefinition> Providers;

    /** Active custom-endpoint provider used when the global provider is Custom. */
    UPROPERTY(Config)
    FString ActiveProviderName;

    /**
     * Persisted model identifiers selected through live discovery for built-in providers. These
     * override the plugin's compiled enum/string defaults without duplicating provider credentials.
     */
    UPROPERTY(Config)
    TMap<EN2CLLMProvider, FString> BuiltInModelOverrides;

    const FN2CCustomProviderDefinition* GetActiveProvider() const;
    FN2CCustomProviderDefinition* GetProvider(const FString& ProviderName);
    const FN2CCustomProviderDefinition* GetProvider(const FString& ProviderName) const;

    bool AddProvider(const FString& ProviderName, EN2CCustomProviderApiType ApiType);

    /** Save a model-specific profile that references an existing built-in provider configuration. */
    bool AddBuiltInProviderProfile(
        EN2CLLMProvider BuiltInProvider,
        const FString& Model,
        FString& OutProfileName);

    /** Duplicate a profile and, for custom endpoints, copy its current secret into the new profile. */
    bool DuplicateProvider(const FString& ProviderName, FString& OutDuplicateName);

    bool RenameProvider(const FString& ProviderName, const FString& NewProviderName);
    bool RemoveProvider(const FString& ProviderName);
    bool SetActiveProvider(const FString& ProviderName);
    void SaveDefinitions();

    /** Return the persisted discovered-model override for a built-in provider, if any. */
    FString GetBuiltInModelOverride(EN2CLLMProvider Provider) const;

    /** Persist the built-in provider's effective model identifier selected in Project Settings. */
    void SetBuiltInModelOverride(EN2CLLMProvider Provider, const FString& Model);

    FString GetApiKey(const FString& ProviderName) const;
    void SetApiKey(const FString& ProviderName, const FString& ApiKey);

private:
    FString MakeUniqueProviderName(const FString& BaseName) const;
    const FN2CCustomProviderDefinition* FindFirstCustomEndpointProvider() const;

    static FString GetSecretsFilePath();
    static void EnsureSecretsDirectoryExists();
    void LoadApiKeys() const;
    void SaveApiKeys() const;

    mutable bool bApiKeysLoaded = false;
    mutable TMap<FString, FString> ApiKeys;
};
