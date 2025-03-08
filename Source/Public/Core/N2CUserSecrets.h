// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CUserSecrets.generated.h"

/**
 * @class UN2CUserSecrets
 * @brief Stores sensitive configuration data like API keys
 */

UCLASS(config = EditorNodeToCodeSecrets)
class NODETOCODE_API UN2CUserSecrets : public UObject
{
    GENERATED_BODY()
public:
    /** OpenAI API Key */
    UPROPERTY(config, EditAnywhere, Category = "Node to Code | API Keys")
    FString OpenAI_API_Key;

    /** Anthropic API Key */
    UPROPERTY(config, EditAnywhere, Category = "Node to Code | API Keys")
    FString Anthropic_API_Key;

	/** Anthropic API Key */
	UPROPERTY(config, EditAnywhere, Category = "Node to Code | API Keys")
	FString Gemini_API_Key;

    /** DeepSeek API Key */
    UPROPERTY(config, EditAnywhere, Category = "Node to Code | API Keys")
    FString DeepSeek_API_Key;
};
