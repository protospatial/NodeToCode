// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Core/N2CEditorIntegration.h"

/**
 * create-blueprint-function and delete-blueprint-function must not rename or delete what the call did not name (card
 * 0280, review mcp-tools-graph-functions-10, -11, -18, -19, -20, -21, -30), on transient Blueprints.
 *
 * create: a name any graph, variable or parent function has (EventGraph, an event dispatcher, a member variable,
 * BeginPlay's ReceiveBeginPlay) is refused with a FindUniqueKismetName suggestion, and nothing changes (CreateNewGraph
 * used to rename the event graph or the dispatcher's signature graph out of the way). A direction other than input or
 * output, a default the pin refuses, isStatic outside a function library, isOverride and the net flags are refused
 * before the Blueprint is touched. The successful cases (two parameters named Value keep their own defaults; private
 * has no FUNC_Public) open the Blueprint editor, as the tool always does, so they run only where a headless Blueprint
 * editor can be closed (UE 5.8).
 *
 * delete: force removes only the call nodes that resolve to this Blueprint's function, not a call to another class's
 * same-named function (a function PrintString and a call to UKismetSystemLibrary::PrintString); a call from another Blueprint needs force and is left in place (reported); the function's editor
 * tab is closed, so the focused-graph resolver never hands back the removed graph (UE 5.8, editor needed).
 */
BEGIN_DEFINE_SPEC(FN2CFunctionCreateDeleteSpec, "NodeToCode.MCP.FunctionCreateDelete", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;

	bool Build(const TCHAR* Name);
	/** {blueprintPath, functionName} for create-blueprint-function */
	TSharedPtr<FJsonObject> CreateArgs(const FString& FunctionName) const;
	/** Adds an event dispatcher the way the Blueprint editor's "+ Event Dispatcher" does */
	UEdGraph* AddDispatcher(const FName Name);
	/** Runs create-blueprint-function expecting a refusal starting with Code; nothing in the Blueprint may change */
	void ExpectCreateRefused(const FString& What, const TSharedPtr<FJsonObject>& Args, const FString& Code);
	UK2Node_FunctionEntry* EntryOf(const FString& FunctionName) const;
END_DEFINE_SPEC(FN2CFunctionCreateDeleteSpec)

bool FN2CFunctionCreateDeleteSpec::Build(const TCHAR* Name)
{
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FN2CFunctionCreateDeleteSpec::CreateArgs(const FString& FunctionName) const
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("blueprintPath"), Test->Path());
	Args->SetStringField(TEXT("functionName"), FunctionName);
	return Args;
}

UEdGraph* FN2CFunctionCreateDeleteSpec::AddDispatcher(const FName Name)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	FEdGraphPinType DelegateType;
	DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, DelegateType))
	{
		return nullptr;
	}
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	Graph->bEditable = false;
	K2Schema->CreateDefaultNodesForGraph(*Graph);
	K2Schema->CreateFunctionGraphTerminators(*Graph, (UClass*)nullptr);
	K2Schema->AddExtraFunctionFlags(Graph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
	K2Schema->MarkFunctionEntryAsEditable(Graph, true);
	Blueprint->DelegateSignatureGraphs.Add(Graph);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Graph;
}

void FN2CFunctionCreateDeleteSpec::ExpectCreateRefused(const FString& What, const TSharedPtr<FJsonObject>& Args, const FString& Code)
{
	const FString Before = N2CTest::Snapshot(Blueprint);
	const int32 FunctionsBefore = Blueprint->FunctionGraphs.Num();
	const int32 QueueBefore = N2CTest::UndoQueueLength();
	const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-blueprint-function"), Args);
	const FString Text = N2CTest::ResultText(Result);
	TestTrue(What + TEXT(": refused with ") + Code + TEXT(": ") + Text.Left(300), Result.bIsError && Text.StartsWith(Code));
	TestEqual(What + TEXT(": no function added"), Blueprint->FunctionGraphs.Num(), FunctionsBefore);
	TestEqual(What + TEXT(": no undo step"), N2CTest::UndoQueueLength(), QueueBefore);
	TestFalse(What + TEXT(": package clean"), Test->IsDirty());
	const FString After = N2CTest::Snapshot(Blueprint);
	TestTrue(What + TEXT(": every graph, node and variable as before: ") + N2CTest::FirstSnapshotDifference(Before, After), Before == After);
}

UK2Node_FunctionEntry* FN2CFunctionCreateDeleteSpec::EntryOf(const FString& FunctionName) const
{
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName() == FunctionName)
		{
			return Test->FunctionEntry(Graph);
		}
	}
	return nullptr;
}

void FN2CFunctionCreateDeleteSpec::Define()
{
	AfterEach([this]()
	{
		Test.Reset();
		Blueprint = nullptr;
	});

	Describe(TEXT("create-blueprint-function"), [this]()
	{
		It(TEXT("refuses a name the event graph, an event dispatcher, a member variable or a parent function has, and renames nothing"), [this]()
		{
			if (!Build(TEXT("BP_N2CCreateFunctionNames")))
			{
				return;
			}
			UEdGraph* Dispatcher = AddDispatcher(TEXT("OnDied"));
			Test->AddMemberVariable(TEXT("Health"), N2CTest::FTestBlueprint::IntPinType());
			if (!TestNotNull(TEXT("dispatcher OnDied"), Dispatcher) || !TestEqual(TEXT("setup: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num(), 0))
			{
				return;
			}
			Test->Settle();
			UEdGraph* EventGraph = Test->EventGraph();

			ExpectCreateRefused(TEXT("EventGraph"), CreateArgs(TEXT("EventGraph")), TEXT("NAME_COLLISION"));
			TestEqual(TEXT("the event graph keeps its name"), EventGraph->GetName(), FString(TEXT("EventGraph")));

			ExpectCreateRefused(TEXT("OnDied"), CreateArgs(TEXT("OnDied")), TEXT("NAME_COLLISION"));
			TestEqual(TEXT("the dispatcher's graph keeps its name"), Dispatcher->GetName(), FString(TEXT("OnDied")));
			TestTrue(TEXT("still a dispatcher"), Blueprint->DelegateSignatureGraphs.Contains(Dispatcher));

			ExpectCreateRefused(TEXT("Health"), CreateArgs(TEXT("Health")), TEXT("NAME_COLLISION"));
			ExpectCreateRefused(TEXT("ReceiveBeginPlay"), CreateArgs(TEXT("ReceiveBeginPlay")), TEXT("NAME_COLLISION"));

			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-blueprint-function"), CreateArgs(TEXT("OnDied")));
			const FString Text = N2CTest::ResultText(Result);
			TestTrue(TEXT("the refusal says what has the name and suggests a free one: ") + Text,
				Text.Contains(TEXT("event dispatcher")) && Text.Contains(TEXT("'OnDied_0'")));
		});

		It(TEXT("refuses an unknown direction, a refused default, isStatic, isOverride and net flags before anything changes"), [this]()
		{
			if (!Build(TEXT("BP_N2CCreateFunctionRefusals")))
			{
				return;
			}
			Test->Settle();

			auto WithParam = [this](const FString& Direction, const FString& Type, const FString& Default)
			{
				const TSharedPtr<FJsonObject> Args = CreateArgs(TEXT("NewFunc"));
				const TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
				Param->SetStringField(TEXT("name"), TEXT("Value"));
				Param->SetStringField(TEXT("type"), Type);
				if (!Direction.IsEmpty())
				{
					Param->SetStringField(TEXT("direction"), Direction);
				}
				if (!Default.IsEmpty())
				{
					Param->SetStringField(TEXT("defaultValue"), Default);
				}
				TArray<TSharedPtr<FJsonValue>> Params;
				Params.Add(MakeShared<FJsonValueObject>(Param));
				Args->SetArrayField(TEXT("parameters"), Params);
				return Args;
			};
			auto WithFlag = [this](const FString& Flag, bool bValue)
			{
				const TSharedPtr<FJsonObject> Args = CreateArgs(TEXT("NewFunc"));
				const TSharedPtr<FJsonObject> Flags = MakeShared<FJsonObject>();
				Flags->SetBoolField(Flag, bValue);
				Args->SetObjectField(TEXT("functionFlags"), Flags);
				return Args;
			};

			ExpectCreateRefused(TEXT("direction return"), WithParam(TEXT("return"), TEXT("int"), FString()), TEXT("INVALID_PARAMETER"));
			ExpectCreateRefused(TEXT("unknown type"), WithParam(TEXT("input"), TEXT("Actorr"), FString()), TEXT("INVALID_PARAMETER_TYPE"));
			ExpectCreateRefused(TEXT("int default 'abc'"), WithParam(TEXT("input"), TEXT("int"), TEXT("abc")), TEXT("INVALID_DEFAULT_VALUE"));
			ExpectCreateRefused(TEXT("bool default 'maybe'"), WithParam(TEXT("input"), TEXT("bool"), TEXT("maybe")), TEXT("INVALID_DEFAULT_VALUE"));
			ExpectCreateRefused(TEXT("default on an output"), WithParam(TEXT("output"), TEXT("int"), TEXT("1")), TEXT("INVALID_PARAMETER"));
			ExpectCreateRefused(TEXT("isStatic on an Actor Blueprint"), WithFlag(TEXT("isStatic"), true), TEXT("INVALID_FLAGS"));
			ExpectCreateRefused(TEXT("isOverride"), WithFlag(TEXT("isOverride"), true), TEXT("INVALID_FLAGS"));
			ExpectCreateRefused(TEXT("isReliableReplication"), WithFlag(TEXT("isReliableReplication"), true), TEXT("INVALID_FLAGS"));
			ExpectCreateRefused(TEXT("isMulticast"), WithFlag(TEXT("isMulticast"), true), TEXT("INVALID_FLAGS"));

			const TSharedPtr<FJsonObject> BadAccess = CreateArgs(TEXT("NewFunc"));
			const TSharedPtr<FJsonObject> AccessFlags = MakeShared<FJsonObject>();
			AccessFlags->SetStringField(TEXT("accessSpecifier"), TEXT("internal"));
			BadAccess->SetObjectField(TEXT("functionFlags"), AccessFlags);
			ExpectCreateRefused(TEXT("accessSpecifier internal"), BadAccess, TEXT("INVALID_FLAGS"));
		});

		It(TEXT("gives two parameters named Value their own defaults and lists the pins made (UE 5.8: opens the editor)"), [this]()
		{
			if (!Build(TEXT("BP_N2CCreateFunctionDefaults")))
			{
				return;
			}
			Test->Settle();
			if (!Test->OpenInEditor(*this, Test->EventGraph()))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Args = CreateArgs(TEXT("TwoValues"));
			TArray<TSharedPtr<FJsonValue>> Params;
			for (const TCHAR* Default : { TEXT("1"), TEXT("2") })
			{
				const TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
				Param->SetStringField(TEXT("name"), TEXT("Value"));
				Param->SetStringField(TEXT("type"), TEXT("int"));
				Param->SetStringField(TEXT("direction"), TEXT("Input"));
				Param->SetStringField(TEXT("defaultValue"), Default);
				Params.Add(MakeShared<FJsonValueObject>(Param));
			}
			const TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("name"), TEXT("Result"));
			Out->SetStringField(TEXT("type"), TEXT("int"));
			Out->SetStringField(TEXT("direction"), TEXT("OUTPUT"));
			Params.Add(MakeShared<FJsonValueObject>(Out));
			Args->SetArrayField(TEXT("parameters"), Params);

			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-blueprint-function"), Args);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("create-blueprint-function"), Result))
			{
				return;
			}
			UK2Node_FunctionEntry* Entry = EntryOf(TEXT("TwoValues"));
			if (!TestNotNull(TEXT("TwoValues made"), Entry))
			{
				return;
			}
			TArray<FString> Stored;
			for (const TSharedPtr<FUserPinInfo>& Info : Entry->UserDefinedPins)
			{
				Stored.Add(FString::Printf(TEXT("%s=%s"), *Info->PinName.ToString(), *Info->PinDefaultValue));
			}
			TestEqual(TEXT("each Value keeps its own default in UserDefinedPins"), FString::Join(Stored, TEXT(",")), FString(TEXT("Value=1,Value1=2")));
			const UEdGraphPin* First = Entry->FindPin(TEXT("Value"), EGPD_Output);
			const UEdGraphPin* Second = Entry->FindPin(TEXT("Value1"), EGPD_Output);
			TestTrue(TEXT("and on the pins"), First && Second && First->DefaultValue == TEXT("1") && Second->DefaultValue == TEXT("2"));

			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
			const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
			if (TestTrue(TEXT("result lists parameters"), Json.IsValid() && Json->TryGetArrayField(TEXT("parameters"), Pins) && Pins->Num() == 3))
			{
				const TSharedPtr<FJsonObject> P1 = (*Pins)[1]->AsObject();
				TestEqual(TEXT("second: requested name"), P1->GetStringField(TEXT("name")), FString(TEXT("Value")));
				TestEqual(TEXT("second: actual name"), P1->GetStringField(TEXT("actualName")), FString(TEXT("Value1")));
				TestEqual(TEXT("second: pinId"), P1->GetStringField(TEXT("pinId")), Second ? Second->PinId.ToString() : FString());
				TestEqual(TEXT("third: an output"), (*Pins)[2]->AsObject()->GetStringField(TEXT("direction")), FString(TEXT("output")));
			}
		});

		It(TEXT("makes a private function without FUNC_Public (UE 5.8: opens the editor)"), [this]()
		{
			if (!Build(TEXT("BP_N2CCreateFunctionPrivate")))
			{
				return;
			}
			Test->Settle();
			if (!Test->OpenInEditor(*this, Test->EventGraph()))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Args = CreateArgs(TEXT("Secret"));
			const TSharedPtr<FJsonObject> Flags = MakeShared<FJsonObject>();
			Flags->SetStringField(TEXT("accessSpecifier"), TEXT("private"));
			Args->SetObjectField(TEXT("functionFlags"), Flags);
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-blueprint-function"), Args);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("create-blueprint-function"), Result))
			{
				return;
			}
			UK2Node_FunctionEntry* Entry = EntryOf(TEXT("Secret"));
			if (!TestNotNull(TEXT("Secret made"), Entry))
			{
				return;
			}
			const int32 Flags32 = Entry->GetExtraFlags();
			TestTrue(TEXT("FUNC_Private set"), (Flags32 & FUNC_Private) != 0);
			TestFalse(TEXT("FUNC_Public cleared"), (Flags32 & FUNC_Public) != 0);
			const UFunction* Compiled = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->FindFunctionByName(TEXT("Secret")) : nullptr;
			TestTrue(TEXT("the compiled function is private only"), Compiled && Compiled->HasAnyFunctionFlags(FUNC_Private) && !Compiled->HasAnyFunctionFlags(FUNC_Public));
		});
	});

	Describe(TEXT("delete-blueprint-function"), [this]()
	{
		It(TEXT("with force removes only the calls to this Blueprint's PrintString, not a call to the same-named UKismetSystemLibrary::PrintString"), [this]()
		{
			if (!Build(TEXT("BP_N2CDeleteFunctionSameName")))
			{
				return;
			}
			UEdGraph* FunctionGraph = Test->AddFunction(TEXT("PrintString"));
			Test->Settle();
			UEdGraph* EventGraph = Test->EventGraph();
			UK2Node_CustomEvent* Go = Test->AddCustomEvent(EventGraph, TEXT("Go"));
			UK2Node_CallFunction* SelfCall = Test->AddSelfCall(EventGraph, TEXT("PrintString"));
			UK2Node_CallFunction* LibraryCall = Test->AddCall(EventGraph, UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
			Test->Link(Go, UEdGraphSchema_K2::PN_Then, LibraryCall, UEdGraphSchema_K2::PN_Execute);
			Test->Link(LibraryCall, UEdGraphSchema_K2::PN_Then, SelfCall, UEdGraphSchema_K2::PN_Execute);
			if (!TestEqual(TEXT("setup: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num(), 0) || !TestNotNull(TEXT("PrintString function"), FunctionGraph))
			{
				return;
			}
			Test->Settle();
			const FString OtherGuid = LibraryCall->NodeGuid.ToString();
			const FString SelfGuid = SelfCall->NodeGuid.ToString();

			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, FunctionGraph);
			const FMcpToolCallResult Refused = N2CTest::RunTool(TEXT("delete-blueprint-function"), Args);
			TestTrue(TEXT("without force: refused for the one self call: ") + N2CTest::ResultText(Refused).Left(300),
				Refused.bIsError && N2CTest::ResultText(Refused).Contains(TEXT("1 call node(s) in")));
			TestFalse(TEXT("refusal leaves the package clean"), Test->IsDirty());

			Args->SetBoolField(TEXT("force"), true);
			const FMcpToolCallResult Deleted = N2CTest::RunTool(TEXT("delete-blueprint-function"), Args);
			if (!TestFalse(TEXT("force delete: ") + N2CTest::ResultText(Deleted).Left(300), Deleted.bIsError))
			{
				return;
			}
			TestFalse(TEXT("the PrintString function is gone"), Blueprint->FunctionGraphs.Contains(FunctionGraph));
			TestNull(TEXT("the self call is removed"), N2CTest::FindNodeByGuid(Blueprint, SelfGuid));
			UEdGraphNode* Kept = N2CTest::FindNodeByGuid(Blueprint, OtherGuid);
			TestNotNull(TEXT("the call to UKismetSystemLibrary::PrintString is intact"), Kept);
			if (Kept)
			{
				const UEdGraphPin* KeptExec = N2CTest::ExecIn(Kept);
				TestTrue(TEXT("and keeps its link from Go"), KeptExec && KeptExec->LinkedTo.Num() == 1 && KeptExec->LinkedTo[0]->GetOwningNode() == Go);
			}
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Deleted);
			const TArray<TSharedPtr<FJsonValue>>* Removed = nullptr;
			TestTrue(TEXT("referencesRemoved lists the self call only"), Json.IsValid() && Json->TryGetArrayField(TEXT("referencesRemoved"), Removed)
				&& Removed->Num() == 1 && (*Removed)[0]->AsObject()->GetStringField(TEXT("nodeId")) == SelfGuid);
			N2CTest::ExpectSymmetricLinks(*this, TEXT("after delete"), Blueprint);
		});

		It(TEXT("needs force for a call from another Blueprint and leaves that call in place"), [this]()
		{
			if (!Build(TEXT("BP_N2CDeleteFunctionExternal")))
			{
				return;
			}
			UEdGraph* Helper = Test->AddFunction(TEXT("Helper"));
			Test->Settle();
			N2CTest::FTestBlueprint* Caller = Test->AddSiblingBlueprint(TEXT("BP_N2CDeleteFunctionCaller"));
			UK2Node_CallFunction* ExternalCall = Caller ? Caller->AddCall(Caller->EventGraph(), Blueprint->GeneratedClass, TEXT("Helper")) : nullptr;
			if (!TestNotNull(TEXT("call to Helper from another Blueprint: ") + FString::Join(Test->Errors(), TEXT("; ")) + (Caller ? FString::Join(Caller->Errors(), TEXT("; ")) : FString()), ExternalCall))
			{
				return;
			}
			const FString ExternalGuid = ExternalCall->NodeGuid.ToString();
			Test->Settle();

			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Helper);
			const FMcpToolCallResult Refused = N2CTest::RunTool(TEXT("delete-blueprint-function"), Args);
			TestTrue(TEXT("without force: refused, naming the other Blueprint: ") + N2CTest::ResultText(Refused).Left(400),
				Refused.bIsError && N2CTest::ResultText(Refused).Contains(Caller->GetBlueprint()->GetPathName()));
			TestTrue(TEXT("Helper still there"), Blueprint->FunctionGraphs.Contains(Helper));
			TestFalse(TEXT("refusal leaves the package clean"), Test->IsDirty());

			Args->SetBoolField(TEXT("force"), true);
			const FMcpToolCallResult Deleted = N2CTest::RunTool(TEXT("delete-blueprint-function"), Args);
			if (!TestFalse(TEXT("force delete: ") + N2CTest::ResultText(Deleted).Left(300), Deleted.bIsError))
			{
				return;
			}
			TestFalse(TEXT("Helper is gone"), Blueprint->FunctionGraphs.Contains(Helper));
			TestNotNull(TEXT("the other Blueprint's call node is not deleted"), N2CTest::FindNodeByGuid(Caller->GetBlueprint(), ExternalGuid));
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Deleted);
			const TArray<TSharedPtr<FJsonValue>>* Left = nullptr;
			TestTrue(TEXT("externalReferencesLeft names it"), Json.IsValid() && Json->TryGetArrayField(TEXT("externalReferencesLeft"), Left)
				&& Left->Num() == 1 && (*Left)[0]->AsObject()->GetStringField(TEXT("nodeId")) == ExternalGuid);
		});

		It(TEXT("closes the function's editor tab, so the focused graph is never the removed one (UE 5.8: opens the editor)"), [this]()
		{
			if (!Build(TEXT("BP_N2CDeleteFunctionTab")))
			{
				return;
			}
			UEdGraph* Doomed = Test->AddFunction(TEXT("Doomed"));
			Test->Settle();
			if (!Test->OpenInEditor(*this, Doomed))
			{
				return;
			}
			const TSharedPtr<FBlueprintEditor> Editor = StaticCastSharedPtr<FBlueprintEditor>(FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, false));
			TArray<TSharedPtr<SDockTab>> Tabs;
			if (!TestTrue(TEXT("setup: a tab shows Doomed"), Editor.IsValid() && Editor->FindOpenTabsContainingDocument(Doomed, Tabs)))
			{
				return;
			}

			const FMcpToolCallResult Deleted = N2CTest::RunTool(TEXT("delete-blueprint-function"), N2CTest::TargetArgs(Blueprint, Doomed));
			if (!TestFalse(TEXT("delete: ") + N2CTest::ResultText(Deleted).Left(300), Deleted.bIsError))
			{
				return;
			}
			Tabs.Reset();
			TestFalse(TEXT("no tab shows the removed graph"), Editor->FindOpenTabsContainingDocument(Doomed, Tabs));
			TestTrue(TEXT("the focused graph is not the removed one"), FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor() != Doomed);
			const FN2CGraphTarget Focused = FN2CGraphTarget::Resolve(FN2CGraphTargetArgs(), /*bForWrite*/ true);
			TestTrue(TEXT("the focus resolver does not hand back the removed graph"), !Focused.IsValid() || Focused.Graph != Doomed);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
