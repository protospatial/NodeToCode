// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/Object.h"  // Add this for UObject support

// Include processor headers
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "Utils/Processors/N2CFunctionCallProcessor.h"
#include "Utils/Processors/N2CVariableProcessor.h"
#include "Utils/Processors/N2CEventProcessor.h"
#include "Utils/Processors/N2CStructProcessor.h"
#include "Utils/Processors/N2CFlowControlProcessor.h"
#include "Utils/Processors/N2CDelegateProcessor.h"
#include "Utils/Processors/N2CFunctionEntryProcessor.h"
#include "Utils/Processors/N2CArrayProcessor.h"
#include "Utils/Processors/N2CCastProcessor.h"

#include "NodeToCode.generated.h"  // Add this for GENERATED_BODY() support

/** 
 * Helper class to update HTTP config settings
 * Uses the PerObjectConfig system to write to DefaultEngine.ini
 */
UCLASS(Config=Engine, PerObjectConfig)
class UHttpTimeoutConfig : public UObject
{
    GENERATED_BODY()

public:

    UPROPERTY(Config)
    float HttpConnectionTimeout = 300.0f;

    UPROPERTY(Config)
    float HttpActivityTimeout = 3600.0f;
    
    virtual void OverridePerObjectConfigSection(FString& SectionName) override 
    { 
        SectionName = TEXT("HTTP"); 
    }
};

/**
 * @class FNodeToCodeModule
 * @brief Main module implementation for the Node to Code plugin
 *
 * Provides functionality for converting Blueprint nodes to C++ code
 * by collecting live nodes from the Blueprint Editor.
 */
class FNodeToCodeModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    
    /** Configure HTTP timeout settings for LLM operations */
    void ConfigureHttpTimeouts();

    /** Show notification that editor restart is required for config settings to work*/
    void ShowRestartRequiredNotification();
};