// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetOpenBlueprintEditorsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetOpenBlueprintEditorsTool)

FMcpToolDefinition FN2CMcpGetOpenBlueprintEditorsTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("get-open-blueprint-editors"),
		TEXT("Returns a list of all currently open Blueprint editors with their asset paths and available graphs"),
		TEXT("Blueprint Discovery")
	);
	
	// No input parameters required for this tool
	Definition.InputSchema = BuildEmptyObjectSchema();
	
	return Definition;
}

FMcpToolCallResult FN2CMcpGetOpenBlueprintEditorsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		FN2CLogger::Get().Log(TEXT("get-open-blueprint-editors: Starting execution"), EN2CLogSeverity::Debug);
		
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> Editors;
		
		// Get the asset editor subsystem to find all open assets
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!AssetEditorSubsystem)
		{
			FN2CLogger::Get().LogError(TEXT("get-open-blueprint-editors: Could not get AssetEditorSubsystem"));
			return FMcpToolCallResult::CreateErrorResult(TEXT("Could not get asset editor subsystem"));
		}
		
		// Get all currently edited assets
		TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
		const TSharedPtr<FBlueprintEditor> FocusedEditor = FN2CBlueprintEditorTracker::Get().GetFocusedEditor();

		// Process assets from AssetEditorSubsystem
		for (UObject* Asset : EditedAssets)
		{
			UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
			if (!Blueprint)
			{
				continue;
			}
			
			// Find the editor instance for this Blueprint
			IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Blueprint, false);
			if (!EditorInstance)
			{
				continue;
			}
			
			// Check if this is actually a Blueprint editor
			FString EditorName = EditorInstance->GetEditorName().ToString();
			if (EditorName != TEXT("BlueprintEditor"))
			{
				continue;
			}
			
			// This Blueprint's own editor (card 0150: not only the focused one, which left every other open
			// Blueprint without its editor state)
			const TSharedPtr<FBlueprintEditor> BlueprintEditor = FN2CBlueprintEditorTracker::GetEditorFor(Blueprint);

			// If we found a valid Blueprint editor, collect its info
			if (BlueprintEditor.IsValid())
			{
				TSharedPtr<FJsonObject> EditorInfo = CollectEditorInfo(BlueprintEditor);
				if (EditorInfo.IsValid())
				{
					// The editor the focus-based tools act on (the one the user last activated)
					EditorInfo->SetBoolField(TEXT("isFocused"), BlueprintEditor == FocusedEditor);
					Editors.Add(MakeShareable(new FJsonValueObject(EditorInfo)));
				}
			}
			else
			{
				// Fallback: Create basic info from the Blueprint asset itself
				TSharedPtr<FJsonObject> EditorInfo = MakeShareable(new FJsonObject);
				EditorInfo->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
				EditorInfo->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
				EditorInfo->SetStringField(TEXT("blueprintType"), GetBlueprintTypeString(Blueprint->BlueprintType));
				
				// Collect graphs directly from the Blueprint
				EditorInfo->SetArrayField(TEXT("graphs"), CollectBlueprintGraphs(Blueprint));
				
				// We don't have editor state info in this case
				EditorInfo->SetBoolField(TEXT("isInEditingMode"), true); // Assume true if editor is open
				EditorInfo->SetBoolField(TEXT("isCompileEnabled"), true); // Default assumption
				
				Editors.Add(MakeShareable(new FJsonValueObject(EditorInfo)));
			}
		}
		
		// Additionally, we can try to get more editors through FN2CEditorIntegration
		// This is simplified since we don't have direct access to all Blueprint editors through BlueprintEditorModule
		
		// Set the results
		Result->SetArrayField(TEXT("editors"), Editors);
		Result->SetNumberField(TEXT("count"), Editors.Num());
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("get-open-blueprint-editors: Found %d open Blueprint editors"), Editors.Num()), EN2CLogSeverity::Debug);
		
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		return FMcpToolCallResult::CreateTextResult(JsonString);
	});
}

TSharedPtr<FJsonObject> FN2CMcpGetOpenBlueprintEditorsTool::CollectEditorInfo(const TSharedPtr<FBlueprintEditor>& Editor) const
{
	if (!Editor.IsValid())
	{
		return nullptr;
	}
	
	UBlueprint* Blueprint = Editor->GetBlueprintObj();
	if (!Blueprint)
	{
		return nullptr;
	}
	
	TSharedPtr<FJsonObject> EditorInfo = MakeShareable(new FJsonObject);
	
	// Basic info
	EditorInfo->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	EditorInfo->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
	EditorInfo->SetStringField(TEXT("blueprintType"), GetBlueprintTypeString(Blueprint->BlueprintType));
	
	// Parent class info
	if (Blueprint->ParentClass)
	{
		EditorInfo->SetStringField(TEXT("parentClass"), Blueprint->ParentClass->GetName());
	}
	
	// Current focused graph
	UEdGraph* FocusedGraph = Editor->GetFocusedGraph();
	if (FocusedGraph)
	{
		EditorInfo->SetStringField(TEXT("focusedGraph"), FocusedGraph->GetFName().ToString());
		
		// Add focused graph type
		FString GraphType = TEXT("Unknown");
		if (Blueprint->FunctionGraphs.Contains(FocusedGraph))
		{
			GraphType = TEXT("Function");
		}
		else if (Blueprint->EventGraphs.Contains(FocusedGraph))
		{
			GraphType = TEXT("Event");
		}
		else if (Blueprint->MacroGraphs.Contains(FocusedGraph))
		{
			GraphType = TEXT("Macro");
		}
		else if (Blueprint->DelegateSignatureGraphs.Contains(FocusedGraph))
		{
			GraphType = TEXT("Delegate");
		}
		// Construction script detection removed - requires different approach
		EditorInfo->SetStringField(TEXT("focusedGraphType"), GraphType);
	}
	
	// Collect available graphs
	EditorInfo->SetArrayField(TEXT("graphs"), CollectBlueprintGraphs(Blueprint));
	
	// Editor state info - these methods are not available in the interface, so we'll use default values
	EditorInfo->SetBoolField(TEXT("isInEditingMode"), true); // Assume true if editor is open
	EditorInfo->SetBoolField(TEXT("isCompileEnabled"), true); // Assume true by default
	
	// Additional state info
	EditorInfo->SetBoolField(TEXT("hasCompilerResults"), Blueprint->Status != BS_Unknown && Blueprint->Status != BS_Dirty);
	EditorInfo->SetBoolField(TEXT("isDirty"), Blueprint->Status == BS_Dirty);
	
	// Add graph counts for quick overview
	TSharedPtr<FJsonObject> GraphCounts = MakeShareable(new FJsonObject);
	GraphCounts->SetNumberField(TEXT("event_graphs"), Blueprint->EventGraphs.Num());
	GraphCounts->SetNumberField(TEXT("function_graphs"), Blueprint->FunctionGraphs.Num());
	GraphCounts->SetNumberField(TEXT("macro_graphs"), Blueprint->MacroGraphs.Num());
	GraphCounts->SetNumberField(TEXT("delegate_graphs"), Blueprint->DelegateSignatureGraphs.Num());
	EditorInfo->SetObjectField(TEXT("graph_counts"), GraphCounts);
	
	return EditorInfo;
}

FString FN2CMcpGetOpenBlueprintEditorsTool::GetBlueprintTypeString(EBlueprintType Type) const
{
	switch (Type)
	{
		case BPTYPE_Normal: return TEXT("Normal");
		case BPTYPE_Const: return TEXT("Const");
		case BPTYPE_MacroLibrary: return TEXT("MacroLibrary");
		case BPTYPE_Interface: return TEXT("Interface");
		case BPTYPE_LevelScript: return TEXT("LevelScript");
		case BPTYPE_FunctionLibrary: return TEXT("FunctionLibrary");
		default: return TEXT("Unknown");
	}
}

TArray<TSharedPtr<FJsonValue>> FN2CMcpGetOpenBlueprintEditorsTool::CollectBlueprintGraphs(UBlueprint* Blueprint) const
{
	TArray<TSharedPtr<FJsonValue>> GraphList;
	
	if (!Blueprint)
	{
		return GraphList;
	}
	
	// Function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph)
		{
			TSharedPtr<FJsonObject> GraphInfo = MakeShareable(new FJsonObject);
			GraphInfo->SetStringField(TEXT("name"), Graph->GetFName().ToString());
			GraphInfo->SetStringField(TEXT("type"), TEXT("Function"));
			GraphInfo->SetBoolField(TEXT("isEditable"), !Graph->bEditable ? false : true);
			GraphList.Add(MakeShareable(new FJsonValueObject(GraphInfo)));
		}
	}
	
	// Event graphs
	for (UEdGraph* Graph : Blueprint->EventGraphs)
	{
		if (Graph)
		{
			TSharedPtr<FJsonObject> GraphInfo = MakeShareable(new FJsonObject);
			GraphInfo->SetStringField(TEXT("name"), Graph->GetFName().ToString());
			GraphInfo->SetStringField(TEXT("type"), TEXT("Event"));
			GraphInfo->SetBoolField(TEXT("isEditable"), !Graph->bEditable ? false : true);
			GraphList.Add(MakeShareable(new FJsonValueObject(GraphInfo)));
		}
	}
	
	// Macro graphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph)
		{
			TSharedPtr<FJsonObject> GraphInfo = MakeShareable(new FJsonObject);
			GraphInfo->SetStringField(TEXT("name"), Graph->GetFName().ToString());
			GraphInfo->SetStringField(TEXT("type"), TEXT("Macro"));
			GraphInfo->SetBoolField(TEXT("isEditable"), !Graph->bEditable ? false : true);
			GraphList.Add(MakeShareable(new FJsonValueObject(GraphInfo)));
		}
	}
	
	// Delegate signature graphs
	for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
	{
		if (Graph)
		{
			TSharedPtr<FJsonObject> GraphInfo = MakeShareable(new FJsonObject);
			GraphInfo->SetStringField(TEXT("name"), Graph->GetFName().ToString());
			GraphInfo->SetStringField(TEXT("type"), TEXT("Delegate"));
			GraphInfo->SetBoolField(TEXT("isEditable"), !Graph->bEditable ? false : true);
			GraphList.Add(MakeShareable(new FJsonValueObject(GraphInfo)));
		}
	}
	
	// Construction script graph handling removed - requires different approach
	
	return GraphList;
}