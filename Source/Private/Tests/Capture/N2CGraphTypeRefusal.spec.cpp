// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/N2CCapture.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Utils/N2CMcpBatchTargets.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Utils/N2CGraphUtils.h"
#include "Utils/N2CNodeTypeRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace N2CGraphTypeRefusalSpec
{
	/** A refused schema, by its script path (NodeToCode links neither AnimGraph nor ControlRigDeveloper) */
	struct FRefusedSchema
	{
		const TCHAR* ClassPath;
		const TCHAR* Kind;
		const TCHAR* Described;
	};

	const FRefusedSchema RefusedSchemas[] = {
		{ TEXT("/Script/AnimGraph.AnimationGraphSchema"), TEXT("animgraph"), TEXT("an AnimGraph") },
		{ TEXT("/Script/AnimGraph.AnimationStateGraphSchema"), TEXT("state"), TEXT("an animation state") },
		{ TEXT("/Script/AnimGraph.AnimationStateMachineSchema"), TEXT("state-machine"), TEXT("an animation state machine") },
		{ TEXT("/Script/AnimGraph.AnimationCustomTransitionSchema"), TEXT("animgraph"), TEXT("an animation pose graph") },
		{ TEXT("/Script/ControlRigDeveloper.ControlRigGraphSchema"), TEXT("rig"), TEXT("a Control Rig (RigVM) graph") },
	};

	UClass* SchemaClass(const TCHAR* ClassPath)
	{
		return FindObject<UClass>(nullptr, ClassPath);
	}

	/** A graph with this schema among the Blueprint's function graphs (where an Anim Blueprint keeps its AnimGraph) */
	UEdGraph* AddGraph(UBlueprint* Blueprint, const FName Name, UClass* Schema)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), Schema);
		Blueprint->FunctionGraphs.Add(Graph);
		return Graph;
	}

	TSharedPtr<FJsonObject> Target(const UBlueprint* Blueprint, const UEdGraph* Graph = nullptr)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		if (Graph)
		{
			Json->SetStringField(TEXT("graphGuid"), Graph->GraphGuid.ToString());
		}
		return Json;
	}

	TSharedPtr<FJsonObject> TargetsArgs(const TArray<TSharedPtr<FJsonObject>>& Targets)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TSharedPtr<FJsonObject>& Each : Targets)
		{
			Values.Add(MakeShared<FJsonValueObject>(Each));
		}
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetArrayField(TEXT("targets"), Values);
		return Json;
	}
}

/**
 * Decision 0008 (card 0248): the graph types NodeToCode refuses, decided by the graph's schema class and never its
 * name. AnimGraphs, states, custom blends, state machines and Control Rig graphs are refused everywhere a graph is
 * read: FN2CGraphUtils::CanTranslateGraph (the overlay, the NodeToCode window, the batch orchestrator, the token
 * estimate), get-blueprint-n2cjson, the editor's Copy JSON and translate path (GetGraphAsJson), and the batch tools'
 * targets, each with UNSUPPORTED_GRAPH_TYPE instead of EMPTY_GRAPH or a capture of pose nodes all marked unused.
 * list-blueprint-graphs names the kind and translatable: false. UAnimationGraphSchema derives from UEdGraphSchema_K2,
 * so an IsA<UEdGraphSchema_K2> check let the AnimGraph through (gate 0407): the AnimGraph case below fails if the
 * refusal goes back to a plain K2 check. Transition and conduit rules stay supported, and their TransitionResult node
 * is a result boundary (F1), so the nodes that feed it are not "unused". The real MoverExamples fixtures (ABP_Manny,
 * CR_Mannequin_BasicFootIK) are checked live by Tests/mcp_probe/graph_type_matrix_0246.py.
 */
BEGIN_DEFINE_SPEC(FN2CGraphTypeRefusalSpec, "NodeToCode.Capture.GraphTypes", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;

	FString Path() const { return Test->GetBlueprint()->GetPathName(); }

	/** A refused-schema graph holding one K2 node, so "no nodes" is never the reason it is refused */
	UEdGraph* AddRefusedGraph(const N2CGraphTypeRefusalSpec::FRefusedSchema& Schema, const FName Name)
	{
		UClass* Class = N2CGraphTypeRefusalSpec::SchemaClass(Schema.ClassPath);
		if (!TestNotNull(FString::Printf(TEXT("schema class %s is loaded"), Schema.ClassPath), Class))
		{
			return nullptr;
		}
		UEdGraph* Graph = N2CGraphTypeRefusalSpec::AddGraph(Test->GetBlueprint(), Name, Class);
		Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), FName(TEXT("PrintString")));
		return Graph;
	}

	void ExpectRefused(const FString& What, const FMcpToolCallResult& Result)
	{
		const FString Text = N2CTest::ResultText(Result);
		TestTrue(What + TEXT(" fails"), Result.bIsError);
		TestTrue(FString::Printf(TEXT("%s: UNSUPPORTED_GRAPH_TYPE (got: %s)"), *What, *Text.Left(300)), Text.StartsWith(TEXT("UNSUPPORTED_GRAPH_TYPE")));
	}
END_DEFINE_SPEC(FN2CGraphTypeRefusalSpec)

void FN2CGraphTypeRefusalSpec::Define()
{
	using namespace N2CGraphTypeRefusalSpec;

	BeforeEach([this]()
	{
		Test = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CGraphTypes"), TEXT("/Game"));
		Test->AddCall(Test->EventGraph(), UKismetSystemLibrary::StaticClass(), FName(TEXT("PrintString")));
	});

	AfterEach([this]()
	{
		Test.Reset();
	});

	It(TEXT("the AnimGraph schema passes a K2 check, so the refusal cannot be IsA<UEdGraphSchema_K2>"), [this]()
	{
		const UClass* AnimGraphSchema = SchemaClass(TEXT("/Script/AnimGraph.AnimationGraphSchema"));
		if (TestNotNull(TEXT("UAnimationGraphSchema is loaded"), AnimGraphSchema))
		{
			TestTrue(TEXT("UAnimationGraphSchema derives from UEdGraphSchema_K2"), AnimGraphSchema->IsChildOf(UEdGraphSchema_K2::StaticClass()));
		}
	});

	for (const FRefusedSchema& Schema : RefusedSchemas)
	{
		Describe(FString::Printf(TEXT("a graph with schema %s"), Schema.ClassPath), [this, Schema]()
		{
			It(TEXT("is refused by the classifier and CanTranslateGraph, naming its type"), [this, Schema]()
			{
				UEdGraph* Graph = AddRefusedGraph(Schema, FName(TEXT("Refused")));
				if (!Graph)
				{
					return;
				}
				const FN2CGraphTypeInfo Info = FN2CGraphUtils::ClassifyGraphType(Graph);
				TestFalse(TEXT("not supported"), Info.bSupported);
				TestEqual(TEXT("kind"), Info.Kind, FString(Schema.Kind));
				TestEqual(TEXT("GraphKind is the schema's kind"), FN2CGraphTarget::GraphKind(Graph), FString(Schema.Kind));

				FString Reason;
				TestFalse(TEXT("CanTranslateGraph refuses it"), FN2CGraphUtils::CanTranslateGraph(Graph, &Reason));
				TestTrue(TEXT("the reason names the type: ") + Reason, Reason.Contains(Schema.Described));
				TestTrue(TEXT("the reason says not supported: ") + Reason, Reason.Contains(TEXT("not supported")));
				TestTrue(TEXT("the tools' error has the code"),
					FN2CGraphUtils::UnsupportedGraphTypeError(*Graph).StartsWith(TEXT("UNSUPPORTED_GRAPH_TYPE: ")));

				// The capture entry point refuses it too (card 0448), whatever the caller checked first
				const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*Graph);
				TestTrue(TEXT("N2C::CaptureGraph: UNSUPPORTED_GRAPH_TYPE"), Capture.HasError()
					&& Capture.GetError().StartsWith(TEXT("UNSUPPORTED_GRAPH_TYPE: ")) && Capture.GetError().Contains(Schema.Described));
				const TValueOrError<FN2CCapture, FString> Listed = N2C::CaptureGraphs({ Test->EventGraph(), Graph });
				TestTrue(TEXT("N2C::CaptureGraphs with it listed: UNSUPPORTED_GRAPH_TYPE"), Listed.HasError()
					&& Listed.GetError().StartsWith(TEXT("UNSUPPORTED_GRAPH_TYPE: ")));
			});

			It(TEXT("gets UNSUPPORTED_GRAPH_TYPE from get-blueprint-n2cjson and Copy JSON, not EMPTY_GRAPH"), [this, Schema]()
			{
				UEdGraph* Graph = AddRefusedGraph(Schema, FName(TEXT("Refused")));
				if (!Graph)
				{
					return;
				}
				const TSharedPtr<FJsonObject> Args = Target(Test->GetBlueprint(), Graph);
				ExpectRefused(TEXT("get-blueprint-n2cjson by GUID"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args));

				FString Error;
				const FString Json = FN2CEditorIntegration::Get().GetGraphAsJson(Graph, false, Error);
				TestTrue(TEXT("GetGraphAsJson returns nothing"), Json.IsEmpty());
				TestTrue(TEXT("GetGraphAsJson: UNSUPPORTED_GRAPH_TYPE (got: ") + Error + TEXT(")"), Error.StartsWith(TEXT("UNSUPPORTED_GRAPH_TYPE")));
			});

			It(TEXT("fails a batch target that names it and is skipped from a whole-Blueprint target"), [this, Schema]()
			{
				UEdGraph* Graph = AddRefusedGraph(Schema, FName(TEXT("Refused")));
				if (!Graph)
				{
					return;
				}
				const FN2CBatchTargets Named = N2CMcpBatchTargets::Resolve(TargetsArgs({ Target(Test->GetBlueprint(), Graph) }), 20, TEXT("spec"));
				TestEqual(TEXT("nothing to work on"), Named.Graphs.Num(), 0);
				if (TestEqual(TEXT("one failure"), Named.Failures.Num(), 1))
				{
					TestEqual(TEXT("its code"), N2CMcpBatchTargets::ErrorCode(Named.Failures[0].Error), FString(TEXT("UNSUPPORTED_GRAPH_TYPE")));
				}

				const FN2CBatchTargets Whole = N2CMcpBatchTargets::Resolve(TargetsArgs({ Target(Test->GetBlueprint()) }), 20, TEXT("spec"));
				TestFalse(TEXT("the refused graph is not worked on"), Whole.Graphs.ContainsByPredicate([Graph](const FN2CBatchGraph& Each) { return Each.Graph == Graph; }));
				TestTrue(TEXT("the event graph still is"), Whole.Graphs.ContainsByPredicate([this](const FN2CBatchGraph& Each) { return Each.Graph == Test->EventGraph(); }));
				TestTrue(TEXT("the refused graph is skipped with UNSUPPORTED_GRAPH_TYPE"), Whole.Skipped.ContainsByPredicate([Graph](const FN2CBatchTargetIssue& Issue)
				{
					return Issue.GraphGuid == Graph->GraphGuid.ToString() && N2CMcpBatchTargets::ErrorCode(Issue.Error) == TEXT("UNSUPPORTED_GRAPH_TYPE");
				}));
				TestFalse(TEXT("GraphsToCapture leaves it out"), N2CMcpBlueprintExport::GraphsToCapture(*Test->GetBlueprint()).Contains(Graph));
			});

			It(TEXT("is listed with its kind and translatable false"), [this, Schema]()
			{
				UEdGraph* Graph = AddRefusedGraph(Schema, FName(TEXT("Refused")));
				if (!Graph)
				{
					return;
				}
				const TSharedRef<FJsonObject> Listed = N2CMcpBlueprintExport::ListGraphs(*Test->GetBlueprint());
				bool bFound = false;
				for (const TSharedPtr<FJsonValue>& Value : Listed->GetArrayField(TEXT("graphs")))
				{
					const TSharedPtr<FJsonObject> Entry = Value->AsObject();
					if (Entry->GetStringField(TEXT("graphGuid")) == Graph->GraphGuid.ToString())
					{
						bFound = true;
						TestEqual(TEXT("kind"), Entry->GetStringField(TEXT("kind")), FString(Schema.Kind));
						TestFalse(TEXT("translatable"), Entry->GetBoolField(TEXT("translatable")));
					}
					else if (Entry->GetStringField(TEXT("name")) == TEXT("EventGraph"))
					{
						TestTrue(TEXT("the event graph is translatable"), Entry->GetBoolField(TEXT("translatable")));
					}
				}
				TestTrue(TEXT("listed"), bFound);
			});
		});
	}

	It(TEXT("refuses a whole-Blueprint read when every graph is of a refused type"), [this]()
	{
		// A Control Rig is a UBlueprint whose graphs are all RigVM graphs: stand in with a Blueprint of only refused graphs
		UBlueprint* Blueprint = Test->GetBlueprint();
		UClass* Rig = SchemaClass(TEXT("/Script/ControlRigDeveloper.ControlRigGraphSchema"));
		if (!TestNotNull(TEXT("ControlRigGraphSchema is loaded"), Rig))
		{
			return;
		}
		Blueprint->UbergraphPages.Reset();
		Blueprint->FunctionGraphs.Reset();
		AddGraph(Blueprint, FName(TEXT("RigGraph")), Rig);
		ExpectRefused(TEXT("get-blueprint-n2cjson of every graph"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Target(Blueprint)));
		const FN2CBatchTargets Whole = N2CMcpBatchTargets::Resolve(TargetsArgs({ Target(Blueprint) }), 20, TEXT("spec"));
		if (TestEqual(TEXT("one failure"), Whole.Failures.Num(), 1))
		{
			TestEqual(TEXT("its code"), N2CMcpBatchTargets::ErrorCode(Whole.Failures[0].Error), FString(TEXT("UNSUPPORTED_GRAPH_TYPE")));
		}
	});

	Describe(TEXT("a transition rule"), [this]()
	{
		It(TEXT("is supported, listed as transition, and so is a conduit rule"), [this]()
		{
			UClass* Transition = SchemaClass(TEXT("/Script/AnimGraph.AnimationTransitionSchema"));
			UClass* Conduit = SchemaClass(TEXT("/Script/AnimGraph.AnimationConduitGraphSchema"));
			if (!TestNotNull(TEXT("transition schema"), Transition) || !TestNotNull(TEXT("conduit schema"), Conduit))
			{
				return;
			}
			UEdGraph* Rule = AddGraph(Test->GetBlueprint(), FName(TEXT("Transition")), Transition);
			UEdGraph* ConduitRule = AddGraph(Test->GetBlueprint(), FName(TEXT("Conduit")), Conduit);
			FString Reason;
			TestTrue(TEXT("CanTranslateGraph takes the transition rule: ") + Reason, FN2CGraphUtils::CanTranslateGraph(Rule, &Reason));
			TestTrue(TEXT("CanTranslateGraph takes the conduit rule: ") + Reason, FN2CGraphUtils::CanTranslateGraph(ConduitRule, &Reason));
			TestEqual(TEXT("transition kind"), FN2CGraphTarget::GraphKind(Rule), FString(TEXT("transition")));
			TestEqual(TEXT("conduit kind"), FN2CGraphTarget::GraphKind(ConduitRule), FString(TEXT("conduit")));
			TestTrue(TEXT("ordinary K2 graphs stay supported"), FN2CGraphUtils::ClassifyGraphType(Test->EventGraph()).bSupported);
		});

		It(TEXT("keeps the nodes that feed its TransitionResult live (F1)"), [this]()
		{
			UClass* Transition = SchemaClass(TEXT("/Script/AnimGraph.AnimationTransitionSchema"));
			UClass* ResultClass = SchemaClass(TEXT("/Script/AnimGraph.AnimGraphNode_TransitionResult"));
			if (!TestNotNull(TEXT("transition schema"), Transition) || !TestNotNull(TEXT("TransitionResult class"), ResultClass))
			{
				return;
			}
			UEdGraph* Rule = AddGraph(Test->GetBlueprint(), FName(TEXT("Transition")), Transition);

			// Can Enter Transition = NOT(false): one pure node feeding the result, as a real rule's comparisons do
			UK2Node_CallFunction* Not = Test->AddCall(Rule, UKismetMathLibrary::StaticClass(), FName(TEXT("Not_PreBool")));
			UK2Node* Result = NewObject<UK2Node>(Rule, ResultClass);
			Result->CreateNewGuid();
			Result->AllocateDefaultPins();
			Rule->AddNode(Result, false, false);
			UEdGraphPin* CanEnter = Result->FindPin(TEXT("bCanEnterTransition"), EGPD_Input);
			if (!TestNotNull(TEXT("the result's Can Enter Transition pin"), CanEnter) || !TestNotNull(TEXT("the Not node"), Not))
			{
				return;
			}
			TestTrue(TEXT("linked"), Test->Link(Not->GetReturnValuePin(), CanEnter));

			TestTrue(TEXT("TransitionResult is a FunctionResult"), FN2CNodeTypeRegistry::Get().GetNodeType(Result) == EN2CNodeType::FunctionResult);

			const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*Rule);
			if (!TestFalse(TEXT("captured: ") + (Capture.HasError() ? Capture.GetError() : FString()), Capture.HasError())
				|| !TestEqual(TEXT("one graph"), Capture.GetValue().Blueprint.Graphs.Num(), 1))
			{
				return;
			}
			int32 Checked = 0;
			for (const FN2CNodeDefinition& Node : Capture.GetValue().Blueprint.Graphs[0].Nodes)
			{
				if (Node.NodeType == EN2CNodeType::FunctionResult || Node.MemberName == TEXT("Not_PreBool"))
				{
					++Checked;
					TestFalse(Node.Title + TEXT(" is not unused"), static_cast<bool>(Node.bUnused));
				}
			}
			TestEqual(TEXT("the result and the Not node were captured"), Checked, 2);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
