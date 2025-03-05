// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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
};
