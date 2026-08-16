// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"

/**
<<<<<<< HEAD
 * Consolidates the independently translated graphs from Translate Entire Blueprint into the
 * single C++ class/header pair that the operation conceptually represents.
=======
 * Builds and validates the final LLM reconciliation pass used by Translate Entire Blueprint.
 * Semantic merging belongs to the selected LLM; this helper only prepares source material,
 * validates the final structural contract, and writes the resulting C++ pair.
>>>>>>> 38cd82a23ba8e51bf03ff6b70ec83cbd1acc7efe
 */
class FN2CBatchTranslationConsolidator
{
public:
<<<<<<< HEAD
    /** Build one ClassItSelf-style translation from all successfully parsed graph responses. */
    static bool BuildCppResponse(
        const FN2CTranslationResponse& SessionResponse,
        const FN2CBlueprint& Blueprint,
        FN2CTranslationResponse& OutResponse);

    /** Write the consolidated C++ header/source (and optional combined notes) directly to RootPath. */
=======
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
>>>>>>> 38cd82a23ba8e51bf03ff6b70ec83cbd1acc7efe
    static bool SaveCppFiles(
        const FN2CTranslationResponse& ConsolidatedResponse,
        const FString& RootPath);
};
