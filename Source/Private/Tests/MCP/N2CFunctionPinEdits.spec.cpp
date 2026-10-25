// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpFunctionPinUtils.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CFunctionPinEditsSpec
{
	FMcpToolCallResult Call(const FString& Name, const TSharedPtr<FJsonObject>& Args)
	{
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(Name);
		return Tool.IsValid() ? Tool->Execute(Args)
			: FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("N2CTest: no registered tool '%s'"), *Name));
	}

	/** {blueprintPath, graphGuid, pinName[, typeIdentifier]} for the pin tools */
	TSharedPtr<FJsonObject> PinArgs(const UBlueprint* Blueprint, const UEdGraph* Function, const FString& PinName,
		const FString& TypeIdentifier = FString())
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Args->SetStringField(TEXT("graphGuid"), Function->GraphGuid.ToString());
		Args->SetStringField(TEXT("pinName"), PinName);
		if (!TypeIdentifier.IsEmpty())
		{
			Args->SetStringField(TEXT("typeIdentifier"), TypeIdentifier);
		}
		return Args;
	}

	/** Every link in the Blueprint as "<node GUID>.<pin> -> <node GUID>.<pin>", from the output side */
	TSet<FString> Links(UBlueprint* Blueprint)
	{
		TSet<FString> Found;
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				for (const UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
				{
					if (Pin && Pin->Direction == EGPD_Output)
					{
						for (const UEdGraphPin* Other : Pin->LinkedTo)
						{
							Found.Add(FString::Printf(TEXT("%s.%s -> %s.%s"), *Node->NodeGuid.ToString(), *Pin->PinName.ToString(),
								*Other->GetOwningNode()->NodeGuid.ToString(), *Other->PinName.ToString()));
						}
					}
				}
			}
		}
		return Found;
	}

	bool HasUserPin(const UK2Node_EditablePinBase* Node, const FName Name)
	{
		return Node && Node->UserDefinedPins.ContainsByPredicate(
			[Name](const TSharedPtr<FUserPinInfo>& Info) { return Info.IsValid() && Info->PinName == Name; });
	}
}

/**
 * The function signature tools rebuild call sites against the regenerated skeleton (card 0156): add-function-input-pin,
 * add-function-return-pin, remove-function-entry-pin and remove-function-return-pin (and the Python bridge's
 * equivalents, which share FN2CMcpFunctionPinUtils::AddFunctionPin / RemoveFunctionPin) on MyFunc(Value) -> Result,
 * which has two Return nodes (a Branch's two outputs) and is called from the event graph and from another function.
 * Every call node gets exactly the new signature, the links already made stay, the Blueprint compiles with no errors,
 * every Return node gets the return value, one undo restores the pins, UserDefinedPins and call sites, and a
 * misspelled pin name is refused with no transaction and a clean package. The bridge runs on the focused graph, so its
 * case opens a Blueprint editor and runs on UE 5.8 only (a headless Blueprint editor cannot be closed below 5.8, see
 * NodeToCode.MCP.CompileOnce). Capture of an edited signature: the FunctionPinEdits case of NodeToCode.Capture.Golden
 * (golden, DataFlows, ExecFlows).
 */
BEGIN_DEFINE_SPEC(FN2CFunctionPinEditsSpec, "NodeToCode.MCP.FunctionPinEdits", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	bool bEditorOpen = false;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* MyFunc = nullptr;
	UEdGraph* VoidFunc = nullptr;
	UEdGraph* Caller = nullptr;
	UK2Node_FunctionEntry* MyEntry = nullptr;
	UK2Node_FunctionResult* ReturnA = nullptr;
	UK2Node_FunctionResult* ReturnB = nullptr;
	UK2Node_CallFunction* EventCall = nullptr;
	UK2Node_CallFunction* CallerCall = nullptr;
	UK2Node_CallFunction* EventVoidCall = nullptr;
	TSet<FString> LinksBefore;

	/** The Blueprint above, compiled with no errors, its package not transient (so dirtying applies) and clean */
	bool Build(const TCHAR* Name);
	/** Compile, clean package, links recorded */
	void Settle();
	/** Each call node of Function has exactly one input per entry parameter and one output per return value, none orphaned */
	void ExpectCallSitesMatch(const FString& What, UEdGraph* Function, const TArray<UK2Node_CallFunction*>& Calls);
	/** The result's compile object (a tool result's JSON, or a bridge result's data) reports success and no errors */
	void ExpectCompiled(const FString& What, const TSharedPtr<FJsonObject>& Json);
	/** A tool call that must succeed; returns its parsed JSON */
	TSharedPtr<FJsonObject> CallOk(const FString& What, const FString& Tool, const TSharedPtr<FJsonObject>& Args);
	/** Compiles directly and checks the Blueprint is not in error */
	void ExpectBlueprintCompiles(const FString& What);
END_DEFINE_SPEC(FN2CFunctionPinEditsSpec)

bool FN2CFunctionPinEditsSpec::Build(const TCHAR* Name)
{
	using N2CTest::FTestBlueprint;
	Test = MakeUnique<FTestBlueprint>(Name, TEXT("/Game"));
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	// FTestBlueprint packages are transient, and MarkPackageDirty skips transient objects (AfterEach puts it back)
	Test->GetPackage()->ClearFlags(RF_Transient);

	// MyFunc(Value: int) -> Result: Entry -> Branch; True -> Return A, False -> Return B; Value -> Result on both
	MyFunc = Test->AddFunction(TEXT("MyFunc"));
	Test->AddFunctionInput(MyFunc, TEXT("Value"), FTestBlueprint::IntPinType());
	MyEntry = Test->FunctionEntry(MyFunc);
	ReturnA = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(MyEntry);
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
	Test->FunctionEntry(MyFunc)->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
	UK2Node_IfThenElse* Branch = Test->AddBranch(MyFunc);
	Test->Link(MyEntry, UEdGraphSchema_K2::PN_Then, Branch, UEdGraphSchema_K2::PN_Execute);
	Test->Link(Branch, UEdGraphSchema_K2::PN_Then, ReturnA, UEdGraphSchema_K2::PN_Execute);
	Test->Link(Branch, UEdGraphSchema_K2::PN_Else, ReturnB, UEdGraphSchema_K2::PN_Execute);
	Test->Link(MyEntry, TEXT("Value"), ReturnA, TEXT("Result"));
	Test->Link(MyEntry, TEXT("Value"), ReturnB, TEXT("Result"));

	// VoidFunc(): no parameters and no Return node
	VoidFunc = Test->AddFunction(TEXT("VoidFunc"));

	// Caller(In: int): Entry -> MyFunc(Value = In)
	Caller = Test->AddFunction(TEXT("Caller"));
	Test->AddFunctionInput(Caller, TEXT("In"), FTestBlueprint::IntPinType());
	CallerCall = Test->AddSelfCall(Caller, TEXT("MyFunc"));
	Test->Link(Test->FunctionEntry(Caller), UEdGraphSchema_K2::PN_Then, CallerCall, UEdGraphSchema_K2::PN_Execute);
	Test->Link(Test->FunctionEntry(Caller), TEXT("In"), CallerCall, TEXT("Value"));

	// Event graph: Go -> MyFunc -> VoidFunc, MyFunc's Result -> Add.A
	UEdGraph* EventGraph = Test->EventGraph();
	UK2Node_CustomEvent* Go = Test->AddCustomEvent(EventGraph, TEXT("Go"));
	EventCall = Test->AddSelfCall(EventGraph, TEXT("MyFunc"));
	EventVoidCall = Test->AddSelfCall(EventGraph, TEXT("VoidFunc"));
	UK2Node_CallFunction* Add = Test->AddCall(EventGraph, UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
	Test->Link(Go, UEdGraphSchema_K2::PN_Then, EventCall, UEdGraphSchema_K2::PN_Execute);
	Test->Link(EventCall, UEdGraphSchema_K2::PN_Then, EventVoidCall, UEdGraphSchema_K2::PN_Execute);
	Test->Link(EventCall, TEXT("Result"), Add, TEXT("A"));

	if (!TestTrue(TEXT("test graph built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0))
	{
		return false;
	}
	Settle();
	TestEqual(TEXT("baseline: two Return nodes in MyFunc"), FN2CMcpFunctionPinUtils::GetAllResultNodes(MyFunc).Num(), 2);
	ExpectBlueprintCompiles(TEXT("baseline"));
	return Blueprint->Status != BS_Error;
}

void FN2CFunctionPinEditsSpec::Settle()
{
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	Test->GetPackage()->SetDirtyFlag(false);
	LinksBefore = N2CFunctionPinEditsSpec::Links(Blueprint);
}

void FN2CFunctionPinEditsSpec::ExpectBlueprintCompiles(const FString& What)
{
	FN2CCompileDiagnostics Diagnostics;
	FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);
	TestEqual(What + TEXT(": compile errors"), Diagnostics.ErrorCount, 0);
	for (const FN2CCompilerMessage& Message : Diagnostics.Messages)
	{
		AddInfo(What + TEXT(": compiler: ") + FN2CCompileDiagnostics::MessageToJson(Message)->GetStringField(TEXT("message")));
	}
}

void FN2CFunctionPinEditsSpec::ExpectCallSitesMatch(const FString& What, UEdGraph* Function, const TArray<UK2Node_CallFunction*>& Calls)
{
	const UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
	const TArray<UK2Node_FunctionResult*> Results = FN2CMcpFunctionPinUtils::GetAllResultNodes(Function);
	TArray<FName> Inputs;
	TArray<FName> Outputs;
	for (const TSharedPtr<FUserPinInfo>& Info : Entry->UserDefinedPins)
	{
		Inputs.Add(Info->PinName);
	}
	if (Results.Num() > 0)
	{
		for (const TSharedPtr<FUserPinInfo>& Info : Results[0]->UserDefinedPins)
		{
			Outputs.Add(Info->PinName);
		}
	}
	for (const UK2Node_FunctionResult* Result : Results)
	{
		TestEqual(What + TEXT(": every Return node has the same outputs"), Result->UserDefinedPins.Num(), Outputs.Num());
		for (const FName Output : Outputs)
		{
			TestTrue(What + TEXT(": Return node has ") + Output.ToString(), N2CFunctionPinEditsSpec::HasUserPin(Result, Output)
				&& Result->FindPin(Output, EGPD_Input) != nullptr);
		}
	}
	for (const UK2Node_CallFunction* Call : Calls)
	{
		const FString Where = What + TEXT(" (call in ") + Call->GetGraph()->GetName() + TEXT(")");
		TArray<FName> CallInputs;
		TArray<FName> CallOutputs;
		for (const UEdGraphPin* Pin : Call->Pins)
		{
			TestFalse(Where + TEXT(": no orphaned pin ") + Pin->PinName.ToString(), Pin->bOrphanedPin);
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinName == UEdGraphSchema_K2::PN_Self)
			{
				continue;
			}
			(Pin->Direction == EGPD_Input ? CallInputs : CallOutputs).Add(Pin->PinName);
		}
		TestEqual(Where + TEXT(": inputs are the parameters"), FString::JoinBy(CallInputs, TEXT(","), [](FName N) { return N.ToString(); }),
			FString::JoinBy(Inputs, TEXT(","), [](FName N) { return N.ToString(); }));
		TestEqual(Where + TEXT(": outputs are the return values"), FString::JoinBy(CallOutputs, TEXT(","), [](FName N) { return N.ToString(); }),
			FString::JoinBy(Outputs, TEXT(","), [](FName N) { return N.ToString(); }));
	}
}

void FN2CFunctionPinEditsSpec::ExpectCompiled(const FString& What, const TSharedPtr<FJsonObject>& Json)
{
	const TSharedPtr<FJsonObject>* Compile = nullptr;
	if (!TestTrue(What + TEXT(": the result carries the compile"), Json.IsValid() && Json->TryGetObjectField(TEXT("compile"), Compile)))
	{
		return;
	}
	TestTrue(What + TEXT(": compile success"), (*Compile)->GetBoolField(TEXT("success")));
	TestEqual(What + TEXT(": compile errorCount"), static_cast<int32>((*Compile)->GetNumberField(TEXT("errorCount"))), 0);
	const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
	if ((*Compile)->TryGetArrayField(TEXT("messages"), Messages))
	{
		for (const TSharedPtr<FJsonValue>& Message : *Messages)
		{
			AddInfo(What + TEXT(": compiler: ") + Message->AsObject()->GetStringField(TEXT("message")));
		}
	}
}

TSharedPtr<FJsonObject> FN2CFunctionPinEditsSpec::CallOk(const FString& What, const FString& Tool, const TSharedPtr<FJsonObject>& Args)
{
	const FMcpToolCallResult Result = N2CFunctionPinEditsSpec::Call(Tool, Args);
	const FString Text = N2CTest::ResultText(Result);
	if (!TestFalse(What + TEXT(": ") + Tool + TEXT(" succeeds: ") + Text.Left(400), Result.bIsError))
	{
		return nullptr;
	}
	const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(Text);
	ExpectCompiled(What, Json);
	return Json;
}

void FN2CFunctionPinEditsSpec::Define()
{
	using namespace N2CFunctionPinEditsSpec;

	AfterEach([this]()
	{
		if (Test.IsValid())
		{
			if (bEditorOpen && GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Test->GetBlueprint());
			}
			Test->GetPackage()->SetDirtyFlag(false);
			Test->GetPackage()->SetFlags(RF_Transient);
		}
		// The undo buffer holds the test Blueprint's objects: drop them so the Blueprint can be collected
		if (GEditor && GEditor->Trans)
		{
			GEditor->ResetTransaction(NSLOCTEXT("NodeToCode", "N2CFunctionPinEditsReset", "NodeToCode function pin tests"));
		}
		bEditorOpen = false;
		Test.Reset();
		Blueprint = nullptr;
	});

	It(TEXT("add-function-input-pin gives both call sites the new input, keeps their links, compiles, and one undo restores them"), [this]()
	{
		if (!Build(TEXT("BP_N2CPinEditsInput")) || !TestTrue(TEXT("an editor transaction buffer"), GEditor && GEditor->Trans))
		{
			return;
		}
		const int32 QueueBefore = GEditor->Trans->GetQueueLength();
		const TSharedPtr<FJsonObject> Args = PinArgs(Blueprint, MyFunc, TEXT("Speed"), TEXT("float"));
		Args->SetStringField(TEXT("tooltip"), TEXT("How fast to go"));
		const TSharedPtr<FJsonObject> Json = CallOk(TEXT("add Speed"), TEXT("add-function-input-pin"), Args);
		if (!Json.IsValid())
		{
			return;
		}
		TestEqual(TEXT("actualName"), Json->GetStringField(TEXT("actualName")), FString(TEXT("Speed")));
		TestTrue(TEXT("entry has Speed"), HasUserPin(MyEntry, TEXT("Speed")) && MyEntry->FindPin(TEXT("Speed"), EGPD_Output));
		ExpectCallSitesMatch(TEXT("after add"), MyFunc, { EventCall, CallerCall });
		const UEdGraphPin* CallSpeed = EventCall->FindPin(TEXT("Speed"), EGPD_Input);
		TestTrue(TEXT("the call's Speed is a real"), CallSpeed && CallSpeed->PinType.PinCategory == UEdGraphSchema_K2::PC_Real);
		TestTrue(TEXT("the links already made stay"), Links(Blueprint).Includes(LinksBefore) && LinksBefore.Includes(Links(Blueprint)));
		TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());
		TestTrue(TEXT("the tooltip is kept in the function metadata as @param"),
			MyEntry->MetaData.ToolTip.ToString().Contains(TEXT("@param Speed How fast to go")));
		FString HoverText;
		if (CallSpeed)
		{
			EventCall->GetPinHoverText(*CallSpeed, HoverText);
		}
		TestTrue(TEXT("the call node shows it as the pin's tooltip: ") + HoverText, HoverText.Contains(TEXT("How fast to go")));
		TestEqual(TEXT("one transaction recorded"), GEditor->Trans->GetQueueLength(), QueueBefore + 1);

		TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
		TestFalse(TEXT("undo: Speed gone from UserDefinedPins"), HasUserPin(MyEntry, TEXT("Speed")));
		TestNull(TEXT("undo: Speed gone from the entry"), MyEntry->FindPin(TEXT("Speed")));
		TestNull(TEXT("undo: Speed gone from the event graph call"), EventCall->FindPin(TEXT("Speed")));
		TestNull(TEXT("undo: Speed gone from Caller's call"), CallerCall->FindPin(TEXT("Speed")));
		TestFalse(TEXT("undo: the tooltip line gone"), MyEntry->MetaData.ToolTip.ToString().Contains(TEXT("@param Speed")));
		TestTrue(TEXT("undo: links as before"), Links(Blueprint).Includes(LinksBefore) && LinksBefore.Includes(Links(Blueprint)));
		ExpectBlueprintCompiles(TEXT("after undo"));
		ExpectCallSitesMatch(TEXT("after undo and compile"), MyFunc, { EventCall, CallerCall });
	});

	It(TEXT("add-function-return-pin adds the output to both Return nodes and both call sites, and one undo removes it everywhere"), [this]()
	{
		if (!Build(TEXT("BP_N2CPinEditsReturn")))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Json = CallOk(TEXT("add bOk"), TEXT("add-function-return-pin"), PinArgs(Blueprint, MyFunc, TEXT("bOk"), TEXT("bool")));
		if (!Json.IsValid())
		{
			return;
		}
		TestTrue(TEXT("Return A has bOk"), HasUserPin(ReturnA, TEXT("bOk")));
		TestTrue(TEXT("Return B has bOk"), HasUserPin(ReturnB, TEXT("bOk")));
		TestEqual(TEXT("still two Return nodes"), FN2CMcpFunctionPinUtils::GetAllResultNodes(MyFunc).Num(), 2);
		ExpectCallSitesMatch(TEXT("after add"), MyFunc, { EventCall, CallerCall });
		TestTrue(TEXT("the links already made stay"), Links(Blueprint).Includes(LinksBefore) && LinksBefore.Includes(Links(Blueprint)));
		TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());

		TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
		TestFalse(TEXT("undo: Return A without bOk"), HasUserPin(ReturnA, TEXT("bOk")) || ReturnA->FindPin(TEXT("bOk")));
		TestFalse(TEXT("undo: Return B without bOk"), HasUserPin(ReturnB, TEXT("bOk")) || ReturnB->FindPin(TEXT("bOk")));
		TestNull(TEXT("undo: the event graph call without bOk"), EventCall->FindPin(TEXT("bOk")));
		TestNull(TEXT("undo: Caller's call without bOk"), CallerCall->FindPin(TEXT("bOk")));
		ExpectBlueprintCompiles(TEXT("after undo"));
	});

	It(TEXT("add-function-return-pin on a function with no Return node makes one with a GUID, wired from the entry, undone in one step"), [this]()
	{
		if (!Build(TEXT("BP_N2CPinEditsVoid")))
		{
			return;
		}
		const int32 NodesBefore = VoidFunc->Nodes.Num();
		if (!CallOk(TEXT("add bOk to VoidFunc"), TEXT("add-function-return-pin"), PinArgs(Blueprint, VoidFunc, TEXT("bOk"), TEXT("bool"))).IsValid())
		{
			return;
		}
		const TArray<UK2Node_FunctionResult*> Results = FN2CMcpFunctionPinUtils::GetAllResultNodes(VoidFunc);
		if (!TestEqual(TEXT("one Return node"), Results.Num(), 1))
		{
			return;
		}
		UK2Node_FunctionResult* Result = Results[0];
		TestTrue(TEXT("valid NodeGuid"), Result->NodeGuid.IsValid());
		TestTrue(TEXT("RF_Transactional"), Result->HasAnyFlags(RF_Transactional));
		TestTrue(TEXT("editable"), Result->IsEditable());
		const UEdGraphPin* Then = Test->FunctionEntry(VoidFunc)->FindPin(UEdGraphSchema_K2::PN_Then);
		TestTrue(TEXT("wired from the entry"), Then && Then->LinkedTo.Num() == 1 && Then->LinkedTo[0]->GetOwningNode() == Result);
		ExpectCallSitesMatch(TEXT("after add"), VoidFunc, { EventVoidCall });
		TestTrue(TEXT("the links already made stay"), Links(Blueprint).Includes(LinksBefore));

		TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
		TestEqual(TEXT("undo: no Return node"), FN2CMcpFunctionPinUtils::GetAllResultNodes(VoidFunc).Num(), 0);
		TestEqual(TEXT("undo: the graph is as before"), VoidFunc->Nodes.Num(), NodesBefore);
		TestNull(TEXT("undo: the call without bOk"), EventVoidCall->FindPin(TEXT("bOk")));
		ExpectBlueprintCompiles(TEXT("after undo"));
	});

	It(TEXT("remove-function-entry-pin and remove-function-return-pin rebuild the call sites and compile"), [this]()
	{
		if (!Build(TEXT("BP_N2CPinEditsRemove")))
		{
			return;
		}
		const TSet<FString> Original = LinksBefore;
		if (!CallOk(TEXT("add Speed"), TEXT("add-function-input-pin"), PinArgs(Blueprint, MyFunc, TEXT("Speed"), TEXT("float"))).IsValid()
			|| !CallOk(TEXT("add bOk"), TEXT("add-function-return-pin"), PinArgs(Blueprint, MyFunc, TEXT("bOk"), TEXT("bool"))).IsValid())
		{
			return;
		}
		Settle();

		// By display name, as an agent may send it
		if (!CallOk(TEXT("remove Speed"), TEXT("remove-function-entry-pin"), PinArgs(Blueprint, MyFunc, TEXT("speed"))).IsValid())
		{
			return;
		}
		TestFalse(TEXT("Speed gone from the entry"), HasUserPin(MyEntry, TEXT("Speed")));
		ExpectCallSitesMatch(TEXT("after removing Speed"), MyFunc, { EventCall, CallerCall });
		TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());

		if (!CallOk(TEXT("remove bOk"), TEXT("remove-function-return-pin"), PinArgs(Blueprint, MyFunc, TEXT("bOk"))).IsValid())
		{
			return;
		}
		TestFalse(TEXT("bOk gone from Return A"), HasUserPin(ReturnA, TEXT("bOk")));
		TestFalse(TEXT("bOk gone from Return B"), HasUserPin(ReturnB, TEXT("bOk")));
		ExpectCallSitesMatch(TEXT("after removing bOk"), MyFunc, { EventCall, CallerCall });
		TestTrue(TEXT("the original links are all there"), Links(Blueprint).Includes(Original) && Original.Includes(Links(Blueprint)));
	});

	It(TEXT("a misspelled pin name is refused with no transaction and a clean package"), [this]()
	{
		if (!Build(TEXT("BP_N2CPinEditsTypo")) || !TestTrue(TEXT("an editor transaction buffer"), GEditor && GEditor->Trans))
		{
			return;
		}
		// The tool logs a refused removal of an entry pin as an error
		AddExpectedError(TEXT("RemoveFunctionEntryPin: Failed to remove pin"), EAutomationExpectedErrorFlags::Contains, 2);
		const int32 QueueBefore = GEditor->Trans->GetQueueLength();
		struct FRefused { const TCHAR* Tool; UEdGraph* Function; const TCHAR* Pin; const TCHAR* Expect; };
		for (const FRefused& Case : TArray<FRefused>{
			{ TEXT("remove-function-entry-pin"), MyFunc, TEXT("Vaule"), TEXT("Pin 'Vaule' not found") },
			{ TEXT("remove-function-return-pin"), MyFunc, TEXT("Reslt"), TEXT("Pin 'Reslt' not found") },
			{ TEXT("remove-function-entry-pin"), MyFunc, TEXT("then"), TEXT("Cannot remove execution pin") },
			{ TEXT("remove-function-return-pin"), VoidFunc, TEXT("bOk"), TEXT("No function result node found") } })
		{
			const FMcpToolCallResult Result = Call(Case.Tool, PinArgs(Blueprint, Case.Function, Case.Pin));
			const FString Text = N2CTest::ResultText(Result);
			const FString What = FString(Case.Tool) + TEXT(" ") + Case.Pin;
			TestTrue(What + TEXT(": refused"), Result.bIsError);
			TestTrue(What + TEXT(": says why: ") + Text.Left(200), Text.Contains(Case.Expect));
			TestFalse(What + TEXT(": package clean"), Test->GetPackage()->IsDirty());
			TestEqual(What + TEXT(": no transaction"), GEditor->Trans->GetQueueLength(), QueueBefore);
		}
		TestTrue(TEXT("Value still on the entry"), HasUserPin(MyEntry, TEXT("Value")));
		TestTrue(TEXT("Result still on both Return nodes"), HasUserPin(ReturnA, TEXT("Result")) && HasUserPin(ReturnB, TEXT("Result")));
	});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	It(TEXT("the Python bridge's four pin functions do the same on the focused function (UE 5.8)"), [this]()
	{
		if (!Build(TEXT("BP_N2CPinEditsBridge")))
		{
			return;
		}
		TSharedPtr<IBlueprintEditor> Opened;
		FString OpenError;
		if (!TestTrue(TEXT("Blueprint editor opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint, Opened, OpenError)))
		{
			return;
		}
		bEditorOpen = true;
		StaticCastSharedPtr<FBlueprintEditor>(Opened)->JumpToHyperlink(MyFunc, false);
		Settle();
		const TSet<FString> Original = LinksBefore;

		auto Bridge = [this](const FString& What, const FString& Text, bool bExpectSuccess) -> TSharedPtr<FJsonObject>
		{
			const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(Text);
			if (!TestTrue(What + TEXT(": JSON result"), Json.IsValid()))
			{
				return nullptr;
			}
			TestEqual(What + TEXT(": success: ") + Text.Left(300), Json->GetBoolField(TEXT("success")), bExpectSuccess);
			const TSharedPtr<FJsonObject>* Data = nullptr;
			if (bExpectSuccess && Json->TryGetObjectField(TEXT("data"), Data))
			{
				ExpectCompiled(What, *Data);
				return *Data;
			}
			return Json;
		};

		// A misspelled name first: refused, no transaction, package clean
		const int32 QueueBefore = GEditor->Trans->GetQueueLength();
		Bridge(TEXT("bridge remove Typo"), UN2CPythonBridge::RemoveFunctionEntryPin(TEXT("Typo")), false);
		TestFalse(TEXT("bridge remove Typo: package clean"), Test->GetPackage()->IsDirty());
		TestEqual(TEXT("bridge remove Typo: no transaction"), GEditor->Trans->GetQueueLength(), QueueBefore);

		Bridge(TEXT("bridge add Speed"), UN2CPythonBridge::AddFunctionInputPin(TEXT("Speed"), TEXT("float")), true);
		Bridge(TEXT("bridge add bOk"), UN2CPythonBridge::AddFunctionReturnPin(TEXT("bOk"), TEXT("bool")), true);
		TestTrue(TEXT("bridge: entry has Speed"), HasUserPin(MyEntry, TEXT("Speed")));
		TestTrue(TEXT("bridge: both Return nodes have bOk"), HasUserPin(ReturnA, TEXT("bOk")) && HasUserPin(ReturnB, TEXT("bOk")));
		ExpectCallSitesMatch(TEXT("bridge after adds"), MyFunc, { EventCall, CallerCall });
		TestTrue(TEXT("bridge: links kept"), Links(Blueprint).Includes(Original) && Original.Includes(Links(Blueprint)));
		TestTrue(TEXT("bridge: package dirty"), Test->GetPackage()->IsDirty());

		// One undo takes back the last edit (bOk) everywhere, and leaves Speed
		TestTrue(TEXT("bridge: undo runs"), GEditor->UndoTransaction());
		TestFalse(TEXT("bridge undo: bOk gone from both Return nodes"), HasUserPin(ReturnA, TEXT("bOk")) || HasUserPin(ReturnB, TEXT("bOk")));
		TestNull(TEXT("bridge undo: bOk gone from the call"), EventCall->FindPin(TEXT("bOk")));
		TestTrue(TEXT("bridge undo: Speed stays"), HasUserPin(MyEntry, TEXT("Speed")) && EventCall->FindPin(TEXT("Speed")) != nullptr);
		ExpectBlueprintCompiles(TEXT("bridge after undo"));

		Bridge(TEXT("bridge add bOk again"), UN2CPythonBridge::AddFunctionReturnPin(TEXT("bOk"), TEXT("bool")), true);
		Bridge(TEXT("bridge remove Speed"), UN2CPythonBridge::RemoveFunctionEntryPin(TEXT("Speed")), true);
		Bridge(TEXT("bridge remove bOk"), UN2CPythonBridge::RemoveFunctionReturnPin(TEXT("bOk")), true);
		ExpectCallSitesMatch(TEXT("bridge after removes"), MyFunc, { EventCall, CallerCall });
		TestTrue(TEXT("bridge: the original links are all there"), Links(Blueprint).Includes(Original) && Original.Includes(Links(Blueprint)));
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
