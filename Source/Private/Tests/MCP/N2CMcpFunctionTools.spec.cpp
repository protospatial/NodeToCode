// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"

/**
 * The function signature tools through the tool manager on a multi-return function (card 0147, review
 * imp-mcp-tools-graph-functions-9): MyFunc(Value: int) -> Result: int with two Return nodes (a Branch's two outputs),
 * called from the event graph and from another function (exec links only: removing a parameter whose call-site pin is
 * linked leaves that pin orphaned with "In use pin ... no longer exists", in the editor too), named by blueprintPath
 * plus graphGuid so every case runs on every engine. Value feeds Result on both Return nodes inside MyFunc, so the
 * remove cases break and restore links. Each of add-function-input-pin, add-function-return-pin,
 * remove-function-entry-pin and remove-function-return-pin leaves the package dirty, one undo step, symmetric links,
 * both Return nodes with the same outputs and a Blueprint that compiles with no errors, and Undo then Redo restore every
 * node, pin, link and UserDefinedPins entry exactly (N2CTest::Snapshot). Already covered in
 * NodeToCode.MCP.FunctionPinEdits and not repeated: the call sites get exactly the new signature, the tooltip, a Return
 * node made for a function without one, refused pin names with no transaction, the Python bridge (UE 5.8); and in
 * NodeToCode.MCP.GraphTarget: editing a function by the ID it had before a rename, READ_ONLY_GRAPH and
 * NOT_A_FUNCTION_GRAPH.
 */
BEGIN_DEFINE_SPEC(FN2CMcpFunctionToolsSpec, "NodeToCode.MCP.Functions", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* MyFunc = nullptr;
	UK2Node_FunctionEntry* MyEntry = nullptr;
	UK2Node_FunctionResult* ReturnA = nullptr;
	UK2Node_FunctionResult* ReturnB = nullptr;

	bool Build(const TCHAR* Name);
	/** {blueprintPath, graphGuid, pinName[, typeIdentifier]} for MyFunc */
	TSharedPtr<FJsonObject> PinArgs(const FString& PinName, const FString& TypeIdentifier = FString()) const;
	/** Runs Tool on MyFunc and checks every invariant; Check runs after the edit, before Undo */
	void ExpectEdit(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TFunction<void()> Check);
	/** Both Return nodes have the same user-defined outputs, in the same order */
	void ExpectReturnsAgree(const FString& What);
END_DEFINE_SPEC(FN2CMcpFunctionToolsSpec)

bool FN2CMcpFunctionToolsSpec::Build(const TCHAR* Name)
{
	using N2CTest::FTestBlueprint;
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	MyFunc = Test->AddFunction(TEXT("MyFunc"));
	Test->AddFunctionInput(MyFunc, TEXT("Value"), FTestBlueprint::IntPinType());
	MyEntry = Test->FunctionEntry(MyFunc);
	ReturnA = MyEntry ? FBlueprintEditorUtils::FindOrCreateFunctionResultNode(MyEntry) : nullptr;
	if (!TestNotNull(TEXT("MyFunc entry and Return A"), ReturnA))
	{
		return false;
	}
	ReturnA->CreateUserDefinedPin(TEXT("Result"), FTestBlueprint::IntPinType(), EGPD_Input, /*bUseUniqueName*/ false);
	{
		FGraphNodeCreator<UK2Node_FunctionResult> Creator(*MyFunc);
		ReturnB = Creator.CreateNode();
		ReturnB->FunctionReference = MyEntry->FunctionReference;
		ReturnB->bIsEditable = true;
		ReturnB->NodePosX = ReturnA->NodePosX;
		ReturnB->NodePosY = ReturnA->NodePosY + 200;
		Creator.Finalize(); // PostPlacedNewNode copies Return A's outputs
	}
	MyEntry->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
	UK2Node_IfThenElse* Branch = Test->AddBranch(MyFunc);
	Test->Link(MyEntry, UEdGraphSchema_K2::PN_Then, Branch, UEdGraphSchema_K2::PN_Execute);
	Test->Link(Branch, UEdGraphSchema_K2::PN_Then, ReturnA, UEdGraphSchema_K2::PN_Execute);
	Test->Link(Branch, UEdGraphSchema_K2::PN_Else, ReturnB, UEdGraphSchema_K2::PN_Execute);
	Test->Link(MyEntry, TEXT("Value"), ReturnA, TEXT("Result"));
	Test->Link(MyEntry, TEXT("Value"), ReturnB, TEXT("Result"));

	UEdGraph* Caller = Test->AddFunction(TEXT("Caller"));
	Test->AddFunctionInput(Caller, TEXT("In"), FTestBlueprint::IntPinType());
	UK2Node_CallFunction* CallerCall = Test->AddSelfCall(Caller, TEXT("MyFunc"));
	Test->Link(Test->FunctionEntry(Caller), UEdGraphSchema_K2::PN_Then, CallerCall, UEdGraphSchema_K2::PN_Execute);

	UEdGraph* EventGraph = Test->EventGraph();
	UK2Node_CustomEvent* Go = Test->AddCustomEvent(EventGraph, TEXT("Go"));
	UK2Node_CallFunction* EventCall = Test->AddSelfCall(EventGraph, TEXT("MyFunc"));
	Test->Link(Go, UEdGraphSchema_K2::PN_Then, EventCall, UEdGraphSchema_K2::PN_Execute);

	if (!TestTrue(TEXT("test graph built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0))
	{
		return false;
	}
	Test->Settle();
	TestEqual(TEXT("baseline: two Return nodes in MyFunc"), FN2CMcpFunctionPinUtils::GetAllResultNodes(MyFunc).Num(), 2);
	const bool bCompiles = N2CTest::ExpectCompilesClean(*this, TEXT("baseline"), Blueprint);
	Test->Settle();
	return bCompiles;
}

TSharedPtr<FJsonObject> FN2CMcpFunctionToolsSpec::PinArgs(const FString& PinName, const FString& TypeIdentifier) const
{
	const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, MyFunc);
	Args->SetStringField(TEXT("pinName"), PinName);
	if (!TypeIdentifier.IsEmpty())
	{
		Args->SetStringField(TEXT("typeIdentifier"), TypeIdentifier);
	}
	return Args;
}

void FN2CMcpFunctionToolsSpec::ExpectReturnsAgree(const FString& What)
{
	auto Names = [](const UK2Node_FunctionResult* Node)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FUserPinInfo>& Info : Node->UserDefinedPins)
		{
			Out.Add(Info.IsValid() ? Info->PinName.ToString() : FString(TEXT("<null>")));
		}
		return FString::Join(Out, TEXT(","));
	};
	TestEqual(What + TEXT(": both Return nodes have the same outputs"), Names(ReturnB), Names(ReturnA));
}

void FN2CMcpFunctionToolsSpec::ExpectEdit(const FString& Tool, const TSharedPtr<FJsonObject>& Args, TFunction<void()> Check)
{
	const FString Before = N2CTest::Snapshot(Blueprint);
	const int32 QueueBefore = N2CTest::UndoQueueLength();
	const FMcpToolCallResult Result = N2CTest::RunTool(Tool, Args);
	if (!N2CTest::ExpectToolCompiled(*this, Tool, Result))
	{
		return;
	}
	N2CTest::ExpectResultTargets(*this, Tool, Result, Blueprint, MyFunc);
	TestTrue(Tool + TEXT(": package dirty"), Test->IsDirty());
	TestEqual(Tool + TEXT(": one undo step"), N2CTest::UndoQueueLength(), QueueBefore + 1);
	N2CTest::ExpectSymmetricLinks(*this, Tool, Blueprint);
	ExpectReturnsAgree(Tool);
	Check();
	if (N2CTest::ExpectUndoRedoExact(*this, Tool, Blueprint, Before))
	{
		ExpectReturnsAgree(Tool + TEXT(" after redo"));
		N2CTest::ExpectCompilesClean(*this, Tool + TEXT(" after redo"), Blueprint);
	}
}

void FN2CMcpFunctionToolsSpec::Define()
{
	AfterEach([this]()
	{
		Test.Reset();
		Blueprint = nullptr;
		MyFunc = nullptr;
		MyEntry = nullptr;
		ReturnA = ReturnB = nullptr;
	});

	Describe(TEXT("on a multi-return function"), [this]()
	{
		It(TEXT("add-function-input-pin: Undo then Redo restore Nodes, Pins and UserDefinedPins exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CFunctionSpecAddInput")))
			{
				return;
			}
			ExpectEdit(TEXT("add-function-input-pin"), PinArgs(TEXT("Speed"), TEXT("float")), [this]()
			{
				TestTrue(TEXT("entry has Speed"), N2CTest::HasUserPin(MyEntry, TEXT("Speed")) && MyEntry->FindPin(TEXT("Speed"), EGPD_Output));
			});
		});

		It(TEXT("add-function-return-pin: Undo then Redo restore Nodes, Pins and UserDefinedPins exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CFunctionSpecAddReturn")))
			{
				return;
			}
			ExpectEdit(TEXT("add-function-return-pin"), PinArgs(TEXT("bOk"), TEXT("bool")), [this]()
			{
				TestTrue(TEXT("both Return nodes have bOk"), N2CTest::HasUserPin(ReturnA, TEXT("bOk")) && N2CTest::HasUserPin(ReturnB, TEXT("bOk")));
				TestEqual(TEXT("still two Return nodes"), FN2CMcpFunctionPinUtils::GetAllResultNodes(MyFunc).Num(), 2);
			});
		});

		It(TEXT("remove-function-entry-pin of an input linked inside the function: Undo then Redo restore Nodes, Pins, links and UserDefinedPins exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CFunctionSpecRemoveInput")))
			{
				return;
			}
			ExpectEdit(TEXT("remove-function-entry-pin"), PinArgs(TEXT("Value")), [this]()
			{
				TestFalse(TEXT("Value gone from the entry"), N2CTest::HasUserPin(MyEntry, TEXT("Value")) || MyEntry->FindPin(TEXT("Value")));
			});
		});

		It(TEXT("remove-function-return-pin of an output linked inside the function: Undo then Redo restore Nodes, Pins, links and UserDefinedPins exactly"), [this]()
		{
			if (!Build(TEXT("BP_N2CFunctionSpecRemoveReturn")))
			{
				return;
			}
			ExpectEdit(TEXT("remove-function-return-pin"), PinArgs(TEXT("Result")), [this]()
			{
				TestFalse(TEXT("Result gone from both Return nodes"), N2CTest::HasUserPin(ReturnA, TEXT("Result")) || N2CTest::HasUserPin(ReturnB, TEXT("Result")));
			});
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
