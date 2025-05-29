// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LLM/N2CBaseLLMService.h"
#include "N2COpenAIService.h"
#include "N2COpenAICompatibleService.generated.h"

// Forward declarations
class UN2CSystemPromptManager;

/**
 * @class UN2COpenAICompatibleService
 * @brief Implementation of OpenAI's Chat Completion API integration
 */
UCLASS()
class NODETOCODE_API UN2COpenAICompatibleService : public UN2COpenAIService
{
    GENERATED_BODY()

public:
    // Provider-specific implementations
    virtual EN2CLLMProvider GetProviderType() const override { return EN2CLLMProvider::OpenAICompatible; }

protected:
    // Provider-specific implementations
    virtual FString GetDefaultEndpoint() const override;
};
