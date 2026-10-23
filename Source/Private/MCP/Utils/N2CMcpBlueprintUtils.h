// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class IBlueprintEditor;

/**
 * Structure to hold compiler message information
 */
struct FN2CCompilerMessage
{
    /** Message severity: Error, Warning, Info */
    FString Severity;
    
    /** The actual message text */
    FString Message;
    
    FN2CCompilerMessage(const FString& InSeverity, const FString& InMessage)
        : Severity(InSeverity)
        , Message(InMessage)
    {
    }
};

class NODETOCODE_API FN2CMcpBlueprintUtils
{
public:
    /**
     * Refreshes the Blueprint Action Database.
     * Call this after operations that might alter the available Blueprint actions
     * to prevent editor context menu freezes.
     *
     * WARNING: Calling this immediately after MarkBlueprintAsStructurallyModified can cause
     * crashes if preview actors exist in transient worlds. Consider using DeferredRefreshBlueprintActionDatabase instead.
     */
    static void RefreshBlueprintActionDatabase();

    /**
     * Schedules a deferred refresh of the Blueprint Action Database.
     * This is safer to call after Blueprint modifications as it allows the engine
     * to complete internal updates (like refreshing preview actors) before triggering
     * the database refresh.
     *
     * @param DelayFrames Number of frames to delay before refreshing (default: 2)
     */
    static void DeferredRefreshBlueprintActionDatabase(int32 DelayFrames = 2);

    /**
     * Resolves a UBlueprint from an optional asset path.
     * If OptionalBlueprintPath is empty, it attempts to get the currently focused Blueprint from the active editor.
     * @param OptionalBlueprintPath The asset path of the Blueprint (e.g., "/Game/Blueprints/MyActor.MyActor"). Can be empty.
     * @param OutErrorMsg Error message if resolution fails. Populated with a code (e.g., ASSET_NOT_FOUND, NO_ACTIVE_BLUEPRINT) and description.
     * @return The resolved UBlueprint or nullptr if not found or an error occurred.
     */
    static UBlueprint* ResolveBlueprint(const FString& OptionalBlueprintPath, FString& OutErrorMsg);

    /**
     * Gets the currently focused UEdGraph and its owning UBlueprint from the active editor.
     * @param OutBlueprint Will be set to the owning UBlueprint of the focused graph.
     * @param OutGraph Will be set to the focused UEdGraph.
     * @param OutErrorMsg Error message if no focused graph or Blueprint is found. Populated with a code (e.g., NO_FOCUSED_GRAPH) and description.
     * @return True if a focused graph and its Blueprint were successfully retrieved, false otherwise.
     */
    static bool GetFocusedEditorGraph(UBlueprint*& OutBlueprint, UEdGraph*& OutGraph, FString& OutErrorMsg);

    /**
     * Opens or focuses a Blueprint editor for the given Blueprint.
     * This method will reuse existing editors if one is already open for the Blueprint.
     * @param Blueprint The Blueprint to open in the editor.
     * @param OutEditor The resulting Blueprint editor instance.
     * @param OutErrorMsg Error message if the editor could not be opened.
     * @return True if the editor was successfully opened or focused, false otherwise.
     */
    static bool OpenBlueprintEditor(UBlueprint* Blueprint, TSharedPtr<IBlueprintEditor>& OutEditor, FString& OutErrorMsg);

    /**
     * Marks a Blueprint as structurally modified and compiles it synchronously.
     * This is a safer alternative to FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified
     * which queues deferred compilation. Deferred compilation can cause crashes when Slate
     * UI updates occur before the compile finishes, leaving preview actors in a "DEADCLASS" state.
     * After the compile it marks the Blueprint's package dirty, since the compile restores the pre-compile
     * dirty flag; call it only after a change the user should be asked to save.
     *
     * @param Blueprint The Blueprint to mark as modified and compile.
     * @param bSkipGarbageCollection If true, skips garbage collection during compilation for better performance.
     */
    static void MarkBlueprintAsModifiedAndCompile(UBlueprint* Blueprint, bool bSkipGarbageCollection = true);

    /**
     * Compiles a Blueprint and returns compilation results.
     * @param Blueprint The Blueprint to compile.
     * @param bSkipGarbageCollection If true, skips garbage collection during compilation for better performance.
     * @param OutErrorCount Number of errors encountered during compilation.
     * @param OutWarningCount Number of warnings encountered during compilation.
     * @param OutCompilationTime Time taken to compile in seconds.
     * @param OutMessages Optional array to receive detailed compilation messages.
     * @return True if compilation succeeded (no errors), false if there were errors.
     */
    static bool CompileBlueprint(UBlueprint* Blueprint, bool bSkipGarbageCollection, 
        int32& OutErrorCount, int32& OutWarningCount, float& OutCompilationTime,
        TArray<TSharedPtr<struct FN2CCompilerMessage>>* OutMessages = nullptr);
};
