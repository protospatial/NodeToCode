// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/N2CTestBlueprintFactory.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
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
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace N2CTest
{
	FTestBlueprint::FTestBlueprint(const FString& BlueprintName)
	{
		const FString PackageName = FString::Printf(TEXT("/Temp/N2CTest_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
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
}

#endif // WITH_DEV_AUTOMATION_TESTS
