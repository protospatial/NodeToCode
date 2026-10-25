// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpOpenBlueprintFunctionTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Blueprint.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "K2Node_FunctionEntry.h"
#include "EdGraphSchema_K2.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register this tool with the MCP tool manager
REGISTER_MCP_TOOL(FN2CMcpOpenBlueprintFunctionTool)

FMcpToolDefinition FN2CMcpOpenBlueprintFunctionTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("open-blueprint-function"),
		TEXT("Opens a Blueprint function in the editor and focuses it, named by its functionGuid (or graphName) in blueprintPath or the focused Blueprint"),
		TEXT("Blueprint Function Management")
	);
	
	// Define input schema
	TSharedPtr<FJsonObject> InputSchema = MakeShareable(new FJsonObject);
	InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// functionGuid parameter (required)
	TSharedPtr<FJsonObject> FunctionGuid = MakeShareable(new FJsonObject);
	FunctionGuid->SetStringField(TEXT("type"), TEXT("string"));
	FunctionGuid->SetStringField(TEXT("description"), TEXT("The function's ID (functionGuid from create-blueprint-function, guid from list-blueprint-functions): its graph GUID, stable across renames and moves. Same as graphGuid. Pass this or graphName."));
	Properties->SetObjectField(TEXT("functionGuid"), FunctionGuid);
	
	// blueprintPath parameter (optional)
	TSharedPtr<FJsonObject> BlueprintPath = MakeShareable(new FJsonObject);
	BlueprintPath->SetStringField(TEXT("type"), TEXT("string"));
	BlueprintPath->SetStringField(TEXT("description"), TEXT("Asset path of the Blueprint. Without it, the focused Blueprint. A path that does not load is an error (ASSET_NOT_FOUND)"));
	Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPath);
	
	// centerView parameter (optional)
	TSharedPtr<FJsonObject> CenterView = MakeShareable(new FJsonObject);
	CenterView->SetStringField(TEXT("type"), TEXT("boolean"));
	CenterView->SetStringField(TEXT("description"), TEXT("Accepted but has no effect"));
	CenterView->SetBoolField(TEXT("default"), true);
	Properties->SetObjectField(TEXT("centerView"), CenterView);
	
	// selectNodes parameter (optional)
	TSharedPtr<FJsonObject> SelectNodes = MakeShareable(new FJsonObject);
	SelectNodes->SetStringField(TEXT("type"), TEXT("boolean"));
	SelectNodes->SetStringField(TEXT("description"), TEXT("Accepted but has no effect"));
	SelectNodes->SetBoolField(TEXT("default"), true);
	Properties->SetObjectField(TEXT("selectNodes"), SelectNodes);
	
	InputSchema->SetObjectField(TEXT("properties"), Properties);
	
	Definition.InputSchema = InputSchema;
	
	FN2CGraphTarget::AddSchemaProperties(Definition);

	return Definition;
}

FMcpToolCallResult FN2CMcpOpenBlueprintFunctionTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FN2CMcpArgumentParser ArgParser(Arguments);
		FString ErrorMsg;
		
		// The function: functionGuid (or graphGuid) or graphName, in blueprintPath or else the focused Blueprint, through
		// the one graph resolver (card 0152). A blueprintPath that does not load is an error (ASSET_NOT_FOUND); it no
		// longer falls back to the focused Blueprint. An old name-hash ID still resolves, with a warning, until 2.1
		FN2CGraphTargetArgs TargetArgs = FN2CGraphTargetArgs::FromJson(Arguments);
		const FString FunctionGuidString = ArgParser.GetOptionalString(TEXT("functionGuid")).TrimStartAndEnd();
		if (!FunctionGuidString.IsEmpty())
		{
			TargetArgs.GraphGuid = FunctionGuidString;
		}
		if (TargetArgs.GraphGuid.IsEmpty() && TargetArgs.GraphName.IsEmpty())
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Missing the function: pass functionGuid (from list-blueprint-functions or create-blueprint-function) or graphName"));
		}
		bool bCenterView = ArgParser.GetOptionalBool(TEXT("centerView"), true);
		bool bSelectNodes = ArgParser.GetOptionalBool(TEXT("selectNodes"), true);

		const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(TargetArgs, /*bForWrite*/ false);
		if (!Target.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Target.Error);
		}
		UBlueprint* Blueprint = Target.Blueprint;
		UEdGraph* FunctionGraph = Target.Graph;
		if (!FN2CMcpFunctionGuidUtils::IsFunctionGraph(Blueprint, FunctionGraph))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(FString::Printf(
				TEXT("NOT_A_FUNCTION_GRAPH: %s (%s graph) of %s is not a function. Use open-blueprint-asset with focus_graph for other graphs."),
				*FunctionGraph->GetName(), *FN2CGraphTarget::GraphKind(FunctionGraph), *Blueprint->GetName())));
		}
		const FGuid FunctionGuid = FN2CMcpFunctionGuidUtils::GetFunctionId(FunctionGraph);

		// Open the Blueprint editor
		TSharedPtr<IBlueprintEditor> Editor;
		if (!OpenBlueprintEditor(Blueprint, Editor))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to open Blueprint editor")));
		}
		
		// Navigate to the function
		if (!NavigateToFunction(Editor, FunctionGraph))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to navigate to function")));
		}
		
		// Optional: Center view
		if (bCenterView)
		{
			CenterViewOnFunction(Editor, FunctionGraph);
		}
		
		// Optional: Select nodes
		if (bSelectNodes)
		{
			SelectAllNodesInFunction(Editor, FunctionGraph);
		}
		
		// Build success result
		TSharedPtr<FJsonObject> SuccessData = BuildSuccessResult(Blueprint, FunctionGraph, FunctionGuid);
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(SuccessData.ToSharedRef(), Writer);
		
		return Target.Finish(FMcpToolCallResult::CreateTextResult(JsonString));
	});
}

UEdGraph* FN2CMcpOpenBlueprintFunctionTool::FindFunctionByGuid(const FGuid& FunctionGuid, const FString& BlueprintPath, UBlueprint*& OutBlueprint) const
{
	FString ResolveError;
	// First try the specified Blueprint path
	if (!BlueprintPath.IsEmpty())
	{
		OutBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(BlueprintPath, ResolveError); // Use new utility
		if (OutBlueprint)
		{
			UEdGraph* FoundGraph = SearchFunctionInBlueprint(OutBlueprint, FunctionGuid);
			if (FoundGraph)
			{
				return FoundGraph;
			}
		}
		// If BlueprintPath was specified but not found, OutBlueprint will be null.
		// For now, it falls through to focused.
	}
	
	// Try the focused Blueprint if path was empty or BP not found via path
	if (!OutBlueprint) 
	{
		// Use ResolveBlueprint with empty path to get focused, or error.
		OutBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(TEXT(""), ResolveError); 
		if (OutBlueprint)
		{
			UEdGraph* FoundGraph = SearchFunctionInBlueprint(OutBlueprint, FunctionGuid);
			if (FoundGraph)
			{
				return FoundGraph;
			}
		}
	}
	
	OutBlueprint = nullptr; // Ensure it's null if no function found in any BP
	return nullptr;
}

UEdGraph* FN2CMcpOpenBlueprintFunctionTool::SearchFunctionInBlueprint(UBlueprint* Blueprint, const FGuid& FunctionGuid) const
{
	if (!Blueprint)
	{
		return nullptr;
	}
	
	// Use the utility function to find the function by GUID
	return FN2CMcpFunctionGuidUtils::FindFunctionByGuid(Blueprint, FunctionGuid);
}

FGuid FN2CMcpOpenBlueprintFunctionTool::GetFunctionGuid(const UEdGraph* FunctionGraph) const
{
	if (!FunctionGraph)
	{
		return FGuid();
	}
	
	// Use the utility to get the stored GUID
	return FN2CMcpFunctionGuidUtils::GetFunctionId(FunctionGraph);
}

bool FN2CMcpOpenBlueprintFunctionTool::OpenBlueprintEditor(UBlueprint* Blueprint, TSharedPtr<IBlueprintEditor>& OutEditor) const
{
	if (!Blueprint)
	{
		return false;
	}
	
	// Use the utility function to open or focus the Blueprint editor
	FString ErrorMsg;
	bool bSuccess = FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint, OutEditor, ErrorMsg);
	
	if (!bSuccess)
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to open Blueprint editor: %s"), *ErrorMsg));
		return false;
	}
	
	return true;
}

bool FN2CMcpOpenBlueprintFunctionTool::NavigateToFunction(TSharedPtr<IBlueprintEditor> Editor, UEdGraph* FunctionGraph) const
{
	if (!Editor.IsValid() || !FunctionGraph)
	{
		return false;
	}
	
	// Jump to the function graph
	Editor->JumpToHyperlink(FunctionGraph, false);
	Editor->FocusWindow();
	
	return true;
}

bool FN2CMcpOpenBlueprintFunctionTool::CenterViewOnFunction(TSharedPtr<IBlueprintEditor> Editor, UEdGraph* FunctionGraph) const
{
	// Centering view requires direct access to SGraphEditor which is complex
	// For now, just navigating to the function is sufficient
	return true;
}

bool FN2CMcpOpenBlueprintFunctionTool::SelectAllNodesInFunction(TSharedPtr<IBlueprintEditor> Editor, UEdGraph* FunctionGraph) const
{
	// Node selection requires direct access to SGraphEditor which is complex
	// For now, just navigating to the function is sufficient
	return true;
}

TSharedPtr<FJsonObject> FN2CMcpOpenBlueprintFunctionTool::BuildSuccessResult(UBlueprint* Blueprint, UEdGraph* FunctionGraph, const FGuid& FunctionGuid) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("functionName"), GetFunctionDisplayName(FunctionGraph));
	Result->SetStringField(TEXT("functionGuid"), FunctionGuid.ToString());
	Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	Result->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("graphName"), FunctionGraph->GetName());
	Result->SetStringField(TEXT("editorState"), TEXT("opened"));
	
	return Result;
}

UK2Node_FunctionEntry* FN2CMcpOpenBlueprintFunctionTool::GetFunctionEntryNode(UEdGraph* FunctionGraph) const
{
	if (!FunctionGraph)
	{
		return nullptr;
	}
	
	for (UEdGraphNode* Node : FunctionGraph->Nodes)
	{
		if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
		{
			return EntryNode;
		}
	}
	
	return nullptr;
}

FString FN2CMcpOpenBlueprintFunctionTool::GetFunctionDisplayName(UEdGraph* FunctionGraph) const
{
	if (!FunctionGraph)
	{
		return TEXT("Unknown");
	}
	
	// Try to get the display name from the entry node
	UK2Node_FunctionEntry* EntryNode = GetFunctionEntryNode(FunctionGraph);
	if (EntryNode)
	{
		return EntryNode->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
	}
	
	// Fall back to graph name
	return FunctionGraph->GetName();
}
