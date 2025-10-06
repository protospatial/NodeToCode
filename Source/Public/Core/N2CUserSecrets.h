// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CUserSecrets.generated.h"

/**
 * @class UN2CUserSecrets
 * @brief Stores sensitive configuration data like API keys
 * 
 * Uses a custom JSON storage system instead of Unreal's config system
 * to ensure consistent behavior across engine versions.
 */

UCLASS()
class NODETOCODE_API UN2CUserSecrets : public UObject
{
    GENERATED_BODY()
public:
    UN2CUserSecrets();
    
    /** Load API keys from storage */
    void LoadSecrets();
    
    /** Save API keys to storage */
    void SaveSecrets();
    
    /** Get the path to the secrets file */
    static FString GetSecretsFilePath();
    
    /** OpenAI API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString OpenAI_API_Key;

    /** Anthropic API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString Anthropic_API_Key;

    /** Gemini API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString Gemini_API_Key;

    /** DeepSeek API Key */
    UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
    FString DeepSeek_API_Key;

	/** xAI API Key */
	UPROPERTY(EditAnywhere, Category = "Node to Code | API Keys")
	FString xAI_API_Key;
    
private:
    /** Ensure the secrets directory exists */
    static void EnsureSecretsDirectoryExists();
};
