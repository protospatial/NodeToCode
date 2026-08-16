// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"

/**
 * Consolidates the independently translated graphs from Translate Entire Blueprint into the
 * single C++ class/header pair that the operation conceptually represents.
 */
class FN2CBatchTranslationConsolidator
{
public:
    /** Build one ClassItSelf-style translation from all successfully parsed graph responses. */
    static bool BuildCppResponse(
        const FN2CTranslationResponse& SessionResponse,
        const FN2CBlueprint& Blueprint,
        FN2CTranslationResponse& OutResponse);

    /** Write the consolidated C++ header/source (and optional combined notes) directly to RootPath. */
    static bool SaveCppFiles(
        const FN2CTranslationResponse& ConsolidatedResponse,
        const FString& RootPath);
};
