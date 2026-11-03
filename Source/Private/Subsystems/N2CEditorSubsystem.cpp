// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Subsystems/N2CEditorSubsystem.h"
#include "Core/N2CGraphStateManager.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "UObject/Stack.h"

namespace N2CEditorSubsystemPrivate
{
	/** True when Path names something; else raises INVALID_ARGUMENT for Function */
	bool RequirePath(const TCHAR* Function, const FString& Path)
	{
		if (Path.TrimStartAndEnd().IsEmpty())
		{
			UN2CEditorSubsystem::RaiseError(Function, Path,
				TEXT("INVALID_ARGUMENT: a Blueprint asset path is required, such as /Game/Blueprints/BP_Door"));
			return false;
		}
		return true;
	}

	/** The Blueprint at a non-empty path, or null after raising the loader's error */
	UBlueprint* LoadOrRaise(const TCHAR* Function, const FString& Path)
	{
		if (!RequirePath(Function, Path))
		{
			return nullptr;
		}
		FString Error;
		UBlueprint* Blueprint = FN2CGraphTarget::LoadBlueprint(Path.TrimStartAndEnd(), Error);
		if (!Blueprint)
		{
			UN2CEditorSubsystem::RaiseError(Function, Path, Error.IsEmpty() ? FString(TEXT("ASSET_NOT_FOUND: no Blueprint at this path")) : Error);
		}
		return Blueprint;
	}

	FString StringField(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		return Json.IsValid() && Json->TryGetStringField(Field, Value) ? Value : FString();
	}
}

void UN2CEditorSubsystem::RaiseError(const TCHAR* Function, const FString& Path, const FString& Error)
{
	const FString Message = FString::Printf(TEXT("%s: %s: %s"), Function, Path.IsEmpty() ? TEXT("(no path)") : *Path, *Error);
	FFrame::KismetExecutionMessage(*Message, ELogVerbosity::Error);
}

FN2CCompileResult UN2CEditorSubsystem::CompileBlueprint(const FString& BlueprintPath)
{
	using namespace N2CEditorSubsystemPrivate;
	FN2CCompileResult Result;
	UBlueprint* Blueprint = LoadOrRaise(TEXT("CompileBlueprint"), BlueprintPath);
	if (!Blueprint)
	{
		return Result;
	}
	// The compile the MCP tools and the bridge run, with the compiler's messages (card 0151)
	FN2CCompileDiagnostics Diagnostics;
	FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics, /*bSkipGarbageCollection*/ false, /*bReportInToolResult*/ false);
	Result.BlueprintPath = Blueprint->GetPathName();
	Result.Status = Diagnostics.Status;
	Result.ErrorCount = Diagnostics.ErrorCount;
	Result.WarningCount = Diagnostics.WarningCount;
	Result.NoteCount = Diagnostics.NoteCount;
	Result.CompileSeconds = Diagnostics.CompileSeconds;
	for (const FN2CCompilerMessage& Message : Diagnostics.Messages)
	{
		FN2CCompileMessageInfo& Info = Result.Messages.AddDefaulted_GetRef();
		Info.Severity = Message.Severity;
		Info.Message = Message.Message;
		Info.NodeGuid = Message.NodeGuid;
		Info.NodeTitle = Message.NodeTitle;
		Info.GraphName = Message.GraphName;
		Info.PinName = Message.PinName;
	}
	return Result;
}

bool UN2CEditorSubsystem::SaveBlueprintCore(const FString& BlueprintPath, bool bOnlyIfDirty, FN2CSaveResult& OutResult, FString& OutError)
{
	OutResult = FN2CSaveResult();
	UBlueprint* Blueprint = FN2CGraphTarget::LoadBlueprint(BlueprintPath.TrimStartAndEnd(), OutError);
	if (!Blueprint)
	{
		return false;
	}
	UPackage* Package = Blueprint->GetOutermost();
	OutResult.BlueprintPath = Blueprint->GetPathName();
	OutResult.bWasDirty = Package && Package->IsDirty();
	OutResult.FileName = Package ? Package->GetLoadedPath().GetLocalFullPath() : FString();
	if (bOnlyIfDirty && !OutResult.bWasDirty)
	{
		return true;
	}
	UEditorAssetSubsystem* AssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	OutResult.bWasSaved = AssetSubsystem && AssetSubsystem->SaveLoadedAsset(Blueprint, bOnlyIfDirty);
	if (!OutResult.bWasSaved)
	{
		OutError = TEXT("SAVE_FAILED: the package could not be written (a read-only or checked-in file, or the save was refused)");
		return false;
	}
	if (Package)
	{
		OutResult.FileName = Package->GetLoadedPath().GetLocalFullPath();
	}
	return true;
}

FN2CSaveResult UN2CEditorSubsystem::SaveBlueprint(const FString& BlueprintPath, bool bOnlyIfDirty)
{
	using namespace N2CEditorSubsystemPrivate;
	FN2CSaveResult Result;
	FString Error;
	if (RequirePath(TEXT("SaveBlueprint"), BlueprintPath) && !SaveBlueprintCore(BlueprintPath, bOnlyIfDirty, Result, Error))
	{
		RaiseError(TEXT("SaveBlueprint"), BlueprintPath, Error);
	}
	return Result;
}

FN2CBlueprintGraphList UN2CEditorSubsystem::ListBlueprintGraphs(const FString& BlueprintPath)
{
	using namespace N2CEditorSubsystemPrivate;
	FN2CBlueprintGraphList Result;
	UBlueprint* Blueprint = LoadOrRaise(TEXT("ListBlueprintGraphs"), BlueprintPath);
	if (!Blueprint)
	{
		return Result;
	}
	// The list list-blueprint-graphs and the bridge return
	const TSharedRef<FJsonObject> Json = N2CMcpBlueprintExport::ListGraphs(*Blueprint);
	Result.BlueprintPath = Blueprint->GetPathName();
	Result.BlueprintName = Blueprint->GetName();
	const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
	if (Json->TryGetArrayField(TEXT("graphs"), Graphs))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Graphs)
		{
			const TSharedPtr<FJsonObject> Graph = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Graph.IsValid())
			{
				continue;
			}
			FN2CBlueprintGraphInfo& Info = Result.Graphs.AddDefaulted_GetRef();
			Info.Name = StringField(Graph, TEXT("name"));
			Info.GraphGuid = StringField(Graph, TEXT("graphGuid"));
			Info.Kind = StringField(Graph, TEXT("kind"));
			Graph->TryGetNumberField(TEXT("nodeCount"), Info.NodeCount);
			Graph->TryGetBoolField(TEXT("readOnly"), Info.bReadOnly);
			Graph->TryGetBoolField(TEXT("translatable"), Info.bTranslatable);
			Info.ParentGraph = StringField(Graph, TEXT("parentGraph"));
		}
	}
	return Result;
}

FN2CBlueprintParentInfo UN2CEditorSubsystem::GetBlueprintParentClass(const FString& BlueprintPath)
{
	using namespace N2CEditorSubsystemPrivate;
	FN2CBlueprintParentInfo Result;
	UBlueprint* Blueprint = LoadOrRaise(TEXT("GetBlueprintParentClass"), BlueprintPath);
	if (!Blueprint)
	{
		return Result;
	}
	Result.BlueprintPath = Blueprint->GetPathName();
	Result.ParentClass = Blueprint->ParentClass;
	if (!Blueprint->ParentClass)
	{
		RaiseError(TEXT("GetBlueprintParentClass"), BlueprintPath, TEXT("PARENT_CLASS_MISSING: the Blueprint's parent class is not loaded (deleted or renamed)"));
		return Result;
	}
	Result.ParentClassPath = Blueprint->ParentClass->GetPathName();
	if (const UClass* Native = FBlueprintEditorUtils::FindFirstNativeClass(Blueprint->ParentClass.Get()))
	{
		Result.NativeParentClassPath = Native->GetPathName();
	}
	return Result;
}

FN2CBlueprintN2CJson UN2CEditorSubsystem::GetBlueprintN2CJson(const FString& BlueprintPath, const FString& Graph, int32 Depth,
	bool bToFile, bool bCompact)
{
	using namespace N2CEditorSubsystemPrivate;
	FN2CBlueprintN2CJson Result;
	if (!RequirePath(TEXT("GetBlueprintN2CJson"), BlueprintPath))
	{
		return Result;
	}
	// The read get-blueprint-n2cjson and the bridge run; a script has no output limit, so nothing spills to a file
	const FN2CGraphTargetArgs Target = FN2CGraphTargetArgs::FromPathAndGraph(BlueprintPath, Graph);
	FN2CBlueprintReadRequest Request;
	Request.BlueprintPath = Target.BlueprintPath;
	Request.GraphName = Target.GraphName;
	Request.GraphGuid = Target.GraphGuid;
	Request.Depth = Depth < 0 ? INDEX_NONE : Depth;
	Request.bToFile = bToFile;
	Request.bCompact = bCompact;
	Request.bSpillLargeToFile = false;
	const FN2CBlueprintReadResult Read = N2CMcpBlueprintExport::Read(Request);
	if (!Read.IsValid())
	{
		RaiseError(TEXT("GetBlueprintN2CJson"), BlueprintPath, Read.Error);
		return Result;
	}
	Result.UntrustedContent = FN2CMcpUntrustedContent::MarkerText();
	Result.BlueprintPath = Read.Blueprint->GetPathName();
	Result.BlueprintName = Read.Blueprint->GetName();
	Result.GraphCount = Read.GraphCount;
	Result.NodeCount = Read.NodeCount;
	Result.EstimatedTokens = Read.EstimatedTokens;
	if (Read.WroteFile())
	{
		Result.FilePath = Read.FilePath;
		Result.FileBytes = Read.FileBytes;
	}
	else
	{
		Result.N2CJson = Read.Json;
	}
	if (Read.Target.Graph)
	{
		const TSharedRef<FJsonObject> TargetJson = Read.Target.ToJson();
		Result.GraphName = StringField(TargetJson, TEXT("graphName"));
		Result.GraphGuid = StringField(TargetJson, TEXT("graphGuid"));
		Result.GraphKind = StringField(TargetJson, TEXT("graphKind"));
		Result.Warning = StringField(TargetJson, TEXT("warning"));
		Result.ResolvedBy = StringField(TargetJson, TEXT("resolvedBy"));
		Result.DeprecatedId = StringField(TargetJson, TEXT("deprecatedId"));
	}
	return Result;
}

bool UN2CEditorSubsystem::TagGraphCore(const FString& BlueprintPath, const FString& Graph, const FString& Tag, const FString& Category,
	const FString& Description, FN2CTagInfo& OutTag, FString& OutError)
{
	const FString TagName = Tag.TrimStartAndEnd();
	if (TagName.IsEmpty())
	{
		OutError = TEXT("Tag name cannot be empty");
		return false;
	}
	// Tagging edits no graph, so a read-only graph can be tagged
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(FN2CGraphTargetArgs::FromPathAndGraph(BlueprintPath, Graph), /*bForWrite*/ false);
	if (!Target.IsValid() || !Target.Blueprint || !Target.Graph)
	{
		OutError = Target.Error.IsEmpty() ? FString(TEXT("GRAPH_NOT_FOUND: no graph to tag")) : Target.Error;
		return false;
	}
	const FN2CTaggedBlueprintGraph TaggedGraph(TagName, Category.IsEmpty() ? TEXT("Default") : Category, Description,
		Target.Graph->GraphGuid, Target.Graph->GetName(), FSoftObjectPath(Target.Blueprint));
	if (!UN2CGraphStateManager::Get().AddTag(TaggedGraph))
	{
		OutError = TEXT("Failed to add tag - tag may already exist on this graph");
		return false;
	}
	UN2CGraphStateManager::Get().SaveState();
	FN2CLogger::Get().Log(FString::Printf(TEXT("Tagged graph %s with '%s'"), *Target.Graph->GetName(), *TagName), EN2CLogSeverity::Info);
	OutTag = FN2CTagInfo::FromTaggedGraph(TaggedGraph);
	return true;
}

FN2CTagInfo UN2CEditorSubsystem::TagGraph(const FString& BlueprintPath, const FString& Graph, const FString& Tag,
	const FString& Category, const FString& Description)
{
	using namespace N2CEditorSubsystemPrivate;
	FN2CTagInfo Result;
	FString Error;
	if (RequirePath(TEXT("TagGraph"), BlueprintPath) && !TagGraphCore(BlueprintPath, Graph, Tag, Category, Description, Result, Error))
	{
		RaiseError(TEXT("TagGraph"), BlueprintPath, Error);
	}
	return Result;
}

TArray<FN2CTagInfo> UN2CEditorSubsystem::ListTagsCore(const FString& Category, const FString& Tag, const FString& BlueprintPath)
{
	const UN2CGraphStateManager& Store = UN2CGraphStateManager::Get();
	TArray<FN2CTaggedBlueprintGraph> Tags;
	if (!Tag.IsEmpty())
	{
		Tags = Store.FindTaggedGraphs(Tag, Category);
	}
	else if (!Category.IsEmpty())
	{
		Tags = Store.FindTaggedGraphsInCategory(Category);
	}
	else
	{
		Tags = Store.GetAllTags();
	}
	// Only one Blueprint's graphs, when asked (card 0498): copies share graph GUIDs
	if (!BlueprintPath.TrimStartAndEnd().IsEmpty())
	{
		const FSoftObjectPath Wanted = FN2CGraphKey::NormalizeBlueprintPath(BlueprintPath);
		Tags.RemoveAll([&Wanted](const FN2CTaggedBlueprintGraph& TagInfo)
		{
			return FN2CGraphKey::NormalizeBlueprintPath(TagInfo.OwningBlueprint) != Wanted;
		});
	}
	TArray<FN2CTagInfo> Result;
	Result.Reserve(Tags.Num());
	for (const FN2CTaggedBlueprintGraph& TagInfo : Tags)
	{
		Result.Add(FN2CTagInfo::FromTaggedGraph(TagInfo));
	}
	return Result;
}

TArray<FN2CTagInfo> UN2CEditorSubsystem::ListTags(const FString& Category, const FString& Tag, const FString& BlueprintPath)
{
	return ListTagsCore(Category, Tag, BlueprintPath);
}

bool UN2CEditorSubsystem::RemoveTagCore(const FString& BlueprintPath, const FGuid& GraphGuid, const FString& Tag, FN2CTagRemoval& OutResult,
	FString& OutError)
{
	OutResult = FN2CTagRemoval();
	const FString TagName = Tag.TrimStartAndEnd();
	if (!GraphGuid.IsValid() || TagName.IsEmpty())
	{
		OutError = TEXT("GraphGuid and Tag cannot be empty");
		return false;
	}
	// The graph is {Blueprint, GraphGuid} (card 0192): a duplicated Blueprint shares its graph GUIDs with the original,
	// so a GUID alone names a graph only while one Blueprint has it (card 0498)
	const FN2CGraphKey Key = BlueprintPath.TrimStartAndEnd().IsEmpty()
		? FN2CGraphKey(GraphGuid)
		: FN2CGraphKey(FN2CGraphKey::NormalizeBlueprintPath(BlueprintPath), GraphGuid);
	UN2CGraphStateManager& Store = UN2CGraphStateManager::Get();
	FN2CTaggedBlueprintGraph Removed;
	const int32 RemovedCount = Store.RemoveTagByName(Key, TagName, Removed);
	if (RemovedCount == 0)
	{
		if (!Key.HasBlueprint() && Store.FindKeysForGuid(GraphGuid).Num() > 1)
		{
			OutError = FString::Printf(TEXT("Graph %s is in more than one Blueprint (a duplicated Blueprint keeps its graph GUIDs). Pass blueprint_path (from list_tags) to say which."),
				*GraphGuid.ToString());
		}
		else
		{
			OutError = FString::Printf(TEXT("Tag '%s' not found on graph %s"), *TagName, *Key.ToString());
		}
		return false;
	}
	Store.SaveState();
	OutResult.RemovedCount = RemovedCount;
	OutResult.BlueprintPath = Removed.OwningBlueprint.ToString();
	OutResult.GraphGuid = GraphGuid.ToString();
	OutResult.RemainingCount = Store.GetTaggedGraphs(Key).Num();
	FN2CLogger::Get().Log(FString::Printf(TEXT("Removed %d tag(s) '%s' from graph %s"), RemovedCount, *TagName, *GraphGuid.ToString()), EN2CLogSeverity::Info);
	return true;
}

FN2CTagRemoval UN2CEditorSubsystem::RemoveTag(const FString& BlueprintPath, const FString& Graph, const FString& Tag)
{
	using namespace N2CEditorSubsystemPrivate;
	FN2CTagRemoval Result;
	if (!RequirePath(TEXT("RemoveTag"), BlueprintPath))
	{
		return Result;
	}
	// A graph name is resolved in the Blueprint; a GraphGuid is used as it is, so a tag on a deleted graph can go too
	FGuid GraphGuid;
	if (!FGuid::Parse(Graph.TrimStartAndEnd(), GraphGuid))
	{
		const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(FN2CGraphTargetArgs::FromPathAndGraph(BlueprintPath, Graph), /*bForWrite*/ false);
		if (!Target.IsValid() || !Target.Graph)
		{
			RaiseError(TEXT("RemoveTag"), BlueprintPath, Target.Error.IsEmpty() ? FString(TEXT("GRAPH_NOT_FOUND: no such graph")) : Target.Error);
			return Result;
		}
		GraphGuid = Target.Graph->GraphGuid;
	}
	FString Error;
	if (!RemoveTagCore(BlueprintPath, GraphGuid, Tag, Result, Error))
	{
		RaiseError(TEXT("RemoveTag"), BlueprintPath, Error);
	}
	return Result;
}
