// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CPythonBridge.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "LLM/N2CModelCatalog.h"
#include "Core/N2CGraphStateManager.h"
#include "Subsystems/N2CEditorSubsystem.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolsets.h"
#include "MCP/Tools/N2CToolCatalog.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "MCP/Tools/Implementations/Translation/N2CMcpGetAvailableTranslationTargetsTool.h"
#include "MCP/Tools/Implementations/Dependencies/N2CMcpDependencies.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CJsonContract.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/Optional.h"

#define LOCTEXT_NAMESPACE "NodeToCode"

namespace N2CPythonBridgePrivate
{
	/** The name of the loaded Blueprint at an object path (its own name: the path cannot be split where the name has a '.' or ':') */
	FString BlueprintNameAt(const FString& ObjectPath)
	{
		const UBlueprint* Blueprint = FindObject<UBlueprint>(nullptr, *ObjectPath);
		return Blueprint ? Blueprint->GetName() : FPackageName::ObjectPathToObjectName(ObjectPath);
	}
}
using N2CPythonBridgePrivate::BlueprintNameAt;

namespace N2CPythonBridgePrivate
{
	/**
	 * A tool manager that serves every registered tool except the PythonTools toolset, whatever the MCP server
	 * serves (card 0342: in the Python Scripting profile the server's manager holds only the Python tools). Built on
	 * the first InvokeTool; it shares the registry's tool instances and is never listed to an MCP client.
	 */
	FN2CMcpToolManager& InvokeToolManager()
	{
		static FN2CMcpToolManager Manager;
		static bool bConfigured = false;
		if (!bConfigured)
		{
			bConfigured = true;
			FN2CMcpToolConfig Config;
			for (const FString& Name : N2CMcpToolsets::AllNames())
			{
				if (!Name.Equals(N2CMcpToolsets::Python.Name, ESearchCase::IgnoreCase))
				{
					Config.Toolsets.Add(Name);
				}
			}
			Manager.Configure(Config);
		}
		return Manager;
	}

	/** True when a tool of the PythonTools toolset has this name (run-python) */
	bool IsPythonToolsetTool(const FString& ToolName)
	{
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (Tool.IsValid())
			{
				const FMcpToolDefinition Definition = Tool->GetDefinition();
				if (Definition.Name == ToolName)
				{
					return Definition.Toolset == &N2CMcpToolsets::Python;
				}
			}
		}
		return false;
	}

	FString InvokeToolError(EN2CToolErrorCode Code, const FString& Message, const FString& Field = FString())
	{
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetBoolField(TEXT("success"), false);
		Response->SetField(TEXT("data"), MakeShared<FJsonValueNull>());
		Response->SetStringField(TEXT("error"), Message);
		Response->SetStringField(TEXT("code"), FN2CToolError::CodeName(Code));
		if (!Field.IsEmpty())
		{
			Response->SetStringField(TEXT("field"), Field);
		}
		FString Text;
		FJsonSerializer::Serialize(Response.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}
}

FString UN2CPythonBridge::InvokeTool(const FString& ToolName, const FString& ArgumentsJson)
{
	using namespace N2CPythonBridgePrivate;
	const FString Name = ToolName.TrimStartAndEnd();
	if (!IsInGameThread())
	{
		return InvokeToolError(EN2CToolErrorCode::NotRunnableHere, TEXT("InvokeTool runs on the game thread only"));
	}
	if (Name.IsEmpty())
	{
		return InvokeToolError(EN2CToolErrorCode::InvalidArgument, TEXT("No tool name given"), TEXT("tool_name"));
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	const FString ArgumentsText = ArgumentsJson.TrimStartAndEnd();
	if (!ArgumentsText.IsEmpty())
	{
		TSharedPtr<FJsonValue> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsText);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid() || Parsed->Type != EJson::Object)
		{
			return InvokeToolError(EN2CToolErrorCode::InvalidArgument,
				TEXT("arguments_json is not a JSON object: pass the tool's arguments as {\"name\": value, ...}"),
				TEXT("arguments_json"));
		}
		Arguments = Parsed->AsObject();
	}

	return InvokeToolWith(Name, Arguments.ToSharedRef());
}

FString UN2CPythonBridge::InvokeToolWith(const FString& Name, const TSharedRef<FJsonObject>& Arguments)
{
	using namespace N2CPythonBridgePrivate;
	if (!IsInGameThread())
	{
		return InvokeToolError(EN2CToolErrorCode::NotRunnableHere, TEXT("InvokeTool runs on the game thread only"));
	}
	const FN2CToolOutcome Outcome = FN2CToolCatalog(InvokeToolManager()).InvokeNow(Name, Arguments);
	if (Outcome.IsError() && Outcome.Error->Code == EN2CToolErrorCode::UnknownTool && IsPythonToolsetTool(Name))
	{
		return InvokeToolError(EN2CToolErrorCode::UnknownTool, FString::Printf(
			TEXT("'%s' is a PythonTools tool, which InvokeTool does not run: you are in Python already, so run the code directly"),
			*Name));
	}
	if (Outcome.IsError())
	{
		return InvokeToolError(Outcome.Error->Code, Outcome.Error->Message, Outcome.Error->Field);
	}

	const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), true);
	Response->SetField(TEXT("data"), Outcome.Value.IsValid() ? Outcome.Value : MakeShared<FJsonValueNull>());
	Response->SetField(TEXT("error"), MakeShared<FJsonValueNull>());
	FString Text;
	FJsonSerializer::Serialize(Response.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
	return Text;
}

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
	// The capture get-focused-blueprint and get-blueprint-n2cjson run (card 0343): the default events the editor placed
	// stay visible, marked disabled, so a script finds and wires a new Blueprint's BeginPlay instead of adding a second
	// one; a graph of only those events is three disabled nodes, not an error (cards 0137, 0433)
	if (!FocusedGraph)
	{
		return MakeErrorJson(TEXT("NO_FOCUSED_GRAPH: no graph is focused"));
	}
	const TValueOrError<FN2CCapture, FString> Captured = N2CMcpBlueprintExport::CaptureGraph(*FocusedGraph);
	if (Captured.HasError())
	{
		return MakeErrorJson(Captured.GetError());
	}
	const FN2CCapture& Capture = Captured.GetValue();

	// This document keeps the serializer's own form (each node's and pin's "id"), unlike the tools' "ids" objects
	const FString JsonOutput = FN2CEditorIntegration::Get().SerializeN2CBlueprintToJson(Capture.Blueprint, false);
	TSharedPtr<FJsonObject> N2CJsonObject;
	const TSharedRef<TJsonReader<>> N2CJsonReader = TJsonReaderFactory<>::Create(JsonOutput);
	if (JsonOutput.IsEmpty() || !FJsonSerializer::Deserialize(N2CJsonReader, N2CJsonObject) || !N2CJsonObject.IsValid())
	{
		return MakeErrorJson(TEXT("CAPTURE_FAILED: the captured graph could not be written as N2C JSON"));
	}

	// node_count: the nodes of the focused graph itself (graphs it calls, captured at depth, are not counted)
	int32 NodeCount = 0;
	for (const FN2CGraph& Graph : Capture.Blueprint.Graphs)
	{
		if (Graph.Name == FocusedGraph->GetName())
		{
			NodeCount = Graph.Nodes.Num();
			break;
		}
	}
	if (NodeCount == 0 && Capture.Blueprint.Graphs.Num() > 0)
	{
		NodeCount = Capture.Blueprint.Graphs[0].Nodes.Num();
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), OwningBlueprint ? OwningBlueprint->GetName() : TEXT("Unknown"));
	Data->SetStringField(TEXT("path"), OwningBlueprint ? OwningBlueprint->GetPathName() : TEXT(""));
	Data->SetStringField(TEXT("graph_name"), FocusedGraph->GetName());
	Data->SetNumberField(TEXT("node_count"), NodeCount);
	Data->SetObjectField(TEXT("n2c_json"), N2CJsonObject);
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

FString UN2CPythonBridge::GetBlueprintDependencies(const TArray<FString>& Paths, bool bIncludeCallGraph,
	bool bIncludeReferenced, bool bToFile)
{
	N2CMcpDependencies::FRequest Request;
	Request.Paths = Paths;
	Request.bIncludeCallGraph = bIncludeCallGraph;
	Request.bIncludeReferenced = bIncludeReferenced;
	Request.bToFile = bToFile;
	FString Error;
	const TSharedPtr<FJsonObject> Json = N2CMcpDependencies::Run(Request, Error);
	return Json.IsValid() ? MakeSuccessJson(Json) : MakeErrorJson(Error);
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
	Data->SetStringField(TEXT("blueprint_path"), OwningBlueprint->GetPathName()); // the Blueprint acted on (card 0297)
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
	// The Blueprint BlueprintPath names, else the focused one (card 0274); the save UN2CEditorSubsystem::SaveBlueprint runs (card 0301)
	FN2CSaveResult Saved;
	FString Error;
	const bool bOk = UN2CEditorSubsystem::SaveBlueprintCore(BlueprintPath, bOnlyIfDirty, Saved, Error);
	if (Saved.BlueprintPath.IsEmpty())
	{
		return MakeErrorJson(Error);
	}
	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("blueprint_name"), BlueprintNameAt(Saved.BlueprintPath));
	Data->SetStringField(TEXT("blueprint_path"), Saved.BlueprintPath); // the Blueprint acted on (card 0297)
	Data->SetBoolField(TEXT("was_dirty"), Saved.bWasDirty);
	Data->SetBoolField(TEXT("was_saved"), Saved.bWasSaved);
	return bOk ? MakeSuccessJson(Data) : MakeErrorJson(TEXT("Failed to save Blueprint"), Data);
}

// ========== Tagging System ==========

FString UN2CPythonBridge::TagFocusedGraph(const FString& Tag, const FString& Category, const FString& Description,
	const FString& BlueprintPath, const FString& Graph)
{
	// The graph BlueprintPath / Graph name, else the focused one (card 0274); the tagging UN2CEditorSubsystem::TagGraph runs (card 0301)
	FN2CTagInfo Tagged;
	FString Error;
	if (!UN2CEditorSubsystem::TagGraphCore(BlueprintPath, Graph, Tag, Category, Description, Tagged, Error))
	{
		return MakeErrorJson(Error);
	}
	FGuid GraphGuid;
	FGuid::Parse(Tagged.GraphGuid, GraphGuid);
	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("tag"), Tagged.Tag);
	Data->SetStringField(TEXT("category"), Tagged.Category);
	Data->SetStringField(TEXT("description"), Tagged.Description);
	Data->SetStringField(TEXT("graph_guid"), GraphGuid.ToString());
	Data->SetStringField(TEXT("graph_name"), Tagged.GraphName);
	Data->SetStringField(TEXT("blueprint_name"), BlueprintNameAt(Tagged.BlueprintPath));
	// With graph_guid, what RemoveTag takes to name this graph even when a copy of the Blueprint shares its GUID
	Data->SetStringField(TEXT("blueprint_path"), Tagged.BlueprintPath);
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::ListTags(const FString& Category, const FString& Tag, const FString& BlueprintPath)
{
	// The tags UN2CEditorSubsystem::ListTags returns (card 0301)
	const TArray<FN2CTagInfo> Tags = UN2CEditorSubsystem::ListTagsCore(Category, Tag, BlueprintPath);
	TArray<TSharedPtr<FJsonValue>> TagsArray;
	for (const FN2CTagInfo& TagInfo : Tags)
	{
		FGuid GraphGuid;
		FGuid::Parse(TagInfo.GraphGuid, GraphGuid);
		const TSharedPtr<FJsonObject> TagObject = MakeShared<FJsonObject>();
		TagObject->SetStringField(TEXT("tag"), TagInfo.Tag);
		TagObject->SetStringField(TEXT("category"), TagInfo.Category);
		TagObject->SetStringField(TEXT("description"), TagInfo.Description);
		TagObject->SetStringField(TEXT("graph_guid"), GraphGuid.ToString());
		TagObject->SetStringField(TEXT("graph_name"), TagInfo.GraphName);
		TagObject->SetStringField(TEXT("blueprint_path"), TagInfo.BlueprintPath);
		TagObject->SetStringField(TEXT("timestamp"), TagInfo.Timestamp.ToString());
		TagsArray.Add(MakeShared<FJsonValueObject>(TagObject));
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("tags"), TagsArray);
	Data->SetNumberField(TEXT("count"), Tags.Num());
	Data->SetNumberField(TEXT("total_categories"), UN2CGraphStateManager::Get().GetAllCategories().Num());
	Data->SetNumberField(TEXT("total_unique_tags"), UN2CGraphStateManager::Get().GetAllTagNames().Num());
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::RemoveTag(const FString& GraphGuid, const FString& Tag, const FString& BlueprintPath)
{
	if (GraphGuid.IsEmpty() || Tag.IsEmpty())
	{
		return MakeErrorJson(TEXT("GraphGuid and Tag cannot be empty"));
	}
	FGuid ParsedGuid;
	if (!FGuid::Parse(GraphGuid, ParsedGuid))
	{
		return MakeErrorJson(FString::Printf(TEXT("Invalid GUID format: %s"), *GraphGuid));
	}
	// The removal UN2CEditorSubsystem::RemoveTag runs (card 0301)
	FN2CTagRemoval Removed;
	FString Error;
	if (!UN2CEditorSubsystem::RemoveTagCore(BlueprintPath, ParsedGuid, Tag, Removed, Error))
	{
		return MakeErrorJson(Error);
	}
	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("removed"), true);
	Data->SetNumberField(TEXT("removed_count"), Removed.RemovedCount);
	Data->SetStringField(TEXT("tag"), Tag);
	Data->SetStringField(TEXT("graph_guid"), GraphGuid);
	Data->SetStringField(TEXT("blueprint_path"), Removed.BlueprintPath);
	Data->SetNumberField(TEXT("remaining_tags"), Removed.RemainingCount);
	return MakeSuccessJson(Data);
}

// ========== LLM Provider Info ==========

namespace N2CPythonBridgePrivate
{
	/** The model catalog's profile of a provider (Content/Data/N2CModelCatalog.json, card 0343: no table of its own) */
	const FN2CProviderProfile* ProviderProfile(EN2CLLMProvider Provider)
	{
		for (const FN2CProviderProfile& Profile : FN2CModelCatalog::Get().Providers())
		{
			if (Profile.Provider == Provider)
			{
				return &Profile;
			}
		}
		return nullptr;
	}

	/** The provider's name as the settings enum spells it: OpenAI, Anthropic, Gemini, Ollama, DeepSeek, LMStudio */
	FString ProviderName(EN2CLLMProvider Provider)
	{
		return StaticEnum<EN2CLLMProvider>()->GetNameStringByValue(static_cast<int64>(Provider));
	}

	/**
	 * Where the provider's requests go: the catalog's base URL and path, or for Ollama and LM Studio the user's endpoint
	 * with the path added when it lacks it (as their services do)
	 */
	FString ProviderEndpoint(EN2CLLMProvider Provider, const FN2CProviderProfile& Profile)
	{
		FString Override;
		if (Provider == EN2CLLMProvider::Ollama)
		{
			Override = GetDefault<UN2CSettings>()->OllamaConfig.OllamaEndpoint;
		}
		else if (Provider == EN2CLLMProvider::LMStudio)
		{
			Override = GetDefault<UN2CUserSettings>()->LMStudioEndpoint;
		}
		Override.TrimStartAndEndInline();
		if (Override.IsEmpty())
		{
			return Profile.BaseUrl + Profile.Path;
		}
		Override.RemoveFromEnd(TEXT("/"));
		return Override.EndsWith(Profile.Path) ? Override : Override + Profile.Path;
	}
}

FString UN2CPythonBridge::GetLLMProviders()
{
	using namespace N2CPythonBridgePrivate;
	const EN2CLLMProvider Current = GetDefault<UN2CUserSettings>()->Provider;
	TArray<TSharedPtr<FJsonValue>> ProvidersArray;
	const UEnum* Enum = StaticEnum<EN2CLLMProvider>();
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		const EN2CLLMProvider Provider = static_cast<EN2CLLMProvider>(Enum->GetValueByIndex(Index));
		const FN2CProviderProfile* Profile = ProviderProfile(Provider);
		const TSharedPtr<FJsonObject> ProviderObject = MakeShared<FJsonObject>();
		ProviderObject->SetStringField(TEXT("name"), ProviderName(Provider));
		ProviderObject->SetStringField(TEXT("display_name"), Profile ? Profile->DisplayName : Enum->GetDisplayNameTextByIndex(Index).ToString());
		ProviderObject->SetBoolField(TEXT("is_local"), Profile && Profile->bIsLocal);
		ProviderObject->SetBoolField(TEXT("is_current"), Provider == Current);
		ProvidersArray.Add(MakeShared<FJsonValueObject>(ProviderObject));
	}

	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetArrayField(TEXT("providers"), ProvidersArray);
	Data->SetStringField(TEXT("current_provider"), ProviderName(Current));
	Data->SetNumberField(TEXT("provider_count"), ProvidersArray.Num());
	return MakeSuccessJson(Data);
}

FString UN2CPythonBridge::GetActiveProvider()
{
	using namespace N2CPythonBridgePrivate;
	const EN2CLLMProvider Current = GetDefault<UN2CUserSettings>()->Provider;
	const FN2CProviderProfile* Profile = ProviderProfile(Current);
	if (!Profile)
	{
		return MakeErrorJson(FString::Printf(TEXT("The model catalog has no provider %s"), *ProviderName(Current)));
	}
	const TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), ProviderName(Current));
	Data->SetStringField(TEXT("display_name"), Profile->DisplayName);
	Data->SetStringField(TEXT("model"), GetDefault<UN2CSettings>()->GetActiveModelForProvider(Current));
	Data->SetStringField(TEXT("endpoint"), ProviderEndpoint(Current, *Profile));
	Data->SetBoolField(TEXT("is_local"), Profile->bIsLocal);
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

// ========== MCP runner ==========

namespace N2CPythonBridgePrivate
{
	/** Answers of nodetocode._mcp_runner by call id (game thread only, like every Python call) */
	TMap<FString, FString>& McpResults()
	{
		static TMap<FString, FString> Results;
		return Results;
	}
}

void UN2CPythonBridge::SubmitMcpResult(const FString& CallId, const FString& ResultJson)
{
	check(IsInGameThread());
	N2CPythonBridgePrivate::McpResults().Add(CallId, ResultJson);
}

bool UN2CPythonBridge::TakeMcpResult(const FString& CallId, FString& OutResultJson)
{
	check(IsInGameThread());
	TMap<FString, FString>& Results = N2CPythonBridgePrivate::McpResults();
	const bool bFound = Results.RemoveAndCopyValue(CallId, OutResultJson);
	// An answer nobody takes (a script that called SubmitMcpResult itself) does not pile up
	Results.Reset();
	return bFound;
}

// The graph-editing and navigation functions below forward to their MCP tool through InvokeToolWith (card 0343): the
// tool is the one implementation, and each returns InvokeTool's answer, {success, data: <the tool's result>, error}

namespace N2CPythonBridgePrivate
{
	/** A tool's arguments with the target arguments (blueprintPath, graphGuid or graphName) a bridge call gives */
	TSharedRef<FJsonObject> TargetArguments(const FString& BlueprintPath, const FString& Graph)
	{
		const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
		const FN2CGraphTargetArgs Target = FN2CGraphTargetArgs::FromPathAndGraph(BlueprintPath, Graph);
		if (!Target.BlueprintPath.IsEmpty())
		{
			Arguments->SetStringField(TEXT("blueprintPath"), Target.BlueprintPath);
		}
		if (!Target.GraphGuid.IsEmpty())
		{
			Arguments->SetStringField(TEXT("graphGuid"), Target.GraphGuid);
		}
		else if (!Target.GraphName.IsEmpty())
		{
			Arguments->SetStringField(TEXT("graphName"), Target.GraphName);
		}
		return Arguments;
	}

	/** Parses a JSON array argument; false, with OutError an InvokeTool INVALID_ARGUMENT answer, when Text is not one */
	bool ParseArrayArgument(const FString& Text, const TCHAR* Field, TArray<TSharedPtr<FJsonValue>>& OutArray, FString& OutError)
	{
		TSharedPtr<FJsonValue> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid() || Parsed->Type != EJson::Array)
		{
			OutError = InvokeToolError(EN2CToolErrorCode::InvalidArgument, FString::Printf(TEXT("%s is not a JSON array"), Field), Field);
			return false;
		}
		OutArray = Parsed->AsArray();
		return true;
	}
	/**
	 * InvokeToolWith, plus the rule nodetocode's wrappers apply (utils._invoke): a tool that reports a failure inside
	 * its value ("success": false, as compile-blueprint does for compile errors) makes the answer a failure, with the
	 * tool's message as "error" and its value kept as "data"
	 */
	FString ForwardToTool(const TCHAR* ToolName, const TSharedRef<FJsonObject>& Arguments)
	{
		const FString Answer = UN2CPythonBridge::InvokeToolWith(ToolName, Arguments);
		TSharedPtr<FJsonObject> Response;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Answer);
		const TSharedPtr<FJsonObject>* Data = nullptr;
		bool bToolSuccess = true;
		if (!FJsonSerializer::Deserialize(Reader, Response) || !Response.IsValid() || !Response->GetBoolField(TEXT("success"))
			|| !Response->TryGetObjectField(TEXT("data"), Data) || !(*Data)->TryGetBoolField(TEXT("success"), bToolSuccess) || bToolSuccess)
		{
			return Answer;
		}
		FString Message;
		if (!(*Data)->TryGetStringField(TEXT("message"), Message) && !(*Data)->TryGetStringField(TEXT("error"), Message))
		{
			Message = FString::Printf(TEXT("%s failed"), ToolName);
		}
		Response->SetBoolField(TEXT("success"), false);
		Response->SetStringField(TEXT("error"), Message);
		FString Text;
		FJsonSerializer::Serialize(Response.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}
}
using N2CPythonBridgePrivate::ForwardToTool;
using N2CPythonBridgePrivate::TargetArguments;
using N2CPythonBridgePrivate::ParseArrayArgument;

// ========== Blueprint Editor Navigation ==========

FString UN2CPythonBridge::OpenBlueprint(const FString& BlueprintPath, const FString& FocusGraph)
{
	const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("asset_path"), BlueprintPath.TrimStartAndEnd());
	if (!FocusGraph.TrimStartAndEnd().IsEmpty())
	{
		Arguments->SetStringField(TEXT("focus_graph"), FocusGraph.TrimStartAndEnd());
	}
	return ForwardToTool(TEXT("open-blueprint-asset"), Arguments);
}

FString UN2CPythonBridge::OpenBlueprintFunction(const FString& FunctionName)
{
	const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("graphName"), FunctionName.TrimStartAndEnd());
	return ForwardToTool(TEXT("open-blueprint-function"), Arguments);
}

// ========== Blueprint Graph Editing ==========

FString UN2CPythonBridge::SearchBlueprintNodes(const FString& SearchTerm, bool bContextSensitive, int32 MaxResults,
	const FString& CategoryFilter, bool bExcludeVMFunctions, const FString& BlueprintPath, const FString& Graph)
{
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetStringField(TEXT("searchTerm"), SearchTerm.TrimStartAndEnd());
	Arguments->SetBoolField(TEXT("contextSensitive"), bContextSensitive);
	Arguments->SetNumberField(TEXT("maxResults"), MaxResults);
	Arguments->SetBoolField(TEXT("excludeVMFunctions"), bExcludeVMFunctions);
	if (!CategoryFilter.TrimStartAndEnd().IsEmpty())
	{
		Arguments->SetStringField(TEXT("category"), CategoryFilter.TrimStartAndEnd());
	}
	return ForwardToTool(TEXT("search-blueprint-nodes"), Arguments);
}

FString UN2CPythonBridge::AddNodeToGraph(const FString& NodeName, const FString& ActionIdentifier, float LocationX, float LocationY,
	const FString& BlueprintPath, const FString& Graph)
{
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetStringField(TEXT("nodeName"), NodeName.TrimStartAndEnd());
	Arguments->SetStringField(TEXT("actionIdentifier"), ActionIdentifier.TrimStartAndEnd());
	const TSharedRef<FJsonObject> Location = MakeShared<FJsonObject>();
	Location->SetNumberField(TEXT("x"), LocationX);
	Location->SetNumberField(TEXT("y"), LocationY);
	Arguments->SetObjectField(TEXT("location"), Location);
	return ForwardToTool(TEXT("add-bp-node-to-active-graph"), Arguments);
}

FString UN2CPythonBridge::ConnectPins(const FString& ConnectionsJson, bool bBreakExistingLinks,
	const FString& BlueprintPath, const FString& Graph)
{
	TArray<TSharedPtr<FJsonValue>> Connections;
	FString Error;
	if (!ParseArrayArgument(ConnectionsJson, TEXT("connections"), Connections, Error))
	{
		return Error;
	}
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetArrayField(TEXT("connections"), Connections);
	const TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
	Options->SetBoolField(TEXT("breakExistingLinks"), bBreakExistingLinks);
	Arguments->SetObjectField(TEXT("options"), Options);
	return ForwardToTool(TEXT("connect-pins"), Arguments);
}

FString UN2CPythonBridge::SetPinValue(const FString& NodeGuid, const FString& PinGuid, const FString& Value, const FString& PinName,
	const FString& BlueprintPath, const FString& Graph)
{
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetStringField(TEXT("nodeGuid"), NodeGuid.TrimStartAndEnd());
	Arguments->SetStringField(TEXT("pinGuid"), PinGuid.TrimStartAndEnd());
	Arguments->SetStringField(TEXT("value"), Value);
	if (!PinName.TrimStartAndEnd().IsEmpty())
	{
		Arguments->SetStringField(TEXT("pinName"), PinName.TrimStartAndEnd());
	}
	return ForwardToTool(TEXT("set-input-pin-value"), Arguments);
}

FString UN2CPythonBridge::DeleteNodes(const FString& NodeGuidsJson, bool bPreserveConnections, bool bForce,
	const FString& BlueprintPath, const FString& Graph)
{
	TArray<TSharedPtr<FJsonValue>> NodeGuids;
	FString Error;
	if (!ParseArrayArgument(NodeGuidsJson, TEXT("nodeGuids"), NodeGuids, Error))
	{
		return Error;
	}
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetArrayField(TEXT("nodeGuids"), NodeGuids);
	Arguments->SetBoolField(TEXT("preserveConnections"), bPreserveConnections);
	Arguments->SetBoolField(TEXT("force"), bForce);
	return ForwardToTool(TEXT("delete-blueprint-node"), Arguments);
}

FString UN2CPythonBridge::FindNodesInGraph(const FString& SearchTermsJson, const FString& SearchType, bool bCaseSensitive, int32 MaxResults,
	const FString& BlueprintPath, const FString& Graph)
{
	TArray<TSharedPtr<FJsonValue>> SearchTerms;
	FString Error;
	if (!ParseArrayArgument(SearchTermsJson, TEXT("searchTerms"), SearchTerms, Error))
	{
		return Error;
	}
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetArrayField(TEXT("searchTerms"), SearchTerms);
	Arguments->SetStringField(TEXT("searchType"), SearchType);
	Arguments->SetBoolField(TEXT("caseSensitive"), bCaseSensitive);
	Arguments->SetNumberField(TEXT("maxResults"), MaxResults);
	return ForwardToTool(TEXT("find-nodes-in-graph"), Arguments);
}

FString UN2CPythonBridge::CreateCommentNode(const FString& NodeGuidsJson, const FString& CommentText, float ColorR, float ColorG, float ColorB,
	int32 FontSize, const FString& MoveMode, float Padding, const FString& BlueprintPath, const FString& Graph)
{
	TArray<TSharedPtr<FJsonValue>> NodeGuids;
	FString Error;
	if (!ParseArrayArgument(NodeGuidsJson, TEXT("nodeGuids"), NodeGuids, Error))
	{
		return Error;
	}
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetArrayField(TEXT("nodeGuids"), NodeGuids);
	Arguments->SetStringField(TEXT("commentText"), CommentText);
	const TSharedRef<FJsonObject> Color = MakeShared<FJsonObject>();
	Color->SetNumberField(TEXT("r"), ColorR);
	Color->SetNumberField(TEXT("g"), ColorG);
	Color->SetNumberField(TEXT("b"), ColorB);
	Arguments->SetObjectField(TEXT("color"), Color);
	Arguments->SetNumberField(TEXT("fontSize"), FontSize);
	Arguments->SetStringField(TEXT("moveMode"), MoveMode);
	Arguments->SetNumberField(TEXT("padding"), Padding);
	return ForwardToTool(TEXT("create-comment-node"), Arguments);
}

// ========== Function Pin Management ==========

FString UN2CPythonBridge::AddFunctionInputPin(const FString& PinName, const FString& TypeIdentifier, const FString& DefaultValue,
	bool bIsPassByReference, const FString& Tooltip, const FString& BlueprintPath, const FString& Graph)
{
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetStringField(TEXT("pinName"), PinName.TrimStartAndEnd());
	Arguments->SetStringField(TEXT("typeIdentifier"), TypeIdentifier.TrimStartAndEnd());
	Arguments->SetStringField(TEXT("defaultValue"), DefaultValue);
	Arguments->SetBoolField(TEXT("isPassByReference"), bIsPassByReference);
	Arguments->SetStringField(TEXT("tooltip"), Tooltip);
	return ForwardToTool(TEXT("add-function-input-pin"), Arguments);
}

FString UN2CPythonBridge::AddFunctionReturnPin(const FString& PinName, const FString& TypeIdentifier, const FString& Tooltip,
	const FString& BlueprintPath, const FString& Graph)
{
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetStringField(TEXT("pinName"), PinName.TrimStartAndEnd());
	Arguments->SetStringField(TEXT("typeIdentifier"), TypeIdentifier.TrimStartAndEnd());
	Arguments->SetStringField(TEXT("tooltip"), Tooltip);
	return ForwardToTool(TEXT("add-function-return-pin"), Arguments);
}

FString UN2CPythonBridge::RemoveFunctionEntryPin(const FString& PinName, const FString& BlueprintPath, const FString& Graph)
{
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetStringField(TEXT("pinName"), PinName.TrimStartAndEnd());
	return ForwardToTool(TEXT("remove-function-entry-pin"), Arguments);
}

FString UN2CPythonBridge::RemoveFunctionReturnPin(const FString& PinName, const FString& BlueprintPath, const FString& Graph)
{
	const TSharedRef<FJsonObject> Arguments = TargetArguments(BlueprintPath, Graph);
	Arguments->SetStringField(TEXT("pinName"), PinName.TrimStartAndEnd());
	return ForwardToTool(TEXT("remove-function-return-pin"), Arguments);
}

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
