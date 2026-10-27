// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "Core/N2CTagManager.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CExplicitTargetsSpec
{
	/** The bridge's {success, data, error} envelope */
	TSharedPtr<FJsonObject> ParseEnvelope(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	/** {blueprintPath} only */
	TSharedPtr<FJsonObject> PathArgs(const N2CTest::FToolSpecBlueprint& Test)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Test.Path());
		return Args;
	}

	/** The result's target says the graph came from the arguments, not from editor focus */
	bool ExpectExplicit(FAutomationTestBase& Spec, const FString& What, const FMcpToolCallResult& Result)
	{
		const TSharedPtr<FJsonObject> Target = N2CTest::ResultTarget(Result);
		FString ResolvedBy;
		return Spec.TestTrue(What + TEXT(": target.resolvedBy is explicit: ") + N2CTest::ResultText(Result).Left(300),
			Target.IsValid() && Target->TryGetStringField(TEXT("resolvedBy"), ResolvedBy) && ResolvedBy == TEXT("explicit"));
	}

	/** Whether a JSON array of objects has one whose Field is Value */
	bool HasEntry(const TSharedPtr<FJsonObject>& Json, const TCHAR* Array, const TCHAR* Field, const FString& Value)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Json.IsValid() || !Json->TryGetArrayField(Array, Entries))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			FString Text;
			if (Entry->AsObject().IsValid() && Entry->AsObject()->TryGetStringField(Field, Text) && Text == Value)
			{
				return true;
			}
		}
		return false;
	}
}

/**
 * The Variables tools, tag-blueprint-graph, search-variable-types and the Python bridge's focus-based calls take an
 * explicit target (card 0274, review imp-mcp-tools-variables-4 and imp-mcp-tools-bp-structure-7): blueprintPath, plus
 * graphName or graphGuid where a graph applies, resolved by FN2CGraphTarget and echoed as the result's target. Every
 * case runs on a Blueprint built in code with no Blueprint editor open, so it runs on 5.6, 5.7 and 5.8; with no editor
 * focused, the old focus-only tools failed every one of these calls.
 */
BEGIN_DEFINE_SPEC(FN2CExplicitTargetsSpec, "NodeToCode.MCP.ExplicitTargets", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
	UBlueprint* Blueprint = nullptr;
	UEdGraph* EventGraph = nullptr;
	UEdGraph* Function = nullptr;

	/** An Actor Blueprint with a function N2CTargetSpecFunction, compiled and clean, in no editor */
	bool Build(const TCHAR* Name);
END_DEFINE_SPEC(FN2CExplicitTargetsSpec)

bool FN2CExplicitTargetsSpec::Build(const TCHAR* Name)
{
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name);
	Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return false;
	}
	EventGraph = Test->EventGraph();
	Function = Test->AddFunction(FName(TEXT("N2CTargetSpecFunction")));
	if (!TestTrue(TEXT("event graph and function"), EventGraph && Function))
	{
		return false;
	}
	Test->Settle();
	return TestFalse(TEXT("no Blueprint editor is open"), Test->IsEditorOpen());
}

void FN2CExplicitTargetsSpec::Define()
{
	using namespace N2CExplicitTargetsSpec;

	AfterEach([this]()
	{
		Test.Reset();
		Blueprint = nullptr;
		EventGraph = nullptr;
		Function = nullptr;
	});

	Describe(TEXT("member variable tools by blueprintPath"), [this]()
	{
		It(TEXT("create-variable, get-blueprint-member-variables and set-member-variable-default-value act on the named Blueprint"), [this]()
		{
			if (!Build(TEXT("BP_N2CTargetSpecMember")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Create = PathArgs(*Test);
			Create->SetStringField(TEXT("variableName"), TEXT("N2CTargetHealth"));
			Create->SetStringField(TEXT("typeIdentifier"), TEXT("float"));
			const FMcpToolCallResult Created = N2CTest::RunTool(TEXT("create-variable"), Create);
			if (!TestFalse(TEXT("create-variable: ") + N2CTest::ResultText(Created).Left(300), Created.bIsError))
			{
				return;
			}
			TestTrue(TEXT("the variable is on the Blueprint"),
				FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(TEXT("N2CTargetHealth"))) != INDEX_NONE);
			FString ResultPath;
			TestTrue(TEXT("the result names the Blueprint"), N2CTest::ResultJson(Created).IsValid()
				&& N2CTest::ResultJson(Created)->TryGetStringField(TEXT("blueprintPath"), ResultPath) && ResultPath == Blueprint->GetPathName());

			const TSharedPtr<FJsonObject> Default = PathArgs(*Test);
			Default->SetStringField(TEXT("variableName"), TEXT("N2CTargetHealth"));
			Default->SetStringField(TEXT("defaultValue"), TEXT("42.5"));
			const FMcpToolCallResult Defaulted = N2CTest::RunTool(TEXT("set-member-variable-default-value"), Default);
			TestFalse(TEXT("set-member-variable-default-value: ") + N2CTest::ResultText(Defaulted).Left(300), Defaulted.bIsError);

			const FMcpToolCallResult Listed = N2CTest::RunTool(TEXT("get-blueprint-member-variables"), PathArgs(*Test));
			TestFalse(TEXT("get-blueprint-member-variables: ") + N2CTest::ResultText(Listed).Left(300), Listed.bIsError);
			TestTrue(TEXT("it lists the variable"), HasEntry(N2CTest::ResultJson(Listed), TEXT("variables"), TEXT("name"), TEXT("N2CTargetHealth")));
		});

		It(TEXT("create-variable refuses a blueprintPath that names no Blueprint, with ASSET_NOT_FOUND"), [this]()
		{
			const TSharedPtr<FJsonObject> Create = MakeShared<FJsonObject>();
			Create->SetStringField(TEXT("blueprintPath"), TEXT("/Temp/N2CTargetSpec_NoSuchBlueprint"));
			Create->SetStringField(TEXT("variableName"), TEXT("N2CTargetHealth"));
			Create->SetStringField(TEXT("typeIdentifier"), TEXT("float"));
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-variable"), Create);
			TestTrue(TEXT("ASSET_NOT_FOUND: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError && N2CTest::ResultText(Result).Contains(TEXT("ASSET_NOT_FOUND")));
		});
	});

	Describe(TEXT("local variable tools by blueprintPath and graphGuid"), [this]()
	{
		It(TEXT("create-local-variable, the local list, its default and its Get and Set nodes act on the named function"), [this]()
		{
			if (!Build(TEXT("BP_N2CTargetSpecLocal")))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Create = N2CTest::TargetArgs(Blueprint, Function);
			Create->SetStringField(TEXT("variableName"), TEXT("N2CTargetCount"));
			Create->SetStringField(TEXT("typeIdentifier"), TEXT("int"));
			const FMcpToolCallResult Created = N2CTest::RunTool(TEXT("create-local-variable"), Create);
			if (!N2CTest::ExpectToolCompiled(*this, TEXT("create-local-variable"), Created))
			{
				return;
			}
			ExpectExplicit(*this, TEXT("create-local-variable"), Created);
			N2CTest::ExpectResultTargets(*this, TEXT("create-local-variable"), Created, Blueprint, Function);
			UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
			TestTrue(TEXT("the local is on the function"), Entry && Entry->LocalVariables.ContainsByPredicate(
				[](const FBPVariableDescription& Local) { return Local.VarName == FName(TEXT("N2CTargetCount")); }));

			const TSharedPtr<FJsonObject> Default = N2CTest::TargetArgs(Blueprint, Function);
			Default->SetStringField(TEXT("variableName"), TEXT("N2CTargetCount"));
			Default->SetStringField(TEXT("defaultValue"), TEXT("7"));
			const FMcpToolCallResult Defaulted = N2CTest::RunTool(TEXT("set-local-function-variable-default-value"), Default);
			TestFalse(TEXT("set-local-function-variable-default-value: ") + N2CTest::ResultText(Defaulted).Left(300), Defaulted.bIsError);
			ExpectExplicit(*this, TEXT("set-local-function-variable-default-value"), Defaulted);

			const FMcpToolCallResult Listed = N2CTest::RunTool(TEXT("get-blueprint-function-local-variables"), N2CTest::TargetArgs(Blueprint, Function));
			TestFalse(TEXT("get-blueprint-function-local-variables: ") + N2CTest::ResultText(Listed).Left(300), Listed.bIsError);
			ExpectExplicit(*this, TEXT("get-blueprint-function-local-variables"), Listed);
			TestTrue(TEXT("it lists the local with its default"), HasEntry(N2CTest::ResultJson(Listed), TEXT("localVariables"), TEXT("defaultValue"), TEXT("7")));

			const int32 NodesBefore = Function->Nodes.Num();
			for (const TCHAR* Tool : { TEXT("create-get-local-function-variable-node"), TEXT("create-set-local-function-variable-node") })
			{
				const TSharedPtr<FJsonObject> Node = N2CTest::TargetArgs(Blueprint, Function);
				Node->SetStringField(TEXT("variableName"), TEXT("N2CTargetCount"));
				const FMcpToolCallResult Made = N2CTest::RunTool(Tool, Node);
				TestFalse(FString(Tool) + TEXT(": ") + N2CTest::ResultText(Made).Left(300), Made.bIsError);
				ExpectExplicit(*this, Tool, Made);
			}
			TestEqual(TEXT("two nodes more in the function"), Function->Nodes.Num(), NodesBefore + 2);
			N2CTest::ExpectCompilesClean(*this, TEXT("after the local edits"), Blueprint);
		});

		It(TEXT("create-get-member-variable-node places its node in the graph graphName names"), [this]()
		{
			if (!Build(TEXT("BP_N2CTargetSpecMemberNode")))
			{
				return;
			}
			FEdGraphPinType FloatType;
			FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
			FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(TEXT("N2CTargetSpeed")), FloatType);
			Test->Settle();
			const int32 NodesBefore = EventGraph->Nodes.Num();
			const TSharedPtr<FJsonObject> Args = PathArgs(*Test);
			Args->SetStringField(TEXT("graphName"), EventGraph->GetName());
			Args->SetStringField(TEXT("variableName"), TEXT("N2CTargetSpeed"));
			const FMcpToolCallResult Made = N2CTest::RunTool(TEXT("create-get-member-variable-node"), Args);
			TestFalse(TEXT("create-get-member-variable-node: ") + N2CTest::ResultText(Made).Left(300), Made.bIsError);
			ExpectExplicit(*this, TEXT("create-get-member-variable-node"), Made);
			N2CTest::ExpectResultTargets(*this, TEXT("create-get-member-variable-node"), Made, Blueprint, EventGraph);
			TestEqual(TEXT("one node more in the event graph"), EventGraph->Nodes.Num(), NodesBefore + 1);
		});

		It(TEXT("get-blueprint-function-local-variables refuses the event graph with NOT_A_FUNCTION_GRAPH and names it"), [this]()
		{
			if (!Build(TEXT("BP_N2CTargetSpecNotFunction")))
			{
				return;
			}
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-blueprint-function-local-variables"), N2CTest::TargetArgs(Blueprint, EventGraph));
			TestTrue(TEXT("NOT_A_FUNCTION_GRAPH: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError && N2CTest::ResultText(Result).Contains(TEXT("NOT_A_FUNCTION_GRAPH")));
		});
	});

	Describe(TEXT("tools that need no editor"), [this]()
	{
		It(TEXT("search-variable-types searches with no Blueprint editor open"), [this]()
		{
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("searchTerm"), TEXT("Vector"));
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("search-variable-types"), Args);
			TestFalse(TEXT("not an error: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError);
			TestTrue(TEXT("finds Vector"), N2CTest::ResultText(Result).Contains(TEXT("/Script/CoreUObject.Vector")));
		});

		It(TEXT("tag-blueprint-graph tags the graph blueprintPath and graphGuid name"), [this]()
		{
			if (!Build(TEXT("BP_N2CTargetSpecTag")))
			{
				return;
			}
			const FString Tag = TEXT("N2CTargetSpecTag");
			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Function);
			Args->SetStringField(TEXT("tag"), Tag);
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("tag-blueprint-graph"), Args);
			TestFalse(TEXT("tag-blueprint-graph: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError);
			ExpectExplicit(*this, TEXT("tag-blueprint-graph"), Result);
			const TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetTagsForGraph(Function->GraphGuid);
			TestTrue(TEXT("the function carries the tag"), Tags.ContainsByPredicate([&Tag](const FN2CTaggedBlueprintGraph& T) { return T.Tag == Tag; }));
			for (const FN2CTaggedBlueprintGraph& T : Tags)
			{
				UN2CTagManager::Get().RemoveTag(T.GraphGuid, T.Tag, T.Category);
			}
		});
	});

	Describe(TEXT("the Python bridge by BlueprintPath and Graph"), [this]()
	{
		It(TEXT("SearchBlueprintNodes and AddNodeToGraph act on the function named by GUID"), [this]()
		{
			if (!Build(TEXT("BP_N2CTargetSpecBridge")))
			{
				return;
			}
			const FString Guid = Function->GraphGuid.ToString();
			const TSharedPtr<FJsonObject> Searched = ParseEnvelope(UN2CPythonBridge::SearchBlueprintNodes(TEXT("Print String"), true, 20,
				FString(), true, Test->Path(), Guid));
			bool bSuccess = false;
			const TSharedPtr<FJsonObject>* SearchData = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (!TestTrue(TEXT("search by path and GUID"), Searched.IsValid() && Searched->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess
				&& Searched->TryGetObjectField(TEXT("data"), SearchData) && (*SearchData)->TryGetArrayField(TEXT("nodes"), Nodes)))
			{
				return;
			}
			FString Action;
			for (const TSharedPtr<FJsonValue>& Node : *Nodes)
			{
				const FString Id = Node->AsObject()->GetObjectField(TEXT("spawnMetadata"))->GetStringField(TEXT("actionIdentifier"));
				if (Id.Contains(TEXT("KismetSystemLibrary:PrintString")))
				{
					Action = Id;
					break;
				}
			}
			if (!TestFalse(TEXT("Print String found"), Action.IsEmpty()))
			{
				return;
			}
			const int32 FunctionNodes = Function->Nodes.Num();
			const int32 EventNodes = EventGraph->Nodes.Num();
			const TSharedPtr<FJsonObject> Added = ParseEnvelope(UN2CPythonBridge::AddNodeToGraph(TEXT("Print String"), Action, 300.0f, 0.0f,
				Test->Path(), Guid));
			TestTrue(TEXT("AddNodeToGraph by path and GUID"), Added.IsValid() && Added->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess);
			TestEqual(TEXT("one node more in the function"), Function->Nodes.Num(), FunctionNodes + 1);
			TestEqual(TEXT("the event graph unchanged"), EventGraph->Nodes.Num(), EventNodes);
		});

		It(TEXT("CompileFocusedBlueprint and TagFocusedGraph act on the Blueprint and graph named, and a bad path fails"), [this]()
		{
			if (!Build(TEXT("BP_N2CTargetSpecBridgeTag")))
			{
				return;
			}
			bool bSuccess = false;
			const TSharedPtr<FJsonObject> Compiled = ParseEnvelope(UN2CPythonBridge::CompileFocusedBlueprint(Test->Path()));
			TestTrue(TEXT("CompileFocusedBlueprint by path"), Compiled.IsValid() && Compiled->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess);
			// Card 0297: the result names the Blueprint compiled, so nodetocode.compile_blueprint(path) can be checked
			const TSharedPtr<FJsonObject>* CompiledData = nullptr;
			FString CompiledPath;
			TestTrue(TEXT("its blueprint_path is the one named"), Compiled.IsValid() && Compiled->TryGetObjectField(TEXT("data"), CompiledData)
				&& (*CompiledData)->TryGetStringField(TEXT("blueprint_path"), CompiledPath) && CompiledPath == Blueprint->GetPathName());

			// SaveFocusedBlueprint by path on the clean Blueprint (Settle): nothing to write, and it says which Blueprint
			Test->Settle();
			const TSharedPtr<FJsonObject> Saved = ParseEnvelope(UN2CPythonBridge::SaveFocusedBlueprint(true, Test->Path()));
			const TSharedPtr<FJsonObject>* SavedData = nullptr;
			FString SavedPath;
			bool bWasSaved = true;
			TestTrue(TEXT("SaveFocusedBlueprint by path: success, the Blueprint named, not saved (clean)"), Saved.IsValid()
				&& Saved->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess && Saved->TryGetObjectField(TEXT("data"), SavedData)
				&& (*SavedData)->TryGetStringField(TEXT("blueprint_path"), SavedPath) && SavedPath == Blueprint->GetPathName()
				&& (*SavedData)->TryGetBoolField(TEXT("was_saved"), bWasSaved) && !bWasSaved);

			const FString Tag = TEXT("N2CTargetSpecBridgeTag");
			const TSharedPtr<FJsonObject> Tagged = ParseEnvelope(UN2CPythonBridge::TagFocusedGraph(Tag, TEXT("Default"), FString(),
				Test->Path(), Function->GetName()));
			TestTrue(TEXT("TagFocusedGraph by path and graph name"), Tagged.IsValid() && Tagged->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess);
			const TArray<FN2CTaggedBlueprintGraph> Tags = UN2CTagManager::Get().GetTagsForGraph(Function->GraphGuid);
			TestTrue(TEXT("the function carries the tag"), Tags.ContainsByPredicate([&Tag](const FN2CTaggedBlueprintGraph& T) { return T.Tag == Tag; }));
			for (const FN2CTaggedBlueprintGraph& T : Tags)
			{
				UN2CTagManager::Get().RemoveTag(T.GraphGuid, T.Tag, T.Category);
			}

			const TSharedPtr<FJsonObject> Missing = ParseEnvelope(UN2CPythonBridge::CompileFocusedBlueprint(TEXT("/Temp/N2CTargetSpec_NoSuchBlueprint")));
			FString Error;
			TestTrue(TEXT("a bad path fails with ASSET_NOT_FOUND"), Missing.IsValid() && Missing->TryGetBoolField(TEXT("success"), bSuccess) && !bSuccess
				&& Missing->TryGetStringField(TEXT("error"), Error) && Error.Contains(TEXT("ASSET_NOT_FOUND")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
