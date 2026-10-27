// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMeshActor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

namespace N2CPinDefaultSpec
{
	TSharedPtr<FJsonObject> SetArgs(UBlueprint* Blueprint, UEdGraph* Graph, const UEdGraphPin* Pin, const FString& Value)
	{
		const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Graph);
		Args->SetStringField(TEXT("nodeGuid"), Pin->GetOwningNode()->NodeGuid.ToString());
		Args->SetStringField(TEXT("pinGuid"), Pin->PinId.ToString());
		Args->SetStringField(TEXT("value"), Value);
		return Args;
	}
}

/**
 * Pin default writes take the K2 schema's verdict (card 0278; review mcp-tools-graph-functions-8, -24, python-bridge-21,
 * mcp-tools-variables-20). set-input-pin-value, the Python bridge's SetPinValue and the Set variable node path check a
 * value with the schema before they touch the node: a refused value is reported with the schema's reason and the pin
 * keeps its old default (no TrySetDefaultObject(nullptr) wipe, no transaction, package clean); an enum index or display
 * name becomes the entry's name; an empty value resets the pin; newValue is read back from the pin.
 */
BEGIN_DEFINE_SPEC(FN2CPinDefaultWritesSpec, "NodeToCode.MCP.PinDefault", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	UK2Node_CallFunction* Add = nullptr;
	UK2Node_CallFunction* GetAll = nullptr;
	UK2Node_CallFunction* Trace = nullptr;
	UK2Node_CallFunction* Print = nullptr;

	enum class EPin : uint8 { Int, Class, Enum, String, Bool };
	UEdGraphPin* PinOf(EPin Which) const
	{
		switch (Which)
		{
		case EPin::Int: return IntPin();
		case EPin::Class: return ClassPin();
		case EPin::Enum: return EnumPin();
		case EPin::String: return StringPin();
		default: return BoolPin();
		}
	}

	// Looked up on each use: a compile (FBlueprintEditorUtils::ReconstructAllNodes) replaces a node's pins: a compile or a node reconstruction may replace a node's pins
	/** Add_IntInt.B, default 1 */
	UEdGraphPin* IntPin() const { return Add ? Add->FindPin(TEXT("B"), EGPD_Input) : nullptr; }
	/** GetAllActorsOfClass.ActorClass, default None */
	UEdGraphPin* ClassPin() const { return GetAll ? GetAll->FindPin(TEXT("ActorClass"), EGPD_Input) : nullptr; }
	/** LineTraceSingle.DrawDebugType (EDrawDebugTrace), default None */
	UEdGraphPin* EnumPin() const { return Trace ? Trace->FindPin(TEXT("DrawDebugType"), EGPD_Input) : nullptr; }
	/** PrintString.InString, default Hello */
	UEdGraphPin* StringPin() const { return Print ? Print->FindPin(TEXT("InString"), EGPD_Input) : nullptr; }
	/** PrintString.bPrintToScreen, default true */
	UEdGraphPin* BoolPin() const { return Print ? Print->FindPin(TEXT("bPrintToScreen"), EGPD_Input) : nullptr; }

	bool Build(const TCHAR* Name);
	/** set-input-pin-value on the pin; the result's JSON on success, else null */
	TSharedPtr<FJsonObject> SetOk(const FString& What, EPin Which, const FString& Value);
	/** set-input-pin-value on the pin must fail without touching anything; the error text */
	FString SetRefused(const FString& What, EPin Which, const FString& Value);
END_DEFINE_SPEC(FN2CPinDefaultWritesSpec)

bool FN2CPinDefaultWritesSpec::Build(const TCHAR* Name)
{
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	Graph = Test->EventGraph();
	Add = Test->AddCall(Graph, UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Add_IntInt));
	GetAll = Test->AddCall(Graph, UGameplayStatics::StaticClass(), GET_FUNCTION_NAME_CHECKED(UGameplayStatics, GetAllActorsOfClass));
	Trace = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, LineTraceSingle));
	Print = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	if (!TestTrue(TEXT("nodes"), Add && GetAll && Trace && Print))
	{
		return false;
	}
	if (!TestTrue(TEXT("pins"), IntPin() && ClassPin() && EnumPin() && StringPin() && BoolPin())
		|| !TestTrue(TEXT("test graph built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0))
	{
		return false;
	}
	TestEqual(TEXT("B starts at 1"), IntPin()->GetDefaultAsString(), FString(TEXT("1")));
	TestEqual(TEXT("InString starts at Hello"), StringPin()->GetDefaultAsString(), FString(TEXT("Hello")));
	Test->Settle();
	return true;
}

TSharedPtr<FJsonObject> FN2CPinDefaultWritesSpec::SetOk(const FString& What, EPin Which, const FString& Value)
{
	const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("set-input-pin-value"), N2CPinDefaultSpec::SetArgs(Blueprint, Graph, PinOf(Which), Value));
	if (!TestFalse(What + TEXT(" succeeds: ") + N2CTest::ResultText(Result).Left(400), Result.bIsError))
	{
		return nullptr;
	}
	const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
	TestTrue(What + TEXT(": newValue is what the pin holds"), Json.IsValid() && Json->GetStringField(TEXT("newValue")) == PinOf(Which)->GetDefaultAsString());
	return Json;
}

FString FN2CPinDefaultWritesSpec::SetRefused(const FString& What, EPin Which, const FString& Value)
{
	Test->Settle();
	UEdGraphPin* Pin = PinOf(Which);
	const FString Before = N2CTest::Snapshot(Blueprint);
	const FString DefaultBefore = Pin->GetDefaultAsString();
	const UObject* ObjectBefore = Pin->DefaultObject;
	const int32 QueueBefore = N2CTest::UndoQueueLength();
	const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("set-input-pin-value"), N2CPinDefaultSpec::SetArgs(Blueprint, Graph, Pin, Value));
	const FString Text = N2CTest::ResultText(Result);
	TestTrue(What + TEXT(" is an error: ") + Text.Left(400), Result.bIsError);
	TestTrue(What + TEXT(": names the old default it keeps: ") + Text.Left(400), Text.Contains(FString::Printf(TEXT("keeps its default '%s'"), *DefaultBefore)));
	TestEqual(What + TEXT(": the pin keeps its default"), PinOf(Which)->GetDefaultAsString(), DefaultBefore);
	TestTrue(What + TEXT(": the pin keeps its default object"), PinOf(Which)->DefaultObject == ObjectBefore);
	const FString Difference = N2CTest::FirstSnapshotDifference(Before, N2CTest::Snapshot(Blueprint));
	TestTrue(What + TEXT(": graph unchanged ") + Difference, Difference.IsEmpty());
	TestEqual(What + TEXT(": no transaction recorded"), N2CTest::UndoQueueLength(), QueueBefore);
	TestFalse(What + TEXT(": package not dirty"), Test->IsDirty());
	return Text;
}

void FN2CPinDefaultWritesSpec::Define()
{
	AfterEach([this]()
	{
		Test.Reset();
		Blueprint = nullptr;
		Graph = nullptr;
		Add = GetAll = Trace = Print = nullptr;
	});

	Describe(TEXT("set-input-pin-value"), [this]()
	{
		It(TEXT("refuses 1.5 on an integer pin with the schema's reason and keeps the old value"), [this]()
		{
			if (!Build(TEXT("BP_N2CPinDefaultInt")))
			{
				return;
			}
			const FString Error = SetRefused(TEXT("1.5 on B"), EPin::Int, TEXT("1.5"));
			TestTrue(TEXT("the schema's integer message: ") + Error, Error.Contains(TEXT("integer")));
			SetRefused(TEXT("abc on B"), EPin::Int, TEXT("abc"));
			SetRefused(TEXT("maybe on bPrintToScreen"), EPin::Bool, TEXT("maybe"));

			const FString Before = N2CTest::Snapshot(Blueprint);
			const TSharedPtr<FJsonObject> Set = SetOk(TEXT("-7 on B"), EPin::Int, TEXT("-7"));
			if (Set.IsValid())
			{
				TestEqual(TEXT("oldValue"), Set->GetStringField(TEXT("oldValue")), FString(TEXT("1")));
				TestEqual(TEXT("newValue"), Set->GetStringField(TEXT("newValue")), FString(TEXT("-7")));
				TestTrue(TEXT("package dirty"), Test->IsDirty());
				N2CTest::ExpectUndoRedoExact(*this, TEXT("-7 on B"), Blueprint, Before);
			}
		});

		It(TEXT("refuses a Blueprint asset path on a class pin, keeps the previous class and names the _C class to send"), [this]()
		{
			if (!Build(TEXT("BP_N2CPinDefaultClass")))
			{
				return;
			}
			const FString MeshActor = AStaticMeshActor::StaticClass()->GetPathName();
			const TSharedPtr<FJsonObject> First = SetOk(TEXT("StaticMeshActor on ActorClass"), EPin::Class, MeshActor);
			if (!First.IsValid() || !TestTrue(TEXT("the class is set"), ClassPin()->DefaultObject == AStaticMeshActor::StaticClass()))
			{
				return;
			}
			TestEqual(TEXT("newValue is the class path"), First->GetStringField(TEXT("newValue")), MeshActor);

			const FString Error = SetRefused(TEXT("the Blueprint asset on ActorClass"), EPin::Class, Blueprint->GetPathName());
			TestTrue(TEXT("the schema's message: ") + Error, Error.Contains(TEXT("is not a class")));
			TestTrue(TEXT("names the generated class: ") + Error, Error.Contains(Blueprint->GeneratedClass->GetPathName()));

			SetRefused(TEXT("a relative path on ActorClass"), EPin::Class, TEXT("BP_Nothing"));

			const TSharedPtr<FJsonObject> Generated = SetOk(TEXT("the generated class on ActorClass"), EPin::Class, Blueprint->GeneratedClass->GetPathName());
			TestTrue(TEXT("the generated class is set"), ClassPin()->DefaultObject == Blueprint->GeneratedClass);
			if (Generated.IsValid())
			{
				TestEqual(TEXT("oldValue is the previous class"), Generated->GetStringField(TEXT("oldValue")), MeshActor);
			}
		});

		It(TEXT("maps an enum index and display name to the entry's name and refuses an index out of range"), [this]()
		{
			if (!Build(TEXT("BP_N2CPinDefaultEnum")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> ByIndex = SetOk(TEXT("2 on DrawDebugType"), EPin::Enum, TEXT("2"));
			TestEqual(TEXT("index 2 is ForDuration"), EnumPin()->DefaultValue, FString(TEXT("ForDuration")));
			if (ByIndex.IsValid())
			{
				TestEqual(TEXT("newValue is the entry name"), ByIndex->GetStringField(TEXT("newValue")), FString(TEXT("ForDuration")));
			}
			SetOk(TEXT("For One Frame on DrawDebugType"), EPin::Enum, TEXT("For One Frame"));
			TestEqual(TEXT("the display name maps to ForOneFrame"), EnumPin()->DefaultValue, FString(TEXT("ForOneFrame")));
			SetOk(TEXT("Persistent on DrawDebugType"), EPin::Enum, TEXT("Persistent"));
			TestEqual(TEXT("the entry name is kept"), EnumPin()->DefaultValue, FString(TEXT("Persistent")));
			const FString Error = SetRefused(TEXT("9 on DrawDebugType"), EPin::Enum, TEXT("9"));
			TestTrue(TEXT("the schema's enum message: ") + Error, Error.Contains(TEXT("not a valid enumerant")));
			SetRefused(TEXT("the generated MAX on DrawDebugType"), EPin::Enum, TEXT("4"));
		});

		It(TEXT("resets a pin to its default on an empty value"), [this]()
		{
			if (!Build(TEXT("BP_N2CPinDefaultReset")))
			{
				return;
			}
			SetOk(TEXT("x on InString"), EPin::String, TEXT("x"));
			const TSharedPtr<FJsonObject> Reset = SetOk(TEXT("empty on InString"), EPin::String, FString());
			TestEqual(TEXT("InString back to Hello"), StringPin()->GetDefaultAsString(), FString(TEXT("Hello")));
			if (Reset.IsValid())
			{
				TestEqual(TEXT("oldValue"), Reset->GetStringField(TEXT("oldValue")), FString(TEXT("x")));
				TestEqual(TEXT("newValue"), Reset->GetStringField(TEXT("newValue")), FString(TEXT("Hello")));
			}
			SetOk(TEXT("StaticMeshActor on ActorClass"), EPin::Class, AStaticMeshActor::StaticClass()->GetPathName());
			SetOk(TEXT("empty on ActorClass"), EPin::Class, FString());
			TestNull(TEXT("ActorClass back to None"), ClassPin()->DefaultObject.Get());
			SetOk(TEXT("9 on B"), EPin::Int, TEXT("9"));
			SetOk(TEXT("empty on B"), EPin::Int, FString());
			TestEqual(TEXT("B back to 1"), IntPin()->GetDefaultAsString(), FString(TEXT("1")));
		});
	});

	Describe(TEXT("the Set variable node path"), [this]()
	{
		It(TEXT("reports defaultValueApplied with the schema's reason, and true for a value that took"), [this]()
		{
			if (!Build(TEXT("BP_N2CPinDefaultSetNode"))
				|| !TestTrue(TEXT("member Health"), Test->AddMemberVariable(TEXT("Health"), N2CTest::FTestBlueprint::DoublePinType())))
			{
				return;
			}
			Test->Settle();
			for (const TPair<const TCHAR*, bool>& Case : { TPair<const TCHAR*, bool>(TEXT("abc"), false), TPair<const TCHAR*, bool>(TEXT("75"), true) })
			{
				const FString What = FString::Printf(TEXT("Set Health to %s"), Case.Key);
				TSharedPtr<FJsonObject> Result;
				FString Error;
				N2CTest::InToolCall([&]()
				{
					Result = FN2CMcpNodeSpawn::CreateMemberVariableNode(Blueprint, Graph, TEXT("Health"), /*bSet*/ true, FVector2D(0, 0), Case.Key, Error);
				});
				if (!TestTrue(What + TEXT(": the node is made: ") + Error, Result.IsValid()))
				{
					continue;
				}
				bool bApplied = !Case.Value;
				TestTrue(What + TEXT(": defaultValueApplied present"), Result->TryGetBoolField(TEXT("defaultValueApplied"), bApplied));
				TestEqual(What + TEXT(": defaultValueApplied"), bApplied, Case.Value);
				FString Reason;
				TestEqual(What + TEXT(": defaultValueReason only when refused"), Result->TryGetStringField(TEXT("defaultValueReason"), Reason), !Case.Value);
				if (!Case.Value)
				{
					TestTrue(What + TEXT(": the schema's reason: ") + Reason, Reason.Contains(TEXT("Expected a valid number")));
				}
				const UK2Node_Variable* Node = Cast<UK2Node_Variable>(N2CTest::FindNodeByGuid(Blueprint, Result->GetStringField(TEXT("nodeGuid"))));
				const UEdGraphPin* Value = Node ? Node->FindPin(TEXT("Health"), EGPD_Input) : nullptr;
				if (TestNotNull(What + TEXT(": value pin"), Value))
				{
					TestEqual(What + TEXT(": the pin holds 75 only when it took"), Value->DefaultValue.StartsWith(TEXT("75")), Case.Value);
				}
			}
		});
	});

#if N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
	Describe(TEXT("the Python bridge's SetPinValue on the focused graph (UE 5.8)"), [this]()
	{
		It(TEXT("refuses what the schema refuses, keeps the old value, maps an enum index and resets on empty"), [this]()
		{
			if (!Build(TEXT("BP_N2CPinDefaultBridge")) || !Test->OpenInEditor(*this, Graph))
			{
				return;
			}
			auto Call = [this](UEdGraphPin* Pin, const FString& Value)
			{
				return N2CTest::ParseJsonObject(UN2CPythonBridge::SetPinValue(Pin->GetOwningNode()->NodeGuid.ToString(), Pin->PinId.ToString(), Value, FString()));
			};
			auto ExpectRefused = [this, &Call](const FString& What, EPin Which, const FString& Value, const TCHAR* Reason)
			{
				const FString DefaultBefore = PinOf(Which)->GetDefaultAsString();
				const UObject* ObjectBefore = PinOf(Which)->DefaultObject;
				const int32 QueueBefore = N2CTest::UndoQueueLength();
				const TSharedPtr<FJsonObject> Json = Call(PinOf(Which), Value);
				FString Error;
				if (TestTrue(What + TEXT(": a response"), Json.IsValid()))
				{
					TestFalse(What + TEXT(": fails"), Json->GetBoolField(TEXT("success")));
					Json->TryGetStringField(TEXT("error"), Error);
				}
				TestTrue(What + TEXT(": the schema's reason: ") + Error, Error.Contains(Reason));
				TestEqual(What + TEXT(": the pin keeps its default"), PinOf(Which)->GetDefaultAsString(), DefaultBefore);
				TestTrue(What + TEXT(": the pin keeps its default object"), PinOf(Which)->DefaultObject == ObjectBefore);
				TestEqual(What + TEXT(": no transaction recorded"), N2CTest::UndoQueueLength(), QueueBefore);
			};
			auto ExpectSet = [this, &Call](const FString& What, EPin Which, const FString& Value, const FString& Expected)
			{
				const TSharedPtr<FJsonObject> Json = Call(PinOf(Which), Value);
				const TSharedPtr<FJsonObject>* Data = nullptr;
				if (TestTrue(What + TEXT(": succeeds"), Json.IsValid() && Json->GetBoolField(TEXT("success")) && Json->TryGetObjectField(TEXT("data"), Data)))
				{
					TestEqual(What + TEXT(": newValue read back"), (*Data)->GetStringField(TEXT("newValue")), Expected);
				}
				TestEqual(What + TEXT(": the pin"), PinOf(Which)->GetDefaultAsString(), Expected);
			};

			ExpectRefused(TEXT("1.5 on B"), EPin::Int, TEXT("1.5"), TEXT("integer"));
			ExpectSet(TEXT("StaticMeshActor on ActorClass"), EPin::Class, AStaticMeshActor::StaticClass()->GetPathName(), AStaticMeshActor::StaticClass()->GetPathName());
			ExpectRefused(TEXT("the Blueprint asset on ActorClass"), EPin::Class, Blueprint->GetPathName(), TEXT("is not a class"));
			ExpectSet(TEXT("2 on DrawDebugType"), EPin::Enum, TEXT("2"), TEXT("ForDuration"));
			ExpectSet(TEXT("x on InString"), EPin::String, TEXT("x"), TEXT("x"));
			ExpectSet(TEXT("empty on InString"), EPin::String, FString(), TEXT("Hello"));
		});
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
