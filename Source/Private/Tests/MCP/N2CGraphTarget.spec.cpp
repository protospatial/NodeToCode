// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
#include "Core/N2CEditorIntegration.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"

namespace N2CGraphTargetSpec
{
	void AddConnection(const TSharedPtr<FJsonObject>& Args, const UEdGraphPin* From, const UEdGraphPin* To)
	{
		const TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
		Connection->SetField(TEXT("from"), N2CTest::PinRef(From));
		Connection->SetField(TEXT("to"), N2CTest::PinRef(To));
		TArray<TSharedPtr<FJsonValue>> Connections;
		Connections.Add(MakeShared<FJsonValueObject>(Connection));
		Args->SetArrayField(TEXT("connections"), Connections);
	}

	UK2Node_FunctionResult* ResultNode(UEdGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node))
			{
				return Result;
			}
		}
		return nullptr;
	}

	/** The 14 Graph and Functions tools of card 0152 */
	const TCHAR* const GraphTools[] = {
		TEXT("add-bp-node-to-active-graph"), TEXT("connect-pins"), TEXT("create-comment-node"), TEXT("delete-blueprint-node"),
		TEXT("find-nodes-in-graph"), TEXT("search-blueprint-nodes"), TEXT("set-input-pin-value"),
		TEXT("add-function-input-pin"), TEXT("add-function-return-pin"), TEXT("delete-blueprint-function"),
		TEXT("open-blueprint-function"), TEXT("remove-function-entry-pin"), TEXT("remove-function-return-pin") };
}

/**
 * The one graph-target resolver (FN2CGraphTarget, card 0152) and stable function IDs, on Blueprints built in code
 * with no Blueprint editor open: every graph kind by name and GUID, the error codes, the Graph and Functions tools
 * resolving through it, and a function found by the ID it had before a rename and a move.
 */
BEGIN_DEFINE_SPEC(FN2CGraphTargetSpec, "NodeToCode.MCP.GraphTarget", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	UEdGraph* Function = nullptr;
	UEdGraph* Macro = nullptr;
	UK2Node_Composite* Collapsed = nullptr;

	FN2CGraphTarget Resolve(const FString& Path, const FString& Name, const FString& Guid, bool bForWrite = false)
	{
		FN2CGraphTargetArgs Args;
		Args.BlueprintPath = Path;
		Args.GraphName = Name;
		Args.GraphGuid = Guid;
		return FN2CGraphTarget::Resolve(Args, bForWrite);
	}
	FString Path() const { return Test->GetBlueprint()->GetPathName(); }
	/** True when nothing is focused, so a focus fallback can be checked; AddInfo and false otherwise */
	bool NothingFocused()
	{
		if (FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor() || FN2CEditorIntegration::Get().GetActiveBlueprintEditor())
		{
			AddInfo(TEXT("A Blueprint editor is focused in this session; the no-focus case is not checked"));
			return false;
		}
		return true;
	}
END_DEFINE_SPEC(FN2CGraphTargetSpec)

void FN2CGraphTargetSpec::Define()
{
	using namespace N2CGraphTargetSpec;

	BeforeEach([this]()
	{
		Test = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CGraphTarget"));
		Function = Test->AddFunction(FName(TEXT("N2CTargetFunction")));
		Macro = Test->AddMacro(FName(TEXT("N2CTargetMacro")));
		Collapsed = Test->AddCollapsedGraph(Test->EventGraph(), FName(TEXT("N2CTargetCollapsed")));
		TestTrue(TEXT("test graphs built: ") + FString::Join(Test->Errors(), TEXT("; ")),
			Test->Errors().Num() == 0 && Function && Macro && Collapsed);
	});

	AfterEach([this]()
	{
		Function = nullptr;
		Macro = nullptr;
		Collapsed = nullptr;
		Test.Reset();
	});

	Describe(TEXT("Resolve"), [this]()
	{
		It(TEXT("finds the event graph, a function, a macro and a collapsed graph by name"), [this]()
		{
			struct FCase { const TCHAR* Name; UEdGraph* Expected; const TCHAR* Kind; };
			const FCase Cases[] = {
				{ TEXT("EventGraph"), Test->EventGraph(), TEXT("event") },
				{ TEXT("N2CTargetFunction"), Function, TEXT("function") },
				{ TEXT("n2ctargetmacro"), Macro, TEXT("macro") },
				{ TEXT("N2CTargetCollapsed"), Collapsed->BoundGraph, TEXT("collapsed") },
			};
			for (const FCase& Case : Cases)
			{
				const FN2CGraphTarget Target = Resolve(Path(), Case.Name, FString());
				if (TestTrue(FString::Printf(TEXT("%s resolves: %s"), Case.Name, *Target.Error), Target.IsValid()))
				{
					TestTrue(FString::Printf(TEXT("%s is the right graph"), Case.Name), Target.Graph == Case.Expected);
					TestTrue(TEXT("in this Blueprint"), Target.Blueprint == Test->GetBlueprint());
					TestEqual(FString::Printf(TEXT("%s kind"), Case.Name), FN2CGraphTarget::GraphKind(Target.Graph), FString(Case.Kind));
					TestEqual(TEXT("resolvedBy"), FN2CGraphTarget::ResolvedByToString(Target.ResolvedBy), FString(TEXT("explicit")));
				}
			}
		});

		It(TEXT("finds every graph kind by its GraphGuid, hyphenated or not"), [this]()
		{
			for (UEdGraph* Graph : TArray<UEdGraph*>{ Test->EventGraph(), Function, Macro, Collapsed->BoundGraph.Get() })
			{
				for (const EGuidFormats Format : { EGuidFormats::Digits, EGuidFormats::DigitsWithHyphens })
				{
					const FN2CGraphTarget Target = Resolve(Path(), FString(), Graph->GraphGuid.ToString(Format));
					TestTrue(FString::Printf(TEXT("%s by GUID: %s"), *Graph->GetName(), *Target.Error), Target.IsValid() && Target.Graph == Graph);
				}
			}
		});

		It(TEXT("lets graphGuid win over graphName"), [this]()
		{
			const FN2CGraphTarget Target = Resolve(Path(), TEXT("EventGraph"), Function->GraphGuid.ToString());
			TestTrue(TEXT("the GUID's graph"), Target.IsValid() && Target.Graph == Function);
		});

		It(TEXT("uses the event graph for blueprintPath alone when none of its graphs is focused"), [this]()
		{
			const FN2CGraphTarget Target = Resolve(Path(), FString(), FString());
			if (TestTrue(TEXT("resolves: ") + Target.Error, Target.IsValid()))
			{
				TestTrue(TEXT("the event graph"), Target.Graph == FBlueprintEditorUtils::FindEventGraph(Test->GetBlueprint()));
				TestEqual(TEXT("resolvedBy"), FN2CGraphTarget::ResolvedByToString(Target.ResolvedBy), FString(TEXT("eventGraph")));
			}
		});

		It(TEXT("echoes blueprintPath, graphName, graphGuid, graphKind and resolvedBy"), [this]()
		{
			const TSharedRef<FJsonObject> Json = Resolve(Path(), TEXT("N2CTargetFunction"), FString()).ToJson();
			TestEqual(TEXT("blueprintPath"), Json->GetStringField(TEXT("blueprintPath")), Path());
			TestEqual(TEXT("graphName"), Json->GetStringField(TEXT("graphName")), FString(TEXT("N2CTargetFunction")));
			TestEqual(TEXT("graphGuid"), Json->GetStringField(TEXT("graphGuid")), Function->GraphGuid.ToString());
			TestEqual(TEXT("graphKind"), Json->GetStringField(TEXT("graphKind")), FString(TEXT("function")));
			TestEqual(TEXT("resolvedBy"), Json->GetStringField(TEXT("resolvedBy")), FString(TEXT("explicit")));
		});

		It(TEXT("tells duplicated Blueprints apart by blueprintPath although their graphs share GUIDs"), [this]()
		{
			UBlueprint* Copy = DuplicateObject<UBlueprint>(Test->GetBlueprint(), Test->GetPackage(), FName(TEXT("BP_N2CGraphTargetCopy")));
			if (!TestNotNull(TEXT("duplicate"), Copy))
			{
				return;
			}
			UEdGraph* CopyFunction = FN2CGraphTarget::FindGraphsByName(Copy, TEXT("N2CTargetFunction")).Num() == 1
				? FN2CGraphTarget::FindGraphsByName(Copy, TEXT("N2CTargetFunction"))[0] : nullptr;
			if (!TestNotNull(TEXT("the copy has the function"), CopyFunction))
			{
				return;
			}
			TestEqual(TEXT("the engine keeps the GraphGuid in the copy (editor-integration-2)"), CopyFunction->GraphGuid.ToString(), Function->GraphGuid.ToString());
			const FN2CGraphTarget InOriginal = Resolve(Path(), FString(), Function->GraphGuid.ToString());
			const FN2CGraphTarget InCopy = Resolve(Copy->GetPathName(), FString(), Function->GraphGuid.ToString());
			TestTrue(TEXT("original path -> original graph"), InOriginal.IsValid() && InOriginal.Graph == Function);
			TestTrue(TEXT("copy path -> copy's graph"), InCopy.IsValid() && InCopy.Graph == CopyFunction && InCopy.Blueprint == Copy);
		});

		Describe(TEXT("errors"), [this]()
		{
			It(TEXT("ASSET_NOT_FOUND for a path that loads no Blueprint"), [this]()
			{
				const FN2CGraphTarget Target = Resolve(TEXT("/Game/N2CNoSuchFolder/BP_Missing.BP_Missing"), TEXT("EventGraph"), FString());
				TestEqual(TEXT("code"), Target.ErrorCode(), FString(TEXT("ASSET_NOT_FOUND")));
				TestFalse(TEXT("not valid"), Target.IsValid());
			});

			It(TEXT("GRAPH_NOT_FOUND for an unknown name, an unknown GUID and a GUID that does not parse, listing the graphs"), [this]()
			{
				for (const FN2CGraphTarget& Target : {
					Resolve(Path(), TEXT("NoSuchGraph"), FString()),
					Resolve(Path(), FString(), FGuid::NewGuid().ToString()),
					Resolve(Path(), FString(), TEXT("not-a-guid")) })
				{
					TestEqual(TEXT("code: ") + Target.Error, Target.ErrorCode(), FString(TEXT("GRAPH_NOT_FOUND")));
					TestTrue(TEXT("lists the graphs"), Target.Error.Contains(TEXT("N2CTargetFunction")));
				}
			});

			It(TEXT("AMBIGUOUS_GRAPH_NAME when two collapsed graphs share a name, and the GUID still resolves each"), [this]()
			{
				UK2Node_Composite* Second = Test->AddCollapsedGraph(Function, FName(TEXT("N2CSecondCollapsed")));
				if (!TestNotNull(TEXT("second collapsed graph"), Second))
				{
					return;
				}
				// Different outers (two composite nodes), so the engine lets both graphs have the same name
				Second->BoundGraph->Rename(TEXT("N2CTargetCollapsed"), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
				const FN2CGraphTarget Target = Resolve(Path(), TEXT("N2CTargetCollapsed"), FString());
				TestEqual(TEXT("code: ") + Target.Error, Target.ErrorCode(), FString(TEXT("AMBIGUOUS_GRAPH_NAME")));
				TestTrue(TEXT("names both GUIDs"), Target.Error.Contains(Collapsed->BoundGraph->GraphGuid.ToString())
					&& Target.Error.Contains(Second->BoundGraph->GraphGuid.ToString()));
				const FN2CGraphTarget ByGuid = Resolve(Path(), FString(), Second->BoundGraph->GraphGuid.ToString());
				TestTrue(TEXT("by GUID"), ByGuid.IsValid() && ByGuid.Graph == Second->BoundGraph);
			});

			It(TEXT("READ_ONLY_GRAPH for a write to a read-only graph, while a read resolves"), [this]()
			{
				Function->bEditable = false;
				const FN2CGraphTarget Write = Resolve(Path(), TEXT("N2CTargetFunction"), FString(), /*bForWrite*/ true);
				const FN2CGraphTarget Read = Resolve(Path(), TEXT("N2CTargetFunction"), FString(), /*bForWrite*/ false);
				Function->bEditable = true;
				TestEqual(TEXT("write code: ") + Write.Error, Write.ErrorCode(), FString(TEXT("READ_ONLY_GRAPH")));
				TestTrue(TEXT("read resolves"), Read.IsValid() && Read.Graph == Function);
			});

			It(TEXT("NO_FOCUSED_GRAPH with no target and nothing focused, and for a graph name without blueprintPath"), [this]()
			{
				if (!NothingFocused())
				{
					return;
				}
				TestEqual(TEXT("no arguments"), Resolve(FString(), FString(), FString()).ErrorCode(), FString(TEXT("NO_FOCUSED_GRAPH")));
				TestEqual(TEXT("graphName only"), Resolve(FString(), TEXT("EventGraph"), FString()).ErrorCode(), FString(TEXT("NO_FOCUSED_GRAPH")));
			});
		});
	});

	Describe(TEXT("function IDs"), [this]()
	{
		It(TEXT("are the function graph's GraphGuid in list-blueprint-functions"), [this]()
		{
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), Path());
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("list-blueprint-functions"), Args);
			TestFalse(TEXT("list succeeds: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError);
			TestTrue(TEXT("lists the GraphGuid"), N2CTest::ResultText(Result).Contains(Function->GraphGuid.ToString()));
			TestEqual(TEXT("GetFunctionId"), FN2CMcpFunctionGuidUtils::GetFunctionId(Function).ToString(), Function->GraphGuid.ToString());
		});

		It(TEXT("still find the function after it is renamed and its Blueprint is moved"), [this]()
		{
			const FGuid IdBefore = FN2CMcpFunctionGuidUtils::GetFunctionId(Function);
			const FString LegacyBefore = FN2CMcpFunctionGuidUtils::GetLegacyFunctionId(Function).ToString();

			FBlueprintEditorUtils::RenameGraph(Function, TEXT("N2CTargetFunctionRenamed"));
			TestEqual(TEXT("renamed"), Function->GetName(), FString(TEXT("N2CTargetFunctionRenamed")));

			// Move: the Blueprint goes to another package (its path changes), then comes back for cleanup
			UBlueprint* Blueprint = Test->GetBlueprint();
			UPackage* OtherPackage = CreatePackage(*(TEXT("/Temp/N2CGraphTargetMoved_") + FGuid::NewGuid().ToString()));
			OtherPackage->SetFlags(RF_Transient);
			const FString PathBefore = Blueprint->GetPathName();
			Blueprint->Rename(nullptr, OtherPackage, REN_DontCreateRedirectors | REN_NonTransactional);
			const FString PathAfter = Blueprint->GetPathName();

			TestNotEqual(TEXT("the Blueprint moved"), PathAfter, PathBefore);
			TestEqual(TEXT("the ID is unchanged"), FN2CMcpFunctionGuidUtils::GetFunctionId(Function).ToString(), IdBefore.ToString());
			bool bLegacy = true;
			TestTrue(TEXT("FindFunctionByGuid by the old ID"),
				FN2CMcpFunctionGuidUtils::FindFunctionByGuid(Blueprint, IdBefore, &bLegacy) == Function && !bLegacy);
			const FN2CGraphTarget Target = Resolve(PathAfter, FString(), IdBefore.ToString(), /*bForWrite*/ true);
			TestTrue(TEXT("the resolver by the old ID: ") + Target.Error, Target.IsValid() && Target.Graph == Function);
			TestNotEqual(TEXT("the old hash ID would have changed"), FN2CMcpFunctionGuidUtils::GetLegacyFunctionId(Function).ToString(), LegacyBefore);

			Blueprint->Rename(nullptr, Test->GetPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		});

		It(TEXT("accept an old name-hash ID with a deprecation warning"), [this]()
		{
			const FString OldId = FN2CMcpFunctionGuidUtils::GenerateDeterministicGuid(Test->GetBlueprint(), Function->GetName()).ToString();
			const FN2CGraphTarget Target = Resolve(Path(), FString(), OldId);
			if (TestTrue(TEXT("resolves: ") + Target.Error, Target.IsValid()))
			{
				TestTrue(TEXT("the function"), Target.Graph == Function);
				TestTrue(TEXT("flagged legacy"), Target.bLegacyId);
				const TSharedRef<FJsonObject> Json = Target.ToJson();
				TestEqual(TEXT("deprecatedId"), Json->GetStringField(TEXT("deprecatedId")), OldId);
				TestTrue(TEXT("the warning names the stable ID and the removal release"),
					Json->GetStringField(TEXT("warning")).Contains(Function->GraphGuid.ToString())
					&& Json->GetStringField(TEXT("warning")).Contains(FN2CMcpFunctionGuidUtils::LegacyIdRemovalVersion()));
			}
		});
	});

	Describe(TEXT("Graph and Functions tools"), [this]()
	{
		It(TEXT("declare blueprintPath, graphName and graphGuid"), [this]()
		{
			for (const TCHAR* Name : GraphTools)
			{
				const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(Name);
				if (!TestTrue(FString::Printf(TEXT("%s registered"), Name), Tool.IsValid()))
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Schema = Tool->GetDefinition().InputSchema;
				const TSharedPtr<FJsonObject>* Properties = nullptr;
				const bool bHas = Schema.IsValid() && Schema->TryGetObjectField(TEXT("properties"), Properties)
					&& (*Properties)->HasField(TEXT("blueprintPath")) && (*Properties)->HasField(TEXT("graphName"))
					&& (*Properties)->HasField(TEXT("graphGuid"));
				TestTrue(FString::Printf(TEXT("%s declares the target arguments"), Name), bHas);
			}
			const TSharedPtr<IN2CMcpTool> Create = N2CTest::FindRegisteredTool(TEXT("create-blueprint-function"));
			const TSharedPtr<FJsonObject>* CreateProperties = nullptr;
			TestTrue(TEXT("create-blueprint-function declares blueprintPath"), Create.IsValid()
				&& Create->GetDefinition().InputSchema->TryGetObjectField(TEXT("properties"), CreateProperties)
				&& (*CreateProperties)->HasField(TEXT("blueprintPath")));
		});

		It(TEXT("add a node and connect two pins by blueprintPath plus graphGuid, with no editor open"), [this]()
		{
			UEdGraph* Graph = Test->EventGraph();
			UK2Node_CustomEvent* Event = Test->AddCustomEvent(Graph, FName(TEXT("N2CTargetEvent")));
			if (!TestNotNull(TEXT("event"), Event))
			{
				return;
			}

			// search-blueprint-nodes in that graph for Print String's stable ID
			const TSharedPtr<FJsonObject> SearchArgs = N2CTest::TargetArgs(Test->GetBlueprint(), Graph);
			SearchArgs->SetStringField(TEXT("searchTerm"), TEXT("print string"));
			const FMcpToolCallResult Search = N2CTest::RunTool(TEXT("search-blueprint-nodes"), SearchArgs);
			const TSharedPtr<FJsonObject> SearchTarget = N2CTest::ResultTarget(Search);
			TestTrue(TEXT("search echoes the target"), SearchTarget.IsValid()
				&& SearchTarget->GetStringField(TEXT("graphGuid")) == Graph->GraphGuid.ToString());
			FString ActionId;
			FString NodeName;
			const TSharedPtr<FJsonObject> SearchJson = N2CTest::ResultJson(Search);
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (SearchJson.IsValid() && SearchJson->TryGetArrayField(TEXT("nodes"), Nodes))
			{
				for (const TSharedPtr<FJsonValue>& Node : *Nodes)
				{
					const TSharedPtr<FJsonObject> Spawn = Node->AsObject()->GetObjectField(TEXT("spawnMetadata"));
					if (Spawn->GetStringField(TEXT("actionIdentifier")).Contains(TEXT("KismetSystemLibrary:PrintString")))
					{
						ActionId = Spawn->GetStringField(TEXT("actionIdentifier"));
						NodeName = Spawn->GetStringField(TEXT("nodeName"));
						break;
					}
				}
			}
			if (!TestFalse(TEXT("Print String found: ") + N2CTest::ResultText(Search).Left(300), ActionId.IsEmpty()))
			{
				return;
			}

			const TSharedPtr<FJsonObject> AddArgs = N2CTest::TargetArgs(Test->GetBlueprint(), Graph);
			AddArgs->SetStringField(TEXT("nodeName"), NodeName);
			AddArgs->SetStringField(TEXT("actionIdentifier"), ActionId);
			const int32 NodesBefore = Graph->Nodes.Num();
			const FMcpToolCallResult Added = N2CTest::RunTool(TEXT("add-bp-node-to-active-graph"), AddArgs);
			TestFalse(TEXT("add succeeds: ") + N2CTest::ResultText(Added).Left(300), Added.bIsError);
			TestEqual(TEXT("one node added to that graph"), Graph->Nodes.Num(), NodesBefore + 1);
			const TSharedPtr<FJsonObject> AddTarget = N2CTest::ResultTarget(Added);
			if (TestTrue(TEXT("add echoes the target"), AddTarget.IsValid()))
			{
				TestEqual(TEXT("blueprintPath"), AddTarget->GetStringField(TEXT("blueprintPath")), Path());
				TestEqual(TEXT("graphName"), AddTarget->GetStringField(TEXT("graphName")), Graph->GetName());
				TestEqual(TEXT("graphGuid"), AddTarget->GetStringField(TEXT("graphGuid")), Graph->GraphGuid.ToString());
				TestEqual(TEXT("resolvedBy"), AddTarget->GetStringField(TEXT("resolvedBy")), FString(TEXT("explicit")));
			}
			UK2Node_CallFunction* Print = Cast<UK2Node_CallFunction>(Graph->Nodes.Last());
			if (!TestNotNull(TEXT("the new node is a call"), Print))
			{
				return;
			}

			const TSharedPtr<FJsonObject> ConnectArgs = N2CTest::TargetArgs(Test->GetBlueprint(), Graph);
			UEdGraphPin* Then = Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* Execute = Print->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			AddConnection(ConnectArgs, Then, Execute);
			const FMcpToolCallResult Connected = N2CTest::RunTool(TEXT("connect-pins"), ConnectArgs);
			TestFalse(TEXT("connect succeeds: ") + N2CTest::ResultText(Connected).Left(300), Connected.bIsError);
			TestTrue(TEXT("linked"), Then->LinkedTo.Contains(Execute));
			TestTrue(TEXT("connect echoes the target"), N2CTest::ResultTarget(Connected).IsValid());

			// The other graph tools by graphName
			const TSharedPtr<FJsonObject> FindArgs = MakeShared<FJsonObject>();
			FindArgs->SetStringField(TEXT("blueprintPath"), Path());
			FindArgs->SetStringField(TEXT("graphName"), Graph->GetName());
			TArray<TSharedPtr<FJsonValue>> Terms;
			Terms.Add(MakeShared<FJsonValueString>(Print->NodeGuid.ToString()));
			FindArgs->SetArrayField(TEXT("searchTerms"), Terms);
			FindArgs->SetStringField(TEXT("searchType"), TEXT("guid"));
			const FMcpToolCallResult Found = N2CTest::RunTool(TEXT("find-nodes-in-graph"), FindArgs);
			const TSharedPtr<FJsonObject> FoundJson = N2CTest::ResultJson(Found);
			TestTrue(TEXT("find-nodes-in-graph finds the node by graphName: ") + N2CTest::ResultText(Found).Left(200),
				FoundJson.IsValid() && FoundJson->GetObjectField(TEXT("metadata"))->GetIntegerField(TEXT("totalFound")) == 1);

			const TSharedPtr<FJsonObject> SetArgs = N2CTest::TargetArgs(Test->GetBlueprint(), Graph);
			SetArgs->SetStringField(TEXT("nodeGuid"), Print->NodeGuid.ToString());
			SetArgs->SetStringField(TEXT("pinGuid"), FGuid().ToString());
			SetArgs->SetStringField(TEXT("pinName"), TEXT("InString"));
			SetArgs->SetStringField(TEXT("value"), TEXT("N2C target"));
			const FMcpToolCallResult Set = N2CTest::RunTool(TEXT("set-input-pin-value"), SetArgs);
			TestFalse(TEXT("set-input-pin-value: ") + N2CTest::ResultText(Set).Left(200), Set.bIsError);
			UEdGraphPin* InString = Print->FindPin(TEXT("InString"), EGPD_Input);
			TestTrue(TEXT("value set"), InString && InString->DefaultValue == TEXT("N2C target"));

			const TSharedPtr<FJsonObject> CommentArgs = N2CTest::TargetArgs(Test->GetBlueprint(), Graph);
			TArray<TSharedPtr<FJsonValue>> CommentGuids;
			CommentGuids.Add(MakeShared<FJsonValueString>(Print->NodeGuid.ToString()));
			CommentArgs->SetArrayField(TEXT("nodeGuids"), CommentGuids);
			const FMcpToolCallResult Comment = N2CTest::RunTool(TEXT("create-comment-node"), CommentArgs);
			TestFalse(TEXT("create-comment-node: ") + N2CTest::ResultText(Comment).Left(200), Comment.bIsError);

			const TSharedPtr<FJsonObject> DeleteArgs = N2CTest::TargetArgs(Test->GetBlueprint(), Graph);
			DeleteArgs->SetArrayField(TEXT("nodeGuids"), CommentGuids);
			const FMcpToolCallResult Deleted = N2CTest::RunTool(TEXT("delete-blueprint-node"), DeleteArgs);
			TestFalse(TEXT("delete-blueprint-node: ") + N2CTest::ResultText(Deleted).Left(200), Deleted.bIsError);
			TestFalse(TEXT("the node is gone"), Graph->Nodes.Contains(Print));
		});

		It(TEXT("edit a function by the ID list-blueprint-functions returned before the function was renamed"), [this]()
		{
			const FString IdBefore = FN2CMcpFunctionGuidUtils::GetFunctionId(Function).ToString();
			FBlueprintEditorUtils::RenameGraph(Function, TEXT("N2CTargetRenamedForTools"));

			const TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
			AddArgs->SetStringField(TEXT("blueprintPath"), Path());
			AddArgs->SetStringField(TEXT("graphGuid"), IdBefore);
			AddArgs->SetStringField(TEXT("pinName"), TEXT("N2CInput"));
			AddArgs->SetStringField(TEXT("typeIdentifier"), TEXT("int"));
			const FMcpToolCallResult Added = N2CTest::RunTool(TEXT("add-function-input-pin"), AddArgs);
			TestFalse(TEXT("add-function-input-pin by the old ID: ") + N2CTest::ResultText(Added).Left(300), Added.bIsError);
			const TSharedPtr<FJsonObject> Target = N2CTest::ResultTarget(Added);
			TestTrue(TEXT("the target is the renamed function"), Target.IsValid()
				&& Target->GetStringField(TEXT("graphName")) == TEXT("N2CTargetRenamedForTools"));
			TestNotNull(TEXT("the input exists"), Test->FunctionEntry(Function)->FindPin(TEXT("N2CInput")));

			const TSharedPtr<FJsonObject> RemoveArgs = MakeShared<FJsonObject>();
			RemoveArgs->SetStringField(TEXT("blueprintPath"), Path());
			RemoveArgs->SetStringField(TEXT("graphGuid"), IdBefore);
			RemoveArgs->SetStringField(TEXT("pinName"), TEXT("N2CInput"));
			const FMcpToolCallResult Removed = N2CTest::RunTool(TEXT("remove-function-entry-pin"), RemoveArgs);
			TestFalse(TEXT("remove-function-entry-pin: ") + N2CTest::ResultText(Removed).Left(300), Removed.bIsError);

			const TSharedPtr<FJsonObject> ReturnArgs = MakeShared<FJsonObject>();
			ReturnArgs->SetStringField(TEXT("blueprintPath"), Path());
			ReturnArgs->SetStringField(TEXT("graphGuid"), IdBefore);
			ReturnArgs->SetStringField(TEXT("pinName"), TEXT("bN2COk"));
			ReturnArgs->SetStringField(TEXT("typeIdentifier"), TEXT("bool"));
			const FMcpToolCallResult Returned = N2CTest::RunTool(TEXT("add-function-return-pin"), ReturnArgs);
			TestFalse(TEXT("add-function-return-pin: ") + N2CTest::ResultText(Returned).Left(300), Returned.bIsError);
			const FMcpToolCallResult ReturnRemoved = N2CTest::RunTool(TEXT("remove-function-return-pin"), ReturnArgs);
			TestFalse(TEXT("remove-function-return-pin: ") + N2CTest::ResultText(ReturnRemoved).Left(300), ReturnRemoved.bIsError);

			const TSharedPtr<FJsonObject> DeleteArgs = MakeShared<FJsonObject>();
			DeleteArgs->SetStringField(TEXT("blueprintPath"), Path());
			DeleteArgs->SetStringField(TEXT("functionGuid"), IdBefore);
			const FMcpToolCallResult Deleted = N2CTest::RunTool(TEXT("delete-blueprint-function"), DeleteArgs);
			TestFalse(TEXT("delete-blueprint-function by the old ID: ") + N2CTest::ResultText(Deleted).Left(300), Deleted.bIsError);
			TestFalse(TEXT("the function is gone"), Test->GetBlueprint()->FunctionGraphs.Contains(Function));
			const TSharedPtr<FJsonObject> DeleteTarget = N2CTest::ResultTarget(Deleted);
			TestTrue(TEXT("delete echoes the deleted function"), DeleteTarget.IsValid()
				&& DeleteTarget->GetStringField(TEXT("graphGuid")) == IdBefore);
		});

		It(TEXT("refuse a write to a read-only graph with READ_ONLY_GRAPH and change nothing"), [this]()
		{
			UK2Node_CustomEvent* Event = Test->AddCustomEvent(Function, FName(TEXT("N2CReadOnlyEvent")));
			UK2Node_CallFunction* Print = Test->AddCall(Function, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
			if (!TestTrue(TEXT("nodes"), Event && Print))
			{
				return;
			}
			Function->bEditable = false;
			const TSharedPtr<FJsonObject> Args = N2CTest::TargetArgs(Test->GetBlueprint(), Function);
			UEdGraphPin* Then = Event->FindPin(UEdGraphSchema_K2::PN_Then, EGPD_Output);
			UEdGraphPin* Execute = Print->FindPin(UEdGraphSchema_K2::PN_Execute, EGPD_Input);
			AddConnection(Args, Then, Execute);
			const FMcpToolCallResult Connected = N2CTest::RunTool(TEXT("connect-pins"), Args);

			const TSharedPtr<FJsonObject> PinArgs = N2CTest::TargetArgs(Test->GetBlueprint(), Function);
			PinArgs->SetStringField(TEXT("pinName"), TEXT("N2CReadOnlyInput"));
			PinArgs->SetStringField(TEXT("typeIdentifier"), TEXT("int"));
			const FMcpToolCallResult PinAdded = N2CTest::RunTool(TEXT("add-function-input-pin"), PinArgs);
			Function->bEditable = true;

			TestTrue(TEXT("connect-pins is an error"), Connected.bIsError);
			TestTrue(TEXT("READ_ONLY_GRAPH: ") + N2CTest::ResultText(Connected).Left(200), N2CTest::ResultText(Connected).StartsWith(TEXT("READ_ONLY_GRAPH")));
			TestFalse(TEXT("no link made"), Then->LinkedTo.Contains(Execute));
			TestTrue(TEXT("add-function-input-pin READ_ONLY_GRAPH"), PinAdded.bIsError && N2CTest::ResultText(PinAdded).StartsWith(TEXT("READ_ONLY_GRAPH")));
			TestNull(TEXT("no pin added"), Test->FunctionEntry(Function)->FindPin(TEXT("N2CReadOnlyInput")));
		});

		It(TEXT("refuse add-function-return-pin on an event graph before adding a Return node"), [this]()
		{
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), Path());
			Args->SetStringField(TEXT("graphName"), TEXT("EventGraph"));
			Args->SetStringField(TEXT("pinName"), TEXT("bN2COk"));
			Args->SetStringField(TEXT("typeIdentifier"), TEXT("bool"));
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("add-function-return-pin"), Args);
			TestTrue(TEXT("NOT_A_FUNCTION_GRAPH: ") + N2CTest::ResultText(Result).Left(200),
				Result.bIsError && N2CTest::ResultText(Result).StartsWith(TEXT("NOT_A_FUNCTION_GRAPH")));
			TestNull(TEXT("no Return node in the event graph"), ResultNode(Test->EventGraph()));
			TestTrue(TEXT("the error says which graph"), N2CTest::ResultTarget(Result).IsValid());
		});

		It(TEXT("report a graph that does not resolve with the resolver's code"), [this]()
		{
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprintPath"), Path());
			Args->SetStringField(TEXT("graphName"), TEXT("NoSuchGraph"));
			TArray<TSharedPtr<FJsonValue>> Terms;
			Terms.Add(MakeShared<FJsonValueString>(TEXT("Print")));
			Args->SetArrayField(TEXT("searchTerms"), Terms);
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("find-nodes-in-graph"), Args);
			TestTrue(TEXT("GRAPH_NOT_FOUND: ") + N2CTest::ResultText(Result).Left(200),
				Result.bIsError && N2CTest::ResultText(Result).StartsWith(TEXT("GRAPH_NOT_FOUND")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
