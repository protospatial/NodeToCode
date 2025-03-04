// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CLLMProviderRegistry.h"
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
    TScriptInterface<IN2CLLMService> Result;
    
    // Find the provider class
    TSubclassOf<UN2CBaseLLMService>* ProviderClassPtr = ProviderClasses.Find(ProviderType);
    if (!ProviderClassPtr || !(*ProviderClassPtr))
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Provider type not registered: %s"), 
                *UEnum::GetValueAsString(ProviderType)),
            TEXT("LLMProviderRegistry")
        );
        return Result;
    }
    
    // Create the provider object
    UObject* ServiceObject = nullptr;
    if (Outer)
    {
        ServiceObject = NewObject<UObject>(Outer, *ProviderClassPtr);
    }
    else
    {
        ServiceObject = NewObject<UObject>(*ProviderClassPtr);
    }
    
    if (!ServiceObject)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to create service object for provider type: %s"), 
                *UEnum::GetValueAsString(ProviderType)),
            TEXT("LLMProviderRegistry")
        );
        return Result;
    }
    
    // Set up the interface
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
