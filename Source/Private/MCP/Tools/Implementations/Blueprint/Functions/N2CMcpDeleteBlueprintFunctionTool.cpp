// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpDeleteBlueprintFunctionTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModule.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "K2Node_DelegateSet.h"
#include "EdGraphSchema_K2.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Register this tool with the MCP tool manager
REGISTER_MCP_TOOL(FN2CMcpDeleteBlueprintFunctionTool)

#define LOCTEXT_NAMESPACE "NodeToCode"

FMcpToolDefinition FN2CMcpDeleteBlueprintFunctionTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("delete-blueprint-function"),
		TEXT("Deletes a Blueprint function, named by its functionGuid (or graphName) in blueprintPath or the focused Blueprint. Supports reference detection and forced deletion."),
		N2CMcpToolsets::BlueprintFunction
	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	
	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
	
	// functionGuid property (required)
	TSharedPtr<FJsonObject> FunctionGuidProp = MakeShareable(new FJsonObject);
	FunctionGuidProp->SetStringField(TEXT("type"), TEXT("string"));
	FunctionGuidProp->SetStringField(TEXT("description"), TEXT("The function's ID (functionGuid from create-blueprint-function, guid from list-blueprint-functions): its graph GUID, stable across renames and moves. Same as graphGuid. Pass this or graphName."));
	Properties->SetObjectField(TEXT("functionGuid"), FunctionGuidProp);

	// blueprintPath property (optional)
	TSharedPtr<FJsonObject> BlueprintPathProp = MakeShareable(new FJsonObject);
	BlueprintPathProp->SetStringField(TEXT("type"), TEXT("string"));
	BlueprintPathProp->SetStringField(TEXT("description"), TEXT("Optional: The asset path of the Blueprint. If not provided, uses the currently focused Blueprint."));
	Properties->SetObjectField(TEXT("blueprintPath"), BlueprintPathProp);

	// force property (optional)
	TSharedPtr<FJsonObject> ForceProp = MakeShareable(new FJsonObject);
	ForceProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ForceProp->SetStringField(TEXT("description"), TEXT("If true, bypasses confirmation checks and forces deletion even if the function has references. Default: false"));
	ForceProp->SetBoolField(TEXT("default"), false);
	Properties->SetObjectField(TEXT("force"), ForceProp);

	Schema->SetObjectField(TEXT("properties"), Properties);
	
	Definition.InputSchema = Schema;
	
	FN2CGraphTarget::AddSchemaProperties(Definition);

	return Definition;
}

FMcpToolCallResult FN2CMcpDeleteBlueprintFunctionTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FN2CMcpArgumentParser ArgParser(Arguments);
		FString ErrorMsg;
		
		// The function: functionGuid (or graphGuid) or graphName, in blueprintPath or else the focused Blueprint, through
		// the one graph resolver (card 0152). functionGuid is the function's GraphGuid; an old name-hash ID still
		// resolves, with a warning in the result's target, until NodeToCode 2.1
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
		bool bForce = ArgParser.GetOptionalBool(TEXT("force"), false);

		// Not bForWrite: deleting a function of an interface Blueprint is allowed, as in the editor
		const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(TargetArgs, /*bForWrite*/ false);
		if (!Target.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Target.Error);
		}
		UBlueprint* TargetBlueprint = Target.Blueprint;
		UEdGraph* FunctionGraph = Target.Graph;
		if (!FN2CMcpFunctionGuidUtils::IsFunctionGraph(TargetBlueprint, FunctionGraph))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(FString::Printf(
				TEXT("NOT_A_FUNCTION_GRAPH: %s (%s graph) of %s is not a function"),
				*FunctionGraph->GetName(), *FN2CGraphTarget::GraphKind(FunctionGraph), *TargetBlueprint->GetName())));
		}
		const FGuid FunctionGuid = FN2CMcpFunctionGuidUtils::GetFunctionId(FunctionGraph);
		// Taken now: the graph leaves the Blueprint below
		const TSharedRef<FJsonObject> TargetJson = Target.ToJson();

		// Get function name for reference
		FString FunctionName = GetFunctionDisplayName(FunctionGraph);
		
		// Validate deletion
		FString ValidationError;
		if (!ValidateFunctionDeletion(TargetBlueprint, FunctionGraph, bForce, ValidationError))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(ValidationError));
		}
		
		// Find references to this function
		TArray<UK2Node_CallFunction*> CallNodes;
		FindFunctionReferences(TargetBlueprint, FunctionGraph->GetFName().ToString(), CallNodes);
		
		// Create a single transaction for all operations
		const FScopedTransaction Transaction(LOCTEXT("DeleteBlueprintFunction", "Delete Blueprint Function"));
		
		// Mark the Blueprint as modified for the transaction
		TargetBlueprint->Modify();
		
		// Handle references
		TArray<FRemovedReference> RemovedReferences;
		if (CallNodes.Num() > 0)
		{
			if (!bForce)
			{
				return Target.Finish(FMcpToolCallResult::CreateErrorResult(FString::Printf(
					TEXT("Function '%s' has %d references and cannot be deleted. Use force=true to remove anyway."),
					*FunctionName, CallNodes.Num()
				)));
			}
			
			// Remove references within the transaction
			if (!RemoveFunctionReferences(CallNodes, RemovedReferences))
			{
				return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to remove function references")));
			}
		}
		
		// Delete the function within the same transaction
		if (!DeleteFunctionGraph(TargetBlueprint, FunctionGraph))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to delete function graph")));
		}
		
		// Post-deletion operations
		RefreshBlueprintEditor(TargetBlueprint);
		ShowDeletionNotification(FunctionName, true);
		
		// Build success result
		TSharedPtr<FJsonObject> Result = BuildSuccessResult(
			FunctionName,
			FunctionGuid,
			TargetBlueprint->GetPathName(),
			RemovedReferences
		);
		
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		Result->SetObjectField(TEXT("target"), TargetJson);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		return Target.Finish(FMcpToolCallResult::CreateTextResult(JsonString));
	});
}

UEdGraph* FN2CMcpDeleteBlueprintFunctionTool::FindFunctionByGuid(UBlueprint* Blueprint, const FGuid& FunctionGuid) const
{
	if (!Blueprint || !FunctionGuid.IsValid())
	{
		return nullptr;
	}
	
	// Use the utility function to find the function by GUID
	return FN2CMcpFunctionGuidUtils::FindFunctionByGuid(Blueprint, FunctionGuid);
}

bool FN2CMcpDeleteBlueprintFunctionTool::ValidateFunctionDeletion(UBlueprint* Blueprint, UEdGraph* FunctionGraph, bool bForce, FString& OutError) const
{
	if (!Blueprint || !FunctionGraph)
	{
		OutError = TEXT("Invalid Blueprint or function graph");
		return false;
	}
	
	FString FunctionName = FunctionGraph->GetFName().ToString();
	
	// Check if this is a system function
	if (IsSystemFunction(FunctionName))
	{
		OutError = FString::Printf(TEXT("Cannot delete function '%s' - this is a protected system function"), *FunctionName);
		return false;
	}
	
	// Check if function is currently being debugged
	// TODO: Add debug check if needed
	
	// Check for overrides in child classes
	TArray<UClass*> ChildClasses;
	if (CheckForOverrides(Blueprint, FunctionName, ChildClasses))
	{
		if (!bForce)
		{
			OutError = FString::Printf(
				TEXT("Function '%s' is overridden in %d child Blueprint(s). Use force=true to delete anyway."),
				*FunctionName, ChildClasses.Num()
			);
			return false;
		}
	}
	
	return true;
}

bool FN2CMcpDeleteBlueprintFunctionTool::IsSystemFunction(const FString& FunctionName) const
{
	// List of protected system functions that should not be deleted
	static const TSet<FString> SystemFunctions = {
		TEXT("UserConstructionScript"),
		TEXT("ConstructionScript"),
		TEXT("OnConstruction"),
		TEXT("BeginPlay"),
		TEXT("EndPlay"),
		TEXT("Tick"),
		TEXT("ReceiveBeginPlay"),
		TEXT("ReceiveEndPlay"),
		TEXT("ReceiveTick"),
		TEXT("ReceiveDestroyed")
	};
	
	return SystemFunctions.Contains(FunctionName);
}

bool FN2CMcpDeleteBlueprintFunctionTool::CheckForOverrides(UBlueprint* Blueprint, const FString& FunctionName, TArray<UClass*>& OutChildClasses) const
{
	OutChildClasses.Empty();
	
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return false;
	}
	
	// Get all child classes
	TArray<UClass*> ChildClasses;
	GetDerivedClasses(Blueprint->GeneratedClass, ChildClasses);
	
	// Check each child class for overrides
	for (UClass* ChildClass : ChildClasses)
	{
		if (UFunction* ChildFunc = ChildClass->FindFunctionByName(*FunctionName))
		{
			// Check if it's actually overridden (not just inherited)
			if (ChildFunc->GetOuter() == ChildClass)
			{
				OutChildClasses.Add(ChildClass);
			}
		}
	}
	
	return OutChildClasses.Num() > 0;
}

void FN2CMcpDeleteBlueprintFunctionTool::FindFunctionReferences(UBlueprint* Blueprint, const FString& FunctionName, 
	TArray<UK2Node_CallFunction*>& OutCallNodes) const
{
	OutCallNodes.Empty();
	
	if (!Blueprint)
	{
		return;
	}
	
	// Get all nodes of CallFunction type
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallFunction>(Blueprint, OutCallNodes);
	
	// Filter to only nodes that reference our function
	OutCallNodes.RemoveAll([&FunctionName](UK2Node_CallFunction* CallNode)
	{
		return CallNode->FunctionReference.GetMemberName().ToString() != FunctionName;
	});
}

void FN2CMcpDeleteBlueprintFunctionTool::CollectReferenceInfo(const TArray<UK2Node_CallFunction*>& CallNodes, 
	TArray<FRemovedReference>& OutReferences) const
{
	OutReferences.Empty();
	
	for (UK2Node_CallFunction* CallNode : CallNodes)
	{
		if (!CallNode)
		{
			continue;
		}
		
		FRemovedReference Ref;
		Ref.GraphName = CallNode->GetGraph() ? CallNode->GetGraph()->GetFName().ToString() : TEXT("Unknown");
		Ref.NodeId = CallNode->NodeGuid.ToString();
		Ref.NodeTitle = CallNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		
		OutReferences.Add(Ref);
	}
}

bool FN2CMcpDeleteBlueprintFunctionTool::RemoveFunctionReferences(const TArray<UK2Node_CallFunction*>& CallNodes, 
	TArray<FRemovedReference>& OutRemovedReferences)
{
	// First collect reference info before deletion
	CollectReferenceInfo(CallNodes, OutRemovedReferences);
	
	// Remove each call node
	for (UK2Node_CallFunction* CallNode : CallNodes)
	{
		if (CallNode && CallNode->GetGraph())
		{
			// Mark the graph as modified for the transaction
			CallNode->GetGraph()->Modify();
			
			// Destroy the node
			CallNode->DestroyNode();
		}
	}
	
	return true;
}

bool FN2CMcpDeleteBlueprintFunctionTool::DeleteFunctionGraph(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
	if (!Blueprint || !FunctionGraph)
	{
		return false;
	}
	
	// Note: Transaction is already created in Execute(), and Blueprint->Modify() has been called
	
	// Mark the function graph as modified for the transaction
	FunctionGraph->Modify();
	
	// Remove the graph using FBlueprintEditorUtils
	FBlueprintEditorUtils::RemoveGraph(Blueprint, FunctionGraph);
	
	// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
	
	return true;
}

void FN2CMcpDeleteBlueprintFunctionTool::RefreshBlueprintEditor(UBlueprint* Blueprint) const
{
	if (!Blueprint)
	{
		return;
	}
	
	// Refresh all nodes in the Blueprint
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);
	
	// The Blueprint editor will automatically refresh when the Blueprint is marked as modified
	// which we've already done in DeleteFunctionGraph
}

void FN2CMcpDeleteBlueprintFunctionTool::ShowDeletionNotification(const FString& FunctionName, bool bSuccess) const
{
	FNotificationInfo Info(bSuccess ?
		FText::Format(LOCTEXT("FunctionDeleted", "Function '{0}' deleted successfully"), FText::FromString(FunctionName)) :
		FText::Format(LOCTEXT("FunctionDeletionFailed", "Failed to delete function '{0}'"), FText::FromString(FunctionName))
	);
	
	Info.ExpireDuration = 3.0f;
	Info.bFireAndForget = true;
	Info.Image = bSuccess ? FCoreStyle::Get().GetBrush(TEXT("Icons.SuccessWithCircle")) : FCoreStyle::Get().GetBrush(TEXT("Icons.ErrorWithCircle"));
	
	FSlateNotificationManager::Get().AddNotification(Info);
}

TSharedPtr<FJsonObject> FN2CMcpDeleteBlueprintFunctionTool::BuildSuccessResult(const FString& FunctionName, const FGuid& FunctionGuid,
	const FString& BlueprintPath, const TArray<FRemovedReference>& RemovedReferences) const
{
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	
	Result->SetBoolField(TEXT("success"), true);
	
	// Deleted function info
	TSharedPtr<FJsonObject> DeletedFunction = MakeShareable(new FJsonObject);
	DeletedFunction->SetStringField(TEXT("name"), FunctionName);
	DeletedFunction->SetStringField(TEXT("guid"), FunctionGuid.ToString());
	Result->SetObjectField(TEXT("deletedFunction"), DeletedFunction);
	
	// References removed
	TArray<TSharedPtr<FJsonValue>> ReferencesArray;
	for (const FRemovedReference& Ref : RemovedReferences)
	{
		TSharedPtr<FJsonObject> RefObject = MakeShareable(new FJsonObject);
		RefObject->SetStringField(TEXT("graphName"), Ref.GraphName);
		RefObject->SetStringField(TEXT("nodeId"), Ref.NodeId);
		RefObject->SetStringField(TEXT("nodeTitle"), Ref.NodeTitle);
		ReferencesArray.Add(MakeShareable(new FJsonValueObject(RefObject)));
	}
	Result->SetArrayField(TEXT("referencesRemoved"), ReferencesArray);
	
	// Other info
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	
	return Result;
}

FString FN2CMcpDeleteBlueprintFunctionTool::GetFunctionDisplayName(UEdGraph* FunctionGraph) const
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

UK2Node_FunctionEntry* FN2CMcpDeleteBlueprintFunctionTool::GetFunctionEntryNode(UEdGraph* FunctionGraph) const
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

#undef LOCTEXT_NAMESPACE
