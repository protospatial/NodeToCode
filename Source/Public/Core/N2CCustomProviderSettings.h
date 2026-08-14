// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "N2CCustomProviderSettings.generated.h"

UENUM(BlueprintType)
enum class EN2CCustomProviderApiType : uint8
{
    OpenAI UMETA(DisplayName = "OpenAI")
};

USTRUCT(BlueprintType)
struct FN2CCustomProviderDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    EN2CCustomProviderApiType ApiType = EN2CCustomProviderApiType::OpenAI;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    FString Endpoint;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    FString Model;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Custom Provider")
    bool bUseSystemPrompts = true;
};

UCLASS(Config = NodeToCode, DefaultConfig)
class NODETOCODE_API UN2CCustomProviderSettings : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(Config)
    TArray<FN2CCustomProviderDefinition> Providers;

    UPROPERTY(Config)
    FString ActiveProviderName;

    const FN2CCustomProviderDefinition* GetActiveProvider() const;
    FN2CCustomProviderDefinition* GetProvider(const FString& ProviderName);
    const FN2CCustomProviderDefinition* GetProvider(const FString& ProviderName) const;

    bool AddProvider(const FString& ProviderName, EN2CCustomProviderApiType ApiType);
    bool SetActiveProvider(const FString& ProviderName);
    void SaveDefinitions();

    FString GetApiKey(const FString& ProviderName) const;
    void SetApiKey(const FString& ProviderName, const FString& ApiKey);

private:
    static FString GetSecretsFilePath();
    static void EnsureSecretsDirectoryExists();
    void LoadApiKeys() const;
    void SaveApiKeys() const;

    mutable bool bApiKeysLoaded = false;
    mutable TMap<FString, FString> ApiKeys;
};
