// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "N2CLLMTypes.h"
#include "N2COllamaConfig.h"
#include "N2CLLMPayloadBuilder.generated.h"

/**
 * @class UN2CLLMPayloadBuilder
 * @brief Utility class for building JSON payloads for LLM requests
 */
UCLASS()
class NODETOCODE_API UN2CLLMPayloadBuilder : public UObject
{
    GENERATED_BODY()

public:
    /** Initialize with model name */
    void Initialize(const FString& ModelName);
    
    /** Set the model name */
    void SetModel(const FString& ModelName);
    
    /** Common configuration */
    void SetTemperature(float Value);
    void SetMaxTokens(int32 Value);
    
    /** Message building */
    void AddSystemMessage(const FString& Content);
    void AddUserMessage(const FString& Content);
    
    /** Response format */
    void SetJsonResponseFormat(const TSharedPtr<FJsonObject>& Schema);
    void SetStructuredOutput(const TSharedPtr<FJsonObject>& Schema) { SetJsonResponseFormat(Schema); }
    
    /** Provider-specific extensions */
    void ConfigureForOpenAI();
    void ConfigureForAnthropic();
    void ConfigureForGemini();
    void ConfigureForDeepSeek();
    void ConfigureForOllama(const struct FN2COllamaConfig& OllamaConfig);
    void ConfigureForLMStudio();
    
    /** Generate final payload */
    FString Build();
    
    /** Get the JSON schema for N2C translation responses */
    static TSharedPtr<FJsonObject> GetN2CResponseSchema();

private:
    /** Root JSON object */
    TSharedPtr<FJsonObject> RootObject;
    
    /** Messages array for providers that use it */
    TArray<TSharedPtr<FJsonValue>> MessagesArray;
    
    /** Current provider type */
    EN2CLLMProvider ProviderType;
    
    /** Model name */
    FString ModelName;
};
