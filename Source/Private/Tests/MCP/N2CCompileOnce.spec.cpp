// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintEditor.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformTime.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ScopeExit.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CCompileOnceSpec
{
	/** Full compiles of one Blueprint (UBlueprint::OnCompiled), from construction until destruction */
	struct FCompileCounter
	{
		UBlueprint* Blueprint = nullptr;
		FDelegateHandle Handle;
		int32 Count = 0;

		explicit FCompileCounter(UBlueprint* InBlueprint)
			: Blueprint(InBlueprint)
		{
			Handle = Blueprint->OnCompiled().AddLambda([this](UBlueprint*) { ++Count; });
		}
		~FCompileCounter()
		{
			Blueprint->OnCompiled().Remove(Handle);
		}
	};

	FMcpToolCallResult Call(const FString& Name, const TSharedPtr<FJsonObject>& Args)
	{
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(Name);
		return Tool.IsValid() ? Tool->Execute(Args)
			: FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("N2CTest: no registered tool '%s'"), *Name));
	}

	TSharedPtr<FJsonValue> PinRef(const UEdGraphPin* Pin)
	{
		const TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
		Ref->SetStringField(TEXT("nodeGuid"), Pin->GetOwningNode()->NodeGuid.ToString());
		Ref->SetStringField(TEXT("pinGuid"), Pin->PinId.ToString());
		Ref->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
		return MakeShared<FJsonValueObject>(Ref);
	}

	/** connect-pins arguments linking each From[i] to To[i] */
	TSharedPtr<FJsonObject> ConnectArgs(const TArray<TPair<UEdGraphPin*, UEdGraphPin*>>& Links)
	{
		TArray<TSharedPtr<FJsonValue>> Connections;
		for (const TPair<UEdGraphPin*, UEdGraphPin*>& Link : Links)
		{
			const TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
			Connection->SetField(TEXT("from"), PinRef(Link.Key));
			Connection->SetField(TEXT("to"), PinRef(Link.Value));
			Connections.Add(MakeShared<FJsonValueObject>(Connection));
		}
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetArrayField(TEXT("connections"), Connections);
		return Args;
	}

	/** The result's "compile" object, or null */
	TSharedPtr<FJsonObject> CompileOf(const FMcpToolCallResult& Result)
	{
		const TSharedPtr<FJsonObject> Parsed = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
		const TSharedPtr<FJsonObject>* Compile = nullptr;
		return Parsed.IsValid() && Parsed->TryGetObjectField(TEXT("compile"), Compile) ? *Compile : nullptr;
	}

	UEdGraphPin* ExecOut(UEdGraphNode* Node) { return Node ? Node->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output) : nullptr; }
	UEdGraphPin* ExecIn(UEdGraphNode* Node) { return Node ? Node->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input) : nullptr; }
}

/**
 * One compile per tool call, with the compiler's diagnostics in the result (card 0151), through the MCP tools on a
 * Blueprint open and focused in its editor. The tools act on the focused graph, so these cases open a Blueprint
 * editor; UE 5.6 and 5.7 under -nullrhi cannot close one (see NodeToCode.Editor.Focus), so they run on 5.8 and the
 * same behaviour is checked without an editor in NodeToCode.MCP.CompileScope and live through claude -p.
 */
BEGIN_DEFINE_SPEC(FN2CCompileOnceSpec, "NodeToCode.MCP.CompileOnce", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	TArray<UEdGraphNode*> Chain;
	bool bEditorOpen = false;

	/** A custom event and Count Print String calls, unlinked, in a /Game Blueprint opened and focused in its editor */
	bool OpenChain(const TCHAR* Name, int32 Count);
END_DEFINE_SPEC(FN2CCompileOnceSpec)

bool FN2CCompileOnceSpec::OpenChain(const TCHAR* Name, int32 Count)
{
	Test = MakeUnique<N2CTest::FTestBlueprint>(Name, TEXT("/Game"));
	// FTestBlueprint packages are transient, and MarkPackageDirty skips transient objects: clear the flag so the
	// dirty checks see what a saved Blueprint would (AfterEach puts it back)
	Test->GetPackage()->ClearFlags(RF_Transient);
	Chain.Reset();
	UEdGraph* Graph = Test->EventGraph();
	Chain.Add(Test->AddCustomEvent(Graph, FName(TEXT("N2CCompileOnceEvent"))));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Chain.Add(Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)));
	}
	if (!TestTrue(TEXT("test graph built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0))
	{
		return false;
	}

	TSharedPtr<IBlueprintEditor> Opened;
	FString OpenError;
	if (!TestTrue(TEXT("Blueprint editor opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Test->GetBlueprint(), Opened, OpenError)))
	{
		return false;
	}
	bEditorOpen = true;
	StaticCastSharedPtr<FBlueprintEditor>(Opened)->JumpToHyperlink(Graph, false);
	Test->GetPackage()->SetDirtyFlag(false);
	return true;
}

void FN2CCompileOnceSpec::Define()
{
	using namespace N2CCompileOnceSpec;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
	It(TEXT("runs on UE 5.8 only (a headless Blueprint editor cannot be closed below 5.8)"), [this]()
	{
		AddInfo(TEXT("Skipped below UE 5.8; NodeToCode.MCP.CompileScope covers the same behaviour without an editor"));
	});
#else
	AfterEach([this]()
	{
		if (Test.IsValid())
		{
			Test->GetPackage()->SetDirtyFlag(false);
			if (bEditorOpen && GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Test->GetBlueprint());
			}
			Test->GetPackage()->SetDirtyFlag(false);
			Test->GetPackage()->SetFlags(RF_Transient);
		}
		bEditorOpen = false;
		Chain.Reset();
		Test.Reset();
	});

	It(TEXT("connect-pins with 20 connections runs one compile, reports it and leaves the package dirty"), [this]()
	{
		if (!OpenChain(TEXT("BP_N2CCompileOnce20"), 20))
		{
			return;
		}
		TArray<TPair<UEdGraphPin*, UEdGraphPin*>> Links;
		for (int32 Index = 0; Index + 1 < Chain.Num(); ++Index)
		{
			Links.Emplace(ExecOut(Chain[Index]), ExecIn(Chain[Index + 1]));
		}

		FCompileCounter Compiles(Test->GetBlueprint());
		const double Start = FPlatformTime::Seconds();
		const FMcpToolCallResult Result = Call(TEXT("connect-pins"), ConnectArgs(Links));
		const double Seconds = FPlatformTime::Seconds() - Start;
		AddInfo(FString::Printf(TEXT("connect-pins, 20 connections: %d full compile(s), %.1f ms"), Compiles.Count, Seconds * 1000.0));

		TestFalse(TEXT("connect-pins succeeds: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError);
		int32 Linked = 0;
		for (const TPair<UEdGraphPin*, UEdGraphPin*>& Link : Links)
		{
			Linked += Link.Key->LinkedTo.Contains(Link.Value) ? 1 : 0;
		}
		TestEqual(TEXT("all 20 links made"), Linked, 20);
		TestEqual(TEXT("one full compile for the call"), Compiles.Count, 1);
		TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());

		const TSharedPtr<FJsonObject> Compile = CompileOf(Result);
		if (TestTrue(TEXT("the result carries the compile"), Compile.IsValid()))
		{
			TestTrue(TEXT("compile success"), Compile->GetBoolField(TEXT("success")));
			TestEqual(TEXT("errorCount 0"), static_cast<int32>(Compile->GetNumberField(TEXT("errorCount"))), 0);
			TestTrue(TEXT("warningCount present"), Compile->HasField(TEXT("warningCount")));
			TestTrue(TEXT("messages present"), Compile->HasField(TEXT("messages")));
		}
	});

	It(TEXT("returns the compiler's error, with the node it names, when a connection leaves the graph broken"), [this]()
	{
		if (!OpenChain(TEXT("BP_N2CCompileOnceBroken"), 0))
		{
			return;
		}
		// An Actor Blueprint is not a StaticMeshComponent: Set Static Mesh with no Target wired does not compile
		UK2Node_CallFunction* SetMesh = Test->AddCall(Test->EventGraph(), UStaticMeshComponent::StaticClass(),
			GET_FUNCTION_NAME_CHECKED(UStaticMeshComponent, SetStaticMesh));
		if (!TestNotNull(TEXT("Set Static Mesh node"), SetMesh))
		{
			return;
		}
		Test->GetPackage()->SetDirtyFlag(false);

		FCompileCounter Compiles(Test->GetBlueprint());
		const FMcpToolCallResult Result = Call(TEXT("connect-pins"), ConnectArgs({ { ExecOut(Chain[0]), ExecIn(SetMesh) } }));
		TestFalse(TEXT("the connection itself succeeds"), Result.bIsError);
		TestEqual(TEXT("one compile"), Compiles.Count, 1);

		const TSharedPtr<FJsonObject> Compile = CompileOf(Result);
		if (!TestTrue(TEXT("the result carries the compile: ") + N2CTest::ResultText(Result).Left(300), Compile.IsValid()))
		{
			return;
		}
		TestFalse(TEXT("compile success false"), Compile->GetBoolField(TEXT("success")));
		TestTrue(TEXT("errorCount >= 1"), Compile->GetNumberField(TEXT("errorCount")) >= 1);
		bool bNamesNode = false;
		for (const TSharedPtr<FJsonValue>& Value : Compile->GetArrayField(TEXT("messages")))
		{
			const TSharedPtr<FJsonObject> Message = Value->AsObject();
			FString Guid;
			if (Message->GetStringField(TEXT("severity")) == TEXT("Error") && Message->TryGetStringField(TEXT("nodeGuid"), Guid)
				&& Guid == SetMesh->NodeGuid.ToString())
			{
				bNamesNode = Message->GetStringField(TEXT("graph")) == Test->EventGraph()->GetName();
				AddInfo(TEXT("error reported to the agent: ") + Message->GetStringField(TEXT("message")));
			}
		}
		TestTrue(TEXT("an error names the Set Static Mesh node and its graph"), bNamesNode);
	});

	It(TEXT("create-comment-node dirties the package and never compiles"), [this]()
	{
		if (!OpenChain(TEXT("BP_N2CCompileOnceComment"), 2))
		{
			return;
		}
		FCompileCounter Compiles(Test->GetBlueprint());
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetArrayField(TEXT("nodeGuids"), { MakeShared<FJsonValueString>(Chain[1]->NodeGuid.ToString()),
			MakeShared<FJsonValueString>(Chain[2]->NodeGuid.ToString()) });
		Args->SetStringField(TEXT("commentText"), TEXT("N2C test comment"));
		const FMcpToolCallResult Result = Call(TEXT("create-comment-node"), Args);
		TestFalse(TEXT("comment created: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError);
		TestEqual(TEXT("no compile for a comment"), Compiles.Count, 0);
		TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());
	});

	It(TEXT("times a multi-edit tool sequence and the action-database refresh"), [this]()
	{
		if (!OpenChain(TEXT("BP_N2CCompileOnceTiming"), 20))
		{
			return;
		}
		FCompileCounter Compiles(Test->GetBlueprint());
		double ConnectMs = 0.0;
		double SetValueMs = 0.0;
		double DeleteMs = 0.0;

		// 1. Wire the chain in one connect-pins call
		TArray<TPair<UEdGraphPin*, UEdGraphPin*>> Links;
		for (int32 Index = 0; Index + 1 < Chain.Num(); ++Index)
		{
			Links.Emplace(ExecOut(Chain[Index]), ExecIn(Chain[Index + 1]));
		}
		double Start = FPlatformTime::Seconds();
		TestFalse(TEXT("connect-pins"), Call(TEXT("connect-pins"), ConnectArgs(Links)).bIsError);
		ConnectMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		const int32 AfterConnect = Compiles.Count;

		// 2. Five set-input-pin-value calls
		Start = FPlatformTime::Seconds();
		for (int32 Index = 1; Index <= 5; ++Index)
		{
			const UEdGraphPin* InString = Chain[Index]->FindPin(FName(TEXT("InString")), EGPD_Input);
			if (!TestNotNull(TEXT("InString pin"), InString))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("nodeGuid"), Chain[Index]->NodeGuid.ToString());
			Args->SetStringField(TEXT("pinGuid"), InString->PinId.ToString());
			Args->SetStringField(TEXT("value"), FString::Printf(TEXT("step %d"), Index));
			TestFalse(TEXT("set-input-pin-value"), Call(TEXT("set-input-pin-value"), Args).bIsError);
		}
		SetValueMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		const int32 AfterSetValues = Compiles.Count;

		// 3. Delete the last two nodes in one call
		{
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetArrayField(TEXT("nodeGuids"), { MakeShared<FJsonValueString>(Chain[Chain.Num() - 1]->NodeGuid.ToString()),
				MakeShared<FJsonValueString>(Chain[Chain.Num() - 2]->NodeGuid.ToString()) });
			Start = FPlatformTime::Seconds();
			TestFalse(TEXT("delete-blueprint-node"), Call(TEXT("delete-blueprint-node"), Args).bIsError);
			DeleteMs = (FPlatformTime::Seconds() - Start) * 1000.0;
		}

		AddInfo(FString::Printf(TEXT("TIMING sequence (7 tool calls): connect-pins x20 %.1f ms (%d compiles), set-input-pin-value x5 %.1f ms (%d compiles), delete-blueprint-node %.1f ms; total %.1f ms, %d full compiles"),
			ConnectMs, AfterConnect, SetValueMs, AfterSetValues - AfterConnect, DeleteMs, ConnectMs + SetValueMs + DeleteMs, Compiles.Count));
		TestEqual(TEXT("one compile per tool call: 7"), Compiles.Count, 7);

		// What one deferred refresh costs: the old RefreshAll against the per-Blueprint RefreshAssetActions
		if (FBlueprintActionDatabase* ActionDB = FBlueprintActionDatabase::TryGet())
		{
			Start = FPlatformTime::Seconds();
			ActionDB->RefreshAll();
			const double RefreshAllMs = (FPlatformTime::Seconds() - Start) * 1000.0;
			Start = FPlatformTime::Seconds();
			ActionDB->RefreshAssetActions(Test->GetBlueprint());
			const double RefreshAssetMs = (FPlatformTime::Seconds() - Start) * 1000.0;
			AddInfo(FString::Printf(TEXT("TIMING action database: RefreshAll %.1f ms, RefreshAssetActions(Blueprint) %.2f ms"), RefreshAllMs, RefreshAssetMs));
		}
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
