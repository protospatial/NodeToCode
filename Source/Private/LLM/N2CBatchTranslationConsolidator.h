// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"

/**
 * Builds and validates the final LLM reconciliation pass used by Translate Entire Blueprint.
 * Semantic merging belongs to the selected LLM; this helper only prepares source material,
 * validates the final structural contract, and writes the resulting C++ pair.
 */
class FN2CBatchTranslationConsolidator
{
public:
    /** Build the JSON user payload containing source Blueprint truth and all parsed graph responses. */
    static bool BuildRequestPayload(
        const FN2CTranslationResponse& SessionResponse,
        const FN2CBlueprint& Blueprint,
        FString& OutPayload);

    /** Dedicated system prompt for the final semantic reconciliation pass. */
    static FString GetSystemPrompt();

    /** Validate the final parsed response before replacing the session result. */
    static bool ValidateFinalResponse(
        const FN2CTranslationResponse& Response,
        FString& OutError);

    /** Write the final C++ header/source (and optional notes) directly to RootPath. */
    static bool SaveCppFiles(
        const FN2CTranslationResponse& ConsolidatedResponse,
        const FString& RootPath);
};
