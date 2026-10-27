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
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphToken.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "EdGraphSchema_K2.h"
#include "MCP/Tools/Implementations/Blueprint/Analysis/N2CMcpEditorContext.h"

UEdGraph* FN2CMcpBlueprintUtils::FindTopLevelGraph(const UBlueprint& Blueprint, const FString& GraphName, FString& OutErrorMsg)
{
	const TArray<UEdGraph*> Graphs = N2CMcpEditorContext::TopLevelGraphs(Blueprint);
	// FString == ignores case
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph->GetName() == GraphName)
		{
			return Graph;
		}
	}
	// "ConstructionScript", the name agents use, is the function graph UserConstructionScript (card 0286)
	if (GraphName == TEXT("ConstructionScript"))
	{
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript)
			{
				return Graph;
			}
		}
	}

	TArray<FString> Names;
	for (const UEdGraph* Graph : Graphs)
	{
		Names.Add(Graph->GetName());
	}
	OutErrorMsg = FString::Printf(TEXT("GRAPH_NOT_FOUND: %s has no graph named '%s'. Available graphs: %s"),
		*Blueprint.GetName(), *GraphName, Names.IsEmpty() ? TEXT("(none)") : *FString::Join(Names, TEXT(", ")));
	return nullptr;
}

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

namespace N2CMcpBlueprintUtilsPrivate
{
    /** Open FN2CMcpToolCallScope count (game thread) */
    int32 ScopeDepth = 0;
    /** Blueprints marked modified in the open tool call, in the order they were first marked */
    TArray<TWeakObjectPtr<UBlueprint>> PendingCompiles;
    /** Compiles of the open tool call that report to its result */
    TArray<FN2CCompileDiagnostics> CallCompiles;

    int32 CompileCount = 0;
    int32 RefreshRequestCount = 0;
    int32 RefreshRunCount = 0;
    TArray<TWeakObjectPtr<UBlueprint>> PendingRefreshes;
    FTSTicker::FDelegateHandle RefreshTicker;
    int32 RefreshFramesLeft = 0;

    void RunActionDatabaseRefresh()
    {
        TArray<TWeakObjectPtr<UBlueprint>> ToRefresh = MoveTemp(PendingRefreshes);
        PendingRefreshes.Reset();
        ++RefreshRunCount;

        FBlueprintActionDatabase* ActionDB = FBlueprintActionDatabase::TryGet();
        if (!ActionDB)
        {
            return;
        }
        int32 Refreshed = 0;
        for (const TWeakObjectPtr<UBlueprint>& Weak : ToRefresh)
        {
            if (UBlueprint* Blueprint = Weak.Get())
            {
                ActionDB->RefreshAssetActions(Blueprint);
                ++Refreshed;
            }
        }
        FN2CLogger::Get().Log(FString::Printf(TEXT("Deferred BlueprintActionDatabase refresh completed (%d Blueprint(s))."), Refreshed),
            EN2CLogSeverity::Debug);
    }

    bool TickActionDatabaseRefresh(float /*DeltaTime*/)
    {
        if (--RefreshFramesLeft > 0)
        {
            return true;
        }
        RefreshTicker.Reset();
        RunActionDatabaseRefresh();
        return false;
    }

    FString SeverityToString(EMessageSeverity::Type Severity)
    {
        switch (Severity)
        {
        case EMessageSeverity::Error:
            return TEXT("Error");
        case EMessageSeverity::Warning:
        case EMessageSeverity::PerformanceWarning:
            return TEXT("Warning");
        default:
            return TEXT("Note");
        }
    }

    /** The message with the first node (or pin) it links to, as the Compiler Results log links it */
    FN2CCompilerMessage ToCompilerMessage(const TSharedRef<FTokenizedMessage>& Message)
    {
        FN2CCompilerMessage Out(SeverityToString(Message->GetSeverity()), Message->ToText().ToString());
        for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
        {
            const UObject* Object = nullptr;
            const UEdGraphPin* Pin = nullptr;
            if (Token->GetType() == EMessageToken::EdGraph)
            {
                const TSharedRef<FEdGraphToken> GraphToken = StaticCastSharedRef<FEdGraphToken>(Token);
                Object = GraphToken->GetGraphObject();
                Pin = GraphToken->GetPin();
            }
            else if (Token->GetType() == EMessageToken::Object)
            {
                Object = StaticCastSharedRef<FUObjectToken>(Token)->GetObject().Get();
            }
            else
            {
                continue;
            }

            const UEdGraphNode* Node = Cast<const UEdGraphNode>(Object);
            if (!Node && Pin)
            {
                Node = Pin->GetOwningNodeUnchecked();
            }
            if (!Node)
            {
                if (const UEdGraph* Graph = Cast<const UEdGraph>(Object))
                {
                    if (Out.GraphName.IsEmpty())
                    {
                        Out.GraphName = Graph->GetName();
                    }
                }
                continue;
            }

            Out.NodeGuid = Node->NodeGuid.ToString();
            Out.NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
            if (const UEdGraph* Graph = Node->GetGraph())
            {
                Out.GraphName = Graph->GetName();
            }
            if (Pin)
            {
                Out.PinName = Pin->PinName.ToString();
            }
            break;
        }
        return Out;
    }

    FString SerializeJson(const TSharedRef<FJsonObject>& Object)
    {
        FString Text;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Object, Writer);
        return Text;
    }
}

TSharedRef<FJsonObject> FN2CCompileDiagnostics::MessageToJson(const FN2CCompilerMessage& Message)
{
    const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("severity"), Message.Severity);
    Json->SetStringField(TEXT("message"), Message.Message);
    if (!Message.NodeGuid.IsEmpty())
    {
        Json->SetStringField(TEXT("nodeGuid"), Message.NodeGuid);
        Json->SetStringField(TEXT("nodeTitle"), Message.NodeTitle);
    }
    if (!Message.GraphName.IsEmpty())
    {
        Json->SetStringField(TEXT("graph"), Message.GraphName);
    }
    if (!Message.PinName.IsEmpty())
    {
        Json->SetStringField(TEXT("pin"), Message.PinName);
    }
    return Json;
}

TSharedRef<FJsonObject> FN2CCompileDiagnostics::ToJson() const
{
    const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("blueprint"), BlueprintName);
    Json->SetStringField(TEXT("blueprintPath"), BlueprintPath);
    Json->SetStringField(TEXT("status"), Status);
    Json->SetBoolField(TEXT("success"), bSuccess);
    Json->SetNumberField(TEXT("errorCount"), ErrorCount);
    Json->SetNumberField(TEXT("warningCount"), WarningCount);
    Json->SetNumberField(TEXT("noteCount"), NoteCount);
    Json->SetNumberField(TEXT("compileSeconds"), CompileSeconds);
    TArray<TSharedPtr<FJsonValue>> MessageValues;
    for (const FN2CCompilerMessage& Message : Messages)
    {
        MessageValues.Add(MakeShared<FJsonValueObject>(MessageToJson(Message)));
    }
    Json->SetArrayField(TEXT("messages"), MessageValues);
    return Json;
}

FN2CMcpToolCallScope::FN2CMcpToolCallScope()
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    check(IsInGameThread());
    bOutermost = (ScopeDepth == 0);
    if (bOutermost)
    {
        PendingCompiles.Reset();
        CallCompiles.Reset();
    }
    ++ScopeDepth;
}

FN2CMcpToolCallScope::~FN2CMcpToolCallScope()
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    if (bOutermost)
    {
        // Edits after Flush, or a call that returned without flushing (an error path), still leave the Blueprint
        // compiled, as the old compile-per-edit did
        Flush();
        CallCompiles.Reset();
    }
    --ScopeDepth;
}

TArray<FN2CCompileDiagnostics> FN2CMcpToolCallScope::Flush()
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    bFlushed = true;
    if (!bOutermost)
    {
        return {};
    }
    // Compile removes each Blueprint from PendingCompiles; a compile that marks another modified adds it at the end
    while (PendingCompiles.Num() > 0)
    {
        const TWeakObjectPtr<UBlueprint> Next = PendingCompiles[0];
        PendingCompiles.RemoveAt(0);
        if (UBlueprint* Blueprint = Next.Get())
        {
            FN2CCompileDiagnostics Diagnostics;
            FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);
        }
    }
    TArray<FN2CCompileDiagnostics> Result = MoveTemp(CallCompiles);
    CallCompiles.Reset();
    return Result;
}

bool FN2CMcpToolCallScope::IsActive()
{
    return IsInGameThread() && N2CMcpBlueprintUtilsPrivate::ScopeDepth > 0;
}

void FN2CMcpBlueprintUtils::QueueActionDatabaseRefresh(UBlueprint* Blueprint, int32 DelayFrames)
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    if (!Blueprint || !IsInGameThread())
    {
        return;
    }
    ++RefreshRequestCount;
    PendingRefreshes.AddUnique(Blueprint);
    if (RefreshTicker.IsValid())
    {
        // One refresh is already queued; it refreshes this Blueprint too
        return;
    }
    RefreshFramesLeft = FMath::Max(1, DelayFrames);
    RefreshTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickActionDatabaseRefresh), 0.0f);
    FN2CLogger::Get().Log(FString::Printf(TEXT("Scheduled deferred BlueprintActionDatabase refresh in %d frame(s)."), RefreshFramesLeft),
        EN2CLogSeverity::Debug);
}

void FN2CMcpBlueprintUtils::CancelPendingActionDatabaseRefresh()
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    if (RefreshTicker.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(RefreshTicker);
        RefreshTicker.Reset();
    }
    PendingRefreshes.Reset();
}

bool FN2CMcpBlueprintUtils::FlushActionDatabaseRefreshForTest()
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    if (!RefreshTicker.IsValid())
    {
        return false;
    }
    FTSTicker::GetCoreTicker().RemoveTicker(RefreshTicker);
    RefreshTicker.Reset();
    RunActionDatabaseRefresh();
    return true;
}

int32 FN2CMcpBlueprintUtils::GetCompileCount()
{
    return N2CMcpBlueprintUtilsPrivate::CompileCount;
}

int32 FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRequestCount()
{
    return N2CMcpBlueprintUtilsPrivate::RefreshRequestCount;
}

int32 FN2CMcpBlueprintUtils::GetActionDatabaseRefreshRunCount()
{
    return N2CMcpBlueprintUtilsPrivate::RefreshRunCount;
}

bool FN2CMcpBlueprintUtils::IsActionDatabaseRefreshPending()
{
    return N2CMcpBlueprintUtilsPrivate::RefreshTicker.IsValid();
}

TArray<TWeakObjectPtr<UBlueprint>> FN2CMcpBlueprintUtils::GetPendingActionDatabaseRefreshes()
{
    return N2CMcpBlueprintUtilsPrivate::PendingRefreshes;
}

void FN2CMcpBlueprintUtils::MarkModified(UBlueprint* Blueprint, bool bStructural)
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    if (!Blueprint)
    {
        FN2CLogger::Get().LogError(TEXT("FN2CMcpBlueprintUtils::MarkModified"), TEXT("Cannot mark null Blueprint as modified"));
        return;
    }

    if (bStructural)
    {
        // Regenerates the skeleton class now (FBlueprintCompilationManager::CompileSynchronously with
        // RegenerateSkeletonOnly); the full compile with its reinstancing runs below or when the tool call ends,
        // before the next frame, so Slate never sees half-compiled preview actors
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    else
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
    // MarkBlueprintAsModified dirties the package, but the structural path returns early while the Blueprint is
    // queued for compilation; make sure Save All and the exit prompt see the edit
    Blueprint->MarkPackageDirty();

    if (bStructural)
    {
        QueueActionDatabaseRefresh(Blueprint);
    }

    if (FN2CMcpToolCallScope::IsActive())
    {
        PendingCompiles.AddUnique(Blueprint);
    }
    else
    {
        FN2CCompileDiagnostics Diagnostics;
        Compile(Blueprint, Diagnostics);
    }
}

void FN2CMcpBlueprintUtils::MarkCosmeticChange(UBlueprint* Blueprint)
{
    if (Blueprint)
    {
        Blueprint->MarkPackageDirty();
    }
}

bool FN2CMcpBlueprintUtils::Compile(UBlueprint* Blueprint, FN2CCompileDiagnostics& OutDiagnostics, bool bSkipGarbageCollection,
    bool bReportInToolResult)
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    OutDiagnostics = FN2CCompileDiagnostics();
    if (!Blueprint)
    {
        FN2CLogger::Get().LogError(TEXT("FN2CMcpBlueprintUtils::Compile"), TEXT("Cannot compile null Blueprint"));
        return false;
    }

    OutDiagnostics.BlueprintName = Blueprint->GetName();
    OutDiagnostics.BlueprintPath = Blueprint->GetPathName();
    UPackage* const Package = Blueprint->GetOutermost();
    const bool bWasDirty = Package && Package->IsDirty();

    FCompilerResultsLog CompilerResults;
    CompilerResults.bSilentMode = true;
    CompilerResults.bAnnotateMentionedNodes = true;
    CompilerResults.SetSourcePath(Blueprint->GetPathName());
    CompilerResults.BeginEvent(TEXT("NodeToCode Compile"));

    const EBlueprintCompileOptions CompileFlags = bSkipGarbageCollection
        ? EBlueprintCompileOptions::SkipGarbageCollection
        : EBlueprintCompileOptions::None;
    const double StartTime = FPlatformTime::Seconds();
    FKismetEditorUtilities::CompileBlueprint(Blueprint, CompileFlags, &CompilerResults);
    OutDiagnostics.CompileSeconds = static_cast<float>(FPlatformTime::Seconds() - StartTime);
    CompilerResults.EndEvent();
    ++CompileCount;

    OutDiagnostics.bCompiled = true;
    OutDiagnostics.ErrorCount = CompilerResults.NumErrors;
    OutDiagnostics.WarningCount = CompilerResults.NumWarnings;
    for (const TSharedRef<FTokenizedMessage>& Message : CompilerResults.Messages)
    {
        FN2CCompilerMessage Converted = ToCompilerMessage(Message);
        if (Converted.Severity == TEXT("Note"))
        {
            ++OutDiagnostics.NoteCount;
        }
        OutDiagnostics.Messages.Add(MoveTemp(Converted));
    }
    OutDiagnostics.Status = StatusToString(static_cast<uint8>(Blueprint->Status));
    OutDiagnostics.bSuccess = OutDiagnostics.ErrorCount == 0 && Blueprint->Status != BS_Error;

    // The compile manager restores the package's pre-compile dirty flag; an edited Blueprint stays dirty
    if (bWasDirty && Package && !Package->IsDirty())
    {
        Blueprint->MarkPackageDirty();
    }

    PendingCompiles.Remove(Blueprint);
    if (bReportInToolResult && FN2CMcpToolCallScope::IsActive())
    {
        CallCompiles.Add(OutDiagnostics);
    }

    FN2CLogger::Get().Log(FString::Printf(TEXT("Blueprint '%s' compiled: %s, %d error(s), %d warning(s), %.3f s"),
        *Blueprint->GetName(), *OutDiagnostics.Status, OutDiagnostics.ErrorCount, OutDiagnostics.WarningCount,
        OutDiagnostics.CompileSeconds), OutDiagnostics.bSuccess ? EN2CLogSeverity::Info : EN2CLogSeverity::Warning);

    return OutDiagnostics.bSuccess;
}

TSharedPtr<FJsonObject> FN2CMcpBlueprintUtils::CompileDiagnosticsToJson(const TArray<FN2CCompileDiagnostics>& Compiles)
{
    if (Compiles.Num() == 0)
    {
        return nullptr;
    }
    if (Compiles.Num() == 1)
    {
        return Compiles[0].ToJson();
    }

    // More than one Blueprint compiled in the call: totals, then each one
    const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    bool bSuccess = true;
    int32 Errors = 0;
    int32 Warnings = 0;
    TArray<TSharedPtr<FJsonValue>> Each;
    for (const FN2CCompileDiagnostics& Diagnostics : Compiles)
    {
        bSuccess &= Diagnostics.bSuccess;
        Errors += Diagnostics.ErrorCount;
        Warnings += Diagnostics.WarningCount;
        Each.Add(MakeShared<FJsonValueObject>(Diagnostics.ToJson()));
    }
    Json->SetBoolField(TEXT("success"), bSuccess);
    Json->SetNumberField(TEXT("errorCount"), Errors);
    Json->SetNumberField(TEXT("warningCount"), Warnings);
    Json->SetArrayField(TEXT("blueprints"), Each);
    return Json;
}

void FN2CMcpBlueprintUtils::AttachCompileDiagnostics(FMcpToolCallResult& Result, const TArray<FN2CCompileDiagnostics>& Compiles)
{
    using namespace N2CMcpBlueprintUtilsPrivate;
    const TSharedPtr<FJsonObject> CompileJson = CompileDiagnosticsToJson(Compiles);
    if (!CompileJson.IsValid())
    {
        return;
    }

    // Into the result's JSON text when it is an object without a "compile" field of its own
    for (const TSharedPtr<FJsonObject>& Item : Result.Content)
    {
        FString Type;
        FString Text;
        if (!Item.IsValid() || !Item->TryGetStringField(TEXT("type"), Type) || Type != TEXT("text")
            || !Item->TryGetStringField(TEXT("text"), Text))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Parsed;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
        if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid() && !Parsed->HasField(TEXT("compile")))
        {
            Parsed->SetObjectField(TEXT("compile"), CompileJson);
            Item->SetStringField(TEXT("text"), SerializeJson(Parsed.ToSharedRef()));
            return;
        }
        break;
    }

    // Otherwise (an error text, a non-JSON result) as its own text item
    const TSharedRef<FJsonObject> Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetObjectField(TEXT("compile"), CompileJson);
    const TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
    TextContent->SetStringField(TEXT("type"), TEXT("text"));
    TextContent->SetStringField(TEXT("text"), SerializeJson(Wrapper));
    Result.Content.Add(TextContent);
}

FString FN2CMcpBlueprintUtils::StatusToString(uint8 Status)
{
    switch (static_cast<EBlueprintStatus>(Status))
    {
    case BS_Dirty:
        return TEXT("Dirty");
    case BS_Error:
        return TEXT("Error");
    case BS_UpToDate:
        return TEXT("UpToDate");
    case BS_BeingCreated:
        return TEXT("BeingCreated");
    case BS_UpToDateWithWarnings:
        return TEXT("UpToDateWithWarnings");
    default:
        return TEXT("Unknown");
    }
}
