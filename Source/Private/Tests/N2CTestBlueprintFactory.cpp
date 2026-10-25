// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/N2CTestBlueprintFactory.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/Actor.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/Guid.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace N2CTest
{
	/**
	 * Keeps the editor's undo history out of the user struct and enum edits: FEnumEditorUtils and
	 * FStructureEditorUtils open a transaction for every change, and the transaction buffer would then keep the test
	 * package alive (and add the test's edits to the user's undo history when tests run in an interactive editor)
	 */
	struct FNoTransactions
	{
		TOptional<TGuardValue<TObjectPtr<UTransactor>>> Guard;
		FNoTransactions()
		{
			if (GEditor)
			{
				Guard.Emplace(GEditor->Trans, nullptr);
			}
		}
	};

	FTestBlueprint::FTestBlueprint(const FString& BlueprintName, const FString& MountRoot)
	{
		const FString PackageName = FString::Printf(TEXT("%s/N2CTest_%s"), *MountRoot, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Package = CreatePackage(*PackageName);
		if (!Package)
		{
			AddError(TEXT("could not create the transient package ") + PackageName);
			return;
		}
		Package->SetFlags(RF_Transient);
		Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package, FName(*BlueprintName),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint)
		{
			AddError(TEXT("FKismetEditorUtilities::CreateBlueprint failed for ") + BlueprintName);
		}
	}

	FTestBlueprint::~FTestBlueprint()
	{
		if (Blueprint)
		{
			Blueprint->ClearFlags(RF_Standalone | RF_Public);
			if (Blueprint->GeneratedClass)
			{
				Blueprint->GeneratedClass->ClearFlags(RF_Standalone | RF_Public);
			}
		}
		if (Package)
		{
			// The user structs, enums and input actions made in the package too
			ForEachObjectWithPackage(Package, [](UObject* Object)
			{
				Object->ClearFlags(RF_Standalone | RF_Public);
				return true;
			});
			// The struct compiler leaves standalone STRUCT_REINST_ copies of a user struct in the transient package; they
			// reference the struct's member types (a user enum here) and so would keep the package alive
			for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
			{
				const UUserDefinedStruct* Primary = It->PrimaryStruct.Get();
				if (It->Status == EUserDefinedStructureStatus::UDSS_Duplicate && Primary && Primary->GetPackage() == Package)
				{
					It->ClearFlags(RF_Standalone | RF_Public);
				}
			}
			Package->ClearFlags(RF_Standalone);
		}
	}

	FString FTestBlueprint::GetPackagePath() const
	{
		return Package ? Package->GetName() : FString();
	}

	UEdGraph* FTestBlueprint::EventGraph() const
	{
		return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
	}

	UEdGraph* FTestBlueprint::AddFunction(const FName Name)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!Graph)
		{
			AddError(TEXT("could not create function graph ") + Name.ToString());
			return nullptr;
		}
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, /*bIsUserCreated*/ true, nullptr);
		return Graph;
	}

	UK2Node_FunctionEntry* FTestBlueprint::FunctionEntry(UEdGraph* FunctionGraph) const
	{
		if (FunctionGraph)
		{
			for (UEdGraphNode* Node : FunctionGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
				{
					return Entry;
				}
			}
		}
		return nullptr;
	}

	UEdGraphPin* FTestBlueprint::AddFunctionInput(UEdGraph* FunctionGraph, const FName Name, const FEdGraphPinType& Type)
	{
		UK2Node_FunctionEntry* Entry = FunctionEntry(FunctionGraph);
		if (!Entry)
		{
			AddError(TEXT("no function entry node for input ") + Name.ToString());
			return nullptr;
		}
		UEdGraphPin* NewPin = Entry->CreateUserDefinedPin(Name, Type, EGPD_Output, /*bUseUniqueName*/ false);
		if (!NewPin)
		{
			AddError(TEXT("could not add function input ") + Name.ToString());
		}
		return NewPin;
	}

	UK2Node_FunctionResult* FTestBlueprint::AddFunctionOutput(UEdGraph* FunctionGraph, const FName Name, const FEdGraphPinType& Type)
	{
		if (!FunctionGraph)
		{
			return nullptr;
		}
		UK2Node_FunctionResult* Result = nullptr;
		for (UEdGraphNode* Node : FunctionGraph->Nodes)
		{
			if (UK2Node_FunctionResult* Existing = Cast<UK2Node_FunctionResult>(Node))
			{
				Result = Existing;
				break;
			}
		}
		if (!Result)
		{
			FGraphNodeCreator<UK2Node_FunctionResult> Creator(*FunctionGraph);
			Result = Creator.CreateNode();
			if (UK2Node_FunctionEntry* Entry = FunctionEntry(FunctionGraph))
			{
				Result->FunctionReference = Entry->FunctionReference;
			}
			Creator.Finalize();
		}
		if (!Result->CreateUserDefinedPin(Name, Type, EGPD_Input, /*bUseUniqueName*/ false))
		{
			AddError(TEXT("could not add function output ") + Name.ToString());
		}
		return Result;
	}

	UK2Node_CustomEvent* FTestBlueprint::AddCustomEvent(UEdGraph* Graph, const FName EventName)
	{
		if (!Graph)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
		UK2Node_CustomEvent* Node = Creator.CreateNode();
		Node->CustomFunctionName = EventName;
		Creator.Finalize();
		return Node;
	}

	UK2Node_CallFunction* FTestBlueprint::AddCall(UEdGraph* Graph, UClass* Owner, const FName FunctionName)
	{
		UFunction* Function = Owner ? Owner->FindFunctionByName(FunctionName) : nullptr;
		if (!Graph || !Function)
		{
			AddError(TEXT("no function ") + FunctionName.ToString());
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode();
		Node->SetFromFunction(Function);
		Creator.Finalize();
		return Node;
	}

	UK2Node_IfThenElse* FTestBlueprint::AddBranch(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
		UK2Node_IfThenElse* Node = Creator.CreateNode();
		Creator.Finalize();
		return Node;
	}

	UK2Node_Knot* FTestBlueprint::AddKnot(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_Knot> Creator(*Graph);
		UK2Node_Knot* Node = Creator.CreateNode();
		Creator.Finalize();
		return Node;
	}

	UK2Node_SwitchInteger* FTestBlueprint::AddSwitchOnInt(UEdGraph* Graph, int32 CaseCount)
	{
		if (!Graph)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_SwitchInteger> Creator(*Graph);
		UK2Node_SwitchInteger* Node = Creator.CreateNode();
		Creator.Finalize();
		for (int32 Index = 0; Index < CaseCount; ++Index)
		{
			Node->AddPinToSwitchNode();
		}
		return Node;
	}

	UK2Node_SwitchEnum* FTestBlueprint::AddSwitchOnEnum(UEdGraph* Graph, UEnum* Enum)
	{
		if (!Graph || !Enum)
		{
			AddError(TEXT("no enum for Switch on enum"));
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_SwitchEnum> Creator(*Graph);
		UK2Node_SwitchEnum* Node = Creator.CreateNode();
		Node->SetEnum(Enum);
		Creator.Finalize();
		return Node;
	}

	UK2Node_MakeStruct* FTestBlueprint::AddMakeStruct(UEdGraph* Graph, UScriptStruct* Struct)
	{
		if (!Graph || !Struct)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_MakeStruct> Creator(*Graph);
		UK2Node_MakeStruct* Node = Creator.CreateNode();
		Node->StructType = Struct;
		Creator.Finalize();
		return Node;
	}

	UK2Node_BreakStruct* FTestBlueprint::AddBreakStruct(UEdGraph* Graph, UScriptStruct* Struct)
	{
		if (!Graph || !Struct)
		{
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_BreakStruct> Creator(*Graph);
		UK2Node_BreakStruct* Node = Creator.CreateNode();
		Node->StructType = Struct;
		Creator.Finalize();
		return Node;
	}

	UK2Node_MacroInstance* FTestBlueprint::AddStandardMacro(UEdGraph* Graph, const FName MacroName)
	{
		UBlueprint* Macros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
		UEdGraph* MacroGraph = nullptr;
		if (Macros)
		{
			for (UEdGraph* Candidate : Macros->MacroGraphs)
			{
				if (Candidate && Candidate->GetFName() == MacroName)
				{
					MacroGraph = Candidate;
					break;
				}
			}
		}
		if (!Graph || !MacroGraph)
		{
			AddError(TEXT("no standard macro ") + MacroName.ToString());
			return nullptr;
		}
		return AddMacroInstance(Graph, MacroGraph);
	}

	UK2Node_MacroInstance* FTestBlueprint::AddMacroInstance(UEdGraph* Graph, UEdGraph* MacroGraph)
	{
		if (!Graph || !MacroGraph)
		{
			AddError(TEXT("no graph or macro graph for a macro instance"));
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_MacroInstance> Creator(*Graph);
		UK2Node_MacroInstance* Node = Creator.CreateNode();
		Node->SetMacroGraph(MacroGraph);
		Creator.Finalize();
		return Node;
	}

	UK2Node_Timeline* FTestBlueprint::AddTimeline(UEdGraph* Graph, const FName TimelineName)
	{
		if (!Graph || !Blueprint)
		{
			return nullptr;
		}
		// What the Timeline spawner does, with a fixed name instead of the localised "Timeline_N"
		if (!FBlueprintEditorUtils::AddNewTimeline(Blueprint, TimelineName))
		{
			AddError(TEXT("could not add timeline template ") + TimelineName.ToString());
			return nullptr;
		}
		FGraphNodeCreator<UK2Node_Timeline> Creator(*Graph);
		UK2Node_Timeline* Node = Creator.CreateNode();
		Node->TimelineName = TimelineName;
		Creator.Finalize();
		return Node;
	}

	UK2Node_CallFunction* FTestBlueprint::AddSelfCall(UEdGraph* Graph, const FName FunctionName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		// Regenerates the skeleton class, so functions and parameters added since exist on it
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return AddCall(Graph, Blueprint->SkeletonGeneratedClass, FunctionName);
	}

	UEdGraph* FTestBlueprint::AddMacro(const FName Name)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!Graph)
		{
			AddError(TEXT("could not create macro graph ") + Name.ToString());
			return nullptr;
		}
		FBlueprintEditorUtils::AddMacroGraph(Blueprint, Graph, /*bIsUserCreated*/ true, nullptr);
		return Graph;
	}

	UK2Node_Composite* FTestBlueprint::AddCollapsedGraph(UEdGraph* Graph, const FName GraphName)
	{
		if (!Graph)
		{
			return nullptr;
		}
		// PostPlacedNewNode makes the bound graph and its entry and exit tunnels
		FGraphNodeCreator<UK2Node_Composite> Creator(*Graph);
		UK2Node_Composite* Node = Creator.CreateNode();
		Creator.Finalize();
		if (!Node->BoundGraph)
		{
			AddError(TEXT("the collapsed graph node has no bound graph"));
			return nullptr;
		}
		FBlueprintEditorUtils::RenameGraph(Node->BoundGraph, GraphName.ToString());
		return Node;
	}

	static UK2Node_Tunnel* FindTestTunnel(UEdGraph* TunnelGraph, const bool bEntry)
	{
		if (TunnelGraph)
		{
			for (UEdGraphNode* Node : TunnelGraph->Nodes)
			{
				UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node);
				if (Tunnel && (bEntry ? Tunnel->bCanHaveOutputs && !Tunnel->bCanHaveInputs
					: Tunnel->bCanHaveInputs && !Tunnel->bCanHaveOutputs))
				{
					return Tunnel;
				}
			}
		}
		return nullptr;
	}

	UK2Node_Tunnel* FTestBlueprint::TunnelEntry(UEdGraph* TunnelGraph)
	{
		UK2Node_Tunnel* Tunnel = FindTestTunnel(TunnelGraph, true);
		if (!Tunnel)
		{
			AddError(TEXT("no entry tunnel"));
		}
		return Tunnel;
	}

	UK2Node_Tunnel* FTestBlueprint::TunnelExit(UEdGraph* TunnelGraph)
	{
		UK2Node_Tunnel* Tunnel = FindTestTunnel(TunnelGraph, false);
		if (!Tunnel)
		{
			AddError(TEXT("no exit tunnel"));
		}
		return Tunnel;
	}

	UEdGraphPin* FTestBlueprint::AddTunnelPin(UK2Node_Tunnel* Tunnel, const FName Name, const FEdGraphPinType& Type)
	{
		if (!Tunnel)
		{
			return nullptr;
		}
		const EEdGraphPinDirection Direction = Tunnel->bCanHaveOutputs ? EGPD_Output : EGPD_Input;
		UEdGraphPin* NewPin = Tunnel->CreateUserDefinedPin(Name, Type, Direction, /*bUseUniqueName*/ false);
		if (!NewPin)
		{
			AddError(TEXT("could not add tunnel pin ") + Name.ToString());
		}
		return NewPin;
	}

	TArray<UEdGraphNode*> FTestBlueprint::CopyPaste(UEdGraph* Graph, const TArray<UEdGraphNode*>& Nodes)
	{
		TArray<UEdGraphNode*> Result;
		if (!Graph)
		{
			return Result;
		}
		// FBlueprintEditor::CopySelectedNodes, then PasteNodesHere without the clipboard
		TSet<UObject*> ToExport;
		for (UEdGraphNode* Node : Nodes)
		{
			if (Node)
			{
				Node->PrepareForCopying();
				ToExport.Add(Node);
			}
		}
		FString Text;
		FEdGraphUtilities::ExportNodesToText(ToExport, Text);
		TSet<UEdGraphNode*> Pasted;
		FEdGraphUtilities::ImportNodesFromText(Graph, Text, Pasted);
		for (UEdGraphNode* Node : Pasted)
		{
			// The editor gives each pasted node a new GUID; pin IDs are imported unchanged
			Node->CreateNewGuid();
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Pasted.Contains(Node))
			{
				Result.Add(Node);
			}
		}
		if (Result.Num() != ToExport.Num())
		{
			AddError(FString::Printf(TEXT("copied %d nodes, pasted %d"), ToExport.Num(), Result.Num()));
		}
		return Result;
	}

	UEdGraphNode* FTestBlueprint::AddEnhancedInputAction(UEdGraph* Graph, const FName ActionName)
	{
		UClass* NodeClass = FindObject<UClass>(nullptr, TEXT("/Script/InputBlueprintNodes.K2Node_EnhancedInputAction"));
		UClass* ActionClass = FindObject<UClass>(nullptr, TEXT("/Script/EnhancedInput.InputAction"));
		FObjectPropertyBase* ActionProperty = NodeClass ? FindFProperty<FObjectPropertyBase>(NodeClass, TEXT("InputAction")) : nullptr;
		if (!Graph || !Package || !NodeClass || !ActionClass || !ActionProperty)
		{
			AddError(TEXT("no K2Node_EnhancedInputAction or InputAction class (is the EnhancedInput plugin enabled?)"));
			return nullptr;
		}
		UObject* Action = NewObject<UObject>(Package, ActionClass, ActionName, RF_Public | RF_Standalone | RF_Transactional);
		// What FGraphNodeCreator does, for a class only known at run time
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, /*bUserAction*/ false, /*bSelectNewNode*/ false);
		ActionProperty->SetObjectPropertyValue_InContainer(Node, Action);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		if (Node->Pins.Num() == 0)
		{
			Node->AllocateDefaultPins();
		}
		return Node;
	}

	UUserDefinedEnum* FTestBlueprint::AddUserEnum(const FName Name, const TArray<FString>& DisplayNames)
	{
		if (!Package)
		{
			return nullptr;
		}
		FNoTransactions NoTransactions;
		UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package, Name,
			RF_Public | RF_Standalone | RF_Transactional));
		if (!Enum)
		{
			AddError(TEXT("could not create user enum ") + Name.ToString());
			return nullptr;
		}
		for (int32 Index = 0; Index < DisplayNames.Num(); ++Index)
		{
			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
			if (!FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, FText::FromString(DisplayNames[Index])))
			{
				AddError(FString::Printf(TEXT("could not name enumerator %d of %s '%s'"), Index, *Name.ToString(), *DisplayNames[Index]));
			}
		}
		return Enum;
	}

	UUserDefinedStruct* FTestBlueprint::AddUserStruct(const FName Name, const TArray<TPair<FString, FEdGraphPinType>>& Members)
	{
		if (!Package || Members.Num() == 0)
		{
			return nullptr;
		}
		FNoTransactions NoTransactions;
		// CreateUserDefinedStruct adds one bool member; it becomes the first member
		UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(Package, Name,
			RF_Public | RF_Standalone | RF_Transactional);
		if (!Struct || FStructureEditorUtils::GetVarDesc(Struct).Num() != 1)
		{
			AddError(TEXT("could not create user struct ") + Name.ToString());
			return nullptr;
		}
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const FEdGraphPinType& Type = Members[Index].Value;
			const bool bTyped = Index == 0
				? FStructureEditorUtils::ChangeVariableType(Struct, FStructureEditorUtils::GetVarDesc(Struct)[0].VarGuid, Type)
				: FStructureEditorUtils::AddVariable(Struct, Type);
			const TArray<FStructVariableDescription>& Vars = FStructureEditorUtils::GetVarDesc(Struct);
			if (!bTyped || !Vars.IsValidIndex(Index)
				|| !FStructureEditorUtils::RenameVariable(Struct, Vars[Index].VarGuid, Members[Index].Key))
			{
				AddError(FString::Printf(TEXT("could not add member %s to %s"), *Members[Index].Key, *Name.ToString()));
				return nullptr;
			}
		}
		return Struct;
	}

	UEdGraphPin* FTestBlueprint::Pin(UEdGraphNode* Node, const FName PinName, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			AddError(TEXT("no node for pin ") + PinName.ToString());
			return nullptr;
		}
		for (UEdGraphPin* Candidate : Node->Pins)
		{
			if (Candidate && Candidate->PinName == PinName && (Direction == EGPD_MAX || Candidate->Direction == Direction))
			{
				return Candidate;
			}
		}
		TArray<FString> Names;
		for (UEdGraphPin* Candidate : Node->Pins)
		{
			if (Candidate)
			{
				Names.Add(Candidate->PinName.ToString());
			}
		}
		AddError(FString::Printf(TEXT("%s has no pin '%s' (pins: %s)"), *Node->GetClass()->GetName(), *PinName.ToString(),
			*FString::Join(Names, TEXT(", "))));
		return nullptr;
	}

	bool FTestBlueprint::Link(UEdGraphPin* A, UEdGraphPin* B)
	{
		if (!A || !B)
		{
			AddError(TEXT("link with a missing pin"));
			return false;
		}
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		if (!Schema->TryCreateConnection(A, B))
		{
			AddError(FString::Printf(TEXT("the K2 schema refused %s.%s -> %s.%s"),
				*A->GetOwningNode()->GetClass()->GetName(), *A->PinName.ToString(),
				*B->GetOwningNode()->GetClass()->GetName(), *B->PinName.ToString()));
			return false;
		}
		return true;
	}

	bool FTestBlueprint::Link(UEdGraphNode* From, const FName FromPin, UEdGraphNode* To, const FName ToPin)
	{
		return Link(Pin(From, FromPin, EGPD_Output), Pin(To, ToPin, EGPD_Input));
	}

	FEdGraphPinType FTestBlueprint::IntPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Int;
		return Type;
	}

	FEdGraphPinType FTestBlueprint::BoolPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		return Type;
	}

	FEdGraphPinType FTestBlueprint::EnumPinType(UEnum* Enum)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Byte;
		Type.PinSubCategoryObject = Enum;
		return Type;
	}

	FEdGraphPinType FTestBlueprint::ExecPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Exec;
		return Type;
	}

	FEdGraphPinType FTestBlueprint::ObjectPinType(UClass* Class)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Object;
		Type.PinSubCategoryObject = Class;
		return Type;
	}

	FEdGraphPinType FTestBlueprint::StructPinType(UScriptStruct* Struct)
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
		Type.PinSubCategoryObject = Struct;
		return Type;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
