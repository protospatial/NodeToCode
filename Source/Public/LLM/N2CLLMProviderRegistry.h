// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2CLLMProviderRegistry.generated.h"

/**
 * @class UN2CLLMProviderRegistry
 * @brief Centralized registry for LLM provider factories
 */
UCLASS()
class NODETOCODE_API UN2CLLMProviderRegistry : public UObject
{
    GENERATED_BODY()

public:
    /** Get the singleton instance */
    static UN2CLLMProviderRegistry* Get();
    
    /** Register a provider class */
    void RegisterProvider(EN2CLLMProvider ProviderType, TSubclassOf<UN2CBaseLLMService> ProviderClass);
    
    /** Create a provider instance */
    TScriptInterface<IN2CLLMService> CreateProvider(
        EN2CLLMProvider ProviderType,
        UObject* Outer = nullptr);
        
    /** Check if a provider type is registered */
    bool IsProviderRegistered(EN2CLLMProvider ProviderType) const;
    
    /** Get all registered provider types */
    TArray<EN2CLLMProvider> GetRegisteredProviders() const;

private:
    /** Map of provider types to provider classes */
    TMap<EN2CLLMProvider, TSubclassOf<UN2CBaseLLMService>> ProviderClasses;
};
