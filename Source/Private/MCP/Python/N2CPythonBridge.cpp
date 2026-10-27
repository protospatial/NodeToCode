// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CPythonBridge.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CSettings.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpNodeDeletion.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpGetAvailableTranslationTargetsTool.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "MCP/Utils/N2CPinConnectPolicy.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpNodeActions.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Utils/N2CMcpPinDefaultUtils.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CJsonContract.h"
#include "Utils/N2CStableNames.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintEditor.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/Optional.h"

#define LOCTEXT_NAMESPACE "NodeToCode"

namespace N2CPythonBridgePrivate
{
	/** Adds a call's compile diagnostics to a bridge result as "compile" (nothing when the call compiled nothing) */
	void AddCompileResult(const TSharedPtr<FJsonObject>& Data, const TArray<FN2CCompileDiagnostics>& Compiles)
	{
		if (const TSharedPtr<FJsonObject> CompileJson = FN2CMcpBlueprintUtils::CompileDiagnosticsToJson(Compiles))
		{
			if (Data.IsValid())
			{
				Data->SetObjectField(TEXT("compile"), CompileJson);
			}
		}
	}

	/**
	 * The target arguments of a bridge call (card 0274): BlueprintPath, and Graph as a graph GUID (a function's
	 * functionGuid) or else a graph name. Both empty: the focused graph, as before.
	 */
	FN2CGraphTargetArgs BridgeTargetArgs(const FString& BlueprintPath, const FString& Graph)
	{
		FN2CGraphTargetArgs Args;
		Args.BlueprintPath = BlueprintPath.TrimStartAndEnd();
		const FString GraphText = Graph.TrimStartAndEnd();
		FGuid GraphGuid;
		if (FGuid::Parse(GraphText, GraphGuid))
		{
			Args.GraphGuid = GraphText;
		}
		else
		{
			Args.GraphName = GraphText;
		}
		return Args;
	}
}
using N2CPythonBridgePrivate::AddCompileResult;
using N2CPythonBridgePrivate::BridgeTargetArgs;

FString UN2CPythonBridge::GetFocusedBlueprintJson()
{
	// Get the focused graph
	UBlueprint* OwningBlueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;

	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("GetFocusedBlueprintJson failed: %s"), *GraphError));
		return MakeErrorJson(GraphError);
	}
	return GraphToJson(OwningBlueprint, FocusedGraph);
}

FString UN2CPythonBridge::GraphToJson(UBlueprint* OwningBlueprint, UEdGraph* FocusedGraph)
{
	// Collect nodes
	TArray<UK2Node*> CollectedNodes;
	if (!FN2CEditorIntegration::Get().CollectNodesFromGraph(FocusedGraph, CollectedNodes) || CollectedNodes.IsEmpty())
	{
		return MakeErrorJson(TEXT("Failed to collect nodes or no nodes found in the focused graph."));
	}

	// Capture the nodes. The default events the editor placed stay visible (marked disabled), as in the MCP
	// get-focused-blueprint tool, so a script finds and wires a new Blueprint's BeginPlay instead of adding a second
	// one; a graph of only those events is three disabled nodes, not an error (cards 0137, 0433)
	FN2CCapture Capture;
	FN2CCaptureOptions Options;
	Options.bIncludeGhostNodes = true;
	FString CaptureError;
	if (!FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprintWithMaps(CollectedNodes, Capture, Options, &CaptureError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Could not capture the focused graph: %s"), *CaptureError));
	}

	// Serialize to JSON
	FString JsonOutput = FN2CEditorIntegration::Get().SerializeN2CBlueprintToJson(Capture.Blueprint, false);
	if (JsonOutput.IsEmpty())
	{
		return MakeErrorJson(TEXT("Failed to serialize N2CBlueprint to JSON."));
	}

	// Build metadata response
	FString BlueprintName = OwningBlueprint ? OwningBlueprint->GetName() : TEXT("Unknown");
	FString BlueprintPath = OwningBlueprint ? OwningBlueprint->GetPathName() : TEXT("");
	FString GraphName = FocusedGraph ? FocusedGraph->GetName() : TEXT("Unknown");

	// The N2CJSON goes in as an object, so the response is one document the writer escapes throughout
	TSharedPtr<FJsonObject> N2CJsonObject;
	const TSharedRef<TJsonReader<>> N2CJsonReader = TJsonReaderFactory<>::Create(JsonOutput);
	if (!FJsonSerializer::Deserialize(N2CJsonReader, N2CJsonObject) || !N2CJsonObject.IsValid())
	{
		return MakeErrorJson(TEXT("The serialized N2CBlueprint is not a JSON object."));
	}

	// Create a simplified metadata response along with the full N2CJSON
	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), BlueprintName);
	Data->SetStringField(TEXT("path"), BlueprintPath);
	Data->SetStringField(TEXT("graph_name"), GraphName);
	Data->SetNumberField(TEXT("node_count"), CollectedNodes.Num());
	Data->SetObjectField(TEXT("n2c_json"), N2CJsonObject);

	FN2CLogger::Get().Log(FString::Printf(TEXT("GetFocusedBlueprintJson: Retrieved %s with %d nodes"), *BlueprintName, CollectedNodes.Num()), EN2CLogSeverity::Info);
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::GetBlueprintN2CJson(const FString& BlueprintPath, const FString& GraphName,
	const FString& GraphGuid, int32 Depth, bool bToFile, bool bCompact)
{
	FN2CBlueprintReadRequest Request;
	Request.BlueprintPath = BlueprintPath;
	Request.GraphName = GraphName;
	Request.GraphGuid = GraphGuid;
	Request.Depth = Depth < 0 ? INDEX_NONE : Depth;
	Request.bToFile = bToFile;
	Request.bCompact = bCompact;
	// A script has no tool-output limit: the document is returned whole unless it asks for a file
	Request.bSpillLargeToFile = false;

	const FN2CBlueprintReadResult Result = N2CMcpBlueprintExport::Read(Request);
	if (!Result.IsValid())
	{
		return MakeErrorJson(Result.Error);
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blueprintPath"), Result.Blueprint->GetPathName());
	Data->SetStringField(TEXT("blueprintName"), Result.Blueprint->GetName());
	Data->SetNumberField(TEXT("graphCount"), Result.GraphCount);
	Data->SetNumberField(TEXT("nodeCount"), Result.NodeCount);
	Data->SetNumberField(TEXT("estimatedTokens"), Result.EstimatedTokens);
	if (Result.Target.Graph)
	{
		Data->SetObjectField(TEXT("target"), Result.Target.ToJson());
	}
	if (Result.WroteFile())
	{
		Data->SetStringField(TEXT("path"), Result.FilePath);
		Data->SetNumberField(TEXT("bytes"), static_cast<double>(Result.FileBytes));
		return MakeSuccessJson(Data);
	}

	TSharedPtr<FJsonObject> Document;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.Json);
	if (!FJsonSerializer::Deserialize(Reader, Document) || !Document.IsValid())
	{
		return MakeErrorJson(TEXT("CAPTURE_FAILED: the document is not a JSON object"));
	}
	Data->SetObjectField(TEXT("n2c_json"), Document);
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::ListBlueprintGraphs(const FString& BlueprintPath)
{
	if (BlueprintPath.TrimStartAndEnd().IsEmpty())
	{
		return MakeErrorJson(TEXT("INVALID_ARGUMENT: blueprint_path is required, the asset path of a Blueprint such as /Game/Blueprints/BP_Door"));
	}
	FString LoadError;
	UBlueprint* Blueprint = FN2CGraphTarget::LoadBlueprint(BlueprintPath, LoadError);
	if (!Blueprint)
	{
		return MakeErrorJson(LoadError);
	}
	return MakeSuccessJson(N2CMcpBlueprintExport::ListGraphs(*Blueprint));
}

FString UN2CPythonBridge::ReadN2CJson(const FString& N2CJson)
{
	TSharedPtr<FJsonObject> Source;
	const TSharedRef<TJsonReader<>> SourceReader = TJsonReaderFactory<>::Create(N2CJson);
	if (!FJsonSerializer::Deserialize(SourceReader, Source) || !Source.IsValid())
	{
		return MakeErrorJson(TEXT("The text is not a JSON object."));
	}
	FString SourceVersion;
	Source->TryGetStringField(N2CJsonContract::VersionField, SourceVersion);

	FN2CBlueprint Blueprint;
	TArray<FN2CJsonReadWarning> Warnings;
	if (!FN2CSerializer::FromJson(N2CJson, Blueprint, Warnings))
	{
		return MakeErrorJson(FString::Printf(TEXT("The text is not N2C JSON that this version reads (version '%s'; NodeToCode reads %d.x to %d.x)."),
			*SourceVersion, N2CJsonContract::OldestReadableMajor, N2CJsonContract::Major));
	}

	int32 NodeCount = 0;
	int32 DataEdgeCount = 0;
	int32 ExecEdgeCount = 0;
	for (const FN2CGraph& Graph : Blueprint.Graphs)
	{
		NodeCount += Graph.Nodes.Num();
		for (const FString& Flow : Graph.Flows.Execution)
		{
			// One link per entry ("N1.P2->N3.P4"); a 1.x node chain ("N1->N2->N3") has one per arrow
			TArray<N2CJsonContract::FExecEndpoint> Endpoints;
			ExecEdgeCount += N2CJsonContract::ParseExecFlow(Flow, Endpoints) ? Endpoints.Num() - 1 : 0;
		}
		for (const TPair<FString, FN2CDataFlowTargets>& Flow : Graph.Flows.Data)
		{
			DataEdgeCount += Flow.Value.Targets.Num();
		}
	}

	TSharedPtr<FJsonObject> Rewritten;
	const TSharedRef<TJsonReader<>> RewrittenReader = TJsonReaderFactory<>::Create(FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed));
	if (!FJsonSerializer::Deserialize(RewrittenReader, Rewritten) || !Rewritten.IsValid())
	{
		return MakeErrorJson(TEXT("The N2C JSON was read but could not be written again."));
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("source_version"), SourceVersion);
	Data->SetStringField(TEXT("version"), Blueprint.Version.Value);
	Data->SetBoolField(TEXT("valid"), Blueprint.IsValid());
	Data->SetNumberField(TEXT("graphs"), Blueprint.Graphs.Num());
	Data->SetNumberField(TEXT("nodes"), NodeCount);
	Data->SetNumberField(TEXT("data_edges"), DataEdgeCount);
	Data->SetNumberField(TEXT("exec_edges"), ExecEdgeCount);
	Data->SetObjectField(TEXT("n2c_json"), Rewritten);

	// What the reader kept without knowing it, or skipped (card 0445): an unknown kind or field of a later 2.x document
	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const FN2CJsonReadWarning& Warning : Warnings)
	{
		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("code"), Warning.Code);
		Entry->SetStringField(TEXT("path"), Warning.Path);
		Entry->SetStringField(TEXT("value"), Warning.Value);
		Entry->SetStringField(TEXT("message"), Warning.Message);
		WarningValues.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Data->SetArrayField(TEXT("warnings"), WarningValues);

	// A read that lost part of the graph is not a success (card 0505): the result names what was skipped and still
	// carries the rest, warnings included, as data
	TArray<FString> Skipped;
	for (const FN2CJsonReadWarning& Warning : Warnings)
	{
		if (Warning.Code == N2CJsonContract::ReadWarning::SkippedGraph || Warning.Code == N2CJsonContract::ReadWarning::SkippedNode
			|| Warning.Code == N2CJsonContract::ReadWarning::SkippedPin)
		{
			Skipped.Add(FString::Printf(TEXT("%s %s"), *Warning.Code, *Warning.Path));
		}
	}
	if (!Skipped.IsEmpty())
	{
		constexpr int32 MaxNamed = 5;
		const int32 Named = FMath::Min(Skipped.Num(), MaxNamed);
		FString List = FString::Join(TArrayView<const FString>(Skipped.GetData(), Named), TEXT(", "));
		if (Skipped.Num() > Named)
		{
			List += FString::Printf(TEXT(" and %d more"), Skipped.Num() - Named);
		}
		return MakeErrorJson(FString::Printf(TEXT("The N2C JSON was read with %d part(s) skipped as malformed (a missing required ")
			TEXT("field such as a node's or pin's \"id\"): %s. See data.warnings."), Skipped.Num(), *List), Data);
	}
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::CompileFocusedBlueprint(const FString& BlueprintPath)
{
	// The Blueprint BlueprintPath names, else the focused one (card 0274)
	FString GraphError;
	UBlueprint* OwningBlueprint = FN2CGraphTarget::LoadBlueprint(BlueprintPath, GraphError);
	if (!OwningBlueprint)
	{
		return MakeErrorJson(GraphError);
	}

	if (!OwningBlueprint)
	{
		return MakeErrorJson(TEXT("No Blueprint found to compile."));
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("CompileFocusedBlueprint: Compiling %s"), *OwningBlueprint->GetName()), EN2CLogSeverity::Info);

	// Compile the Blueprint, with the compiler's messages (card 0151)
	FN2CCompileDiagnostics Diagnostics;
	FN2CMcpBlueprintUtils::Compile(OwningBlueprint, Diagnostics, /*bSkipGarbageCollection*/ false, /*bReportInToolResult*/ false);
	const bool bHadErrors = !Diagnostics.bSuccess;
	const bool bHadWarnings = Diagnostics.WarningCount > 0 || OwningBlueprint->Status == BS_UpToDateWithWarnings;

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blueprint_name"), OwningBlueprint->GetName());
	Data->SetStringField(TEXT("status"), Diagnostics.Status);
	Data->SetBoolField(TEXT("had_errors"), bHadErrors);
	Data->SetBoolField(TEXT("had_warnings"), bHadWarnings);
	Data->SetNumberField(TEXT("error_count"), Diagnostics.ErrorCount);
	Data->SetNumberField(TEXT("warning_count"), Diagnostics.WarningCount);
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const FN2CCompilerMessage& Message : Diagnostics.Messages)
	{
		Messages.Add(MakeShared<FJsonValueObject>(FN2CCompileDiagnostics::MessageToJson(Message)));
	}
	Data->SetArrayField(TEXT("messages"), Messages);

	if (bHadErrors)
	{
		return MakeErrorJson(TEXT("Compilation failed with errors"), Data);
	}

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::SaveFocusedBlueprint(bool bOnlyIfDirty, const FString& BlueprintPath)
{
	// The Blueprint BlueprintPath names, else the focused one (card 0274)
	FString GraphError;
	UBlueprint* OwningBlueprint = FN2CGraphTarget::LoadBlueprint(BlueprintPath, GraphError);
	if (!OwningBlueprint)
	{
		return MakeErrorJson(GraphError);
	}

	if (!OwningBlueprint)
	{
		return MakeErrorJson(TEXT("No Blueprint found to save."));
	}

	// Check if dirty
	UPackage* Package = OwningBlueprint->GetOutermost();
	bool bWasDirty = Package ? Package->IsDirty() : false;

	// Skip if not dirty and only_if_dirty is true
	if (bOnlyIfDirty && !bWasDirty)
	{
		const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetStringField(TEXT("blueprint_name"), OwningBlueprint->GetName());
		Data->SetBoolField(TEXT("was_dirty"), false);
		Data->SetBoolField(TEXT("was_saved"), false);
		return MakeSuccessJson(Data);
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("SaveFocusedBlueprint: Saving %s"), *OwningBlueprint->GetName()), EN2CLogSeverity::Info);

	// Save the asset
	FString AssetPath = OwningBlueprint->GetPathName();
	bool bSaved = UEditorAssetLibrary::SaveAsset(AssetPath, bOnlyIfDirty);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blueprint_name"), OwningBlueprint->GetName());
	Data->SetBoolField(TEXT("was_dirty"), bWasDirty);
	Data->SetBoolField(TEXT("was_saved"), bSaved);

	if (!bSaved && bWasDirty)
	{
		return MakeErrorJson(TEXT("Failed to save Blueprint"), Data);
	}

	return MakeSuccessJson(Data);
}

// ========== Tagging System ==========

FString UN2CPythonBridge::TagFocusedGraph(const FString& Tag, const FString& Category, const FString& Description,
	const FString& BlueprintPath, const FString& Graph)
{
	if (Tag.IsEmpty())
	{
		return MakeErrorJson(TEXT("Tag name cannot be empty"));
	}

	// The graph BlueprintPath / Graph name, else the focused one (card 0274); tagging edits no graph
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ false);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	UBlueprint* OwningBlueprint = Target.Blueprint;
	UEdGraph* FocusedGraph = Target.Graph;

	if (!FocusedGraph || !OwningBlueprint)
	{
		return MakeErrorJson(TEXT("No focused graph or Blueprint found"));
	}

	// Create the tag info
	FN2CTaggedBlueprintGraph TaggedGraph(
		Tag,
		Category.IsEmpty() ? TEXT("Default") : Category,
		Description,
		FocusedGraph->GraphGuid,
		FocusedGraph->GetName(),
		FSoftObjectPath(OwningBlueprint)
	);

	// Add the tag
	if (!UN2CGraphStateManager::Get().AddTag(TaggedGraph))
	{
		return MakeErrorJson(TEXT("Failed to add tag - tag may already exist on this graph"));
	}

	// Save tags to persist
	UN2CGraphStateManager::Get().SaveState();

	FN2CLogger::Get().Log(FString::Printf(TEXT("TagFocusedGraph: Added tag '%s' to %s"), *Tag, *FocusedGraph->GetName()), EN2CLogSeverity::Info);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("tag"), Tag);
	Data->SetStringField(TEXT("category"), TaggedGraph.Category);
	Data->SetStringField(TEXT("description"), Description);
	Data->SetStringField(TEXT("graph_guid"), FocusedGraph->GraphGuid.ToString());
	Data->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	Data->SetStringField(TEXT("blueprint_name"), OwningBlueprint->GetName());
	// With graph_guid, what RemoveTag takes to name this graph even when a copy of the Blueprint shares its GUID
	Data->SetStringField(TEXT("blueprint_path"), TaggedGraph.OwningBlueprint.ToString());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::ListTags(const FString& Category, const FString& Tag, const FString& BlueprintPath)
{
	TArray<FN2CTaggedBlueprintGraph> Tags;

	// Get filtered or all tags
	if (!Category.IsEmpty() && !Tag.IsEmpty())
	{
		// Filter by both
		Tags = UN2CGraphStateManager::Get().FindTaggedGraphs(Tag, Category);
	}
	else if (!Category.IsEmpty())
	{
		// Filter by category only
		Tags = UN2CGraphStateManager::Get().FindTaggedGraphsInCategory(Category);
	}
	else if (!Tag.IsEmpty())
	{
		// Filter by tag only (all categories)
		Tags = UN2CGraphStateManager::Get().FindTaggedGraphs(Tag, TEXT(""));
	}
	else
	{
		// Get all tags
		Tags = UN2CGraphStateManager::Get().GetAllTags();
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

	// Build JSON array
	TArray<TSharedPtr<FJsonValue>> TagsArray;
	for (const FN2CTaggedBlueprintGraph& TagInfo : Tags)
	{
		const TSharedPtr<FJsonObject> TagObject = MakeShared<FJsonObject>();
		TagObject->SetStringField(TEXT("tag"), TagInfo.Tag);
		TagObject->SetStringField(TEXT("category"), TagInfo.Category);
		TagObject->SetStringField(TEXT("description"), TagInfo.Description);
		TagObject->SetStringField(TEXT("graph_guid"), TagInfo.GraphGuid.ToString());
		TagObject->SetStringField(TEXT("graph_name"), TagInfo.GraphName);
		TagObject->SetStringField(TEXT("blueprint_path"), TagInfo.OwningBlueprint.ToString());
		TagObject->SetStringField(TEXT("timestamp"), TagInfo.Timestamp.ToString());
		TagsArray.Add(MakeShared<FJsonValueObject>(TagObject));
	}

	// Get summary info
	TArray<FString> AllCategories = UN2CGraphStateManager::Get().GetAllCategories();
	TArray<FString> AllTagNames = UN2CGraphStateManager::Get().GetAllTagNames();

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tags"), TagsArray);
	Data->SetNumberField(TEXT("count"), Tags.Num());
	Data->SetNumberField(TEXT("total_categories"), AllCategories.Num());
	Data->SetNumberField(TEXT("total_unique_tags"), AllTagNames.Num());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::RemoveTag(const FString& GraphGuid, const FString& Tag, const FString& BlueprintPath)
{
	if (GraphGuid.IsEmpty() || Tag.IsEmpty())
	{
		return MakeErrorJson(TEXT("GraphGuid and Tag cannot be empty"));
	}

	// Parse GUID
	FGuid ParsedGuid;
	if (!FGuid::Parse(GraphGuid, ParsedGuid))
	{
		return MakeErrorJson(FString::Printf(TEXT("Invalid GUID format: %s"), *GraphGuid));
	}

	// The graph is {Blueprint, GraphGuid} (card 0192): a duplicated Blueprint shares its graph GUIDs with the original,
	// so a GUID alone names a graph only while one Blueprint has it (card 0498)
	const FN2CGraphKey Key = BlueprintPath.TrimStartAndEnd().IsEmpty()
		? FN2CGraphKey(ParsedGuid)
		: FN2CGraphKey(FN2CGraphKey::NormalizeBlueprintPath(BlueprintPath), ParsedGuid);

	// Try to remove the tag
	FN2CTaggedBlueprintGraph RemovedTag;
	int32 RemovedCount = UN2CGraphStateManager::Get().RemoveTagByName(Key, Tag, RemovedTag);

	if (RemovedCount == 0)
	{
		if (!Key.HasBlueprint() && UN2CGraphStateManager::Get().FindKeysForGuid(ParsedGuid).Num() > 1)
		{
			return MakeErrorJson(FString::Printf(TEXT("Graph %s is in more than one Blueprint (a duplicated Blueprint keeps its graph GUIDs). Pass blueprint_path (from list_tags) to say which."),
				*GraphGuid));
		}
		return MakeErrorJson(FString::Printf(TEXT("Tag '%s' not found on graph %s"), *Tag, *Key.ToString()));
	}

	// Save tags to persist
	UN2CGraphStateManager::Get().SaveState();

	// Get remaining tags for this graph
	TArray<FN2CTaggedBlueprintGraph> RemainingTags = UN2CGraphStateManager::Get().GetTaggedGraphs(Key);

	FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveTag: Removed %d tag(s) '%s' from graph %s"), RemovedCount, *Tag, *GraphGuid), EN2CLogSeverity::Info);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("removed"), true);
	Data->SetNumberField(TEXT("removed_count"), RemovedCount);
	Data->SetStringField(TEXT("tag"), Tag);
	Data->SetStringField(TEXT("graph_guid"), GraphGuid);
	Data->SetStringField(TEXT("blueprint_path"), RemovedTag.OwningBlueprint.ToString());
	Data->SetNumberField(TEXT("remaining_tags"), RemainingTags.Num());

	return MakeSuccessJson(Data);
}

// ========== LLM Provider Info ==========

FString UN2CPythonBridge::GetLLMProviders()
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	if (!Settings)
	{
		return MakeErrorJson(TEXT("Failed to get NodeToCode settings"));
	}

	// Build providers array
	TArray<TSharedPtr<FJsonValue>> ProvidersArray;

	// Define provider info (matching EN2CLLMProvider enum)
	struct FProviderInfo
	{
		FString Name;
		FString DisplayName;
		bool bIsLocal;
	};

	TArray<FProviderInfo> Providers = {
		{TEXT("OpenAI"), TEXT("OpenAI"), false},
		{TEXT("Anthropic"), TEXT("Anthropic"), false},
		{TEXT("Gemini"), TEXT("Google Gemini"), false},
		{TEXT("Ollama"), TEXT("Ollama (Local)"), true},
		{TEXT("DeepSeek"), TEXT("DeepSeek"), false},
		{TEXT("LMStudio"), TEXT("LM Studio (Local)"), true}
	};

	EN2CLLMProvider CurrentProvider = Settings->Provider;

	for (int32 i = 0; i < Providers.Num(); ++i)
	{
		const TSharedPtr<FJsonObject> ProviderObject = MakeShared<FJsonObject>();
		ProviderObject->SetStringField(TEXT("name"), Providers[i].Name);
		ProviderObject->SetStringField(TEXT("display_name"), Providers[i].DisplayName);
		ProviderObject->SetBoolField(TEXT("is_local"), Providers[i].bIsLocal);
		ProviderObject->SetBoolField(TEXT("is_current"), i == static_cast<int32>(CurrentProvider));
		ProvidersArray.Add(MakeShared<FJsonValueObject>(ProviderObject));
	}

	// Get current provider name
	FString CurrentProviderName;
	switch (CurrentProvider)
	{
	case EN2CLLMProvider::OpenAI: CurrentProviderName = TEXT("OpenAI"); break;
	case EN2CLLMProvider::Anthropic: CurrentProviderName = TEXT("Anthropic"); break;
	case EN2CLLMProvider::Gemini: CurrentProviderName = TEXT("Gemini"); break;
	case EN2CLLMProvider::Ollama: CurrentProviderName = TEXT("Ollama"); break;
	case EN2CLLMProvider::DeepSeek: CurrentProviderName = TEXT("DeepSeek"); break;
	case EN2CLLMProvider::LMStudio: CurrentProviderName = TEXT("LMStudio"); break;
	default: CurrentProviderName = TEXT("Unknown"); break;
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("providers"), ProvidersArray);
	Data->SetStringField(TEXT("current_provider"), CurrentProviderName);
	Data->SetNumberField(TEXT("provider_count"), Providers.Num());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::GetActiveProvider()
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	if (!Settings)
	{
		return MakeErrorJson(TEXT("Failed to get NodeToCode settings"));
	}

	EN2CLLMProvider CurrentProvider = Settings->Provider;

	FString ProviderName;
	FString DisplayName;
	FString Model;
	FString Endpoint;
	bool bIsLocal = false;

	switch (CurrentProvider)
	{
	case EN2CLLMProvider::OpenAI:
		ProviderName = TEXT("OpenAI");
		DisplayName = TEXT("OpenAI");
		Model = Settings->GetActiveModel();
		Endpoint = TEXT("https://api.openai.com/v1/chat/completions");
		break;

	case EN2CLLMProvider::Anthropic:
		ProviderName = TEXT("Anthropic");
		DisplayName = TEXT("Anthropic");
		Model = Settings->GetActiveModel();
		Endpoint = TEXT("https://api.anthropic.com/v1/messages");
		break;

	case EN2CLLMProvider::Gemini:
		ProviderName = TEXT("Gemini");
		DisplayName = TEXT("Google Gemini");
		Model = Settings->GetActiveModel();
		Endpoint = TEXT("https://generativelanguage.googleapis.com/v1beta/models");
		break;

	case EN2CLLMProvider::Ollama:
		ProviderName = TEXT("Ollama");
		DisplayName = TEXT("Ollama (Local)");
		Model = Settings->OllamaModel;
		Endpoint = Settings->OllamaConfig.OllamaEndpoint.IsEmpty() ? TEXT("http://localhost:11434/api/chat") : Settings->OllamaConfig.OllamaEndpoint;
		bIsLocal = true;
		break;

	case EN2CLLMProvider::DeepSeek:
		ProviderName = TEXT("DeepSeek");
		DisplayName = TEXT("DeepSeek");
		Model = Settings->GetActiveModel();
		Endpoint = TEXT("https://api.deepseek.com/v1/chat/completions");
		break;

	case EN2CLLMProvider::LMStudio:
		ProviderName = TEXT("LMStudio");
		DisplayName = TEXT("LM Studio (Local)");
		Model = Settings->LMStudioModel;
		Endpoint = Settings->LMStudioEndpoint.IsEmpty() ? TEXT("http://localhost:1234/v1/chat/completions") : Settings->LMStudioEndpoint;
		bIsLocal = true;
		break;

	default:
		ProviderName = TEXT("Unknown");
		DisplayName = TEXT("Unknown");
		break;
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), ProviderName);
	Data->SetStringField(TEXT("display_name"), DisplayName);
	Data->SetStringField(TEXT("model"), Model);
	Data->SetStringField(TEXT("endpoint"), Endpoint);
	Data->SetBoolField(TEXT("is_local"), bIsLocal);

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::GetTranslationTargets()
{
	// The MCP tool's own result, so the two lists cannot drift apart (it reads only the language enum and settings)
	FN2CMcpGetAvailableTranslationTargetsTool Tool;
	const FMcpToolCallResult Result = Tool.Execute(MakeShared<FJsonObject>());
	FString Text;
	for (const TSharedPtr<FJsonObject>& Item : Result.Content)
	{
		FString ItemText;
		if (Item.IsValid() && Item->TryGetStringField(TEXT("text"), ItemText))
		{
			Text += ItemText;
		}
	}
	if (Result.bIsError)
	{
		return MakeErrorJson(Text.IsEmpty() ? FString(TEXT("Failed to list translation targets")) : Text);
	}

	TSharedPtr<FJsonObject> Data;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Data) || !Data.IsValid())
	{
		return MakeErrorJson(TEXT("Failed to list translation targets: the result is not a JSON object"));
	}
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::MarkUntrustedContent(const FString& Text)
{
	return FN2CMcpUntrustedContent::MarkJson(Text);
}

FString UN2CPythonBridge::GetUntrustedContentNotice()
{
	return FN2CMcpUntrustedContent::MarkerText();
}

// ========== Blueprint Editor Navigation ==========

FString UN2CPythonBridge::OpenBlueprint(const FString& BlueprintPath, const FString& FocusGraph)
{
	if (BlueprintPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("BlueprintPath cannot be empty"));
	}

	// Load the Blueprint asset
	FString ResolveError;
	UBlueprint* Blueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ResolveError);
	if (!Blueprint)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to load Blueprint: %s"), *ResolveError));
	}

	// The graph to focus, found before anything opens: an unknown name fails and lists the graphs, instead of opening
	// the Blueprint with nothing focused so the next add_node_to_graph lands in the graph focused before (card 0286)
	UEdGraph* TargetGraph = nullptr;
	if (!FocusGraph.IsEmpty())
	{
		FString GraphError;
		TargetGraph = FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, FocusGraph, GraphError);
		if (!TargetGraph)
		{
			return MakeErrorJson(GraphError);
		}
	}

	// Open the Blueprint editor
	TSharedPtr<IBlueprintEditor> BlueprintEditor;
	FString OpenError;
	if (!FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint, BlueprintEditor, OpenError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to open Blueprint editor: %s"), *OpenError));
	}

	// The editor must be this Blueprint's before anything is focused in it: an editor of another Blueprint would get
	// this Blueprint's graph pushed into it (card 0150)
	const TSharedPtr<FBlueprintEditor> Editor = FN2CBlueprintEditorTracker::GetEditorFor(Blueprint);
	if (!Editor.IsValid() || Editor != StaticCastSharedPtr<FBlueprintEditor>(BlueprintEditor) || Editor->GetBlueprintObj() != Blueprint)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to open Blueprint editor: the editor found does not edit %s"), *Blueprint->GetPathName()));
	}

	// Focus the graph found above; read back what the editor now shows rather than echo the request
	FString FocusedGraphName;
	if (TargetGraph)
	{
		Editor->JumpToHyperlink(TargetGraph, false);
		if (Editor->GetFocusedGraph() == TargetGraph)
		{
			FocusedGraphName = TargetGraph->GetName();
		}
	}

	// Bring to front
	Editor->FocusWindow();

	FN2CLogger::Get().Log(FString::Printf(TEXT("OpenBlueprint: Opened %s"), *Blueprint->GetName()), EN2CLogSeverity::Info);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	Data->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("focusedGraph"), FocusedGraphName);
	Data->SetNumberField(TEXT("eventGraphCount"), Blueprint->UbergraphPages.Num());
	Data->SetNumberField(TEXT("functionCount"), Blueprint->FunctionGraphs.Num());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::OpenBlueprintFunction(const FString& FunctionName)
{
	if (FunctionName.IsEmpty())
	{
		return MakeErrorJson(TEXT("FunctionName cannot be empty"));
	}

	// Get the focused Blueprint
	UBlueprint* Blueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;

	// The focused graph, through the one graph resolver (card 0152)
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(FN2CGraphTargetArgs(), /*bForWrite*/ false);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	Blueprint = Target.Blueprint;
	FocusedGraph = Target.Graph;

	if (!Blueprint)
	{
		return MakeErrorJson(TEXT("No Blueprint found"));
	}

	// Find the function graph
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		// List available functions for error message
		TArray<FString> AvailableFunctions;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				AvailableFunctions.Add(Graph->GetName());
			}
		}

		FString AvailableStr = AvailableFunctions.Num() > 0 ? FString::Join(AvailableFunctions, TEXT(", ")) : TEXT("none");
		return MakeErrorJson(FString::Printf(TEXT("Function '%s' not found in Blueprint. Available functions: %s"), *FunctionName, *AvailableStr));
	}

	// Open the Blueprint editor and navigate to the function
	TSharedPtr<IBlueprintEditor> BlueprintEditor;
	FString OpenError;
	if (!FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint, BlueprintEditor, OpenError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to open Blueprint editor: %s"), *OpenError));
	}

	// Navigate to the function graph, only in this Blueprint's own editor (card 0150)
	const TSharedPtr<FBlueprintEditor> Editor = FN2CBlueprintEditorTracker::GetEditorFor(Blueprint);
	if (!Editor.IsValid() || Editor != StaticCastSharedPtr<FBlueprintEditor>(BlueprintEditor) || Editor->GetBlueprintObj() != Blueprint)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to open Blueprint editor: the editor found does not edit %s"), *Blueprint->GetPathName()));
	}
	Editor->JumpToHyperlink(FunctionGraph, false);
	Editor->FocusWindow();

	FN2CLogger::Get().Log(FString::Printf(TEXT("OpenBlueprintFunction: Opened %s in %s"), *FunctionName, *Blueprint->GetName()), EN2CLogSeverity::Info);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("functionName"), FunctionName);
	Data->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	Data->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
	Data->SetStringField(TEXT("graphGuid"), FunctionGraph->GraphGuid.ToString());

	return MakeSuccessJson(Data);
}

// ========== Blueprint Graph Editing ==========

FString UN2CPythonBridge::SearchBlueprintNodes(const FString& SearchTerm, bool bContextSensitive, int32 MaxResults,
	const FString& CategoryFilter, bool bExcludeVMFunctions, const FString& BlueprintPath, const FString& Graph)
{
	if (SearchTerm.IsEmpty())
	{
		return MakeErrorJson(TEXT("SearchTerm cannot be empty"));
	}

	MaxResults = FMath::Clamp(MaxResults, 1, 100);

	// The focused graph, through the one graph resolver (card 0152), for bContextSensitive true and false: the engine's
	// action filter rejects every action without a Blueprint and a graph, so a search without one found nothing (card 0434)
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ false);
	if (!Target.IsValid())
	{
		return MakeErrorJson(TEXT("No graph to search for: no Blueprint editor is active or graph focused: ") + Target.Error);
	}
	UBlueprint* ContextBlueprint = Target.Blueprint;
	UEdGraph* ContextGraph = Target.Graph;

	// The same action list search-blueprint-nodes and AddNodeToGraph build: the editor's node menu with Context
	// Sensitive off, for either value of bContextSensitive
	FBlueprintActionMenuBuilder MenuBuilder;
	N2CNodeActions::BuildActionList(MenuBuilder, ContextBlueprint, ContextGraph);

	const TArray<FString> LowerFilterTerms = N2CNodeActions::TokenizeSearchTerm(SearchTerm);

	TArray<TSharedPtr<FJsonValue>> ResultNodes;
	int32 ResultCount = 0;

	for (int32 i = 0; i < MenuBuilder.GetNumActions() && ResultCount < MaxResults; ++i)
	{
		const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(i);
		if (!Action.IsValid() || !N2CNodeActions::MatchesSearchTerm(*Action, LowerFilterTerms))
		{
			continue;
		}

		// Category filter on the action's own menu category, not a pipe heuristic over the search text
		if (!N2CNodeActions::MatchesCategory(*Action, CategoryFilter))
		{
			continue;
		}

		if (bExcludeVMFunctions && N2CNodeActions::IsExcludedVMFunction(*Action))
		{
			continue;
		}

		TSharedPtr<FJsonObject> NodeJson = MakeShareable(new FJsonObject);

		// The action's own fields: menuName, category, tooltip and keywords. name and displayName are the menu name
		// (before card 0277 both were the raw search text)
		const FString MenuName = Action->GetMenuDescription().ToString();
		NodeJson->SetStringField(TEXT("name"), MenuName);
		NodeJson->SetStringField(TEXT("displayName"), MenuName);
		N2CNodeActions::AddDescriptionFields(*Action, *NodeJson);

		// Spawn metadata for adding the node later: the stable ID (the spawner signature)
		TSharedPtr<FJsonObject> SpawnMetadata = MakeShareable(new FJsonObject);
		SpawnMetadata->SetStringField(TEXT("actionIdentifier"), N2CNodeActions::GetActionId(Action));
		SpawnMetadata->SetStringField(TEXT("nodeName"), MenuName);
		SpawnMetadata->SetBoolField(TEXT("isContextSensitive"), bContextSensitive);
		NodeJson->SetObjectField(TEXT("spawnMetadata"), SpawnMetadata);

		ResultNodes.Add(MakeShareable(new FJsonValueObject(NodeJson)));
		ResultCount++;
	}

	// Build result JSON
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetArrayField(TEXT("nodes"), ResultNodes);
	ResultObject->SetNumberField(TEXT("count"), ResultCount);

	// Read only: no compile and no action-database refresh (card 0151)
	return MakeSuccessJson(ResultObject);
}

FString UN2CPythonBridge::AddNodeToGraph(const FString& NodeName, const FString& ActionIdentifier, float LocationX, float LocationY,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (NodeName.IsEmpty() || ActionIdentifier.IsEmpty())
	{
		return MakeErrorJson(TEXT("NodeName and ActionIdentifier are required"));
	}

	// Get the active graph context
	UBlueprint* ActiveBlueprint = nullptr;
	UEdGraph* ActiveGraph = nullptr;
	FString ContextError;
	// The focused graph, through the one graph resolver (card 0152); read-only graphs are refused
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	ActiveBlueprint = Target.Blueprint;
	ActiveGraph = Target.Graph;

	// The same action list search_blueprint_nodes builds for this graph
	FBlueprintActionMenuBuilder MenuBuilder;
	N2CNodeActions::BuildActionList(MenuBuilder, ActiveBlueprint, ActiveGraph);

	// The stable ID (spawner signature); pre-0277 search-text identifiers are still accepted (card 0277)
	FString ResolveError;
	const TSharedPtr<FEdGraphSchemaAction> SchemaAction = N2CNodeActions::ResolveAction(MenuBuilder, ActionIdentifier,
		NodeName, ResolveError);
	if (!SchemaAction.IsValid())
	{
		return MakeErrorJson(ResolveError);
	}

	FString SpawnError;
	UEdGraphNode* SpawnedNode = N2CNodeActions::SpawnAction(SchemaAction, ActiveGraph, FVector2f(LocationX, LocationY), SpawnError);
	if (!SpawnedNode)
	{
		return MakeErrorJson(SpawnError);
	}

	// Mark it modified; the call compiles it once, below
	FN2CMcpBlueprintUtils::MarkModified(ActiveBlueprint, /*bStructural*/ false);

	// Build result with pin information for immediate use
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetStringField(TEXT("nodeGuid"), SpawnedNode->NodeGuid.ToString());
	ResultObject->SetStringField(TEXT("nodeName"), SpawnedNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
	ResultObject->SetStringField(TEXT("graphName"), ActiveGraph->GetName());
	ResultObject->SetStringField(TEXT("blueprintName"), ActiveBlueprint->GetName());

	TSharedPtr<FJsonObject> LocationObject = MakeShareable(new FJsonObject);
	LocationObject->SetNumberField(TEXT("x"), SpawnedNode->NodePosX);
	LocationObject->SetNumberField(TEXT("y"), SpawnedNode->NodePosY);
	ResultObject->SetObjectField(TEXT("location"), LocationObject);

	// Add pin information so caller can immediately connect pins without additional lookup
	TArray<TSharedPtr<FJsonValue>> InputPinsArray;
	TArray<TSharedPtr<FJsonValue>> OutputPinsArray;

	for (UEdGraphPin* Pin : SpawnedNode->Pins)
	{
		if (!Pin || Pin->bHidden)
		{
			continue;
		}

		TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
		PinObj->SetStringField(TEXT("pinGuid"), Pin->PinId.ToString());
		PinObj->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("displayName"), Pin->GetDisplayName().ToString());
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

		// Include default value for input pins
		if (Pin->Direction == EGPD_Input && !Pin->GetDefaultAsString().IsEmpty())
		{
			PinObj->SetStringField(TEXT("defaultValue"), Pin->GetDefaultAsString());
		}

		if (Pin->Direction == EGPD_Input)
		{
			InputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
		}
		else
		{
			OutputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
		}
	}

	ResultObject->SetArrayField(TEXT("inputPins"), InputPinsArray);
	ResultObject->SetArrayField(TEXT("outputPins"), OutputPinsArray);

	// Add guidance for the next step - helps LLMs understand what to do after adding a node
	TSharedPtr<FJsonObject> NextStepObject = MakeShareable(new FJsonObject);
	NextStepObject->SetStringField(TEXT("function"), TEXT("connect_pins"));
	NextStepObject->SetStringField(TEXT("description"), TEXT("Use nodetocode.connect_pins() with the nodeGuid and pinGuid values above to wire this node to other nodes in the graph."));
	ResultObject->SetObjectField(TEXT("nextStep"), NextStepObject);

	AddCompileResult(ResultObject, CallScope.Flush());

	return MakeSuccessJson(ResultObject);
}

FString UN2CPythonBridge::ConnectPins(const FString& ConnectionsJson, bool bBreakExistingLinks,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (ConnectionsJson.IsEmpty())
	{
		return MakeErrorJson(TEXT("ConnectionsJson cannot be empty"));
	}

	// Parse connections JSON
	TSharedPtr<FJsonValue> ParsedValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConnectionsJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedValue) || !ParsedValue.IsValid())
	{
		return MakeErrorJson(TEXT("Failed to parse ConnectionsJson"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
	if (!ParsedValue->TryGetArray(ConnectionsArray) || ConnectionsArray->Num() == 0)
	{
		return MakeErrorJson(TEXT("ConnectionsJson must be a non-empty array"));
	}

	// Get focused graph
	UBlueprint* Blueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;
	// The focused graph, through the one graph resolver (card 0152); read-only graphs are refused
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	Blueprint = Target.Blueprint;
	FocusedGraph = Target.Graph;

	const UEdGraphSchema* Schema = FocusedGraph->GetSchema();
	if (!Schema)
	{
		return MakeErrorJson(TEXT("Graph has no schema"));
	}

	// Build node lookup map
	TMap<FGuid, UEdGraphNode*> NodeMap;
	for (UEdGraphNode* Node : FocusedGraph->Nodes)
	{
		if (Node && Node->NodeGuid.IsValid())
		{
			NodeMap.Add(Node->NodeGuid, Node);
		}
	}

	// Process connections
	TArray<TSharedPtr<FJsonValue>> SucceededArray;
	TArray<TSharedPtr<FJsonValue>> FailedArray;

	TOptional<FScopedTransaction> Transaction(InPlace, LOCTEXT("PythonConnectPins", "Python: Connect Pins"));
	Blueprint->Modify();
	FocusedGraph->Modify();

	// Every entry is reported, in order, with its array index: a failed or malformed one in failed with an errorCode
	// (INVALID_REQUEST, INVALID_GUID, NODE_NOT_FOUND, PIN_NOT_FOUND, WOULD_BREAK_EXISTING_LINK, SCHEMA_VALIDATION_FAILED,
	// CONNECTION_FAILED), a made one in succeeded with the schema's response and any conversion node (card 0276)
	auto Fail = [&FailedArray](int32 Index, const TCHAR* Code, const FString& Error, const TSharedPtr<FJsonObject>& Extra = nullptr)
	{
		TSharedPtr<FJsonObject> FailedObj = Extra.IsValid() ? Extra : MakeShared<FJsonObject>();
		FailedObj->SetNumberField(TEXT("index"), Index);
		FailedObj->SetStringField(TEXT("errorCode"), Code);
		FailedObj->SetStringField(TEXT("error"), Error);
		FailedArray.Add(MakeShared<FJsonValueObject>(FailedObj));
	};
	auto FindPin = [](UEdGraphNode* Node, const FString& PinGuidStr, const FString& PinName) -> UEdGraphPin*
	{
		FGuid PinGuid;
		if (FGuid::Parse(PinGuidStr, PinGuid))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->PinId == PinGuid)
				{
					return Pin;
				}
			}
		}
		if (!PinName.IsEmpty())
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && (Pin->PinName.ToString() == PinName || Pin->GetDisplayName().ToString() == PinName))
				{
					return Pin;
				}
			}
		}
		return nullptr;
	};

	for (int32 Index = 0; Index < ConnectionsArray->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& ConnectionValue = (*ConnectionsArray)[Index];
		const TSharedPtr<FJsonObject>* ConnectionObject = nullptr;
		if (!ConnectionValue.IsValid() || !ConnectionValue->TryGetObject(ConnectionObject))
		{
			Fail(Index, TEXT("INVALID_REQUEST"), TEXT("The entry is not an object with 'from' and 'to'"));
			continue;
		}

		const TSharedPtr<FJsonObject>* FromObject = nullptr;
		const TSharedPtr<FJsonObject>* ToObject = nullptr;
		if (!(*ConnectionObject)->TryGetObjectField(TEXT("from"), FromObject) || !(*ConnectionObject)->TryGetObjectField(TEXT("to"), ToObject))
		{
			Fail(Index, TEXT("INVALID_REQUEST"), TEXT("The entry needs a 'from' and a 'to' object"));
			continue;
		}

		// pinGuid and pinName are optional (either finds the pin); a missing field is read as empty, without a log error
		FString FromNodeGuidStr, FromPinGuidStr, FromPinName, ToNodeGuidStr, ToPinGuidStr, ToPinName;
		const bool bHasNodes = (*FromObject)->TryGetStringField(TEXT("nodeGuid"), FromNodeGuidStr)
			&& (*ToObject)->TryGetStringField(TEXT("nodeGuid"), ToNodeGuidStr);
		(*FromObject)->TryGetStringField(TEXT("pinGuid"), FromPinGuidStr);
		(*FromObject)->TryGetStringField(TEXT("pinName"), FromPinName);
		(*ToObject)->TryGetStringField(TEXT("pinGuid"), ToPinGuidStr);
		(*ToObject)->TryGetStringField(TEXT("pinName"), ToPinName);
		if (!bHasNodes)
		{
			Fail(Index, TEXT("INVALID_REQUEST"), TEXT("'from' and 'to' each need a nodeGuid string"));
			continue;
		}

		// Find nodes
		FGuid FromNodeGuid, ToNodeGuid;
		if (!FGuid::Parse(FromNodeGuidStr, FromNodeGuid) || !FGuid::Parse(ToNodeGuidStr, ToNodeGuid))
		{
			Fail(Index, TEXT("INVALID_GUID"), TEXT("Invalid GUID format"));
			continue;
		}

		UEdGraphNode* FromNode = NodeMap.FindRef(FromNodeGuid);
		UEdGraphNode* ToNode = NodeMap.FindRef(ToNodeGuid);

		if (!FromNode || !ToNode)
		{
			Fail(Index, TEXT("NODE_NOT_FOUND"), TEXT("Node not found"));
			continue;
		}

		UEdGraphPin* FromPin = FindPin(FromNode, FromPinGuidStr, FromPinName);
		UEdGraphPin* ToPin = FindPin(ToNode, ToPinGuidStr, ToPinName);

		if (!FromPin || !ToPin)
		{
			Fail(Index, TEXT("PIN_NOT_FOUND"), TEXT("Pin not found"));
			continue;
		}

		// Validate connection; the schema decides what (if anything) a connection breaks or converts
		const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
		TSharedPtr<FJsonObject> ResponseFields = MakeShared<FJsonObject>();
		N2CPinConnectPolicy::WriteResponse(ResponseFields, Response);
		FString PolicyError;
		if (!N2CPinConnectPolicy::Allow(Response, FromPin, ToPin, bBreakExistingLinks, PolicyError))
		{
			Fail(Index, N2CPinConnectPolicy::WouldReplaceExistingLink(Response, FromPin, ToPin) ? TEXT("WOULD_BREAK_EXISTING_LINK") : TEXT("SCHEMA_VALIDATION_FAILED"),
				PolicyError, ResponseFields);
			continue;
		}

		// Make the connection
		FromNode->Modify();
		ToNode->Modify();

		const N2CPinConnectPolicy::FConnectOutcome Outcome = N2CPinConnectPolicy::Connect(Schema, FromPin, ToPin);
		if (Outcome.bConnected)
		{
			TSharedPtr<FJsonObject> SuccessObj = MakeShared<FJsonObject>();
			SuccessObj->SetNumberField(TEXT("index"), Index);
			SuccessObj->SetStringField(TEXT("fromNode"), FromNodeGuidStr);
			SuccessObj->SetStringField(TEXT("fromPin"), FromPin->PinId.ToString());
			SuccessObj->SetStringField(TEXT("toNode"), ToNodeGuidStr);
			SuccessObj->SetStringField(TEXT("toPin"), ToPin->PinId.ToString());
			N2CPinConnectPolicy::WriteResponse(SuccessObj, Response, Outcome.ConversionNode);
			SucceededArray.Add(MakeShared<FJsonValueObject>(SuccessObj));
		}
		else
		{
			Fail(Index, TEXT("CONNECTION_FAILED"), TEXT("Connection failed"), ResponseFields);
		}
	}

	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);

	// Build result
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetArrayField(TEXT("succeeded"), SucceededArray);
	ResultObject->SetArrayField(TEXT("failed"), FailedArray);

	TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
	SummaryObject->SetNumberField(TEXT("totalRequested"), ConnectionsArray->Num());
	SummaryObject->SetNumberField(TEXT("succeeded"), SucceededArray.Num());
	SummaryObject->SetNumberField(TEXT("failed"), FailedArray.Num());
	ResultObject->SetObjectField(TEXT("summary"), SummaryObject);

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(ResultObject, CallScope.Flush());

	return MakeSuccessJson(ResultObject);
}

FString UN2CPythonBridge::SetPinValue(const FString& NodeGuid, const FString& PinGuid, const FString& Value, const FString& PinName,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (NodeGuid.IsEmpty() || PinGuid.IsEmpty())
	{
		return MakeErrorJson(TEXT("NodeGuid and PinGuid are required"));
	}

	// Get focused graph
	UBlueprint* Blueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;
	// The focused graph, through the one graph resolver (card 0152); read-only graphs are refused
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	Blueprint = Target.Blueprint;
	FocusedGraph = Target.Graph;

	// Find the node
	FGuid ParsedNodeGuid;
	if (!FGuid::Parse(NodeGuid, ParsedNodeGuid))
	{
		return MakeErrorJson(TEXT("Invalid node GUID format"));
	}

	UEdGraphNode* TargetNode = nullptr;
	for (UEdGraphNode* Node : FocusedGraph->Nodes)
	{
		if (Node && Node->NodeGuid == ParsedNodeGuid)
		{
			TargetNode = Node;
			break;
		}
	}

	if (!TargetNode)
	{
		return MakeErrorJson(FString::Printf(TEXT("Node with GUID %s not found"), *NodeGuid));
	}

	// Find the pin
	UEdGraphPin* TargetPin = nullptr;
	FGuid ParsedPinGuid;
	if (FGuid::Parse(PinGuid, ParsedPinGuid))
	{
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin && Pin->PinId == ParsedPinGuid)
			{
				TargetPin = Pin;
				break;
			}
		}
	}

	if (!TargetPin && !PinName.IsEmpty())
	{
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin && (Pin->PinName.ToString() == PinName || Pin->GetDisplayName().ToString() == PinName))
			{
				TargetPin = Pin;
				break;
			}
		}
	}

	if (!TargetPin)
	{
		return MakeErrorJson(FString::Printf(TEXT("Pin with GUID %s not found"), *PinGuid));
	}

	// Validate pin can have default value
	if (TargetPin->Direction != EGPD_Input)
	{
		return MakeErrorJson(TEXT("Can only set default values on input pins"));
	}

	if (TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return MakeErrorJson(TEXT("Cannot set default value on execution pin"));
	}

	if (TargetPin->LinkedTo.Num() > 0)
	{
		return MakeErrorJson(TEXT("Cannot set default value on connected pin"));
	}

	// Store old value
	const FString OldValue = FN2CMcpPinDefault::Read(*TargetPin);

	// As set-input-pin-value: the schema checks the value before the node is touched, a refused value is an error with
	// the schema's message and the old default stays (no transaction is opened), an empty value resets the pin (card 0278)
	FString Prepared;
	FString PrepareError;
	if (!FN2CMcpPinDefault::Prepare(*TargetPin, Value, Prepared, PrepareError))
	{
		return MakeErrorJson(FN2CMcpPinDefault::RefusalMessage(*TargetPin, Value, PrepareError, OldValue));
	}
	TOptional<FScopedTransaction> Transaction(InPlace, LOCTEXT("PythonSetPinValue", "Python: Set Pin Value"));
	FN2CMcpPinDefault::Write(*TargetPin, Prepared);

	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("nodeGuid"), NodeGuid);
	Data->SetStringField(TEXT("pinGuid"), PinGuid);
	Data->SetStringField(TEXT("pinName"), TargetPin->GetDisplayName().ToString());
	Data->SetStringField(TEXT("oldValue"), OldValue);
	// What the pin holds now, read back
	Data->SetStringField(TEXT("newValue"), FN2CMcpPinDefault::Read(*TargetPin));

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::DeleteNodes(const FString& NodeGuidsJson, bool bPreserveConnections, bool bForce,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (NodeGuidsJson.IsEmpty())
	{
		return MakeErrorJson(TEXT("NodeGuidsJson cannot be empty"));
	}

	// Parse JSON
	TSharedPtr<FJsonValue> ParsedValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodeGuidsJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedValue) || !ParsedValue.IsValid())
	{
		return MakeErrorJson(TEXT("Failed to parse NodeGuidsJson"));
	}

	const TArray<TSharedPtr<FJsonValue>>* GuidsArray;
	if (!ParsedValue->TryGetArray(GuidsArray) || GuidsArray->Num() == 0)
	{
		return MakeErrorJson(TEXT("NodeGuidsJson must be a non-empty array"));
	}

	// The focused graph, through the one graph resolver (card 0152); read-only graphs are refused
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	UBlueprint* Blueprint = Target.Blueprint;
	UEdGraph* FocusedGraph = Target.Graph;

	// Find the nodes; protection is the node's own CanUserDeleteNode, which bForce overrides (card 0279). A GUID that
	// is not a node of the graph, or names a protected node, is listed under 'skipped' with the reason
	TArray<UEdGraphNode*> NodesToDelete;
	TArray<TSharedPtr<FJsonValue>> SkippedArray;
	auto Skip = [&SkippedArray](const FString& Guid, const FString& Reason)
	{
		const TSharedPtr<FJsonObject> Skipped = MakeShared<FJsonObject>();
		Skipped->SetStringField(TEXT("guid"), Guid);
		Skipped->SetStringField(TEXT("reason"), Reason);
		SkippedArray.Add(MakeShared<FJsonValueObject>(Skipped));
	};

	for (const TSharedPtr<FJsonValue>& Value : *GuidsArray)
	{
		FString GuidString;
		FGuid ParsedGuid;
		if (!Value->TryGetString(GuidString) || !FGuid::Parse(GuidString, ParsedGuid))
		{
			Skip(GuidString, TEXT("not a GUID"));
			continue;
		}
		UEdGraphNode* Node = N2CNodeDeletion::FindNode(FocusedGraph, ParsedGuid);
		if (!Node)
		{
			Skip(GuidString, FString::Printf(TEXT("no node with this GUID in graph %s"), *FocusedGraph->GetName()));
			continue;
		}
		FString Reason;
		if (!N2CNodeDeletion::CanDelete(Node, bForce, Reason))
		{
			Skip(GuidString, Reason);
			continue;
		}
		NodesToDelete.AddUnique(Node);
	}

	if (NodesToDelete.Num() == 0)
	{
		TArray<FString> Reasons;
		for (const TSharedPtr<FJsonValue>& Skipped : SkippedArray)
		{
			Reasons.Add(FString::Printf(TEXT("%s: %s"), *Skipped->AsObject()->GetStringField(TEXT("guid")), *Skipped->AsObject()->GetStringField(TEXT("reason"))));
		}
		return MakeErrorJson(FString::Printf(TEXT("No valid nodes found to delete (%s)"), *FString::Join(Reasons, TEXT("; "))));
	}

	// Delete the way the editor's Delete does, shared with the delete-blueprint-node tool (card 0279)
	TOptional<FScopedTransaction> Transaction(InPlace, LOCTEXT("PythonDeleteNodes", "Python: Delete Nodes"));
	const TArray<N2CNodeDeletion::FDeletedNode> Deleted = N2CNodeDeletion::DeleteNodes(Blueprint, NodesToDelete, bPreserveConnections);

	// Build result
	TArray<TSharedPtr<FJsonValue>> DeletedArray;
	for (const N2CNodeDeletion::FDeletedNode& Info : Deleted)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
		NodeObj->SetStringField(TEXT("guid"), Info.NodeGuid);
		NodeObj->SetStringField(TEXT("name"), Info.ListTitle);
		if (Info.BridgedLinks.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Links;
			for (const N2CNodeDeletion::FBridgedLink& Link : Info.BridgedLinks)
			{
				const TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
				LinkObj->SetStringField(TEXT("fromNode"), Link.FromNodeGuid);
				LinkObj->SetStringField(TEXT("fromPin"), Link.FromPinName);
				LinkObj->SetStringField(TEXT("toNode"), Link.ToNodeGuid);
				LinkObj->SetStringField(TEXT("toPin"), Link.ToPinName);
				LinkObj->SetStringField(TEXT("type"), Link.PinType);
				Links.Add(MakeShared<FJsonValueObject>(LinkObj));
			}
			NodeObj->SetArrayField(TEXT("preservedConnections"), Links);
		}
		DeletedArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetArrayField(TEXT("deletedNodes"), DeletedArray);
	ResultObject->SetNumberField(TEXT("deletedCount"), DeletedArray.Num());
	if (SkippedArray.Num() > 0)
	{
		ResultObject->SetArrayField(TEXT("skipped"), SkippedArray);
	}

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(ResultObject, CallScope.Flush());

	return MakeSuccessJson(ResultObject);
}

FString UN2CPythonBridge::FindNodesInGraph(const FString& SearchTermsJson, const FString& SearchType, bool bCaseSensitive, int32 MaxResults,
	const FString& BlueprintPath, const FString& Graph)
{
	if (SearchTermsJson.IsEmpty())
	{
		return MakeErrorJson(TEXT("SearchTermsJson cannot be empty"));
	}

	MaxResults = FMath::Clamp(MaxResults, 1, 200);

	// Parse JSON
	TSharedPtr<FJsonValue> ParsedValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SearchTermsJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedValue) || !ParsedValue.IsValid())
	{
		return MakeErrorJson(TEXT("Failed to parse SearchTermsJson"));
	}

	const TArray<TSharedPtr<FJsonValue>>* TermsArray;
	if (!ParsedValue->TryGetArray(TermsArray) || TermsArray->Num() == 0)
	{
		return MakeErrorJson(TEXT("SearchTermsJson must be a non-empty array"));
	}

	TArray<FString> SearchTerms;
	for (const TSharedPtr<FJsonValue>& Value : *TermsArray)
	{
		FString Term;
		if (Value->TryGetString(Term) && !Term.IsEmpty())
		{
			SearchTerms.Add(Term);
		}
	}

	if (SearchTerms.Num() == 0)
	{
		return MakeErrorJson(TEXT("No valid search terms found"));
	}

	// Get focused graph
	UBlueprint* Blueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;
	// The focused graph, through the one graph resolver (card 0152)
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ false);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	Blueprint = Target.Blueprint;
	FocusedGraph = Target.Graph;

	// Collect nodes
	TArray<UK2Node*> AllNodes;
	if (!FN2CEditorIntegration::Get().CollectNodesFromGraph(FocusedGraph, AllNodes))
	{
		return MakeErrorJson(TEXT("Failed to collect nodes from graph"));
	}

	// Find matching nodes
	TArray<UK2Node*> MatchingNodes;
	bool bIsGuidSearch = SearchType.Equals(TEXT("guid"), ESearchCase::IgnoreCase);

	for (UK2Node* Node : AllNodes)
	{
		if (!Node)
		{
			continue;
		}

		bool bMatches = false;

		if (bIsGuidSearch)
		{
			FString NodeGuidString = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
			FString NodeGuidStringNoHyphens = Node->NodeGuid.ToString(EGuidFormats::Digits);

			for (const FString& Term : SearchTerms)
			{
				if (NodeGuidString.Equals(Term, ESearchCase::IgnoreCase) ||
					NodeGuidStringNoHyphens.Equals(Term, ESearchCase::IgnoreCase))
				{
					bMatches = true;
					break;
				}
			}
		}
		else // keyword search
		{
			// The title as shown and in source text, the class and the member name (card 0138)
			FString SearchableText = N2CStableNames::NodeSearchText(Node);

			if (!bCaseSensitive)
			{
				SearchableText = SearchableText.ToLower();
			}

			for (const FString& Term : SearchTerms)
			{
				FString SearchTerm = bCaseSensitive ? Term : Term.ToLower();
				if (SearchableText.Contains(SearchTerm))
				{
					bMatches = true;
					break;
				}
			}
		}

		if (bMatches)
		{
			MatchingNodes.Add(Node);
			if (MatchingNodes.Num() >= MaxResults)
			{
				break;
			}
		}
	}

	// Build result
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UK2Node* Node : MatchingNodes)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
		NodeObj->SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("nodeName"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		NodeObj->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		NodeObj->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("posY"), Node->NodePosY);

		// Add pin information
		TArray<TSharedPtr<FJsonValue>> InputPinsArray;
		TArray<TSharedPtr<FJsonValue>> OutputPinsArray;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden)
			{
				continue;
			}

			TSharedPtr<FJsonObject> PinObj = MakeShareable(new FJsonObject);
			PinObj->SetStringField(TEXT("pinGuid"), Pin->PinId.ToString());
			PinObj->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("displayName"), Pin->GetDisplayName().ToString());
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

			if (Pin->Direction == EGPD_Input)
			{
				InputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
			}
			else
			{
				OutputPinsArray.Add(MakeShareable(new FJsonValueObject(PinObj)));
			}
		}

		NodeObj->SetArrayField(TEXT("inputPins"), InputPinsArray);
		NodeObj->SetArrayField(TEXT("outputPins"), OutputPinsArray);

		NodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetArrayField(TEXT("nodes"), NodesArray);

	TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject);
	MetadataObject->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	MetadataObject->SetStringField(TEXT("graphName"), FocusedGraph->GetName());
	MetadataObject->SetNumberField(TEXT("totalFound"), MatchingNodes.Num());
	MetadataObject->SetNumberField(TEXT("totalInGraph"), AllNodes.Num());
	ResultObject->SetObjectField(TEXT("metadata"), MetadataObject);

	return MakeSuccessJson(ResultObject);
}

FString UN2CPythonBridge::CreateCommentNode(const FString& NodeGuidsJson, const FString& CommentText, float ColorR, float ColorG, float ColorB, int32 FontSize, const FString& MoveMode, float Padding,
	const FString& BlueprintPath, const FString& Graph)
{
	if (NodeGuidsJson.IsEmpty())
	{
		return MakeErrorJson(TEXT("NodeGuidsJson cannot be empty"));
	}

	// Parse JSON
	TSharedPtr<FJsonValue> ParsedValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(NodeGuidsJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedValue) || !ParsedValue.IsValid())
	{
		return MakeErrorJson(TEXT("Failed to parse NodeGuidsJson"));
	}

	const TArray<TSharedPtr<FJsonValue>>* GuidsArray;
	if (!ParsedValue->TryGetArray(GuidsArray) || GuidsArray->Num() == 0)
	{
		return MakeErrorJson(TEXT("NodeGuidsJson must be a non-empty array"));
	}

	// Get focused graph
	UBlueprint* Blueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;
	// The focused graph, through the one graph resolver (card 0152); read-only graphs are refused
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	Blueprint = Target.Blueprint;
	FocusedGraph = Target.Graph;

	// Build node lookup and find nodes
	TMap<FGuid, UEdGraphNode*> NodeMap;
	for (UEdGraphNode* Node : FocusedGraph->Nodes)
	{
		if (Node && Node->NodeGuid.IsValid())
		{
			NodeMap.Add(Node->NodeGuid, Node);
		}
	}

	TArray<UEdGraphNode*> NodesToComment;
	for (const TSharedPtr<FJsonValue>& Value : *GuidsArray)
	{
		FString GuidString;
		if (Value->TryGetString(GuidString))
		{
			FGuid ParsedGuid;
			if (FGuid::Parse(GuidString, ParsedGuid))
			{
				UEdGraphNode* Node = NodeMap.FindRef(ParsedGuid);
				if (Node && !Node->IsA<UEdGraphNode_Comment>())
				{
					NodesToComment.Add(Node);
				}
			}
		}
	}

	if (NodesToComment.Num() == 0)
	{
		return MakeErrorJson(TEXT("No valid nodes found to comment"));
	}

	// Calculate bounding box
	const float DefaultNodeWidth = 150.0f;
	const float DefaultNodeHeight = 50.0f;

	float MinX = NodesToComment[0]->NodePosX;
	float MinY = NodesToComment[0]->NodePosY;
	float NodeWidth = NodesToComment[0]->NodeWidth > 0 ? NodesToComment[0]->NodeWidth : DefaultNodeWidth;
	float NodeHeight = NodesToComment[0]->NodeHeight > 0 ? NodesToComment[0]->NodeHeight : DefaultNodeHeight;
	float MaxX = MinX + NodeWidth;
	float MaxY = MinY + NodeHeight;

	for (int32 i = 1; i < NodesToComment.Num(); i++)
	{
		UEdGraphNode* Node = NodesToComment[i];
		MinX = FMath::Min(MinX, (float)Node->NodePosX);
		MinY = FMath::Min(MinY, (float)Node->NodePosY);

		NodeWidth = Node->NodeWidth > 0 ? Node->NodeWidth : DefaultNodeWidth;
		NodeHeight = Node->NodeHeight > 0 ? Node->NodeHeight : DefaultNodeHeight;

		MaxX = FMath::Max(MaxX, Node->NodePosX + NodeWidth);
		MaxY = FMath::Max(MaxY, Node->NodePosY + NodeHeight);
	}

	MinX -= Padding;
	MinY -= Padding;
	MaxX += Padding;
	MaxY += Padding;

	// Create the comment node
	const FScopedTransaction Transaction(LOCTEXT("PythonCreateComment", "Python: Create Comment"));
	FocusedGraph->Modify();

	// Through the schema's template path, as create-comment-node does: RF_Transactional and a GUID (card 0155)
	UEdGraphNode_Comment* CommentNode = FN2CMcpNodeSpawn::SpawnCommentNode(FocusedGraph, FSlateRect(MinX, MinY, MaxX, MaxY),
		CommentText, FLinearColor(ColorR, ColorG, ColorB), FontSize, MoveMode.Equals(TEXT("group"), ESearchCase::IgnoreCase), NodesToComment);
	if (!CommentNode)
	{
		return MakeErrorJson(TEXT("Failed to create comment node"));
	}

	// A comment changes nothing the compiler sees: dirty the package, do not compile (card 0151)
	FN2CMcpBlueprintUtils::MarkCosmeticChange(Blueprint);

	// Build result
	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("commentGuid"), CommentNode->NodeGuid.ToString());
	Data->SetStringField(TEXT("commentText"), CommentText);
	Data->SetNumberField(TEXT("includedNodeCount"), NodesToComment.Num());

	const TSharedPtr<FJsonObject> PositionObject = MakeShared<FJsonObject>();
	PositionObject->SetNumberField(TEXT("x"), CommentNode->NodePosX);
	PositionObject->SetNumberField(TEXT("y"), CommentNode->NodePosY);
	Data->SetObjectField(TEXT("position"), PositionObject);

	const TSharedPtr<FJsonObject> SizeObject = MakeShared<FJsonObject>();
	SizeObject->SetNumberField(TEXT("width"), CommentNode->NodeWidth);
	SizeObject->SetNumberField(TEXT("height"), CommentNode->NodeHeight);
	Data->SetObjectField(TEXT("size"), SizeObject);

	return MakeSuccessJson(Data);
}

// ========== Function Pin Management ==========

FString UN2CPythonBridge::AddFunctionInputPin(const FString& PinName, const FString& TypeIdentifier, const FString& DefaultValue, bool bIsPassByReference, const FString& Tooltip,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty() || TypeIdentifier.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName and TypeIdentifier are required"));
	}

	// The focused function, through the one graph resolver (card 0152): read-only graphs are refused, and a graph that
	// is not a function is refused before anything changes
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	UEdGraph* FocusedGraph = Target.Graph;
	if (!FN2CMcpFunctionGuidUtils::IsFunctionGraph(Target.Blueprint, FocusedGraph))
	{
		return MakeErrorJson(FString::Printf(TEXT("NOT_A_FUNCTION_GRAPH: the focused graph %s (%s graph) is not a function. Open the function first."),
			*FocusedGraph->GetName(), *FN2CGraphTarget::GraphKind(FocusedGraph)));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Resolve type identifier
	FEdGraphPinType PinType;
	FString ResolveError;

	if (!FN2CMcpTypeResolver::ResolvePinType(TypeIdentifier, TEXT(""), TEXT("none"), TEXT(""), bIsPassByReference, false, PinType, ResolveError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to resolve type '%s': %s"), *TypeIdentifier, *ResolveError));
	}

	// The add-function-input-pin path (card 0156): validate, then one transaction that adds the pin, regenerates the
	// skeleton and rebuilds the call sites; it has closed when this returns, so the compile below is not recorded
	FN2CFunctionPinEdit Edit;
	if (!FN2CMcpFunctionPinUtils::AddFunctionPin(Target.Blueprint, FocusedGraph, /*bReturnValue*/ false, PinName, PinType, DefaultValue,
		Tooltip, FText::Format(LOCTEXT("PythonAddInputPin", "Python: Add Input Pin '{0}'"), FText::FromString(PinName)), Edit))
	{
		return MakeErrorJson(Edit.Error);
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("pinName"), PinName);
	Data->SetStringField(TEXT("actualName"), Edit.PinName.ToString());
	Data->SetStringField(TEXT("pinGuid"), Edit.Pin ? Edit.Pin->PinId.ToString() : FString());
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());
	Data->SetStringField(TEXT("typeIdentifier"), TypeIdentifier);

	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::AddFunctionReturnPin(const FString& PinName, const FString& TypeIdentifier, const FString& Tooltip,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty() || TypeIdentifier.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName and TypeIdentifier are required"));
	}

	// The focused function, through the one graph resolver (card 0152): read-only graphs are refused, and a graph that
	// is not a function is refused before anything changes
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	UEdGraph* FocusedGraph = Target.Graph;
	if (!FN2CMcpFunctionGuidUtils::IsFunctionGraph(Target.Blueprint, FocusedGraph))
	{
		return MakeErrorJson(FString::Printf(TEXT("NOT_A_FUNCTION_GRAPH: the focused graph %s (%s graph) is not a function. Open the function first."),
			*FocusedGraph->GetName(), *FN2CGraphTarget::GraphKind(FocusedGraph)));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Resolve type identifier
	FEdGraphPinType PinType;
	FString ResolveError;

	if (!FN2CMcpTypeResolver::ResolvePinType(TypeIdentifier, TEXT(""), TEXT("none"), TEXT(""), false, false, PinType, ResolveError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to resolve type '%s': %s"), *TypeIdentifier, *ResolveError));
	}

	// The add-function-return-pin path (card 0156): validate, then one transaction that adds the pin to every Return node
	// (creating one, with a GUID and wired from the entry, when there is none), regenerates the skeleton and rebuilds the
	// call sites
	FN2CFunctionPinEdit Edit;
	if (!FN2CMcpFunctionPinUtils::AddFunctionPin(Target.Blueprint, FocusedGraph, /*bReturnValue*/ true, PinName, PinType, FString(),
		Tooltip, FText::Format(LOCTEXT("PythonAddReturnPin", "Python: Add Return Pin '{0}'"), FText::FromString(PinName)), Edit))
	{
		return MakeErrorJson(Edit.Error);
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("pinName"), PinName);
	Data->SetStringField(TEXT("actualName"), Edit.PinName.ToString());
	Data->SetStringField(TEXT("pinGuid"), Edit.Pin ? Edit.Pin->PinId.ToString() : FString());
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());
	Data->SetStringField(TEXT("typeIdentifier"), TypeIdentifier);

	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::RemoveFunctionEntryPin(const FString& PinName,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName is required"));
	}

	// The focused function, through the one graph resolver (card 0152): read-only graphs are refused, and a graph that
	// is not a function is refused before anything changes
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	UEdGraph* FocusedGraph = Target.Graph;
	if (!FN2CMcpFunctionGuidUtils::IsFunctionGraph(Target.Blueprint, FocusedGraph))
	{
		return MakeErrorJson(FString::Printf(TEXT("NOT_A_FUNCTION_GRAPH: the focused graph %s (%s graph) is not a function. Open the function first."),
			*FocusedGraph->GetName(), *FN2CGraphTarget::GraphKind(FocusedGraph)));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Validates first (a misspelled name opens no transaction and leaves the package clean), then one transaction that
	// removes the pin, regenerates the skeleton and rebuilds the call sites (card 0156)
	FN2CFunctionPinEdit Edit;
	if (!FN2CMcpFunctionPinUtils::RemoveFunctionPin(Target.Blueprint, FocusedGraph, /*bReturnValue*/ false, PinName,
		FText::Format(LOCTEXT("PythonRemoveEntryPin", "Python: Remove Entry Pin '{0}'"), FText::FromString(PinName)), Edit))
	{
		return MakeErrorJson(Edit.Error);
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removedPin"), PinName);
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());

	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::RemoveFunctionReturnPin(const FString& PinName,
	const FString& BlueprintPath, const FString& Graph)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName is required"));
	}

	// The focused function, through the one graph resolver (card 0152): read-only graphs are refused, and a graph that
	// is not a function is refused before anything changes
	const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(BridgeTargetArgs(BlueprintPath, Graph), /*bForWrite*/ true);
	if (!Target.IsValid())
	{
		return MakeErrorJson(Target.Error);
	}
	UEdGraph* FocusedGraph = Target.Graph;
	if (!FN2CMcpFunctionGuidUtils::IsFunctionGraph(Target.Blueprint, FocusedGraph))
	{
		return MakeErrorJson(FString::Printf(TEXT("NOT_A_FUNCTION_GRAPH: the focused graph %s (%s graph) is not a function. Open the function first."),
			*FocusedGraph->GetName(), *FN2CGraphTarget::GraphKind(FocusedGraph)));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Validates first (a misspelled name opens no transaction and leaves the package clean), then one transaction that
	// removes the pin from every Return node, regenerates the skeleton and rebuilds the call sites (card 0156)
	FN2CFunctionPinEdit Edit;
	if (!FN2CMcpFunctionPinUtils::RemoveFunctionPin(Target.Blueprint, FocusedGraph, /*bReturnValue*/ true, PinName,
		FText::Format(LOCTEXT("PythonRemoveReturnPin", "Python: Remove Return Pin '{0}'"), FText::FromString(PinName)), Edit))
	{
		return MakeErrorJson(Edit.Error);
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removedPin"), PinName);
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());

	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

#undef LOCTEXT_NAMESPACE

FString UN2CPythonBridge::MakeResultJson(bool bSuccess, const TSharedPtr<FJsonObject>& Data, const FString* Error)
{
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	if (Data.IsValid())
	{
		Result->SetObjectField(TEXT("data"), Data);
	}
	else
	{
		Result->SetField(TEXT("data"), MakeShared<FJsonValueNull>());
	}
	if (Error)
	{
		Result->SetStringField(TEXT("error"), *Error);
	}
	else
	{
		Result->SetField(TEXT("error"), MakeShared<FJsonValueNull>());
	}

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Result, Writer);
	return Json;
}

FString UN2CPythonBridge::MakeSuccessJson(const TSharedPtr<FJsonObject>& Data)
{
	return MakeResultJson(true, Data, nullptr);
}

FString UN2CPythonBridge::MakeErrorJson(const FString& ErrorMessage, const TSharedPtr<FJsonObject>& Data)
{
	return MakeResultJson(false, Data, &ErrorMessage);
}
