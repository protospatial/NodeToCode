// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CPythonBridge.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTagManager.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "MCP/Utils/N2CPinConnectPolicy.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpNodeActions.h"
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
}
using N2CPythonBridgePrivate::AddCompileResult;

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
	if (!FN2CSerializer::FromJson(N2CJson, Blueprint))
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
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::CompileFocusedBlueprint()
{
	// Get the focused Blueprint
	UBlueprint* OwningBlueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;

	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
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

FString UN2CPythonBridge::SaveFocusedBlueprint(bool bOnlyIfDirty)
{
	// Get the focused Blueprint
	UBlueprint* OwningBlueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;

	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
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

FString UN2CPythonBridge::TagFocusedGraph(const FString& Tag, const FString& Category, const FString& Description)
{
	if (Tag.IsEmpty())
	{
		return MakeErrorJson(TEXT("Tag name cannot be empty"));
	}

	// Get the focused graph
	UBlueprint* OwningBlueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;

	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(OwningBlueprint, FocusedGraph, GraphError))
	{
		return MakeErrorJson(GraphError);
	}

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
	if (!UN2CTagManager::Get().AddTag(TaggedGraph))
	{
		return MakeErrorJson(TEXT("Failed to add tag - tag may already exist on this graph"));
	}

	// Save tags to persist
	UN2CTagManager::Get().SaveTags();

	FN2CLogger::Get().Log(FString::Printf(TEXT("TagFocusedGraph: Added tag '%s' to %s"), *Tag, *FocusedGraph->GetName()), EN2CLogSeverity::Info);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("tag"), Tag);
	Data->SetStringField(TEXT("category"), TaggedGraph.Category);
	Data->SetStringField(TEXT("description"), Description);
	Data->SetStringField(TEXT("graph_guid"), FocusedGraph->GraphGuid.ToString());
	Data->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	Data->SetStringField(TEXT("blueprint_name"), OwningBlueprint->GetName());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::ListTags(const FString& Category, const FString& Tag)
{
	TArray<FN2CTaggedBlueprintGraph> Tags;

	// Get filtered or all tags
	if (!Category.IsEmpty() && !Tag.IsEmpty())
	{
		// Filter by both
		Tags = UN2CTagManager::Get().GetGraphsWithTag(Tag, Category);
	}
	else if (!Category.IsEmpty())
	{
		// Filter by category only
		Tags = UN2CTagManager::Get().GetTagsInCategory(Category);
	}
	else if (!Tag.IsEmpty())
	{
		// Filter by tag only (all categories)
		Tags = UN2CTagManager::Get().GetGraphsWithTag(Tag, TEXT(""));
	}
	else
	{
		// Get all tags
		Tags = UN2CTagManager::Get().GetAllTags();
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
	TArray<FString> AllCategories = UN2CTagManager::Get().GetAllCategories();
	TArray<FString> AllTagNames = UN2CTagManager::Get().GetAllTagNames();

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tags"), TagsArray);
	Data->SetNumberField(TEXT("count"), Tags.Num());
	Data->SetNumberField(TEXT("total_categories"), AllCategories.Num());
	Data->SetNumberField(TEXT("total_unique_tags"), AllTagNames.Num());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::RemoveTag(const FString& GraphGuid, const FString& Tag)
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

	// Try to remove the tag
	FN2CTaggedBlueprintGraph RemovedTag;
	int32 RemovedCount = UN2CTagManager::Get().RemoveTagByName(ParsedGuid, Tag, RemovedTag);

	if (RemovedCount == 0)
	{
		return MakeErrorJson(FString::Printf(TEXT("Tag '%s' not found on graph %s"), *Tag, *GraphGuid));
	}

	// Save tags to persist
	UN2CTagManager::Get().SaveTags();

	// Get remaining tags for this graph
	TArray<FN2CTaggedBlueprintGraph> RemainingTags = UN2CTagManager::Get().GetTagsForGraph(ParsedGuid);

	FN2CLogger::Get().Log(FString::Printf(TEXT("RemoveTag: Removed %d tag(s) '%s' from graph %s"), RemovedCount, *Tag, *GraphGuid), EN2CLogSeverity::Info);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("removed"), true);
	Data->SetNumberField(TEXT("removed_count"), RemovedCount);
	Data->SetStringField(TEXT("tag"), Tag);
	Data->SetStringField(TEXT("graph_guid"), GraphGuid);
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

	// Focus specific graph if requested
	FString FocusedGraphName;
	if (!FocusGraph.IsEmpty())
	{
		UEdGraph* TargetGraph = nullptr;

		// Check EventGraphs
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph && Graph->GetName() == FocusGraph)
			{
				TargetGraph = Graph;
				break;
			}
		}

		// Check FunctionGraphs if not found
		if (!TargetGraph)
		{
			for (UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (Graph && Graph->GetName() == FocusGraph)
				{
					TargetGraph = Graph;
					break;
				}
			}
		}

		// Check MacroGraphs if not found
		if (!TargetGraph)
		{
			for (UEdGraph* Graph : Blueprint->MacroGraphs)
			{
				if (Graph && Graph->GetName() == FocusGraph)
				{
					TargetGraph = Graph;
					break;
				}
			}
		}

		if (TargetGraph)
		{
			Editor->JumpToHyperlink(TargetGraph, false);
			// Read back what the editor now shows rather than echo the request
			if (Editor->GetFocusedGraph() == TargetGraph)
			{
				FocusedGraphName = TargetGraph->GetName();
			}
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

	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(Blueprint, FocusedGraph, GraphError))
	{
		return MakeErrorJson(TEXT("No Blueprint editor is focused. Please open a Blueprint first."));
	}

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
	const FString& CategoryFilter, bool bExcludeVMFunctions)
{
	if (SearchTerm.IsEmpty())
	{
		return MakeErrorJson(TEXT("SearchTerm cannot be empty"));
	}

	MaxResults = FMath::Clamp(MaxResults, 1, 100);

	// Get context from focused editor
	UBlueprint* ContextBlueprint = nullptr;
	UEdGraph* ContextGraph = nullptr;
	FString GraphError;

	if (bContextSensitive)
	{
		if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(ContextBlueprint, ContextGraph, GraphError))
		{
			return MakeErrorJson(TEXT("Context-sensitive search requested but no Blueprint editor is active: ") + GraphError);
		}
	}

	// The same action list search-blueprint-nodes and AddNodeToGraph build (global without a context)
	FBlueprintActionMenuBuilder MenuBuilder;
	N2CNodeActions::BuildActionList(MenuBuilder, bContextSensitive ? ContextBlueprint : nullptr,
		bContextSensitive ? ContextGraph : nullptr);

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
		SpawnMetadata->SetBoolField(TEXT("isContextSensitive"), bContextSensitive && ContextGraph != nullptr);
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

FString UN2CPythonBridge::AddNodeToGraph(const FString& NodeName, const FString& ActionIdentifier, float LocationX, float LocationY)
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
	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(ActiveBlueprint, ActiveGraph, ContextError))
	{
		return MakeErrorJson(ContextError);
	}

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

FString UN2CPythonBridge::ConnectPins(const FString& ConnectionsJson, bool bBreakExistingLinks)
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
	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(Blueprint, FocusedGraph, GraphError))
	{
		return MakeErrorJson(GraphError);
	}

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

	for (const TSharedPtr<FJsonValue>& ConnectionValue : *ConnectionsArray)
	{
		TSharedPtr<FJsonObject> ConnectionObject = ConnectionValue->AsObject();
		if (!ConnectionObject.IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> FromObject = ConnectionObject->GetObjectField(TEXT("from"));
		TSharedPtr<FJsonObject> ToObject = ConnectionObject->GetObjectField(TEXT("to"));

		if (!FromObject.IsValid() || !ToObject.IsValid())
		{
			continue;
		}

		FString FromNodeGuidStr = FromObject->GetStringField(TEXT("nodeGuid"));
		FString FromPinGuidStr = FromObject->GetStringField(TEXT("pinGuid"));
		FString FromPinName = FromObject->GetStringField(TEXT("pinName"));

		FString ToNodeGuidStr = ToObject->GetStringField(TEXT("nodeGuid"));
		FString ToPinGuidStr = ToObject->GetStringField(TEXT("pinGuid"));
		FString ToPinName = ToObject->GetStringField(TEXT("pinName"));

		// Find nodes
		FGuid FromNodeGuid, ToNodeGuid;
		if (!FGuid::Parse(FromNodeGuidStr, FromNodeGuid) || !FGuid::Parse(ToNodeGuidStr, ToNodeGuid))
		{
			TSharedPtr<FJsonObject> FailedObj = MakeShareable(new FJsonObject);
			FailedObj->SetStringField(TEXT("error"), TEXT("Invalid GUID format"));
			FailedArray.Add(MakeShareable(new FJsonValueObject(FailedObj)));
			continue;
		}

		UEdGraphNode* FromNode = NodeMap.FindRef(FromNodeGuid);
		UEdGraphNode* ToNode = NodeMap.FindRef(ToNodeGuid);

		if (!FromNode || !ToNode)
		{
			TSharedPtr<FJsonObject> FailedObj = MakeShareable(new FJsonObject);
			FailedObj->SetStringField(TEXT("error"), TEXT("Node not found"));
			FailedArray.Add(MakeShareable(new FJsonValueObject(FailedObj)));
			continue;
		}

		// Find pins
		UEdGraphPin* FromPin = nullptr;
		UEdGraphPin* ToPin = nullptr;

		FGuid FromPinGuid, ToPinGuid;
		if (FGuid::Parse(FromPinGuidStr, FromPinGuid))
		{
			for (UEdGraphPin* Pin : FromNode->Pins)
			{
				if (Pin && Pin->PinId == FromPinGuid)
				{
					FromPin = Pin;
					break;
				}
			}
		}

		if (!FromPin && !FromPinName.IsEmpty())
		{
			for (UEdGraphPin* Pin : FromNode->Pins)
			{
				if (Pin && (Pin->PinName.ToString() == FromPinName || Pin->GetDisplayName().ToString() == FromPinName))
				{
					FromPin = Pin;
					break;
				}
			}
		}

		if (FGuid::Parse(ToPinGuidStr, ToPinGuid))
		{
			for (UEdGraphPin* Pin : ToNode->Pins)
			{
				if (Pin && Pin->PinId == ToPinGuid)
				{
					ToPin = Pin;
					break;
				}
			}
		}

		if (!ToPin && !ToPinName.IsEmpty())
		{
			for (UEdGraphPin* Pin : ToNode->Pins)
			{
				if (Pin && (Pin->PinName.ToString() == ToPinName || Pin->GetDisplayName().ToString() == ToPinName))
				{
					ToPin = Pin;
					break;
				}
			}
		}

		if (!FromPin || !ToPin)
		{
			TSharedPtr<FJsonObject> FailedObj = MakeShareable(new FJsonObject);
			FailedObj->SetStringField(TEXT("error"), TEXT("Pin not found"));
			FailedArray.Add(MakeShareable(new FJsonValueObject(FailedObj)));
			continue;
		}

		// Validate connection; the schema decides what (if anything) a connection breaks or converts
		const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
		FString PolicyError;
		if (!N2CPinConnectPolicy::Allow(Response, FromPin, ToPin, bBreakExistingLinks, PolicyError))
		{
			TSharedPtr<FJsonObject> FailedObj = MakeShareable(new FJsonObject);
			FailedObj->SetStringField(TEXT("error"), PolicyError);
			FailedArray.Add(MakeShareable(new FJsonValueObject(FailedObj)));
			continue;
		}

		// Make the connection
		FromNode->Modify();
		ToNode->Modify();

		if (Schema->TryCreateConnection(FromPin, ToPin))
		{
			TSharedPtr<FJsonObject> SuccessObj = MakeShareable(new FJsonObject);
			SuccessObj->SetStringField(TEXT("fromNode"), FromNodeGuidStr);
			SuccessObj->SetStringField(TEXT("fromPin"), FromPinGuidStr);
			SuccessObj->SetStringField(TEXT("toNode"), ToNodeGuidStr);
			SuccessObj->SetStringField(TEXT("toPin"), ToPinGuidStr);
			SucceededArray.Add(MakeShareable(new FJsonValueObject(SuccessObj)));
		}
		else
		{
			TSharedPtr<FJsonObject> FailedObj = MakeShareable(new FJsonObject);
			FailedObj->SetStringField(TEXT("error"), TEXT("Connection failed"));
			FailedArray.Add(MakeShareable(new FJsonValueObject(FailedObj)));
		}
	}

	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);

	// Build result
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetArrayField(TEXT("succeeded"), SucceededArray);
	ResultObject->SetArrayField(TEXT("failed"), FailedArray);

	TSharedPtr<FJsonObject> SummaryObject = MakeShareable(new FJsonObject);
	SummaryObject->SetNumberField(TEXT("succeeded"), SucceededArray.Num());
	SummaryObject->SetNumberField(TEXT("failed"), FailedArray.Num());
	ResultObject->SetObjectField(TEXT("summary"), SummaryObject);

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(ResultObject, CallScope.Flush());

	return MakeSuccessJson(ResultObject);
}

FString UN2CPythonBridge::SetPinValue(const FString& NodeGuid, const FString& PinGuid, const FString& Value, const FString& PinName)
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
	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(Blueprint, FocusedGraph, GraphError))
	{
		return MakeErrorJson(GraphError);
	}

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
	FString OldValue = TargetPin->GetDefaultAsString();

	// Set the new value
	const UEdGraphSchema* Schema = FocusedGraph->GetSchema();
	if (!Schema)
	{
		return MakeErrorJson(TEXT("Graph has no schema"));
	}

	TOptional<FScopedTransaction> Transaction(InPlace, LOCTEXT("PythonSetPinValue", "Python: Set Pin Value"));
	TargetNode->Modify();

	Schema->TrySetDefaultValue(*TargetPin, Value);

	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("nodeGuid"), NodeGuid);
	Data->SetStringField(TEXT("pinGuid"), PinGuid);
	Data->SetStringField(TEXT("pinName"), TargetPin->GetDisplayName().ToString());
	Data->SetStringField(TEXT("oldValue"), OldValue);
	Data->SetStringField(TEXT("newValue"), Value);

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::DeleteNodes(const FString& NodeGuidsJson, bool bPreserveConnections, bool bForce)
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

	// Get focused graph
	UBlueprint* Blueprint = nullptr;
	UEdGraph* FocusedGraph = nullptr;
	FString GraphError;
	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(Blueprint, FocusedGraph, GraphError))
	{
		return MakeErrorJson(GraphError);
	}

	// Parse GUIDs and find nodes
	TArray<UEdGraphNode*> NodesToDelete;
	TArray<FString> DeletedNodeGuids;
	TArray<FString> DeletedNodeNames;

	for (const TSharedPtr<FJsonValue>& Value : *GuidsArray)
	{
		FString GuidString;
		if (!Value->TryGetString(GuidString))
		{
			continue;
		}

		FGuid ParsedGuid;
		if (!FGuid::Parse(GuidString, ParsedGuid))
		{
			continue;
		}

		for (UEdGraphNode* Node : FocusedGraph->Nodes)
		{
			if (Node && Node->NodeGuid == ParsedGuid)
			{
				// Check if node is deletable
				if (!bForce)
				{
					if (Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_FunctionResult>())
					{
						continue; // Skip protected nodes
					}
				}

				NodesToDelete.Add(Node);
				DeletedNodeGuids.Add(GuidString);
				DeletedNodeNames.Add(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
				break;
			}
		}
	}

	if (NodesToDelete.Num() == 0)
	{
		return MakeErrorJson(TEXT("No valid nodes found to delete"));
	}

	// Delete nodes
	TOptional<FScopedTransaction> Transaction(InPlace, LOCTEXT("PythonDeleteNodes", "Python: Delete Nodes"));
	Blueprint->Modify();
	FocusedGraph->Modify();

	for (UEdGraphNode* Node : NodesToDelete)
	{
		Node->Modify();
		Node->DestroyNode();
	}

	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);

	// Build result
	TArray<TSharedPtr<FJsonValue>> DeletedArray;
	for (int32 i = 0; i < DeletedNodeGuids.Num(); ++i)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
		NodeObj->SetStringField(TEXT("guid"), DeletedNodeGuids[i]);
		NodeObj->SetStringField(TEXT("name"), DeletedNodeNames[i]);
		DeletedArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetArrayField(TEXT("deletedNodes"), DeletedArray);
	ResultObject->SetNumberField(TEXT("deletedCount"), DeletedArray.Num());

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(ResultObject, CallScope.Flush());

	return MakeSuccessJson(ResultObject);
}

FString UN2CPythonBridge::FindNodesInGraph(const FString& SearchTermsJson, const FString& SearchType, bool bCaseSensitive, int32 MaxResults)
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
	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(Blueprint, FocusedGraph, GraphError))
	{
		return MakeErrorJson(GraphError);
	}

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

FString UN2CPythonBridge::CreateCommentNode(const FString& NodeGuidsJson, const FString& CommentText, float ColorR, float ColorG, float ColorB, int32 FontSize, const FString& MoveMode, float Padding)
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
	if (!FN2CMcpBlueprintUtils::GetFocusedEditorGraph(Blueprint, FocusedGraph, GraphError))
	{
		return MakeErrorJson(GraphError);
	}

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

	UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(FocusedGraph);
	if (!CommentNode)
	{
		return MakeErrorJson(TEXT("Failed to create comment node"));
	}

	CommentNode->CreateNewGuid();
	CommentNode->NodePosX = MinX;
	CommentNode->NodePosY = MinY;
	CommentNode->NodeWidth = MaxX - MinX;
	CommentNode->NodeHeight = MaxY - MinY;

	FocusedGraph->AddNode(CommentNode, true);

	CommentNode->NodeComment = CommentText;
	CommentNode->CommentColor = FLinearColor(ColorR, ColorG, ColorB);
	CommentNode->FontSize = FMath::Clamp(FontSize, 1, 1000);
	CommentNode->MoveMode = MoveMode.Equals(TEXT("group"), ESearchCase::IgnoreCase) ? ECommentBoxMode::GroupMovement : ECommentBoxMode::NoGroupMovement;
	CommentNode->bCommentBubbleVisible_InDetailsPanel = true;
	CommentNode->bColorCommentBubble = true;

	// Associate nodes
	CommentNode->ClearNodesUnderComment();
	for (UEdGraphNode* Node : NodesToComment)
	{
		CommentNode->AddNodeUnderComment(Node);
	}
	CommentNode->OnUpdateCommentText(CommentNode->NodeComment);
	CommentNode->ReconstructNode();

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

FString UN2CPythonBridge::AddFunctionInputPin(const FString& PinName, const FString& TypeIdentifier, const FString& DefaultValue, bool bIsPassByReference, const FString& Tooltip)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty() || TypeIdentifier.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName and TypeIdentifier are required"));
	}

	// Get focused function graph
	UEdGraph* FocusedGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
	if (!FocusedGraph)
	{
		return MakeErrorJson(TEXT("No focused graph found. Please open a Blueprint function in the editor."));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Find the function entry node
	UK2Node_FunctionEntry* FunctionEntry = FN2CMcpFunctionPinUtils::FindFunctionEntryNode(FocusedGraph);
	if (!FunctionEntry)
	{
		return MakeErrorJson(TEXT("Not in a function graph. Please focus on a Blueprint function."));
	}

	// Resolve type identifier
	FEdGraphPinType PinType;
	FString ResolveError;

	if (!FN2CMcpTypeResolver::ResolvePinType(TypeIdentifier, TEXT(""), TEXT("none"), TEXT(""), bIsPassByReference, false, PinType, ResolveError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to resolve type '%s': %s"), *TypeIdentifier, *ResolveError));
	}

	// Create the pin
	TOptional<FScopedTransaction> Transaction(InPlace, FText::Format(LOCTEXT("PythonAddInputPin", "Python: Add Input Pin '{0}'"), FText::FromString(PinName)));

	FText ErrorMessage;
	if (!FunctionEntry->CanCreateUserDefinedPin(PinType, EGPD_Output, ErrorMessage))
	{
		return MakeErrorJson(FString::Printf(TEXT("Cannot create pin: %s"), *ErrorMessage.ToString()));
	}

	UEdGraphPin* NewPin = FunctionEntry->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Output, true);
	if (!NewPin)
	{
		return MakeErrorJson(TEXT("Failed to create input pin"));
	}

	// Set default value if provided
	if (!DefaultValue.IsEmpty())
	{
		TSharedPtr<FUserPinInfo>* PinInfoPtr = FunctionEntry->UserDefinedPins.FindByPredicate(
			[NewPin](const TSharedPtr<FUserPinInfo>& Info) { return Info->PinName == NewPin->PinName; });
		if (PinInfoPtr && PinInfoPtr->IsValid())
		{
			FunctionEntry->ModifyUserDefinedPinDefaultValue(*PinInfoPtr, DefaultValue);
		}
	}

	// Set tooltip
	if (!Tooltip.IsEmpty())
	{
		FN2CMcpFunctionPinUtils::SetPinTooltip(FunctionEntry, NewPin, Tooltip);
	}

	// Update function call sites
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionEntry);
	if (Blueprint)
	{
		FN2CMcpFunctionPinUtils::UpdateFunctionCallSites(FocusedGraph, Blueprint);
		FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("pinName"), PinName);
	Data->SetStringField(TEXT("actualName"), NewPin->PinName.ToString());
	Data->SetStringField(TEXT("pinGuid"), NewPin->PinId.ToString());
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());
	Data->SetStringField(TEXT("typeIdentifier"), TypeIdentifier);

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::AddFunctionReturnPin(const FString& PinName, const FString& TypeIdentifier, const FString& Tooltip)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty() || TypeIdentifier.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName and TypeIdentifier are required"));
	}

	// Get focused function graph
	UEdGraph* FocusedGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
	if (!FocusedGraph)
	{
		return MakeErrorJson(TEXT("No focused graph found. Please open a Blueprint function in the editor."));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Find or create the function result node
	UK2Node_FunctionResult* FunctionResult = FN2CMcpFunctionPinUtils::EnsureFunctionResultNode(FocusedGraph);
	if (!FunctionResult)
	{
		return MakeErrorJson(TEXT("Failed to find or create function result node"));
	}

	// Resolve type identifier
	FEdGraphPinType PinType;
	FString ResolveError;

	if (!FN2CMcpTypeResolver::ResolvePinType(TypeIdentifier, TEXT(""), TEXT("none"), TEXT(""), false, false, PinType, ResolveError))
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to resolve type '%s': %s"), *TypeIdentifier, *ResolveError));
	}

	// Create the pin
	TOptional<FScopedTransaction> Transaction(InPlace, FText::Format(LOCTEXT("PythonAddReturnPin", "Python: Add Return Pin '{0}'"), FText::FromString(PinName)));

	FText ErrorMessage;
	if (!FunctionResult->CanCreateUserDefinedPin(PinType, EGPD_Input, ErrorMessage))
	{
		return MakeErrorJson(FString::Printf(TEXT("Cannot create return pin: %s"), *ErrorMessage.ToString()));
	}

	UEdGraphPin* NewPin = FunctionResult->CreateUserDefinedPin(FName(*PinName), PinType, EGPD_Input, true);
	if (!NewPin)
	{
		return MakeErrorJson(TEXT("Failed to create return pin"));
	}

	// Set tooltip
	if (!Tooltip.IsEmpty())
	{
		FN2CMcpFunctionPinUtils::SetPinTooltip(FunctionResult, NewPin, Tooltip);
	}

	// Update function call sites
	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionResult);
	if (Blueprint)
	{
		FN2CMcpFunctionPinUtils::UpdateFunctionCallSites(FocusedGraph, Blueprint);
		FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("pinName"), PinName);
	Data->SetStringField(TEXT("actualName"), NewPin->PinName.ToString());
	Data->SetStringField(TEXT("pinGuid"), NewPin->PinId.ToString());
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());
	Data->SetStringField(TEXT("typeIdentifier"), TypeIdentifier);

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::RemoveFunctionEntryPin(const FString& PinName)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName is required"));
	}

	// Get focused function graph
	UEdGraph* FocusedGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
	if (!FocusedGraph)
	{
		return MakeErrorJson(TEXT("No focused graph found. Please open a Blueprint function in the editor."));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Find the function entry node
	UK2Node_FunctionEntry* FunctionEntry = FN2CMcpFunctionPinUtils::FindFunctionEntryNode(FocusedGraph);
	if (!FunctionEntry)
	{
		return MakeErrorJson(TEXT("No function entry node found in the graph"));
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionEntry);
	if (!Blueprint)
	{
		return MakeErrorJson(TEXT("Cannot find Blueprint for function"));
	}

	TOptional<FScopedTransaction> Transaction(InPlace, FText::Format(LOCTEXT("PythonRemoveEntryPin", "Python: Remove Entry Pin '{0}'"), FText::FromString(PinName)));
	FunctionEntry->Modify();

	FString RemovalError;
	if (!FN2CMcpFunctionPinUtils::RemoveFunctionPin(FunctionEntry, PinName, RemovalError))
	{
		return MakeErrorJson(RemovalError);
	}

	FN2CMcpFunctionPinUtils::UpdateFunctionCallSites(FocusedGraph, Blueprint);
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removedPin"), PinName);
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());

	Transaction.Reset(); // close the undo transaction before compiling
	AddCompileResult(Data, CallScope.Flush());

	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::RemoveFunctionReturnPin(const FString& PinName)
{
	// One compile for this call, after its transaction closes (inside run-python, at the end of the tool call)
	FN2CMcpToolCallScope CallScope;

	if (PinName.IsEmpty())
	{
		return MakeErrorJson(TEXT("PinName is required"));
	}

	// Get focused function graph
	UEdGraph* FocusedGraph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
	if (!FocusedGraph)
	{
		return MakeErrorJson(TEXT("No focused graph found. Please open a Blueprint function in the editor."));
	}

	// Check if this is a K2 graph
	if (!FocusedGraph->GetSchema()->IsA<UEdGraphSchema_K2>())
	{
		return MakeErrorJson(TEXT("The focused graph is not a Blueprint graph"));
	}

	// Find the function result node
	UK2Node_FunctionResult* FunctionResult = FN2CMcpFunctionPinUtils::FindFunctionResultNode(FocusedGraph);
	if (!FunctionResult)
	{
		return MakeErrorJson(TEXT("No function result node found. This function has no return values."));
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(FunctionResult);
	if (!Blueprint)
	{
		return MakeErrorJson(TEXT("Cannot find Blueprint for function"));
	}

	TOptional<FScopedTransaction> Transaction(InPlace, FText::Format(LOCTEXT("PythonRemoveReturnPin", "Python: Remove Return Pin '{0}'"), FText::FromString(PinName)));
	FunctionResult->Modify();

	FString RemovalError;
	if (!FN2CMcpFunctionPinUtils::RemoveFunctionPin(FunctionResult, PinName, RemovalError))
	{
		return MakeErrorJson(RemovalError);
	}

	FN2CMcpFunctionPinUtils::UpdateFunctionCallSites(FocusedGraph, Blueprint);
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("removedPin"), PinName);
	Data->SetStringField(TEXT("functionName"), FocusedGraph->GetName());

	Transaction.Reset(); // close the undo transaction before compiling
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
