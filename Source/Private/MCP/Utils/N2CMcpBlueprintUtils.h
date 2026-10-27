// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UBlueprint;
class UEdGraph;
class IBlueprintEditor;
class FJsonObject;

/**
 * One compiler message. NodeGuid, NodeTitle, GraphName and PinName are set when the message points at a node (or a
 * pin) of the Blueprint, the way the editor's Compiler Results log links to it; empty otherwise.
 */
struct FN2CCompilerMessage
{
    /** Message severity: Error, Warning, Note */
    FString Severity;

    /** The actual message text */
    FString Message;

    /** The node the message names (its NodeGuid, as get-focused-blueprint and find-nodes-in-graph report it) */
    FString NodeGuid;
    FString NodeTitle;
    /** The graph that holds that node */
    FString GraphName;
    /** The pin the message names, when it names one */
    FString PinName;

    FN2CCompilerMessage(const FString& InSeverity, const FString& InMessage)
        : Severity(InSeverity)
        , Message(InMessage)
    {
    }
};

/** What one compile of one Blueprint reported */
struct NODETOCODE_API FN2CCompileDiagnostics
{
    FString BlueprintName;
    FString BlueprintPath;
    /** False when there was nothing to compile (no Blueprint) */
    bool bCompiled = false;
    /** No errors */
    bool bSuccess = false;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    int32 NoteCount = 0;
    float CompileSeconds = 0.0f;
    /** The Blueprint's status after the compile (UpToDate, UpToDateWithWarnings, Error, ...) */
    FString Status;
    TArray<FN2CCompilerMessage> Messages;

    /** {blueprint, blueprintPath, status, success, errorCount, warningCount, noteCount, compileSeconds, messages[]} */
    TSharedRef<FJsonObject> ToJson() const;
    /** One message as {severity, message, nodeGuid?, nodeTitle?, graph?, pin?} */
    static TSharedRef<FJsonObject> MessageToJson(const FN2CCompilerMessage& Message);
};

/**
 * One tool call (or Python bridge call), for compiling once per call. While a scope is open on the game thread,
 * FN2CMcpBlueprintUtils::MarkModified records the Blueprint instead of compiling it; the outermost scope compiles
 * every recorded Blueprint once, in Flush() (or in its destructor if Flush was not called). Open the scope before
 * the call's FScopedTransaction and flush it after the transaction has closed, so the compile is not recorded in the
 * undo buffer. FN2CMcpToolBase::ExecuteOnGameThread opens one around every tool's game-thread logic, so a run-python
 * script that makes many bridge calls compiles once, at the end of the tool call. Scopes nest: an inner scope (a
 * bridge call inside run-python) leaves the compile to the outermost one. Game thread only.
 */
class NODETOCODE_API FN2CMcpToolCallScope
{
public:
    FN2CMcpToolCallScope();
    ~FN2CMcpToolCallScope();

    FN2CMcpToolCallScope(const FN2CMcpToolCallScope&) = delete;
    FN2CMcpToolCallScope& operator=(const FN2CMcpToolCallScope&) = delete;

    /**
     * In the outermost scope: compiles each Blueprint marked modified since the scope opened (once each, in the order
     * they were first marked) and returns the diagnostics of every compile this call ran that reports to the tool
     * result, explicit FN2CMcpBlueprintUtils::Compile calls included. In an inner scope: compiles nothing and returns
     * nothing (the outermost scope will).
     */
    TArray<FN2CCompileDiagnostics> Flush();

    /** True when this is the outermost open scope */
    bool IsOutermost() const { return bOutermost; }

    /** True while any scope is open on the game thread */
    static bool IsActive();

private:
    bool bOutermost = false;
    bool bFlushed = false;
};

class NODETOCODE_API FN2CMcpBlueprintUtils
{
public:
    /**
     * Queues a refresh of the Blueprint Action Database entries of this Blueprint (FBlueprintActionDatabase::
     * RefreshAssetActions), for a change that adds or removes actions (variables, functions, components). All
     * requests made before it runs share one deferred refresh (a couple of frames later, after the compile and the
     * engine's own preview-actor updates); each Blueprint is refreshed once. Read-only tools never call this, and
     * nothing here runs FBlueprintActionDatabase::RefreshAll.
     */
    static void QueueActionDatabaseRefresh(UBlueprint* Blueprint, int32 DelayFrames = 2);

    /** Drops a queued refresh (module shutdown) */
    static void CancelPendingActionDatabaseRefresh();

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
     * The graph of Blueprint that open-blueprint-asset and the Python bridge's OpenBlueprint focus by name (card 0286):
     * one of the graphs My Blueprint lists (N2CMcpEditorContext::TopLevelGraphs: event graph pages, functions, macros,
     * interface implementations, delegate signatures), matched case-insensitively. "ConstructionScript" also names the
     * construction script, the function graph UEdGraphSchema_K2::FN_UserConstructionScript, when no graph has that name.
     * @param OutErrorMsg When nothing matches: "GRAPH_NOT_FOUND: ..." listing the Blueprint's graph names.
     * @return The graph, or nullptr when no graph has that name.
     */
    static UEdGraph* FindTopLevelGraph(const UBlueprint& Blueprint, const FString& GraphName, FString& OutErrorMsg);

    /**
     * Opens or focuses a Blueprint editor for the given Blueprint and makes it the focused Blueprint editor.
     * This method will reuse existing editors if one is already open for the Blueprint, and never creates a second one.
     * OutEditor is always the editor of this Blueprint (FN2CBlueprintEditorTracker::GetEditorFor), never whichever
     * editor was focused before. Fails when the Blueprint opens in an editor that is not a Blueprint editor.
     * @param Blueprint The Blueprint to open in the editor.
     * @param OutEditor The resulting Blueprint editor instance (an FBlueprintEditor whose GetBlueprintObj() is Blueprint).
     * @param OutErrorMsg Error message if the editor could not be opened.
     * @return True if the editor was successfully opened or focused, false otherwise.
     */
    static bool OpenBlueprintEditor(UBlueprint* Blueprint, TSharedPtr<IBlueprintEditor>& OutEditor, FString& OutErrorMsg);

    /**
     * Marks a Blueprint modified after an edit that changes what it compiles to, and has it compiled once for the
     * call. bStructural (variables, functions, pins, components, local variables) regenerates the skeleton class at
     * once, as the editor does, so later edits in the same call see the new members, and queues one action-database
     * refresh; otherwise (nodes, links, pin values) it only marks the Blueprint dirty. The package is marked dirty
     * either way. Inside a tool call (FN2CMcpToolCallScope) the full compile runs once when the call ends, after its
     * transaction has closed; outside one it runs here, at once.
     */
    static void MarkModified(UBlueprint* Blueprint, bool bStructural);

    /**
     * Marks the package dirty for a change that does not affect compilation (comments, node positions). Never
     * compiles and leaves the Blueprint's status as it is.
     */
    static void MarkCosmeticChange(UBlueprint* Blueprint);

    /**
     * Compiles a Blueprint now, synchronously, and reports what the compiler said. A package that was dirty before
     * the compile stays dirty. Removes the Blueprint from the open tool call's pending compiles, so the call does not
     * compile it again unless it is marked modified again. Call it after the edit's transaction has closed.
     * @param Blueprint The Blueprint to compile.
     * @param OutDiagnostics Counts and messages, each message with the node and graph it names.
     * @param bSkipGarbageCollection Skip garbage collection after the compile (faster).
     * @param bReportInToolResult Also add the diagnostics to the open tool call's result (the "compile" field); false
     *        for a tool that reports the compile in its own fields.
     * @return True if the compile had no errors.
     */
    static bool Compile(UBlueprint* Blueprint, FN2CCompileDiagnostics& OutDiagnostics, bool bSkipGarbageCollection = true,
        bool bReportInToolResult = true);

    /**
     * Adds a call's compile diagnostics to a tool result: as a "compile" field of the result's JSON text when it is a
     * JSON object without one (the Blueprint's diagnostics, or totals plus a "blueprints" array when the call compiled
     * more than one), otherwise as an extra text item. Does nothing when Compiles is empty.
     */
    static void AttachCompileDiagnostics(struct FMcpToolCallResult& Result, const TArray<FN2CCompileDiagnostics>& Compiles);

    /** The "compile" value AttachCompileDiagnostics adds, or null when Compiles is empty */
    static TSharedPtr<FJsonObject> CompileDiagnosticsToJson(const TArray<FN2CCompileDiagnostics>& Compiles);

    /** Blueprint status as text (UpToDate, UpToDateWithWarnings, Error, Dirty, ...) */
    static FString StatusToString(uint8 Status);

    // Counters for tests and logs (game thread)

    /** Full compiles Compile() has run since the editor started */
    static int32 GetCompileCount();
    /** QueueActionDatabaseRefresh calls since the editor started */
    static int32 GetActionDatabaseRefreshRequestCount();
    /** Deferred refreshes that have run (each refreshes every queued Blueprint once) */
    static int32 GetActionDatabaseRefreshRunCount();
    /** True while a deferred refresh is queued */
    static bool IsActionDatabaseRefreshPending();
    /** The Blueprints the queued refresh will refresh */
    static TArray<TWeakObjectPtr<UBlueprint>> GetPendingActionDatabaseRefreshes();
    /** Runs a queued refresh now instead of a few frames later (tests); false when none is queued */
    static bool FlushActionDatabaseRefreshForTest();
};
