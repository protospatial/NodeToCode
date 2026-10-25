// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditor.h" // Required for TSharedPtr<FBlueprintEditor>
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Utils/N2CLogger.h"
#include "BlueprintActionDatabase.h" // For FBlueprintActionDatabase
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Containers/Ticker.h" // For FTSTicker

UBlueprint* FN2CMcpBlueprintUtils::ResolveBlueprint(const FString& OptionalBlueprintPath, FString& OutErrorMsg)
{
    OutErrorMsg.Empty();
    UBlueprint* ResolvedBlueprint = nullptr;

    if (!OptionalBlueprintPath.IsEmpty())
    {
        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
        FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(OptionalBlueprintPath));

        if (AssetData.IsValid())
        {
            ResolvedBlueprint = Cast<UBlueprint>(AssetData.GetAsset());
        }
        
        if (!ResolvedBlueprint)
        {
            // Try alternate loading method if AssetRegistry fails (e.g. for unsaved assets or direct paths)
            ResolvedBlueprint = LoadObject<UBlueprint>(nullptr, *OptionalBlueprintPath);
        }

        if (!ResolvedBlueprint)
        {
            OutErrorMsg = FString::Printf(TEXT("ASSET_NOT_FOUND: Blueprint not found at path: %s"), *OptionalBlueprintPath);
            return nullptr;
        }
        return ResolvedBlueprint;
    }
    
    // If path is empty, try to get the focused Blueprint
    TSharedPtr<FBlueprintEditor> Editor = FN2CEditorIntegration::Get().GetActiveBlueprintEditor();
    if (Editor.IsValid())
    {
        ResolvedBlueprint = Editor->GetBlueprintObj();
    }

    if (!ResolvedBlueprint)
    {
        OutErrorMsg = TEXT("NO_ACTIVE_BLUEPRINT: No blueprint path provided and no focused editor found.");
        return nullptr;
    }

    return ResolvedBlueprint;
}

bool FN2CMcpBlueprintUtils::GetFocusedEditorGraph(UBlueprint*& OutBlueprint, UEdGraph*& OutGraph, FString& OutErrorMsg)
{
    OutErrorMsg.Empty();
    OutBlueprint = nullptr;
    OutGraph = nullptr;

    OutGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
    if (!OutGraph)
    {
        OutErrorMsg = TEXT("NO_FOCUSED_GRAPH: No graph is currently focused in the active Blueprint editor.");
        return false;
    }

    OutBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(OutGraph);
    if (!OutBlueprint)
    {
        // This case should ideally not happen if a graph is focused.
        OutErrorMsg = FString::Printf(TEXT("INTERNAL_ERROR: Could not find owning Blueprint for focused graph: %s"), *OutGraph->GetName());
        return false;
    }
    
    return true;
}

bool FN2CMcpBlueprintUtils::OpenBlueprintEditor(UBlueprint* Blueprint, TSharedPtr<IBlueprintEditor>& OutEditor, FString& OutErrorMsg)
{
    OutErrorMsg.Empty();
    OutEditor.Reset();

    if (!Blueprint)
    {
        OutErrorMsg = TEXT("INVALID_BLUEPRINT: Blueprint is null");
        return false;
    }

    // Get the asset editor subsystem
    UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
    if (!AssetEditorSubsystem)
    {
        OutErrorMsg = TEXT("EDITOR_SUBSYSTEM_ERROR: Could not get AssetEditorSubsystem");
        return false;
    }

    // The editor already open on this Blueprint, brought to the front; otherwise open one (the subsystem reuses an
    // existing editor and never makes a second one). Either way the editor comes from the Blueprint itself, never from
    // whichever editor was focused before (card 0150)
    TSharedPtr<FBlueprintEditor> Editor = FN2CBlueprintEditorTracker::GetEditorFor(Blueprint);
    if (Editor.IsValid())
    {
        Editor->FocusWindow();
    }
    else
    {
        if (!AssetEditorSubsystem->OpenEditorForAsset(Blueprint))
        {
            OutErrorMsg = TEXT("OPEN_EDITOR_FAILED: Failed to open editor for Blueprint");
            return false;
        }
        Editor = FN2CBlueprintEditorTracker::GetEditorFor(Blueprint);
    }

    if (!Editor.IsValid())
    {
        // Some UBlueprint subclasses open in their own toolkits (Control Rig with RigVM.UseNewEditor=1)
        OutErrorMsg = FString::Printf(TEXT("NOT_A_BLUEPRINT_EDITOR: %s opened in an editor that is not a Blueprint editor"),
            *Blueprint->GetName());
        return false;
    }
    if (Editor->GetBlueprintObj() != Blueprint)
    {
        OutErrorMsg = FString::Printf(TEXT("EDITOR_MISMATCH: the editor found for %s edits %s"), *Blueprint->GetPathName(),
            Editor->GetBlueprintObj() ? *Editor->GetBlueprintObj()->GetPathName() : TEXT("nothing"));
        return false;
    }

    // Opening (or re-opening) a Blueprint through a tool makes it the focused one, as a click on its tab would
    FN2CBlueprintEditorTracker::Get().NotifyEditorActivated(Editor);
    FN2CLogger::Get().Log(FString::Printf(TEXT("OpenBlueprintEditor: %s is open and focused"), *Blueprint->GetName()), EN2CLogSeverity::Debug);
    OutEditor = Editor;
    return true;
}

void FN2CMcpBlueprintUtils::RefreshBlueprintActionDatabase()
{
    if (FBlueprintActionDatabase* ActionDB = FBlueprintActionDatabase::TryGet())
    {
        FN2CLogger::Get().Log(TEXT("Refreshing BlueprintActionDatabase via utility function."), EN2CLogSeverity::Debug);
        ActionDB->RefreshAll();
        FN2CLogger::Get().Log(TEXT("BlueprintActionDatabase refreshed successfully via utility."), EN2CLogSeverity::Debug);
    }
    else
    {
        FN2CLogger::Get().LogWarning(TEXT("FBlueprintActionDatabase not available for refresh via utility. Context menu issues might persist."));
    }
}

void FN2CMcpBlueprintUtils::MarkBlueprintAsModifiedAndCompile(UBlueprint* Blueprint, bool bSkipGarbageCollection)
{
    if (!Blueprint)
    {
        FN2CLogger::Get().LogError(TEXT("FN2CMcpBlueprintUtils::MarkBlueprintAsModifiedAndCompile"),
            TEXT("Cannot mark null Blueprint as modified"));
        return;
    }

    // Compile Blueprint synchronously to ensure preview actors are properly updated
    // Using FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified causes crashes because
    // it queues deferred compilation, but Slate UI updates on the next frame try to access
    // preview actors that are in an invalid "DEADCLASS" state
    EBlueprintCompileOptions CompileOptions = bSkipGarbageCollection
        ? EBlueprintCompileOptions::SkipGarbageCollection
        : EBlueprintCompileOptions::None;

    FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileOptions);

    // The compile manager restores the package's pre-compile dirty flag, and the Modify(false) calls on the pin
    // and node paths (CreatePin, ReconstructNode) never set it, so mark the package dirty here, after the compile,
    // or an edit to a saved Blueprint is skipped by Save All and gets no save prompt on exit. MarkPackageDirty
    // queues no compile, unlike MarkBlueprintAsStructurallyModified above.
    Blueprint->MarkPackageDirty();

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Blueprint '%s' marked as modified and compiled synchronously"), *Blueprint->GetName()),
        EN2CLogSeverity::Debug
    );
}

void FN2CMcpBlueprintUtils::DeferredRefreshBlueprintActionDatabase(int32 DelayFrames)
{
    // Use a shared counter to track remaining frames
    TSharedRef<int32> RemainingFrames = MakeShared<int32>(FMath::Max(1, DelayFrames));

    // Schedule the deferred refresh using a ticker
    FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda([RemainingFrames](float DeltaTime) -> bool
        {
            (*RemainingFrames)--;

            if (*RemainingFrames <= 0)
            {
                // Time to refresh - ensure we're on the game thread
                if (IsInGameThread())
                {
                    if (FBlueprintActionDatabase* ActionDB = FBlueprintActionDatabase::TryGet())
                    {
                        FN2CLogger::Get().Log(TEXT("Executing deferred BlueprintActionDatabase refresh."), EN2CLogSeverity::Debug);
                        ActionDB->RefreshAll();
                        FN2CLogger::Get().Log(TEXT("Deferred BlueprintActionDatabase refresh completed."), EN2CLogSeverity::Debug);
                    }
                }
                return false; // Remove the ticker
            }
            return true; // Continue ticking
        }),
        0.0f // Tick every frame
    );

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Scheduled deferred BlueprintActionDatabase refresh in %d frame(s)."), DelayFrames),
        EN2CLogSeverity::Debug
    );
}

bool FN2CMcpBlueprintUtils::CompileBlueprint(UBlueprint* Blueprint, bool bSkipGarbageCollection, 
    int32& OutErrorCount, int32& OutWarningCount, float& OutCompilationTime,
    TArray<TSharedPtr<FN2CCompilerMessage>>* OutMessages)
{
    OutErrorCount = 0;
    OutWarningCount = 0;
    OutCompilationTime = 0.0f;
    
    if (!Blueprint)
    {
        FN2CLogger::Get().LogError(TEXT("FN2CMcpBlueprintUtils::CompileBlueprint"), 
            TEXT("Cannot compile null Blueprint"));
        return false;
    }
    
    // Record start time
    double StartTime = FPlatformTime::Seconds();
    
    // Create compiler results log
    FCompilerResultsLog CompilerResults;
    CompilerResults.bSilentMode = true; // We'll handle output ourselves
    CompilerResults.bAnnotateMentionedNodes = true;
    CompilerResults.SetSourcePath(Blueprint->GetPathName());
    
    // Begin compilation event
    CompilerResults.BeginEvent(TEXT("MCP Compile"));
    
    // Set compilation flags
    EBlueprintCompileOptions CompileFlags = bSkipGarbageCollection 
        ? EBlueprintCompileOptions::SkipGarbageCollection 
        : EBlueprintCompileOptions::None;
    
    bool bSuccess = false;
    
    try
    {
        // Compile the Blueprint
        FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileFlags, &CompilerResults);
        bSuccess = true;
    }
    catch (...)
    {
        FN2CLogger::Get().LogError(TEXT("FN2CMcpBlueprintUtils::CompileBlueprint"), 
            TEXT("Exception during Blueprint compilation"));
        bSuccess = false;
    }
    
    // End compilation event
    CompilerResults.EndEvent();
    
    // Calculate compilation time
    OutCompilationTime = static_cast<float>(FPlatformTime::Seconds() - StartTime);
    
    // Extract error and warning counts
    OutErrorCount = CompilerResults.NumErrors;
    OutWarningCount = CompilerResults.NumWarnings;
    
    // Extract detailed messages if requested
    if (OutMessages)
    {
        OutMessages->Empty();
        
        for (const TSharedRef<FTokenizedMessage>& Message : CompilerResults.Messages)
        {
            FString Severity;
            switch (Message->GetSeverity())
            {
                case EMessageSeverity::Error:
                    Severity = TEXT("Error");
                    break;
                case EMessageSeverity::Warning:
                    Severity = TEXT("Warning");
                    break;
                case EMessageSeverity::Info:
                    Severity = TEXT("Note");
                    break;
                default:
                    Severity = TEXT("Unknown");
                    break;
            }
            
            OutMessages->Add(MakeShareable(new FN2CCompilerMessage(Severity, Message->ToText().ToString())));
        }
    }
    
    // Log compilation result
    if (OutErrorCount > 0)
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Blueprint '%s' compilation failed with %d error(s) and %d warning(s)"), 
                *Blueprint->GetName(), OutErrorCount, OutWarningCount),
            EN2CLogSeverity::Warning
        );
    }
    else if (OutWarningCount > 0)
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Blueprint '%s' compiled successfully with %d warning(s)"), 
                *Blueprint->GetName(), OutWarningCount),
            EN2CLogSeverity::Info
        );
    }
    else
    {
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Blueprint '%s' compiled successfully"), 
                *Blueprint->GetName()),
            EN2CLogSeverity::Info
        );
    }
    
    // Schedule deferred refresh of Blueprint action database after compilation
    DeferredRefreshBlueprintActionDatabase();
    
    // Return true if compilation succeeded (no errors), false if there were errors
    return bSuccess && (OutErrorCount == 0);
}
