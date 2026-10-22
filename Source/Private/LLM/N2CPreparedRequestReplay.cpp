// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CBaseLLMService.h"

#include "LLM/N2CSystemPromptManager.h"

FString UN2CBaseLLMService::BuildFormattedRequestPayloadFromPreparedMessages(
    const FString& UserMessage,
    const FString& SystemMessage)
{
    // The prepared user content was extracted from the original wire request and already contains
    // the exact reference-source block that was sent then. Suppress automatic insertion while the
    // new provider envelope is built so a cross-provider retry neither duplicates nor silently
    // replaces that context with the files' current contents.
    if (PromptManager)
    {
        PromptManager->SetSkipReferenceSourceFiles(true);
    }

    LastFormattedRequestPayload = FormatRequestPayload(UserMessage, SystemMessage);

    if (PromptManager)
    {
        PromptManager->SetSkipReferenceSourceFiles(false);
    }

    return LastFormattedRequestPayload;
}
