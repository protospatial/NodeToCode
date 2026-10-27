// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpDeleteBlueprintFunctionTool.h"
#include "MCP/Utils/N2CMcpNotification.h"
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
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"
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
		
		// Every call to this function (card 0280): in this Blueprint, matched by the function the node resolves to
		// rather than by name, so a call to another class's same-named function is never counted or deleted; in other
		// loaded Blueprints; and Blueprints the asset registry says reference this one but are not loaded
		FFunctionReferences References;
		FindFunctionReferences(TargetBlueprint, FunctionGraph->GetFName(), References);

		// Refused before anything changes: no transaction, the package stays clean
		if (!bForce && (References.Local.Num() > 0 || References.External.Num() > 0 || References.UncheckedReferencers.Num() > 0))
		{
			TArray<FString> Parts;
			if (References.Local.Num() > 0)
			{
				Parts.Add(FString::Printf(TEXT("%d call node(s) in %s"), References.Local.Num(), *TargetBlueprint->GetName()));
			}
			if (References.External.Num() > 0)
			{
				TArray<FString> Owners;
				for (const UK2Node_CallFunction* CallNode : References.External)
				{
					if (const UBlueprint* Owner = FBlueprintEditorUtils::FindBlueprintForNode(CallNode))
					{
						Owners.AddUnique(Owner->GetPathName());
					}
				}
				Parts.Add(FString::Printf(TEXT("%d call node(s) in other Blueprints (%s), which force does not remove: they stop compiling"),
					References.External.Num(), *FString::Join(Owners, TEXT(", "))));
			}
			if (References.UncheckedReferencers.Num() > 0)
			{
				Parts.Add(FString::Printf(TEXT("%d Blueprint(s) that reference %s and are not loaded, so their calls could not be checked (%s)"),
					References.UncheckedReferencers.Num(), *TargetBlueprint->GetName(), *FString::Join(References.UncheckedReferencers, TEXT(", "))));
			}
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(FString::Printf(
				TEXT("Function '%s' has references and cannot be deleted: %s. Use force=true to remove anyway."),
				*FunctionName, *FString::Join(Parts, TEXT("; "))
			)));
		}

		TArray<FRemovedReference> ExternalReferences;
		CollectReferenceInfo(References.External, ExternalReferences);

		// Create a single transaction for all operations
		const FScopedTransaction Transaction(LOCTEXT("DeleteBlueprintFunction", "Delete Blueprint Function"));

		// Mark the Blueprint as modified for the transaction
		TargetBlueprint->Modify();

		// Remove this Blueprint's call nodes within the transaction
		TArray<FRemovedReference> RemovedReferences;
		if (References.Local.Num() > 0 && !RemoveFunctionReferences(TargetBlueprint, References.Local, RemovedReferences))
		{
			return Target.Finish(FMcpToolCallResult::CreateErrorResult(TEXT("Failed to remove function references")));
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
			RemovedReferences,
			ExternalReferences,
			References.UncheckedReferencers
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

bool FN2CMcpDeleteBlueprintFunctionTool::CallsBlueprintFunction(const UK2Node_CallFunction* CallNode, const UBlueprint* Blueprint,
	const FName FunctionName)
{
	if (!CallNode || !Blueprint || CallNode->FunctionReference.GetMemberName() != FunctionName)
	{
		return false;
	}
	if (const UFunction* Function = CallNode->GetTargetFunction())
	{
		// Owned by the Blueprint's skeleton or generated class (or an older generation of either)
		const UClass* Owner = Function->GetOwnerClass();
		return Owner && (Owner == Blueprint->SkeletonGeneratedClass || Owner == Blueprint->GeneratedClass
			|| Owner->ClassGeneratedBy == Blueprint);
	}
	// Unresolved: only a self call from inside this Blueprint can still mean this function
	return CallNode->FunctionReference.IsSelfContext() && FBlueprintEditorUtils::FindBlueprintForNode(CallNode) == Blueprint;
}

void FN2CMcpDeleteBlueprintFunctionTool::FindFunctionReferences(UBlueprint* Blueprint, const FName FunctionName,
	FFunctionReferences& OutReferences) const
{
	OutReferences = FFunctionReferences();
	if (!Blueprint)
	{
		return;
	}

	// This Blueprint, and every other loaded one (a child Blueprint, or any Blueprint calling it through a reference)
	TSet<FName> ScannedPackages;
	for (TObjectIterator<UBlueprint> It; It; ++It)
	{
		UBlueprint* Other = *It;
		if (!IsValid(Other) || Other->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			continue;
		}
		ScannedPackages.Add(Other->GetOutermost()->GetFName());
		TArray<UK2Node_CallFunction*> CallNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallFunction>(Other, CallNodes);
		for (UK2Node_CallFunction* CallNode : CallNodes)
		{
			if (CallsBlueprintFunction(CallNode, Blueprint, FunctionName))
			{
				(Other == Blueprint ? OutReferences.Local : OutReferences.External).Add(CallNode);
			}
		}
	}

	// Blueprints on disk that hard-reference this one and are not loaded: their call sites cannot be checked unloaded
	const FName PackageName = Blueprint->GetOutermost()->GetFName();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(PackageName, Referencers, UE::AssetRegistry::EDependencyCategory::Package,
		UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
	for (const FName Referencer : Referencers)
	{
		if (Referencer == PackageName || ScannedPackages.Contains(Referencer))
		{
			continue;
		}
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPackageName(Referencer, Assets, /*bIncludeOnlyOnDiskAssets*/ true);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.IsInstanceOf(UBlueprint::StaticClass()))
			{
				OutReferences.UncheckedReferencers.AddUnique(Asset.GetObjectPathString());
			}
		}
	}
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
		if (const UBlueprint* Owner = FBlueprintEditorUtils::FindBlueprintForNode(CallNode))
		{
			Ref.BlueprintPath = Owner->GetPathName();
		}
		Ref.GraphName = CallNode->GetGraph() ? CallNode->GetGraph()->GetFName().ToString() : TEXT("Unknown");
		Ref.NodeId = CallNode->NodeGuid.ToString();
		Ref.NodeTitle = CallNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		
		OutReferences.Add(Ref);
	}
}

bool FN2CMcpDeleteBlueprintFunctionTool::RemoveFunctionReferences(UBlueprint* Blueprint, const TArray<UK2Node_CallFunction*>& CallNodes,
	TArray<FRemovedReference>& OutRemovedReferences)
{
	// First collect reference info before deletion
	CollectReferenceInfo(CallNodes, OutRemovedReferences);
	
	// Remove each call node
	for (UK2Node_CallFunction* CallNode : CallNodes)
	{
		if (CallNode && CallNode->GetGraph())
		{
			// As the editor deletes a node: Modify the graph and node, break the links, remove it (the call compiles later)
			FBlueprintEditorUtils::RemoveNode(Blueprint, CallNode, /*bDontRecompile*/ true);
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

	// The editor's tab on the function would otherwise stay open on a graph the Blueprint no longer owns, and the
	// focused-graph tools would edit it (card 0280)
	CloseFunctionTabs(Blueprint, FunctionGraph);
	
	// Remove the graph using FBlueprintEditorUtils
	FBlueprintEditorUtils::RemoveGraph(Blueprint, FunctionGraph);
	
	// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ true);
	
	return true;
}

void FN2CMcpDeleteBlueprintFunctionTool::CloseFunctionTabs(UBlueprint* Blueprint, UEdGraph* FunctionGraph) const
{
	if (!Blueprint || !FunctionGraph)
	{
		return;
	}
	// Only an editor already open; every Blueprint editor is an FBlueprintEditor
	const TSharedPtr<FBlueprintEditor> Editor = StaticCastSharedPtr<FBlueprintEditor>(FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, /*bOpenEditor*/ false));
	if (!Editor.IsValid())
	{
		return;
	}
	TArray<UEdGraph*> Graphs;
	FunctionGraph->GetAllChildrenGraphs(Graphs);
	Graphs.Add(FunctionGraph);
	for (const UEdGraph* Graph : Graphs)
	{
		Editor->CloseDocumentTab(Graph);
	}
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
	FN2CMcpNotification::Show(bSuccess ?
		FText::Format(LOCTEXT("FunctionDeleted", "Function '{0}' deleted successfully"), FText::FromString(FunctionName)) :
		FText::Format(LOCTEXT("FunctionDeletionFailed", "Failed to delete function '{0}'"), FText::FromString(FunctionName)), bSuccess);
}

TSharedPtr<FJsonObject> FN2CMcpDeleteBlueprintFunctionTool::BuildSuccessResult(const FString& FunctionName, const FGuid& FunctionGuid,
	const FString& BlueprintPath, const TArray<FRemovedReference>& RemovedReferences,
	const TArray<FRemovedReference>& ExternalReferences, const TArray<FString>& UncheckedReferencers) const
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

	// What force left behind (card 0280): call nodes in other Blueprints, which no longer compile, and referencing
	// Blueprints that were not loaded and so not checked
	if (ExternalReferences.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ExternalArray;
		for (const FRemovedReference& Ref : ExternalReferences)
		{
			TSharedPtr<FJsonObject> RefObject = MakeShareable(new FJsonObject);
			RefObject->SetStringField(TEXT("blueprintPath"), Ref.BlueprintPath);
			RefObject->SetStringField(TEXT("graphName"), Ref.GraphName);
			RefObject->SetStringField(TEXT("nodeId"), Ref.NodeId);
			RefObject->SetStringField(TEXT("nodeTitle"), Ref.NodeTitle);
			ExternalArray.Add(MakeShareable(new FJsonValueObject(RefObject)));
		}
		Result->SetArrayField(TEXT("externalReferencesLeft"), ExternalArray);
	}
	if (UncheckedReferencers.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> UncheckedArray;
		for (const FString& Path : UncheckedReferencers)
		{
			UncheckedArray.Add(MakeShareable(new FJsonValueString(Path)));
		}
		Result->SetArrayField(TEXT("uncheckedReferencers"), UncheckedArray);
	}
	
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
