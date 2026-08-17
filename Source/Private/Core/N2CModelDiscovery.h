// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CRequestSettings.h"

/** Runtime model discovery for built-in providers managed from Node to Code Project Settings. */
class FN2CModelDiscovery
{
public:
    using FOnModelsResolved = TFunction<void(bool, TArray<FString>, FString)>;

    /** Whether this built-in provider exposes a model-list endpoint supported by NodeToCode. */
    static bool SupportsProvider(EN2CLLMProvider Provider);

    /**
     * Start asynchronous model discovery. Returns false when discovery cannot be attempted (for
     * example, a cloud provider has no API key). Completion is delivered on the game thread.
     */
    static bool FetchAvailableModels(
        const FN2CResolvedRequestProvider& Provider,
        FOnModelsResolved OnComplete);
};
