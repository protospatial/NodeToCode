// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMProviderRegistry.h"

#include "Core/N2CRequestSettings.h"
#include "LLM/N2CLLMModule.h"
#include "Utils/N2CLogger.h"

UN2CLLMProviderRegistry* UN2CLLMProviderRegistry::Get()
{
    static UN2CLLMProviderRegistry* Instance = nullptr;
    if (!Instance)
    {
        Instance = NewObject<UN2CLLMProviderRegistry>();
        Instance->AddToRoot(); // Prevent garbage collection
    }
    return Instance;
}

void UN2CLLMProviderRegistry::RegisterProvider(EN2CLLMProvider ProviderType, TSubclassOf<UN2CBaseLLMService> ProviderClass)
{
    if (!ProviderClass)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Attempted to register null provider class for provider type: %d"), 
                static_cast<int32>(ProviderType)),
            TEXT("LLMProviderRegistry")
        );
        return;
    }
    
    ProviderClasses.Add(ProviderType, ProviderClass);
    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Registered provider class for provider type: %s"), 
            *UEnum::GetValueAsString(ProviderType)),
        EN2CLogSeverity::Debug,
        TEXT("LLMProviderRegistry")
    );
}

TScriptInterface<IN2CLLMService> UN2CLLMProviderRegistry::CreateProvider(
    EN2CLLMProvider ProviderType,
    UObject* Outer)
{
    EN2CLLMProvider EffectiveProvider = ProviderType;

    // Translation requests are created with the LLM module as their outer. Resolve a transient
    // provider choice here so both graph and whole-Blueprint translation paths share one picker.
    if (UN2CLLMModule* LLMModule = Cast<UN2CLLMModule>(Outer))
    {
        FN2CResolvedRequestProvider ResolvedProvider;
        if (!FN2CRequestRuntime::ResolveProviderForRequest(
                ProviderType,
                GetRegisteredProviders(),
                ResolvedProvider))
        {
            FN2CLogger::Get().Log(
                TEXT("LLM provider creation cancelled before request dispatch"),
                EN2CLogSeverity::Info,
                TEXT("LLMProviderRegistry"));
            return TScriptInterface<IN2CLLMService>();
        }

        EffectiveProvider = ResolvedProvider.Provider;
        LLMModule->ApplyRequestProviderOverride(
            ResolvedProvider.Provider,
            ResolvedProvider.ApiKey,
            ResolvedProvider.Model,
            ResolvedProvider.CustomProviderName);
    }

    return CreateProviderDirect(EffectiveProvider, Outer);
}

TScriptInterface<IN2CLLMService> UN2CLLMProviderRegistry::CreateProviderDirect(
    EN2CLLMProvider ProviderType,
    UObject* Outer)
{
    TScriptInterface<IN2CLLMService> Result;

    TSubclassOf<UN2CBaseLLMService>* ProviderClassPtr = ProviderClasses.Find(ProviderType);
    if (!ProviderClassPtr || !(*ProviderClassPtr))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Provider type not registered: %s"),
                *UEnum::GetValueAsString(ProviderType)),
            TEXT("LLMProviderRegistry"));
        return Result;
    }

    UObject* ServiceObject = Outer
        ? NewObject<UObject>(Outer, *ProviderClassPtr)
        : NewObject<UObject>(*ProviderClassPtr);

    if (!ServiceObject)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to create service object for provider type: %s"),
                *UEnum::GetValueAsString(ProviderType)),
            TEXT("LLMProviderRegistry"));
        return Result;
    }

    Result.SetObject(ServiceObject);
    Result.SetInterface(Cast<IN2CLLMService>(ServiceObject));
    return Result;
}

bool UN2CLLMProviderRegistry::IsProviderRegistered(EN2CLLMProvider ProviderType) const
{
    return ProviderClasses.Contains(ProviderType);
}

TArray<EN2CLLMProvider> UN2CLLMProviderRegistry::GetRegisteredProviders() const
{
    TArray<EN2CLLMProvider> Result;
    ProviderClasses.GetKeys(Result);
    return Result;
}
