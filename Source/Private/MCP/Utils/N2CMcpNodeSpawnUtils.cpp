// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "Utils/N2CLogger.h"
#include "BlueprintNodeBinder.h"
#include "BlueprintVariableNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Layout/SlateRect.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "N2CMcpNodeSpawn"

namespace N2CMcpNodeSpawnPrivate
{
	/** Invokes a variable spawner into Graph inside one transaction (the caller's, when it has one open) */
	UK2Node_Variable* Invoke(const UBlueprintVariableNodeSpawner* Spawner, UEdGraph* Graph, const FVector2D& Location, const FText& Description)
	{
		if (!Spawner)
		{
			return nullptr;
		}
		const FScopedTransaction Transaction(Description);
		Graph->Modify();
		// SpawnEdGraphNode: NewObject, CreateNewGuid, the variable reference, RF_Transactional, AllocateDefaultPins
		// once, PostPlacedNewNode, then UEdGraph::AddNode
		return Cast<UK2Node_Variable>(Spawner->Invoke(Graph, IBlueprintNodeBinder::FBindingSet(), Location));
	}

	bool CheckGraph(UBlueprint* Blueprint, UEdGraph* Graph, FString& OutError)
	{
		if (!Blueprint || !Graph)
		{
			OutError = TEXT("Invalid Blueprint or graph");
			return false;
		}
		if (FBlueprintEditorUtils::FindBlueprintForGraph(Graph) != Blueprint)
		{
			OutError = FString::Printf(TEXT("Graph '%s' is not a graph of Blueprint '%s'"), *Graph->GetName(), *Blueprint->GetName());
			return false;
		}
		return true;
	}

	/** Puts Value on the Set node's value pin through the K2 schema; a warning text when it did not take */
	FString ApplyInputValue(UK2Node_Variable* Node, const FString& Value)
	{
		UEdGraphPin* ValuePin = Node ? Node->FindPin(Node->GetVarName(), EGPD_Input) : nullptr;
		if (!ValuePin)
		{
			return TEXT("The Set node has no value pin; the value was not applied");
		}
		GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*ValuePin, Value, /*bMarkAsModified*/ false);
		if (ValuePin->DefaultValue.IsEmpty() && ValuePin->DefaultObject == nullptr && ValuePin->DefaultTextValue.IsEmpty())
		{
			return FString::Printf(TEXT("The value '%s' was not applied: the pin refused it"), *Value);
		}
		return FString();
	}

	/** The result object the four variable node tools share */
	TSharedPtr<FJsonObject> BuildResult(UK2Node_Variable* Node, UBlueprint* Blueprint, UEdGraph* Graph, const FName VariableName,
		const FEdGraphPinType& VarType, bool bSet, bool bLocal, const FString& Warning)
	{
		const TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		// The node's GUID, which connect-pins, find-nodes-in-graph and delete-blueprint-node take (card 0155)
		Result->SetStringField(TEXT("nodeId"), FN2CMcpNodeSpawn::NodeId(Node));
		FN2CMcpNodeSpawn::AddNodeFields(*Result, Node);
		Result->SetStringField(TEXT("nodeType"), bSet ? TEXT("K2Node_VariableSet") : TEXT("K2Node_VariableGet"));
		Result->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		Result->SetStringField(TEXT("variableName"), VariableName.ToString());
		Result->SetStringField(TEXT("graphName"), Graph->GetName());
		Result->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
		const TSharedPtr<FJsonObject> Location = MakeShared<FJsonObject>();
		Location->SetNumberField(TEXT("x"), Node->NodePosX);
		Location->SetNumberField(TEXT("y"), Node->NodePosY);
		Result->SetObjectField(TEXT("location"), Location);
		if (bLocal)
		{
			// The local tools' own fields, kept
			Result->SetStringField(TEXT("functionName"), FN2CMcpVariableUtils::GetFunctionGraph(Graph)->GetName());
			Result->SetNumberField(TEXT("x"), Node->NodePosX);
			Result->SetNumberField(TEXT("y"), Node->NodePosY);
		}
		Result->SetArrayField(TEXT("pins"), FN2CMcpNodeSpawn::PinsToJson(Node));
		Result->SetObjectField(TEXT("variableType"), FN2CMcpNodeSpawn::VariableTypeJson(VarType));
		if (!Warning.IsEmpty())
		{
			Result->SetStringField(TEXT("warning"), Warning);
		}
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created %s node for %svariable '%s' in graph '%s'"),
			bSet ? TEXT("a Set") : TEXT("a Get"), bLocal ? TEXT("local ") : TEXT(""), *VariableName.ToString(), *Graph->GetName()));
		return Result;
	}
}

UK2Node_Variable* FN2CMcpNodeSpawn::SpawnMemberVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FProperty* Property,
	bool bSet, const FVector2D& Location, FString& OutError)
{
	using namespace N2CMcpNodeSpawnPrivate;
	if (!CheckGraph(Blueprint, Graph, OutError))
	{
		return nullptr;
	}
	if (!Property)
	{
		OutError = TEXT("No variable to make a node for");
		return nullptr;
	}
	const TSubclassOf<UK2Node_Variable> NodeClass = bSet ? UK2Node_VariableSet::StaticClass() : UK2Node_VariableGet::StaticClass();
	// No variable context: with one the spawner treats the property as a function parameter (a local reference).
	// Owner class null: the property's own class. The spawner then makes a self reference (with the member GUID of a
	// Blueprint variable) when the Blueprint is that class or a child of it, as the My Blueprint panel does
	const UBlueprintVariableNodeSpawner* Spawner = UBlueprintVariableNodeSpawner::CreateFromMemberOrParam(NodeClass, Property);
	UK2Node_Variable* Node = Invoke(Spawner, Graph, Location,
		bSet ? LOCTEXT("SpawnSetMember", "Add Set Variable Node") : LOCTEXT("SpawnGetMember", "Add Get Variable Node"));
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Failed to create the %s node for '%s'"), bSet ? TEXT("Set") : TEXT("Get"), *Property->GetName());
	}
	return Node;
}

UK2Node_Variable* FN2CMcpNodeSpawn::SpawnLocalVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FBPVariableDescription& LocalVariable,
	bool bSet, const FVector2D& Location, FString& OutError)
{
	using namespace N2CMcpNodeSpawnPrivate;
	if (!CheckGraph(Blueprint, Graph, OutError))
	{
		return nullptr;
	}
	// The local's scope is the function, also when Graph is a collapsed graph inside it
	UEdGraph* FunctionGraph = FN2CMcpVariableUtils::GetFunctionGraph(Graph);
	if (!FunctionGraph)
	{
		OutError = FString::Printf(TEXT("NOT_A_FUNCTION_GRAPH: '%s' is not in a function; local variables live in functions"), *Graph->GetName());
		return nullptr;
	}
	// As UBlueprintActionDatabase lists them: the property when the skeleton function has it (it may not, before the
	// next compile), the description either way
	FMemberReference Reference;
	Reference.SetLocalMember(LocalVariable.VarName, FunctionGraph->GetName(), LocalVariable.VarGuid);
	FProperty* VarProperty = Blueprint->SkeletonGeneratedClass ? Reference.ResolveMember<FProperty>(Blueprint->SkeletonGeneratedClass) : nullptr;

	const TSubclassOf<UK2Node_Variable> NodeClass = bSet ? UK2Node_VariableSet::StaticClass() : UK2Node_VariableGet::StaticClass();
	const UBlueprintVariableNodeSpawner* Spawner = UBlueprintVariableNodeSpawner::CreateFromLocal(NodeClass, FunctionGraph, LocalVariable, VarProperty);
	UK2Node_Variable* Node = Invoke(Spawner, Graph, Location,
		bSet ? LOCTEXT("SpawnSetLocal", "Add Set Local Variable Node") : LOCTEXT("SpawnGetLocal", "Add Get Local Variable Node"));
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Failed to create the %s node for local '%s'"), bSet ? TEXT("Set") : TEXT("Get"), *LocalVariable.VarName.ToString());
	}
	return Node;
}

UEdGraphNode_Comment* FN2CMcpNodeSpawn::SpawnCommentNode(UEdGraph* Graph, const FSlateRect& Bounds, const FString& Text,
	const FLinearColor& Color, int32 FontSize, bool bGroupMovement, const TArray<UEdGraphNode*>& Enclosed)
{
	if (!Graph)
	{
		return nullptr;
	}
	const FScopedTransaction Transaction(LOCTEXT("SpawnComment", "Add Comment"));
	Graph->Modify();

	// The template path FEdGraphSchemaAction_K2AddComment takes: duplicate into the graph, RF_Transactional, AddNode,
	// CreateNewGuid, PostPlacedNewNode (which applies the editor's comment defaults), AllocateDefaultPins (none)
	UEdGraphNode_Comment* Template = NewObject<UEdGraphNode_Comment>(GetTransientPackage());
	const FVector2D TopLeft(Bounds.Left, Bounds.Top);
	UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(FEdGraphSchemaAction_NewNode::CreateNode(Graph, nullptr, TopLeft, Template));
	if (!Comment)
	{
		return nullptr;
	}

	// After PostPlacedNewNode, which resets the text, color and move mode to the editor defaults; the exact bounds
	// (CreateNode snaps the position to the grid)
	Comment->NodePosX = FMath::RoundToInt(Bounds.Left);
	Comment->NodePosY = FMath::RoundToInt(Bounds.Top);
	Comment->NodeWidth = FMath::RoundToInt(Bounds.GetSize().X);
	Comment->NodeHeight = FMath::RoundToInt(Bounds.GetSize().Y);
	Comment->NodeComment = Text;
	Comment->CommentColor = Color;
	Comment->FontSize = FMath::Clamp(FontSize, 1, 1000);
	Comment->MoveMode = bGroupMovement ? ECommentBoxMode::GroupMovement : ECommentBoxMode::NoGroupMovement;
	Comment->bCommentBubbleVisible_InDetailsPanel = true;
	Comment->bColorCommentBubble = true;

	Comment->ClearNodesUnderComment();
	for (UEdGraphNode* Node : Enclosed)
	{
		if (Node && Node != Comment)
		{
			Comment->AddNodeUnderComment(Node);
		}
	}
	Comment->OnUpdateCommentText(Comment->NodeComment);
	return Comment;
}

FString FN2CMcpNodeSpawn::NodeId(const UEdGraphNode* Node)
{
	return Node ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
}

void FN2CMcpNodeSpawn::AddNodeFields(FJsonObject& Result, const UEdGraphNode* Node)
{
	if (!Node)
	{
		return;
	}
	Result.SetStringField(TEXT("nodeGuid"), Node->NodeGuid.ToString());
	Result.SetStringField(TEXT("nodeName"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

	TArray<TSharedPtr<FJsonValue>> InputPins;
	TArray<TSharedPtr<FJsonValue>> OutputPins;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("pinGuid"), Pin->PinId.ToString());
		PinObj->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("displayName"), Pin->GetDisplayName().ToString());
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		const FString Default = Pin->GetDefaultAsString();
		if (Pin->Direction == EGPD_Input && !Default.IsEmpty())
		{
			PinObj->SetStringField(TEXT("defaultValue"), Default);
		}
		(Pin->Direction == EGPD_Input ? InputPins : OutputPins).Add(MakeShared<FJsonValueObject>(PinObj));
	}
	Result.SetArrayField(TEXT("inputPins"), InputPins);
	Result.SetArrayField(TEXT("outputPins"), OutputPins);
}

TArray<TSharedPtr<FJsonValue>> FN2CMcpNodeSpawn::PinsToJson(const UEdGraphNode* Node)
{
	TArray<TSharedPtr<FJsonValue>> Pins;
	if (!Node)
	{
		return Pins;
	}
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
		PinObj->SetStringField(TEXT("id"), Pin->PinId.ToString());
		PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		PinObj->SetStringField(TEXT("friendlyName"), Pin->PinFriendlyName.IsEmpty() ? Pin->PinName.ToString() : Pin->PinFriendlyName.ToString());
		if (Pin->Direction == EGPD_Input && !Pin->DefaultValue.IsEmpty())
		{
			PinObj->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
		}
		Pins.Add(MakeShared<FJsonValueObject>(PinObj));
	}
	return Pins;
}

TSharedPtr<FJsonObject> FN2CMcpNodeSpawn::VariableTypeJson(const FEdGraphPinType& PinType)
{
	const TSharedPtr<FJsonObject> TypeInfo = MakeShared<FJsonObject>();
	TypeInfo->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
	if (!PinType.PinSubCategory.IsNone())
	{
		TypeInfo->SetStringField(TEXT("subCategory"), PinType.PinSubCategory.ToString());
	}
	if (const UObject* TypeObject = PinType.PinSubCategoryObject.Get())
	{
		TypeInfo->SetStringField(TEXT("typeObject"), TypeObject->GetPathName());
		TypeInfo->SetStringField(TEXT("typeName"), TypeObject->GetName());
	}
	TypeInfo->SetBoolField(TEXT("isArray"), PinType.IsArray());
	TypeInfo->SetBoolField(TEXT("isSet"), PinType.IsSet());
	TypeInfo->SetBoolField(TEXT("isMap"), PinType.IsMap());
	return TypeInfo;
}

TSharedPtr<FJsonObject> FN2CMcpNodeSpawn::CreateMemberVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& VariableName,
	bool bSet, const FVector2D& Location, const FString& InputValue, FString& OutError)
{
	using namespace N2CMcpNodeSpawnPrivate;
	if (!CheckGraph(Blueprint, Graph, OutError))
	{
		return nullptr;
	}
	const FProperty* Property = FN2CMcpVariableUtils::FindMemberVariableProperty(Blueprint, VariableName, bSet, OutError);
	if (!Property)
	{
		return nullptr;
	}
	FEdGraphPinType VarType;
	GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, VarType);

	UK2Node_Variable* Node = nullptr;
	FString Warning;
	{
		const FScopedTransaction Transaction(bSet ? LOCTEXT("CreateSetMember", "Create Set Variable Node") : LOCTEXT("CreateGetMember", "Create Get Variable Node"));
		Node = SpawnMemberVariableNode(Blueprint, Graph, Property, bSet, Location, OutError);
		if (!Node)
		{
			return nullptr;
		}
		if (bSet && !InputValue.IsEmpty())
		{
			Warning = ApplyInputValue(Node, InputValue);
		}
	}
	if (!Warning.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(Warning);
	}
	// Marked modified; the tool call compiles it once, after its transaction closes (card 0151)
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);
	return BuildResult(Node, Blueprint, Graph, Property->GetFName(), VarType, bSet, /*bLocal*/ false, Warning);
}

TSharedPtr<FJsonObject> FN2CMcpNodeSpawn::CreateLocalVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FString& VariableName,
	bool bSet, const FVector2D& Location, const FString& InputValue, FString& OutError)
{
	using namespace N2CMcpNodeSpawnPrivate;
	if (!CheckGraph(Blueprint, Graph, OutError))
	{
		return nullptr;
	}
	UK2Node_FunctionEntry* Entry = FN2CMcpVariableUtils::FindFunctionEntryNode(Graph);
	if (!Entry)
	{
		OutError = TEXT("The focused graph is not a function graph. Please open a function in the Blueprint editor.");
		return nullptr;
	}
	const FBPVariableDescription* Local = FN2CMcpVariableUtils::FindLocalVariable(Entry, VariableName);
	if (!Local)
	{
		OutError = FString::Printf(TEXT("Local variable '%s' not found in function '%s'"), *VariableName,
			*FN2CMcpVariableUtils::GetFunctionGraph(Graph)->GetName());
		return nullptr;
	}
	// Copied: the description lives in the entry node, which the transaction may snapshot
	const FBPVariableDescription LocalCopy = *Local;

	UK2Node_Variable* Node = nullptr;
	FString Warning;
	{
		const FScopedTransaction Transaction(bSet ? LOCTEXT("CreateSetLocal", "Create Set Local Variable Node") : LOCTEXT("CreateGetLocal", "Create Get Local Variable Node"));
		Node = SpawnLocalVariableNode(Blueprint, Graph, LocalCopy, bSet, Location, OutError);
		if (!Node)
		{
			return nullptr;
		}
		if (bSet && !InputValue.IsEmpty())
		{
			Warning = ApplyInputValue(Node, InputValue);
		}
	}
	if (!Warning.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(Warning);
	}
	FN2CMcpBlueprintUtils::MarkModified(Blueprint, /*bStructural*/ false);
	return BuildResult(Node, Blueprint, Graph, LocalCopy.VarName, LocalCopy.VarType, bSet, /*bLocal*/ true, Warning);
}

#undef LOCTEXT_NAMESPACE
