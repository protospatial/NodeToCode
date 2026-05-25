// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditorModule.h"
#include "BlueprintEditor.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Utils/N2CLogger.h"
#include "LLM/IN2CLLMService.h"

/**
 * @class FN2CEditorIntegration
 * @brief Handles integration with the Blueprint Editor
 *
 * Manages Blueprint Editor toolbar extensions and provides
 * access to the active Blueprint Editor instance.
 */
class FN2CEditorIntegration
{
public:
    static FN2CEditorIntegration& Get();

    /** Initialize integration with Blueprint Editor */
    void Initialize();

    /** Cleanup integration */
    void Shutdown();

    /** Get available themes for a language */
    TArray<FName> GetAvailableThemes(EN2CCodeLanguage Language) const;

    /** Get the default theme for a language */
    FName GetDefaultTheme(EN2CCodeLanguage Language) const;

private:
    /** Constructor */
    FN2CEditorIntegration() = default;

public:
    /** Get Blueprint Editor from active tab */
    TSharedPtr<FBlueprintEditor> GetBlueprintEditorFromTab() const;

    /** Register for Blueprint Editor callbacks */
    void RegisterBlueprintEditorCallback();

private:
    /** Map of Blueprint Editor instances to their command lists */
    TMap<TWeakPtr<FBlueprintEditor>, TSharedPtr<FUICommandList>> EditorCommandLists;

    /** Register toolbar for a specific Blueprint Editor */
    void RegisterToolbarForEditor(TSharedPtr<FBlueprintEditor> InEditor);

    /** Execute collect nodes for a specific editor */
    void ExecuteCollectNodesForEditor(TWeakPtr<FBlueprintEditor> InEditor);

    /** Execute copy blueprint JSON to clipboard for a specific editor */
    void ExecuteCopyJsonForEditor(TWeakPtr<FBlueprintEditor> InEditor);
    
    /** Execute translate entire blueprint (all graphs) for a specific editor */
    void ExecuteTranslateEntireBlueprintForEditor(TWeakPtr<FBlueprintEditor> InEditor);
    
    /** Handle asset editor opened callback */
    void HandleAssetEditorOpened(UObject* Asset, IAssetEditorInstance* EditorInstance);

};
