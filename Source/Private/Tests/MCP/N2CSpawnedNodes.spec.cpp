// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Tools/Implementations/Blueprint/Variables/N2CMcpCreateLocalVariableTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpNodeSpawnUtils.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "BlueprintEditor.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_Composite.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CSpawnedNodesSpec
{
	void Recompile(UBlueprint* Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	}

	/** Every node of every graph of the Blueprint with this GUID */
	TArray<UEdGraphNode*> NodesWithGuid(UBlueprint* Blueprint, const FGuid& Guid)
	{
		TArray<UEdGraphNode*> Found;
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == Guid)
				{
					Found.Add(Node);
				}
			}
		}
		return Found;
	}

	bool GraphHas(const UEdGraph* Graph, const UEdGraphNode* Node)
	{
		return Graph && Node && Graph->Nodes.Contains(Node);
	}

	/** A result's nodeId parsed as a GUID (invalid when absent or not a GUID) */
	FGuid ResultGuid(const TSharedPtr<FJsonObject>& Result)
	{
		FGuid Guid;
		FString Text;
		if (Result.IsValid() && Result->TryGetStringField(TEXT("nodeId"), Text))
		{
			FGuid::Parse(Text, Guid);
		}
		return Guid;
	}
}

/**
 * Variable and comment nodes made by the MCP tools come from the engine's spawners (card 0155): each has a unique
 * GUID, RF_Transactional and one set of pins, the tools return that GUID as nodeId, connect-pins wires it, one undo
 * removes the node and the package is dirty. The shared spawn path (FN2CMcpNodeSpawn::CreateMemberVariableNode and
 * CreateLocalVariableNode, what the four variable node tools run after resolving the focused graph) and the
 * create-comment-node tool (which takes blueprintPath and graphGuid) run on every engine; the last case calls the four
 * variable node tools on a focused Blueprint editor and runs on UE 5.8 only (a headless Blueprint editor cannot be
 * closed below 5.8, see NodeToCode.MCP.CompileOnce). Capture of these nodes: the SpawnedVariableNodes case of
 * NodeToCode.Capture.Golden (golden, DataFlows, ExecFlows).
 */
BEGIN_DEFINE_SPEC(FN2CSpawnedNodesSpec, "NodeToCode.MCP.SpawnedNodes", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;

	/** A /Game Actor Blueprint with a double member Health, compiled, package not transient (so dirtying applies) and clean */
	UBlueprint* MakeBlueprint(const TCHAR* Name);
	/** Clean package and a fresh baseline after setup edits */
	void Settle(UBlueprint* Blueprint);
	/** Unique valid GUID in the Blueprint, RF_Transactional, no two pins with the same name and direction */
	void ExpectWellFormed(const FString& What, UBlueprint* Blueprint, const UEdGraphNode* Node);
	/** Runs CreateMemberVariableNode or CreateLocalVariableNode in one tool call and returns the node it made */
	UK2Node_Variable* Spawn(const FString& What, UBlueprint* Blueprint, UEdGraph* Graph, const FString& Name, bool bLocal, bool bSet,
		const FString& Value = FString(), TSharedPtr<FJsonObject>* OutResult = nullptr);
END_DEFINE_SPEC(FN2CSpawnedNodesSpec)

UBlueprint* FN2CSpawnedNodesSpec::MakeBlueprint(const TCHAR* Name)
{
	// Not transient, so MarkPackageDirty applies (FToolSpecBlueprint puts it back)
	Test = MakeUnique<N2CTest::FToolSpecBlueprint>(Name, TEXT("/Game"));
	UBlueprint* Blueprint = Test->GetBlueprint();
	if (!TestNotNull(TEXT("test Blueprint"), Blueprint))
	{
		return nullptr;
	}
	if (!TestTrue(TEXT("member Health added"), Test->AddMemberVariable(TEXT("Health"), N2CTest::FTestBlueprint::DoublePinType())))
	{
		return nullptr;
	}
	Settle(Blueprint);
	return Blueprint;
}

void FN2CSpawnedNodesSpec::Settle(UBlueprint* Blueprint)
{
	N2CSpawnedNodesSpec::Recompile(Blueprint);
	Test->GetPackage()->SetDirtyFlag(false);
}

void FN2CSpawnedNodesSpec::ExpectWellFormed(const FString& What, UBlueprint* Blueprint, const UEdGraphNode* Node)
{
	if (!TestNotNull(What + TEXT(": node"), Node))
	{
		return;
	}
	TestTrue(What + TEXT(": valid GUID"), Node->NodeGuid.IsValid());
	TestEqual(What + TEXT(": the GUID is unique in the Blueprint"), N2CSpawnedNodesSpec::NodesWithGuid(Blueprint, Node->NodeGuid).Num(), 1);
	TestTrue(What + TEXT(": RF_Transactional"), Node->HasAnyFlags(RF_Transactional));
	TSet<FString> Seen;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		const FString Key = Pin->PinName.ToString() + (Pin->Direction == EGPD_Input ? TEXT(" in") : TEXT(" out"));
		TestFalse(What + TEXT(": one pin named ") + Key, Seen.Contains(Key));
		Seen.Add(Key);
	}
}

UK2Node_Variable* FN2CSpawnedNodesSpec::Spawn(const FString& What, UBlueprint* Blueprint, UEdGraph* Graph, const FString& Name,
	bool bLocal, bool bSet, const FString& Value, TSharedPtr<FJsonObject>* OutResult)
{
	TSharedPtr<FJsonObject> Result;
	FString Error;
	const TArray<FN2CCompileDiagnostics> Compiles = N2CTest::InToolCall([&]()
	{
		Result = bLocal
			? FN2CMcpNodeSpawn::CreateLocalVariableNode(Blueprint, Graph, Name, bSet, FVector2D(200, 100), Value, Error)
			: FN2CMcpNodeSpawn::CreateMemberVariableNode(Blueprint, Graph, Name, bSet, FVector2D(200, 100), Value, Error);
	});
	if (!TestTrue(What + TEXT(": created: ") + Error, Result.IsValid()))
	{
		return nullptr;
	}
	if (TestEqual(What + TEXT(": one compile for the call"), Compiles.Num(), 1))
	{
		TestTrue(What + TEXT(": the Blueprint compiles"), Compiles[0].bSuccess);
	}
	const FGuid Guid = N2CSpawnedNodesSpec::ResultGuid(Result);
	TestTrue(What + TEXT(": nodeId is a GUID"), Guid.IsValid());
	TestEqual(What + TEXT(": nodeId has hyphens"), Result->GetStringField(TEXT("nodeId")), Guid.ToString(EGuidFormats::DigitsWithHyphens));
	TestEqual(What + TEXT(": nodeGuid as add-bp-node returns it"), Result->GetStringField(TEXT("nodeGuid")), Guid.ToString());
	TestTrue(What + TEXT(": inputPins and outputPins"), Result->HasField(TEXT("inputPins")) && Result->HasField(TEXT("outputPins")));
	const TArray<UEdGraphNode*> Found = N2CSpawnedNodesSpec::NodesWithGuid(Blueprint, Guid);
	UK2Node_Variable* Node = Found.Num() == 1 ? Cast<UK2Node_Variable>(Found[0]) : nullptr;
	if (!TestNotNull(What + TEXT(": nodeId names the node"), Node))
	{
		return nullptr;
	}
	TestTrue(What + TEXT(": in the graph asked for"), N2CSpawnedNodesSpec::GraphHas(Graph, Node));
	TestEqual(What + TEXT(": a Set node when asked"), Node->IsA<UK2Node_VariableSet>(), bSet);
	TestEqual(What + TEXT(": pins in the result"), Result->GetArrayField(TEXT("pins")).Num(), Node->Pins.Num());
	ExpectWellFormed(What, Blueprint, Node);
	TestTrue(What + TEXT(": package dirty"), Test->GetPackage()->IsDirty());
	if (OutResult)
	{
		*OutResult = Result;
	}
	return Node;
}

void FN2CSpawnedNodesSpec::Define()
{
	using namespace N2CSpawnedNodesSpec;

	AfterEach([this]()
	{
		// Closes the editor, cleans the package and resets the undo buffer
		Test.Reset();
	});

	Describe(TEXT("member variable nodes"), [this]()
	{
		It(TEXT("are spawned with a unique GUID, one set of pins and a self reference, and a Set node takes its value"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CSpawnMember"));
			if (!Blueprint)
			{
				return;
			}
			UEdGraph* Graph = Test->EventGraph();
			UK2Node_Variable* Get = Spawn(TEXT("Get Health"), Blueprint, Graph, TEXT("Health"), false, false);
			TSharedPtr<FJsonObject> SetResult;
			UK2Node_Variable* Set = Spawn(TEXT("Set Health"), Blueprint, Graph, TEXT("health"), false, true, TEXT("75"), &SetResult);
			if (!Get || !Set)
			{
				return;
			}
			TestNotEqual(TEXT("two nodes, two GUIDs"), Get->NodeGuid, Set->NodeGuid);
			TestEqual(TEXT("Get: one output pin (Health) plus the hidden self pin"), Get->Pins.Num(), 2);
			TestEqual(TEXT("Set: execute, then, Health, Output_Get and the hidden self pin"), Set->Pins.Num(), 5);
			TestTrue(TEXT("self reference"), Get->VariableReference.IsSelfContext() && Set->VariableReference.IsSelfContext());
			const int32 Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, TEXT("Health"));
			if (TestTrue(TEXT("Health exists"), Index != INDEX_NONE))
			{
				TestEqual(TEXT("the reference records the variable's GUID"), Get->VariableReference.GetMemberGuid(), Blueprint->NewVariables[Index].VarGuid);
			}
			TestEqual(TEXT("variableName is the variable's own name (lookup ignores case)"), SetResult->GetStringField(TEXT("variableName")), FString(TEXT("Health")));
			const UEdGraphPin* Value = Set->FindPin(TEXT("Health"), EGPD_Input);
			TestTrue(TEXT("the Set node's value pin holds 75"), Value && Value->DefaultValue.StartsWith(TEXT("75")));
			TestFalse(TEXT("no warning"), SetResult->HasField(TEXT("warning")));
		});

		It(TEXT("reach inherited native and component variables, and refuse Set on read-only ones"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CSpawnNative"));
			if (!Blueprint)
			{
				return;
			}
			// A component variable (SCS) and an own BlueprintReadOnly variable
			USCS_Node* MeshNode = Blueprint->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("Mesh"));
			Blueprint->SimpleConstructionScript->AddNode(MeshNode);
			Test->AddMemberVariable(TEXT("Locked"), N2CTest::FTestBlueprint::IntPinType());
			FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, TEXT("Locked"), true);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			Settle(Blueprint);

			UEdGraph* Graph = Test->EventGraph();
			UK2Node_Variable* Root = Spawn(TEXT("Get RootComponent (AActor)"), Blueprint, Graph, TEXT("RootComponent"), false, false);
			if (Root)
			{
				TestTrue(TEXT("RootComponent: a self reference"), Root->VariableReference.IsSelfContext());
			}
			Spawn(TEXT("Get Mesh (component)"), Blueprint, Graph, TEXT("Mesh"), false, false);
			Spawn(TEXT("Get Locked (read-only, readable)"), Blueprint, Graph, TEXT("Locked"), false, false);

			for (const FString& ReadOnly : TArray<FString>{ TEXT("RootComponent"), TEXT("Locked") })
			{
				FString Error;
				TSharedPtr<FJsonObject> Result;
				const int32 NodesBefore = Graph->Nodes.Num();
				N2CTest::InToolCall([&]() { Result = FN2CMcpNodeSpawn::CreateMemberVariableNode(Blueprint, Graph, ReadOnly, true, FVector2D::ZeroVector, FString(), Error); });
				TestFalse(TEXT("Set refused for ") + ReadOnly, Result.IsValid());
				TestTrue(TEXT("READ_ONLY_VARIABLE for ") + ReadOnly + TEXT(": ") + Error, Error.StartsWith(TEXT("READ_ONLY_VARIABLE:")));
				TestEqual(TEXT("no node added for ") + ReadOnly, Graph->Nodes.Num(), NodesBefore);
			}
			FString Error;
			TSharedPtr<FJsonObject> Missing;
			N2CTest::InToolCall([&]() { Missing = FN2CMcpNodeSpawn::CreateMemberVariableNode(Blueprint, Graph, TEXT("NoSuchVariable"), false, FVector2D::ZeroVector, FString(), Error); });
			TestTrue(TEXT("an unknown variable is an error: ") + Error, !Missing.IsValid() && Error.Contains(TEXT("not found")));
		});
	});

	Describe(TEXT("local variable nodes"), [this]()
	{
		It(TEXT("are spawned in a function and in a collapsed graph inside it, scoped to the function, Set with 4 pins"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CSpawnLocal"));
			if (!Blueprint)
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CLocalFn")));
			UK2Node_Composite* Collapsed = Test->AddCollapsedGraph(Function, FName(TEXT("N2CInner")));
			if (!TestNotNull(TEXT("collapsed graph"), Collapsed ? Collapsed->BoundGraph.Get() : nullptr))
			{
				return;
			}
			FString Error;
			FName Local;
			N2CTest::InToolCall([&]()
			{
				Local = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Counter"),
					N2CTest::FTestBlueprint::IntPinType(), FString(), FString(), Error);
			});
			if (!TestFalse(TEXT("local Counter created: ") + Error, Local.IsNone()))
			{
				return;
			}
			Settle(Blueprint);
			TestEqual(TEXT("FindFunctionEntryNode from the collapsed graph finds the function's entry"),
				FN2CMcpVariableUtils::FindFunctionEntryNode(Collapsed->BoundGraph), Test->FunctionEntry(Function));

			for (UEdGraph* Graph : TArray<UEdGraph*>{ Function, Collapsed->BoundGraph.Get() })
			{
				const FString Where = Graph == Function ? TEXT(" in the function") : TEXT(" in the collapsed graph");
				UK2Node_Variable* Get = Spawn(TEXT("Get Counter") + Where, Blueprint, Graph, Local.ToString(), true, false);
				TSharedPtr<FJsonObject> SetResult;
				UK2Node_Variable* Set = Spawn(TEXT("Set Counter") + Where, Blueprint, Graph, Local.ToString(), true, true, TEXT("3"), &SetResult);
				if (!Get || !Set)
				{
					continue;
				}
				TestTrue(TEXT("local reference") + Where, Get->VariableReference.IsLocalScope() && Set->VariableReference.IsLocalScope());
				TestEqual(TEXT("scope is the function") + Where, Set->VariableReference.GetMemberScopeName(), FString(TEXT("N2CLocalFn")));
				TestEqual(TEXT("Set-local has exactly 4 pins") + Where, Set->Pins.Num(), 4);
				TestEqual(TEXT("functionName") + Where, SetResult->GetStringField(TEXT("functionName")), FString(TEXT("N2CLocalFn")));
			}

			TSharedPtr<FJsonObject> Refused;
			N2CTest::InToolCall([&]() { Refused = FN2CMcpNodeSpawn::CreateLocalVariableNode(Blueprint, Test->EventGraph(), Local.ToString(), false, FVector2D::ZeroVector, FString(), Error); });
			TestFalse(TEXT("an event graph has no locals"), Refused.IsValid());
		});
	});

	Describe(TEXT("the returned nodeId"), [this]()
	{
		It(TEXT("is wired by connect-pins (exec and data)"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CSpawnWire"));
			if (!Blueprint)
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CWireFn")));
			Settle(Blueprint);
			TSharedPtr<FJsonObject> GetResult;
			TSharedPtr<FJsonObject> SetResult;
			UK2Node_Variable* Get = Spawn(TEXT("Get Health"), Blueprint, Function, TEXT("Health"), false, false, FString(), &GetResult);
			UK2Node_Variable* Set = Spawn(TEXT("Set Health"), Blueprint, Function, TEXT("Health"), false, true, FString(), &SetResult);
			UK2Node_FunctionEntry* Entry = Test->FunctionEntry(Function);
			if (!Get || !Set || !Entry)
			{
				return;
			}
			auto End = [](const FString& NodeGuid, const TCHAR* PinName)
			{
				const TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
				Object->SetStringField(TEXT("nodeGuid"), NodeGuid);
				Object->SetStringField(TEXT("pinGuid"), FGuid().ToString());
				Object->SetStringField(TEXT("pinName"), PinName);
				return Object;
			};
			auto Connection = [&End](const FString& FromNode, const TCHAR* FromPin, const FString& ToNode, const TCHAR* ToPin)
			{
				const TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
				Object->SetObjectField(TEXT("from"), End(FromNode, FromPin));
				Object->SetObjectField(TEXT("to"), End(ToNode, ToPin));
				return MakeShared<FJsonValueObject>(Object);
			};
			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Function);
			Args->SetArrayField(TEXT("connections"), TArray<TSharedPtr<FJsonValue>>{
				Connection(Entry->NodeGuid.ToString(), TEXT("then"), SetResult->GetStringField(TEXT("nodeId")), TEXT("execute")),
				Connection(GetResult->GetStringField(TEXT("nodeId")), TEXT("Health"), SetResult->GetStringField(TEXT("nodeId")), TEXT("Health")) });
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("connect-pins"), Args);
			TestFalse(TEXT("connect-pins succeeds: ") + N2CTest::ResultText(Result).Left(400), Result.bIsError);
			const UEdGraphPin* Execute = Set->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			const UEdGraphPin* Value = Set->FindPin(TEXT("Health"), EGPD_Input);
			TestTrue(TEXT("exec wired from the entry"), Execute && Execute->LinkedTo.Num() == 1 && Execute->LinkedTo[0]->GetOwningNode() == Entry);
			TestTrue(TEXT("data wired from the Get node"), Value && Value->LinkedTo.Num() == 1 && Value->LinkedTo[0]->GetOwningNode() == Get);
		});
	});

	Describe(TEXT("undo"), [this]()
	{
		It(TEXT("removes a spawned Set node (with its value) in one step and leaves the previous transaction alone"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CSpawnUndo"));
			if (!Blueprint || !TestTrue(TEXT("an editor transaction buffer"), GEditor && GEditor->Trans))
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CUndoFn")));
			FString Error;
			FName Local;
			N2CTest::InToolCall([&]()
			{
				Local = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Counter"),
					N2CTest::FTestBlueprint::IntPinType(), FString(), FString(), Error);
			});
			Settle(Blueprint);
			{
				const FScopedTransaction Earlier(NSLOCTEXT("NodeToCode", "N2CSpawnedNodesEarlier", "Earlier edit (test)"));
				Blueprint->Modify();
				Blueprint->BlueprintDescription = TEXT("Edited before the node");
			}

			for (const bool bLocal : { false, true })
			{
				const FString What = bLocal ? TEXT("Set Counter (local)") : TEXT("Set Health (member)");
				const int32 QueueBefore = GEditor->Trans->GetQueueLength();
				const int32 NodesBefore = Function->Nodes.Num();
				UK2Node_Variable* Set = Spawn(What, Blueprint, Function, bLocal ? Local.ToString() : TEXT("Health"), bLocal, true, TEXT("5"));
				if (!Set)
				{
					return;
				}
				const FGuid Guid = Set->NodeGuid;
				TestEqual(What + TEXT(": one transaction recorded for the call"), GEditor->Trans->GetQueueLength(), QueueBefore + 1);
				TestTrue(What + TEXT(": undo runs"), GEditor->UndoTransaction());
				TestEqual(What + TEXT(": one undo removes the node"), NodesWithGuid(Blueprint, Guid).Num(), 0);
				TestEqual(What + TEXT(": the graph is as before"), Function->Nodes.Num(), NodesBefore);
				TestEqual(What + TEXT(": the earlier edit is still there"), Blueprint->BlueprintDescription, FString(TEXT("Edited before the node")));
				TestTrue(What + TEXT(": redo runs"), GEditor->RedoTransaction());
				TestEqual(What + TEXT(": redo brings the node back"), NodesWithGuid(Blueprint, Guid).Num(), 1);
			}
		});
	});

	Describe(TEXT("create-comment-node"), [this]()
	{
		It(TEXT("makes a transactional comment with a GUID around the nodes, undone in one step, package dirty"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CSpawnComment"));
			if (!Blueprint || !TestTrue(TEXT("an editor transaction buffer"), GEditor && GEditor->Trans))
			{
				return;
			}
			UEdGraph* Graph = Test->EventGraph();
			UK2Node_Variable* Get = Spawn(TEXT("Get Health"), Blueprint, Graph, TEXT("Health"), false, false);
			UK2Node_Variable* Set = Spawn(TEXT("Set Health"), Blueprint, Graph, TEXT("Health"), false, true);
			if (!Get || !Set)
			{
				return;
			}
			Test->GetPackage()->SetDirtyFlag(false);
			const int32 QueueBefore = GEditor->Trans->GetQueueLength();
			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Blueprint, Graph);
			Args->SetArrayField(TEXT("nodeGuids"), TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(Get->NodeGuid.ToString()),
				MakeShared<FJsonValueString>(Set->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)) });
			Args->SetStringField(TEXT("commentText"), TEXT("Spawned"));
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-comment-node"), Args);
			if (!TestFalse(TEXT("create-comment-node succeeds: ") + N2CTest::ResultText(Result).Left(400), Result.bIsError))
			{
				return;
			}
			TArray<UEdGraphNode_Comment*> Comments;
			Graph->GetNodesOfClass(Comments);
			if (!TestEqual(TEXT("one comment"), Comments.Num(), 1))
			{
				return;
			}
			UEdGraphNode_Comment* Comment = Comments[0];
			ExpectWellFormed(TEXT("comment"), Blueprint, Comment);
			TestEqual(TEXT("text"), Comment->NodeComment, FString(TEXT("Spawned")));
			TestEqual(TEXT("encloses both nodes"), Comment->GetNodesUnderComment().Num(), 2);
			TestTrue(TEXT("package dirty"), Test->GetPackage()->IsDirty());
			TestEqual(TEXT("one transaction recorded"), GEditor->Trans->GetQueueLength(), QueueBefore + 1);
			TestTrue(TEXT("undo runs"), GEditor->UndoTransaction());
			Comments.Reset();
			Graph->GetNodesOfClass(Comments);
			TestEqual(TEXT("one undo removes the comment"), Comments.Num(), 0);
			TestTrue(TEXT("the variable nodes stay"), GraphHas(Graph, Get) && GraphHas(Graph, Set));
		});
	});

#if N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
	Describe(TEXT("through the MCP tools on a focused Blueprint (UE 5.8)"), [this]()
	{
		It(TEXT("the four variable node tools return the node GUID, and the local ones work in a collapsed graph"), [this]()
		{
			UBlueprint* Blueprint = MakeBlueprint(TEXT("BP_N2CSpawnTools"));
			if (!Blueprint)
			{
				return;
			}
			UEdGraph* Function = Test->AddFunction(FName(TEXT("N2CToolFn")));
			UK2Node_Composite* Collapsed = Test->AddCollapsedGraph(Function, FName(TEXT("N2CToolInner")));
			FString Error;
			FName Local;
			N2CTest::InToolCall([&]()
			{
				Local = FN2CMcpCreateLocalVariableTool::AddLocalVariableToFunction(Blueprint, Function, TEXT("Counter"),
					N2CTest::FTestBlueprint::IntPinType(), FString(), FString(), Error);
			});
			Settle(Blueprint);
			if (!TestNotNull(TEXT("collapsed graph"), Collapsed ? Collapsed->BoundGraph.Get() : nullptr) || !TestFalse(TEXT("local: ") + Error, Local.IsNone()))
			{
				return;
			}
			if (!Test->OpenInEditor(*this, Function, /*bClean*/ false))
			{
				return;
			}
			const TSharedPtr<FBlueprintEditor> Opened = FN2CBlueprintEditorTracker::GetEditorFor(Blueprint);
			if (!TestTrue(TEXT("the editor is tracked"), Opened.IsValid()))
			{
				return;
			}

			struct FToolCase { const TCHAR* Tool; bool bSet; bool bLocal; UEdGraph* Graph; };
			const FToolCase Cases[] = {
				{ TEXT("create-get-member-variable-node"), false, false, Function },
				{ TEXT("create-set-member-variable-node"), true, false, Function },
				{ TEXT("create-get-local-function-variable-node"), false, true, Function },
				{ TEXT("create-set-local-function-variable-node"), true, true, Function },
				{ TEXT("create-get-local-function-variable-node"), false, true, Collapsed->BoundGraph },
				{ TEXT("create-set-local-function-variable-node"), true, true, Collapsed->BoundGraph },
			};
			for (const FToolCase& Case : Cases)
			{
				const FString What = FString(Case.Tool) + (Case.Graph == Function ? TEXT(" (function)") : TEXT(" (collapsed graph)"));
				Opened->JumpToHyperlink(Case.Graph, false);
				const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("variableName"), Case.bLocal ? Local.ToString() : TEXT("Health"));
				const FMcpToolCallResult Result = N2CTest::RunTool(Case.Tool, Args);
				if (!TestFalse(What + TEXT(" succeeds: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError))
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
				const FGuid Guid = ResultGuid(Json);
				const TArray<UEdGraphNode*> Found = NodesWithGuid(Blueprint, Guid);
				if (!TestEqual(What + TEXT(": nodeId names one node"), Found.Num(), 1))
				{
					continue;
				}
				TestTrue(What + TEXT(": in the focused graph"), GraphHas(Case.Graph, Found[0]));
				ExpectWellFormed(What, Blueprint, Found[0]);
				if (Case.bSet && Case.bLocal)
				{
					TestEqual(What + TEXT(": pins lists 4 pins"), Json->GetArrayField(TEXT("pins")).Num(), 4);
				}
				const TSharedPtr<FJsonObject>* Compile = nullptr;
				if (TestTrue(What + TEXT(": the result carries the compile"), Json->TryGetObjectField(TEXT("compile"), Compile)))
				{
					TestTrue(What + TEXT(": the Blueprint compiles"), (*Compile)->GetBoolField(TEXT("success")));
				}
			}
		});
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
