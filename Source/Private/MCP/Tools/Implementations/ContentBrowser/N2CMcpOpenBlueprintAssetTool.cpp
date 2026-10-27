// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpOpenBlueprintAssetTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Animation/AnimBlueprint.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpOpenBlueprintAssetTool)

FMcpToolDefinition FN2CMcpOpenBlueprintAssetTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("open-blueprint-asset"),
		TEXT("Opens a specified blueprint asset in the Blueprint Editor, allowing agents to programmatically open blueprints for viewing or editing. ")
		TEXT("An already open Blueprint is brought to the front. Either way it becomes the focused Blueprint that ")
		TEXT("get-focused-blueprint and the other focus-based tools act on")
	,

		N2CMcpToolsets::ContentBrowser

	);
	
	// Define input schema
	TSharedPtr<FJsonObject> InputSchema = MakeShareable(new FJsonObject);
	InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// asset_path property (required)
	TSharedPtr<FJsonObject> AssetPathProp = MakeShareable(new FJsonObject);
	AssetPathProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetPathProp->SetStringField(TEXT("description"), TEXT("Object path or package path of the blueprint asset (e.g., '/Game/Blueprints/BP_MyActor.BP_MyActor'). It may be under any mounted root: /Game/, /Engine/ or a plugin's /<PluginName>/."));
	Properties->SetObjectField(TEXT("asset_path"), AssetPathProp);
	
	// bring_to_front property (optional)
	TSharedPtr<FJsonObject> BringToFrontProp = MakeShareable(new FJsonObject);
	BringToFrontProp->SetStringField(TEXT("type"), TEXT("boolean"));
	BringToFrontProp->SetStringField(TEXT("description"), TEXT("Whether to bring the editor window to front"));
	BringToFrontProp->SetBoolField(TEXT("default"), true);
	Properties->SetObjectField(TEXT("bring_to_front"), BringToFrontProp);
	
	// focus_graph property (optional)
	TSharedPtr<FJsonObject> FocusGraphProp = MakeShareable(new FJsonObject);
	FocusGraphProp->SetStringField(TEXT("type"), TEXT("string"));
	FocusGraphProp->SetStringField(TEXT("description"), TEXT("Optional: name of an event, function, macro or delegate graph to focus, matched case-insensitively (e.g., 'EventGraph', or 'ConstructionScript' / 'UserConstructionScript' for an Actor's construction script). An unknown name fails with GRAPH_NOT_FOUND and lists the Blueprint's graphs; the Blueprint is not opened."));
	FocusGraphProp->SetStringField(TEXT("default"), TEXT(""));
	Properties->SetObjectField(TEXT("focus_graph"), FocusGraphProp);
	
	InputSchema->SetObjectField(TEXT("properties"), Properties);
	
	// Define required fields
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShareable(new FJsonValueString(TEXT("asset_path"))));
	InputSchema->SetArrayField(TEXT("required"), Required);
	
	Definition.InputSchema = InputSchema;
	
	// Not read-only, but it opens an editor tab, changing no Blueprint or asset: it keeps working during Play In Editor (card 0170)
	Definition.bAllowedDuringPlaySession = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpOpenBlueprintAssetTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FN2CMcpArgumentParser ArgParser(Arguments);
		FString ErrorMsg;
		
		// Extract required parameters
		FString AssetPath;
		if (!ArgParser.TryGetRequiredString(TEXT("asset_path"), AssetPath, ErrorMsg))
		{
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}
		
		// Optional parameters
		bool bBringToFront = ArgParser.GetOptionalBool(TEXT("bring_to_front"), true);
		FString FocusGraph = ArgParser.GetOptionalString(TEXT("focus_graph"));
		
		// Validate asset path
		if (!ValidateBlueprintAssetPath(AssetPath, ErrorMsg))
		{
			FN2CLogger::Get().LogError(FString::Printf(TEXT("open-blueprint-asset: %s"), *ErrorMsg));
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}
		
		// Load the blueprint asset
		FSoftObjectPath SoftPath(AssetPath);
		UObject* LoadedAsset = SoftPath.TryLoad();
		
		if (!LoadedAsset)
		{
			ErrorMsg = FString::Printf(TEXT("Failed to load asset at path: %s"), *AssetPath);
			FN2CLogger::Get().LogError(FString::Printf(TEXT("open-blueprint-asset: %s"), *ErrorMsg));
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}
		
		// Verify it's actually a blueprint
		UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
		if (!Blueprint)
		{
			ErrorMsg = FString::Printf(TEXT("Asset is not a blueprint: %s"), *AssetPath);
			FN2CLogger::Get().LogError(FString::Printf(TEXT("open-blueprint-asset: %s"), *ErrorMsg));
			return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
		}
		
		// The graph to focus, found before anything opens: an unknown name fails and lists the graphs, instead of
		// opening the Blueprint with nothing focused so the next graph edit lands in the graph focused before (card 0286)
		UEdGraph* TargetGraph = nullptr;
		if (!FocusGraph.IsEmpty())
		{
			TargetGraph = FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, FocusGraph, ErrorMsg);
			if (!TargetGraph)
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("open-blueprint-asset: %s"), *ErrorMsg));
				return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
			}
		}
		
		// Open the blueprint editor using existing utility
		TSharedPtr<IBlueprintEditor> BlueprintEditor;
		if (!FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint, BlueprintEditor, ErrorMsg))
		{
			FN2CLogger::Get().LogError(FString::Printf(TEXT("open-blueprint-asset: Failed to open editor - %s"), *ErrorMsg));
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Failed to open blueprint editor: %s"), *ErrorMsg));
		}
		
		// Bring to front if requested
		if (bBringToFront && BlueprintEditor.IsValid())
		{
			BlueprintEditor->FocusWindow();
		}
		
		// Focus specific graph if requested
		FString FocusedGraphName;
		if (TargetGraph && BlueprintEditor.IsValid())
		{
			BlueprintEditor->JumpToHyperlink(TargetGraph, false);
			// The graph's own name, not the request: "eventgraph" or "ConstructionScript" report the graph focused
			FocusedGraphName = TargetGraph->GetName();
			FN2CLogger::Get().Log(FString::Printf(TEXT("open-blueprint-asset: Focused on graph '%s'"), *FocusedGraphName), EN2CLogSeverity::Info);
		}
		
		// Build success result
		TSharedPtr<FJsonObject> ResultObject = BuildSuccessResult(Blueprint, AssetPath, FocusedGraphName);
		
		// Serialize result
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		if (!FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to serialize result"));
		}
		
		FN2CLogger::Get().Log(FString::Printf(TEXT("open-blueprint-asset: Successfully opened blueprint '%s'"), *Blueprint->GetName()), EN2CLogSeverity::Info);
		
		return FMcpToolCallResult::CreateTextResult(JsonString);
	});
}

bool FN2CMcpOpenBlueprintAssetTool::ValidateBlueprintAssetPath(const FString& AssetPath, FString& OutErrorMsg) const
{
	// Check if path is empty
	if (AssetPath.IsEmpty())
	{
		OutErrorMsg = TEXT("Asset path cannot be empty");
		return false;
	}
	
	// Opening only reads: any mounted root will do (/Game, /Engine, a plugin's /<PluginName>), as for the other
	// read tools (card 0182)
	FString PackagePath = AssetPath;
	int32 DotIndex = INDEX_NONE;
	if (PackagePath.FindChar(TEXT('.'), DotIndex))
	{
		PackagePath.LeftInline(DotIndex);
	}
	if (!AssetPath.StartsWith(TEXT("/")) || !FN2CMcpContentBrowserUtils::ValidateContentPath(PackagePath, OutErrorMsg))
	{
		OutErrorMsg = FString::Printf(TEXT("Asset path must be under a mounted content root (/Game/, /Engine/ or a plugin's /<PluginName>/): %s"),
			OutErrorMsg.IsEmpty() ? TEXT("it must start with '/'") : *OutErrorMsg);
		return false;
	}
	
	// Basic path validation - no double slashes, no trailing slash (except root)
	if (AssetPath.Contains(TEXT("//")))
	{
		OutErrorMsg = TEXT("Asset path contains invalid double slashes");
		return false;
	}
	
	// Check for blueprint-specific naming convention
	// Blueprint paths typically end with the asset name repeated after a dot
	// e.g., /Game/Blueprints/BP_MyActor.BP_MyActor
	int32 LastSlashIndex;
	if (AssetPath.FindLastChar('/', LastSlashIndex))
	{
		FString AssetName = AssetPath.Mid(LastSlashIndex + 1);
		// If the asset name contains a dot, it should follow the convention
		if (AssetName.Contains(TEXT(".")))
		{
			FString BeforeDot, AfterDot;
			if (!AssetName.Split(TEXT("."), &BeforeDot, &AfterDot) || BeforeDot != AfterDot)
			{
				// This is just a warning, not an error, as some assets might not follow this convention
				FN2CLogger::Get().LogWarning(FString::Printf(
					TEXT("Asset path may not follow standard naming convention (expected format: /Path/AssetName.AssetName): %s"), 
					*AssetPath
				));
			}
		}
	}
	
	return true;
}

FString FN2CMcpOpenBlueprintAssetTool::GetBlueprintTypeString(const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return TEXT("Unknown");
	}
	
	switch (Blueprint->BlueprintType)
	{
		case BPTYPE_Const:
			return TEXT("Const");
		case BPTYPE_MacroLibrary:
			return TEXT("MacroLibrary");
		case BPTYPE_Interface:
			return TEXT("Interface");
		case BPTYPE_LevelScript:
			return TEXT("LevelScript");
		case BPTYPE_FunctionLibrary:
			return TEXT("FunctionLibrary");
		default:
			break;
	}
	
	// A normal Blueprint: its kind is its asset class, then its parent class (before card 0503 every normal Blueprint,
	// a Widget Blueprint included, was an Actor)
	if (Blueprint->IsA<UWidgetBlueprint>())
	{
		return TEXT("Widget");
	}
	if (Blueprint->IsA<UAnimBlueprint>())
	{
		return TEXT("Animation");
	}
	if (Blueprint->ParentClass)
	{
		if (Blueprint->ParentClass->IsChildOf(UActorComponent::StaticClass()))
		{
			return TEXT("Component");
		}
		if (Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
		{
			return TEXT("Actor");
		}
	}
	return TEXT("Object");
}

TSharedPtr<FJsonObject> FN2CMcpOpenBlueprintAssetTool::BuildSuccessResult(const UBlueprint* Blueprint, const FString& AssetPath, const FString& FocusedGraph)
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("editor_id"), AssetPath); // Using asset path as editor ID
	Result->SetStringField(TEXT("focused_graph"), FocusedGraph);
	Result->SetStringField(TEXT("asset_type"), GetBlueprintTypeString(Blueprint));
	
	// Add additional blueprint information
	Result->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());
	
	if (Blueprint->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());
	}
	
	// Add graph information
	TSharedPtr<FJsonObject> GraphInfo = MakeShareable(new FJsonObject);
	// The event graph pages: EventGraphs is a transient list the compiler fills, empty on a loaded Blueprint (card 0503)
	GraphInfo->SetNumberField(TEXT("event_graphs"), Blueprint->UbergraphPages.Num());
	GraphInfo->SetNumberField(TEXT("function_graphs"), Blueprint->FunctionGraphs.Num());
	GraphInfo->SetNumberField(TEXT("macro_graphs"), Blueprint->MacroGraphs.Num());
	GraphInfo->SetNumberField(TEXT("delegate_graphs"), Blueprint->DelegateSignatureGraphs.Num());
	
	Result->SetObjectField(TEXT("graph_counts"), GraphInfo);
	
	return Result;
}