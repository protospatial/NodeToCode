// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

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
