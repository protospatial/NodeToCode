// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Tools/Implementations/Blueprint/Graph/N2CMcpSearchBlueprintNodesTool.h"
#include "MCP/Tools/Implementations/Blueprint/Graph/N2CMcpAddBlueprintNodeTool.h"
#include "MCP/Utils/N2CMcpNodeActions.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Core/N2CCapture.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CNodeActionIdsSpec
{
	/** One search result: its stable ID, menu name and node class from N2C JSON */
	struct FHit
	{
		FString Id;
		FString Name;
		FString MemberName;
	};

	TArray<FHit> HitsOf(const TArray<TSharedPtr<FJsonValue>>& Nodes)
	{
		TArray<FHit> Hits;
		for (const TSharedPtr<FJsonValue>& Value : Nodes)
		{
			const TSharedPtr<FJsonObject>* Node = nullptr;
			const TSharedPtr<FJsonObject>* Spawn = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Node) || !(*Node)->TryGetObjectField(TEXT("spawnMetadata"), Spawn))
			{
				continue;
			}
			FHit Hit;
			(*Spawn)->TryGetStringField(TEXT("actionIdentifier"), Hit.Id);
			(*Spawn)->TryGetStringField(TEXT("nodeName"), Hit.Name);
			(*Node)->TryGetStringField(TEXT("member_name"), Hit.MemberName);
			Hits.Add(Hit);
		}
		return Hits;
	}

	/** The context-sensitive search the MCP tool runs, for an explicit graph */
	TArray<FHit> Search(UBlueprint* Blueprint, UEdGraph* Graph, const FString& Term, int32 MaxResults = 100, bool bExcludeVM = true)
	{
		FN2CMcpSearchBlueprintNodesTool Tool;
		return HitsOf(Tool.SearchActions(Term, MaxResults, /*bContextSensitive*/ true, Blueprint, Graph, FString(), bExcludeVM));
	}

	TArray<FString> IdsOf(const TArray<FHit>& Hits)
	{
		TArray<FString> Ids;
		for (const FHit& Hit : Hits)
		{
			Ids.Add(Hit.Id);
		}
		return Ids;
	}

	/** The first control character (below 0x20, or DEL) in Text, or 0 */
	TCHAR FirstControlCharacter(const FString& Text)
	{
		for (const TCHAR Ch : Text)
		{
			if (Ch < 0x20 || Ch == 0x7F)
			{
				return Ch;
			}
		}
		return 0;
	}

	/** The first hit whose ID contains every one of Parts, or null */
	const FHit* FindHit(const TArray<FHit>& Hits, const TArray<FString>& Parts)
	{
		for (const FHit& Hit : Hits)
		{
			bool bAll = true;
			for (const FString& Part : Parts)
			{
				bAll &= Hit.Id.Contains(Part);
			}
			if (bAll)
			{
				return &Hit;
			}
		}
		return nullptr;
	}

	/** The pre-0277 identifier of the action Id names in Graph's list: full search text with '\n' as '>' ('\r' kept) */
	FString OldStyleIdentifier(UBlueprint* Blueprint, UEdGraph* Graph, const FString& Id)
	{
		FBlueprintActionMenuBuilder MenuBuilder;
		N2CNodeActions::BuildActionList(MenuBuilder, Blueprint, Graph);
		for (int32 Index = 0; Index < MenuBuilder.GetNumActions(); ++Index)
		{
			const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
			if (Action.IsValid() && N2CNodeActions::GetActionId(Action) == Id)
			{
				return Action->GetFullSearchText().Replace(TEXT("\n"), TEXT(">"));
			}
		}
		return FString();
	}

	/**
	 * True for the node the palette's "Greater ( > )" makes: with type promotion on (the default since UE 5.0) the
	 * promotable Greater operator, which becomes integer > integer when integers are wired in; without it, Greater_IntInt
	 */
	bool IsIntegerGreater(const UEdGraphNode* Node)
	{
		if (const UK2Node_PromotableOperator* Operator = Cast<UK2Node_PromotableOperator>(Node))
		{
			return Operator->GetOperationName() == FName(TEXT("Greater"));
		}
		const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
		return Call && Call->GetFunctionName() == FName(TEXT("Greater_IntInt"));
	}

	/** The ID parts of the "Greater ( > )" action (promotable operator, else the integer function) */
	const FHit* FindGreater(const TArray<FHit>& Hits)
	{
		const FHit* Hit = FindHit(Hits, { TEXT("K2Node_PromotableOperator"), TEXT(":Greater_") });
		return Hit ? Hit : FindHit(Hits, { TEXT(":Greater_IntInt") });
	}

	FMcpToolCallResult Call(const FString& Name, const TSharedPtr<FJsonObject>& Args)
	{
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(Name);
		return Tool.IsValid() ? Tool->Execute(Args)
			: FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("N2CTest: no registered tool '%s'"), *Name));
	}

	/** The nodes array of a search-blueprint-nodes result (or of the bridge's data object) */
	TArray<TSharedPtr<FJsonValue>> NodesOf(const TSharedPtr<FJsonObject>& Object)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(TEXT("nodes"), Nodes))
		{
			return *Nodes;
		}
		return {};
	}

	/** The node of Graph with this GUID (any format), or null */
	UEdGraphNode* NodeByGuid(UEdGraph* Graph, const FString& GuidText)
	{
		FGuid Guid;
		if (!FGuid::Parse(GuidText, Guid))
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == Guid)
			{
				return Node;
			}
		}
		return nullptr;
	}

	/** Every link in the graph as "node.pin->node.pin", sorted */
	TArray<FString> CollectLinks(const UEdGraph* Graph)
	{
		TArray<FString> Links;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			for (const UEdGraphPin* Pin : Node ? Node->Pins : TArray<UEdGraphPin*>())
			{
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					Links.Add(FString::Printf(TEXT("%s.%s->%s.%s"), *Node->GetName(), *Pin->PinName.ToString(),
						*Linked->GetOwningNode()->GetName(), *Linked->PinName.ToString()));
				}
			}
		}
		Links.Sort();
		return Links;
	}
}

/**
 * Stable action IDs for search-blueprint-nodes results and the search -> add round trip (card 0277). The cases without
 * an editor run on every engine against an explicit graph; the MCP and Python bridge cases act on the focused graph, so
 * they open a Blueprint editor and run on UE 5.8 only (5.6 and 5.7 under -nullrhi cannot close one, see
 * NodeToCode.Editor.Focus). NodeToCode.Robustness.SearchIsReadOnly covers the search's read-only contract on every
 * engine.
 */
BEGIN_DEFINE_SPEC(FN2CNodeActionIdsSpec, "NodeToCode.MCP.NodeActionIds", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	bool bEditorOpen = false;

	/** A /Game Actor Blueprint with an int member variable N2CTestHealth and BeginPlay wired to Print String */
	bool MakeBlueprint(const TCHAR* Name);
	/** Opens Test's Blueprint in its editor on the event graph (the focused graph the MCP tools act on) */
	bool OpenInEditor();
END_DEFINE_SPEC(FN2CNodeActionIdsSpec)

bool FN2CNodeActionIdsSpec::MakeBlueprint(const TCHAR* Name)
{
	Test = MakeUnique<N2CTest::FTestBlueprint>(Name, TEXT("/Game"));
	// FTestBlueprint packages are transient and MarkPackageDirty skips transient objects: clear the flag so the dirty
	// checks see what a saved Blueprint would (AfterEach puts it back)
	Test->GetPackage()->ClearFlags(RF_Transient);
	Test->AddMemberVariable(FName(TEXT("N2CTestHealth")), N2CTest::FTestBlueprint::IntPinType());
	// The editor refreshes a Blueprint's actions when its members change; do it here too, or the variable's Get and Set
	// are missing whenever the action database was built before this Blueprint existed (any earlier test searched)
	FBlueprintActionDatabase::Get().RefreshAssetActions(Test->GetBlueprint());
	// The engine's standard macros are listed once their library is loaded (the editor loads it at start; a headless
	// test run may not have)
	if (UBlueprint* Macros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")))
	{
		FBlueprintActionDatabase::Get().RefreshAssetActions(Macros);
	}
	UEdGraph* Graph = Test->EventGraph();
	UK2Node_Event* BeginPlay = Test->FindEvent(Graph, FName(TEXT("ReceiveBeginPlay")));
	UK2Node_CallFunction* Print = Test->AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
	if (BeginPlay)
	{
		BeginPlay->SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction*/ true);
		Test->Link(BeginPlay, UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
	}
	Test->GetPackage()->SetDirtyFlag(false);
	return TestNotNull(TEXT("Event BeginPlay"), BeginPlay)
		&& TestTrue(TEXT("test graph built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num() == 0);
}

bool FN2CNodeActionIdsSpec::OpenInEditor()
{
	TSharedPtr<IBlueprintEditor> Opened;
	FString OpenError;
	if (!TestTrue(TEXT("Blueprint editor opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Test->GetBlueprint(), Opened, OpenError)))
	{
		return false;
	}
	bEditorOpen = true;
	StaticCastSharedPtr<FBlueprintEditor>(Opened)->JumpToHyperlink(Test->EventGraph(), false);
	Test->GetPackage()->SetDirtyFlag(false);
	return true;
}

void FN2CNodeActionIdsSpec::Define()
{
	using namespace N2CNodeActionIdsSpec;

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
		bEditorOpen = false;
		Test.Reset();
	});

	Describe(TEXT("IDs"), [this]()
	{
		It(TEXT("are spawner signatures without control characters, the same in two searches and after a capture and a compile"), [this]()
		{
			if (!MakeBlueprint(TEXT("BP_N2CActionIdsStable")))
			{
				return;
			}
			UBlueprint* Blueprint = Test->GetBlueprint();
			UEdGraph* Graph = Test->EventGraph();
			const TCHAR* Terms[] = { TEXT("print string"), TEXT("branch"), TEXT("greater"), TEXT("do once"),
				TEXT("n2ctesthealth"), TEXT("destroyed") };

			TMap<FString, TArray<FString>> First;
			for (const TCHAR* Term : Terms)
			{
				const TArray<FHit> Hits = Search(Blueprint, Graph, Term);
				TestTrue(FString::Printf(TEXT("'%s' finds results"), Term), Hits.Num() > 0);
				for (const FHit& Hit : Hits)
				{
					const TCHAR Bad = FirstControlCharacter(Hit.Id);
					TestEqual(FString::Printf(TEXT("'%s' ID %s has no control character"), Term, *Hit.Id.Left(120)), static_cast<int32>(Bad), 0);
					TestTrue(FString::Printf(TEXT("'%s' ID %s is a spawner signature"), Term, *Hit.Id.Left(120)),
						Hit.Id.StartsWith(TEXT("(")) && Hit.Id.Contains(TEXT("NodeName=")));
				}
				First.Add(Term, IdsOf(Hits));
				TestEqual(FString::Printf(TEXT("'%s' gives the same IDs in a second search"), Term), IdsOf(Search(Blueprint, Graph, Term)), First[Term]);
			}

			// A capture and a compile (the Blueprint's own variable's spawners rebuild with the class) change no ID
			TestFalse(TEXT("capture succeeds"), N2C::CaptureGraph(*Graph).HasError());
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			for (const TCHAR* Term : Terms)
			{
				TestEqual(FString::Printf(TEXT("'%s' gives the same IDs after a capture and a compile"), Term), IdsOf(Search(Blueprint, Graph, Term)), First[Term]);
			}

			// The well-known shape (the engine's own signature format)
			const TArray<FHit> PrintHits = Search(Blueprint, Graph, TEXT("print string"));
			const FHit* Print = FindHit(PrintHits, { TEXT("KismetSystemLibrary:PrintString") });
			if (TestNotNull(TEXT("Print String result"), Print))
			{
				TestEqual(TEXT("Print String ID"), Print->Id,
					FString(TEXT("(FieldName=\"/Script/Engine.KismetSystemLibrary:PrintString\",NodeName=\"/Script/BlueprintGraph.K2Node_CallFunction\")")));
			}
		});

		It(TEXT("are refused when unknown, and pre-0277 identifiers (with or without carriage returns) still resolve"), [this]()
		{
			if (!MakeBlueprint(TEXT("BP_N2CActionIdsLegacy")))
			{
				return;
			}
			UBlueprint* Blueprint = Test->GetBlueprint();
			UEdGraph* Graph = Test->EventGraph();
			FN2CMcpAddBlueprintNodeTool Add;

			UEdGraphNode* Spawned = nullptr;
			FString Error;
			TestFalse(TEXT("an unknown ID is refused"), Add.FindAndSpawnNode(TEXT("Nope"),
				TEXT("(NodeName=\"/Script/BlueprintGraph.K2Node_N2CNoSuchNode\")"), Blueprint, Graph, FVector2D::ZeroVector, Spawned, Error));
			TestTrue(TEXT("ACTION_NOT_FOUND: ") + Error, Error.StartsWith(TEXT("ACTION_NOT_FOUND")));

			const TArray<FHit> PrintHits = Search(Blueprint, Graph, TEXT("print string"));
			const FHit* Print = FindHit(PrintHits, { TEXT("KismetSystemLibrary:PrintString") });
			if (!TestNotNull(TEXT("Print String result"), Print))
			{
				return;
			}
			const FString OldId = OldStyleIdentifier(Blueprint, Graph, Print->Id);
			TestFalse(TEXT("old-style identifier found"), OldId.IsEmpty());
			for (const FString& Legacy : { OldId, OldId.Replace(TEXT("\r"), TEXT("")) })
			{
				Spawned = nullptr;
				Error.Reset();
				const bool bAdded = Add.FindAndSpawnNode(TEXT("Print String"), Legacy, Blueprint, Graph, FVector2D(0, 400), Spawned, Error);
				TestTrue(TEXT("old-style identifier adds Print String: ") + Error, bAdded);
				const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Spawned);
				TestTrue(TEXT("it is Print String"), Call && Call->GetFunctionName() == GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
			}

			// Before card 0277 an identifier whose text holds '>' (Greater ( > )) could not be added at all
			const TArray<FHit> GreaterHits = Search(Blueprint, Graph, TEXT("greater"));
			const FHit* Greater = FindGreater(GreaterHits);
			if (TestNotNull(TEXT("Greater result"), Greater))
			{
				const FString OldGreater = OldStyleIdentifier(Blueprint, Graph, Greater->Id);
				Spawned = nullptr;
				Error.Reset();
				TestTrue(TEXT("old-style Greater identifier adds the node: ") + Error,
					Add.FindAndSpawnNode(Greater->Name, OldGreater, Blueprint, Graph, FVector2D(0, 800), Spawned, Error));
				TestTrue(TEXT("it is Greater"), IsIntegerGreater(Spawned));
			}
		});

		It(TEXT("name one action each: no two spawners in an Actor event graph's list share an ID"), [this]()
		{
			if (!MakeBlueprint(TEXT("BP_N2CActionIdsUnique")))
			{
				return;
			}
			FBlueprintActionMenuBuilder MenuBuilder;
			N2CNodeActions::BuildActionList(MenuBuilder, Test->GetBlueprint(), Test->EventGraph());
			TMap<FString, TSet<const UBlueprintNodeSpawner*>> SpawnersById;
			TMap<FString, TArray<FString>> NamesById;
			for (int32 Index = 0; Index < MenuBuilder.GetNumActions(); ++Index)
			{
				const TSharedPtr<FEdGraphSchemaAction>& Action = MenuBuilder.GetSchemaAction(Index);
				if (!Action.IsValid())
				{
					continue;
				}
				const FString Id = N2CNodeActions::GetActionId(Action);
				SpawnersById.FindOrAdd(Id).Add(N2CNodeActions::GetSpawner(*Action));
				NamesById.FindOrAdd(Id).AddUnique(Action->GetMenuDescription().ToString());
			}
			int32 Shared = 0;
			for (const TPair<FString, TSet<const UBlueprintNodeSpawner*>>& Pair : SpawnersById)
			{
				if (Pair.Value.Num() > 1)
				{
					++Shared;
					AddInfo(FString::Printf(TEXT("shared ID %s: %d spawners (%s)"), *Pair.Key, Pair.Value.Num(), *FString::Join(NamesById[Pair.Key], TEXT(", "))));
				}
			}
			AddInfo(FString::Printf(TEXT("%d actions, %d IDs, %d shared"), MenuBuilder.GetNumActions(), SpawnersById.Num(), Shared));
			TestEqual(TEXT("IDs shared by different spawners"), Shared, 0);
		});
	});

	Describe(TEXT("search -> add"), [this]()
	{
		It(TEXT("adds a function call, variable get and set, flow control, an event, a macro and integer > integer from the returned IDs"), [this]()
		{
			if (!MakeBlueprint(TEXT("BP_N2CActionIdsRoundTrip")))
			{
				return;
			}
			UBlueprint* Blueprint = Test->GetBlueprint();
			UEdGraph* Graph = Test->EventGraph();
			FN2CMcpAddBlueprintNodeTool Add;

			struct FCase
			{
				const TCHAR* Label;
				const TCHAR* Term;
				TArray<FString> IdParts;
				TFunction<bool(const UEdGraphNode*)> IsExpected;
			};
			const TArray<FCase> Cases = {
				{ TEXT("function call"), TEXT("print string"), { TEXT("KismetSystemLibrary:PrintString") },
					[](const UEdGraphNode* Node) { const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node); return Call && Call->GetFunctionName() == GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString); } },
				{ TEXT("variable get"), TEXT("n2ctesthealth"), { TEXT("K2Node_VariableGet"), TEXT("N2CTestHealth") },
					[](const UEdGraphNode* Node) { const UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node); return Get && Get->GetVarName() == FName(TEXT("N2CTestHealth")); } },
				{ TEXT("variable set"), TEXT("n2ctesthealth"), { TEXT("K2Node_VariableSet"), TEXT("N2CTestHealth") },
					[](const UEdGraphNode* Node) { const UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node); return Set && Set->GetVarName() == FName(TEXT("N2CTestHealth")); } },
				{ TEXT("flow control"), TEXT("branch"), { TEXT("K2Node_IfThenElse") },
					[](const UEdGraphNode* Node) { return Node && Node->IsA<UK2Node_IfThenElse>(); } },
				{ TEXT("event"), TEXT("destroyed"), { TEXT("K2Node_Event"), TEXT("ReceiveDestroyed") },
					[](const UEdGraphNode* Node) { const UK2Node_Event* Event = Cast<UK2Node_Event>(Node); return Event && Event->EventReference.GetMemberName() == FName(TEXT("ReceiveDestroyed")); } },
				{ TEXT("macro"), TEXT("do once"), { TEXT("K2Node_MacroInstance"), TEXT("DoOnce") },
					[](const UEdGraphNode* Node) { const UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node); return Macro && Macro->GetMacroGraph() && Macro->GetMacroGraph()->GetFName() == FName(TEXT("DoOnce")); } },
				{ TEXT("integer > integer"), TEXT("greater"), { TEXT(":Greater_") }, &IsIntegerGreater },
			};

			int32 Y = 300;
			for (const FCase& Case : Cases)
			{
				const TArray<FHit> Hits = Search(Blueprint, Graph, Case.Term);
				const FHit* Hit = FCString::Strcmp(Case.Term, TEXT("greater")) == 0 ? FindGreater(Hits) : FindHit(Hits, Case.IdParts);
				if (!TestNotNull(FString::Printf(TEXT("%s: a '%s' result (%d results)"), Case.Label, Case.Term, Hits.Num()), Hit))
				{
					for (const FHit& Each : Hits)
					{
						AddInfo(FString::Printf(TEXT("  '%s': %s %s"), Case.Term, *Each.Name, *Each.Id));
					}
					continue;
				}
				const int32 NodesBefore = Graph->Nodes.Num();
				UEdGraphNode* Spawned = nullptr;
				FString Error;
				const bool bAdded = Add.FindAndSpawnNode(Hit->Name, Hit->Id, Blueprint, Graph, FVector2D(600, Y), Spawned, Error);
				Y += 200;
				TestTrue(FString::Printf(TEXT("%s: added from %s: %s"), Case.Label, *Hit->Id, *Error), bAdded);
				TestTrue(FString::Printf(TEXT("%s: the node is the expected kind (%s)"), Case.Label, Spawned ? *Spawned->GetClass()->GetName() : TEXT("null")), Case.IsExpected(Spawned));
				TestTrue(FString::Printf(TEXT("%s: the node is in the graph"), Case.Label), Spawned && Graph->Nodes.Contains(Spawned));
				TestEqual(FString::Printf(TEXT("%s: one node more"), Case.Label), Graph->Nodes.Num(), NodesBefore + 1);
			}
		});
	});

	Describe(TEXT("blueprintContext"), [this]()
	{
		It(TEXT("names an unknown graph GRAPH_NOT_FOUND instead of searching elsewhere"), [this]()
		{
			if (!MakeBlueprint(TEXT("BP_N2CActionIdsContext")))
			{
				return;
			}
			const FString BlueprintPath = Test->GetBlueprint()->GetPathName();

			const TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
			Context->SetStringField(TEXT("graphPath"), BlueprintPath + TEXT(":N2CNoSuchGraph"));
			UBlueprint* Blueprint = nullptr;
			UEdGraph* Graph = nullptr;
			FString Error;
			TestFalse(TEXT("unknown graph is refused"), FN2CMcpSearchBlueprintNodesTool::GetContextFromPaths(Context, Blueprint, Graph, Error));
			TestTrue(TEXT("GRAPH_NOT_FOUND: ") + Error, Error.StartsWith(TEXT("GRAPH_NOT_FOUND")));

			// Through the tool: an error result, not results labelled context-sensitive
			const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("searchTerm"), TEXT("print string"));
			Args->SetObjectField(TEXT("blueprintContext"), Context);
			const FMcpToolCallResult Result = Call(TEXT("search-blueprint-nodes"), Args);
			TestTrue(TEXT("search-blueprint-nodes fails"), Result.bIsError);
			TestTrue(TEXT("with GRAPH_NOT_FOUND: ") + N2CTest::ResultText(Result).Left(200), N2CTest::ResultText(Result).Contains(TEXT("GRAPH_NOT_FOUND")));

			// The event graph by name resolves, and so does the Blueprint alone (its event graph)
			Context->SetStringField(TEXT("graphPath"), BlueprintPath + TEXT(":") + Test->EventGraph()->GetName());
			Error.Reset();
			TestTrue(TEXT("event graph resolves: ") + Error, FN2CMcpSearchBlueprintNodesTool::GetContextFromPaths(Context, Blueprint, Graph, Error));
			TestTrue(TEXT("to the event graph"), Graph == Test->EventGraph());
			const TSharedPtr<FJsonObject> BlueprintOnly = MakeShared<FJsonObject>();
			BlueprintOnly->SetStringField(TEXT("owningBlueprintPath"), BlueprintPath);
			Error.Reset();
			TestTrue(TEXT("Blueprint alone resolves: ") + Error, FN2CMcpSearchBlueprintNodesTool::GetContextFromPaths(BlueprintOnly, Blueprint, Graph, Error));
			TestTrue(TEXT("to its event graph"), Graph == Test->EventGraph());
		});
	});

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
	Describe(TEXT("through MCP and the Python bridge"), [this]()
	{
		It(TEXT("runs on UE 5.8 only (a headless Blueprint editor cannot be closed below 5.8)"), [this]()
		{
			AddInfo(TEXT("Skipped below UE 5.8; the search -> add cases above cover the shared resolution without an editor"));
		});
	});
#else
	Describe(TEXT("through MCP and the Python bridge"), [this]()
	{
		It(TEXT("search-blueprint-nodes leaves nodes, links, timelines, components, the undo buffer and the dirty flag unchanged"), [this]()
		{
			if (!MakeBlueprint(TEXT("BP_N2CActionIdsReadOnly")) || !OpenInEditor())
			{
				return;
			}
			UBlueprint* Blueprint = Test->GetBlueprint();
			UEdGraph* Graph = Test->EventGraph();
			const TArray<UEdGraphNode*> NodesBefore = Graph->Nodes;
			const TArray<FString> LinksBefore = CollectLinks(Graph);
			const int32 TimelinesBefore = Blueprint->Timelines.Num();
			const int32 ComponentsBefore = Blueprint->ComponentTemplates.Num();
			const int32 QueueBefore = GEditor->Trans->GetQueueLength();
			for (const TCHAR* Term : { TEXT("begin play"), TEXT("timeline"), TEXT("add static mesh component") })
			{
				const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
				Args->SetStringField(TEXT("searchTerm"), Term);
				const FMcpToolCallResult Result = Call(TEXT("search-blueprint-nodes"), Args);
				TestFalse(FString::Printf(TEXT("MCP '%s' succeeds"), Term), Result.bIsError);
				TestTrue(FString::Printf(TEXT("MCP '%s' finds results"), Term), NodesOf(N2CTest::ParseJsonObject(N2CTest::ResultText(Result))).Num() > 0);

				const TSharedPtr<FJsonObject> Bridge = N2CTest::ParseJsonObject(UN2CPythonBridge::SearchBlueprintNodes(Term, true, 20, FString(), true));
				const TSharedPtr<FJsonObject>* Data = nullptr;
				TestTrue(FString::Printf(TEXT("bridge '%s' finds results"), Term), Bridge.IsValid() && Bridge->TryGetObjectField(TEXT("data"), Data) && NodesOf(*Data).Num() > 0);

				TestTrue(FString::Printf(TEXT("'%s': same nodes"), Term), Graph->Nodes == NodesBefore);
				TestTrue(FString::Printf(TEXT("'%s': links unchanged"), Term), CollectLinks(Graph) == LinksBefore);
				TestEqual(FString::Printf(TEXT("'%s': Timelines unchanged"), Term), Blueprint->Timelines.Num(), TimelinesBefore);
				TestEqual(FString::Printf(TEXT("'%s': ComponentTemplates unchanged"), Term), Blueprint->ComponentTemplates.Num(), ComponentsBefore);
				TestEqual(FString::Printf(TEXT("'%s': undo buffer unchanged"), Term), GEditor->Trans->GetQueueLength(), QueueBefore);
				TestFalse(FString::Printf(TEXT("'%s': package not dirty"), Term), Test->GetPackage()->IsDirty());
			}
		});

		It(TEXT("'greater' then add-bp-node-to-active-graph with the returned ID makes integer > integer, through MCP and the bridge"), [this]()
		{
			if (!MakeBlueprint(TEXT("BP_N2CActionIdsGreater")) || !OpenInEditor())
			{
				return;
			}
			UEdGraph* Graph = Test->EventGraph();

			// MCP
			const TSharedPtr<FJsonObject> SearchArgs = MakeShared<FJsonObject>();
			SearchArgs->SetStringField(TEXT("searchTerm"), TEXT("greater"));
			SearchArgs->SetNumberField(TEXT("maxResults"), 100);
			const FMcpToolCallResult Searched = Call(TEXT("search-blueprint-nodes"), SearchArgs);
			const TArray<FHit> Hits = HitsOf(NodesOf(N2CTest::ParseJsonObject(N2CTest::ResultText(Searched))));
			const FHit* Hit = FindGreater(Hits);
			if (TestNotNull(FString::Printf(TEXT("MCP search finds integer > integer (%d results)"), Hits.Num()), Hit))
			{
				TestEqual(TEXT("MCP ID has no control character"), static_cast<int32>(FirstControlCharacter(Hit->Id)), 0);
				const TSharedPtr<FJsonObject> AddArgs = MakeShared<FJsonObject>();
				AddArgs->SetStringField(TEXT("nodeName"), Hit->Name);
				AddArgs->SetStringField(TEXT("actionIdentifier"), Hit->Id);
				const FMcpToolCallResult Added = Call(TEXT("add-bp-node-to-active-graph"), AddArgs);
				TestFalse(TEXT("MCP add succeeds: ") + N2CTest::ResultText(Added).Left(300), Added.bIsError);
				const TSharedPtr<FJsonObject> AddedJson = N2CTest::ParseJsonObject(N2CTest::ResultText(Added));
				const UEdGraphNode* Node = AddedJson.IsValid() ? NodeByGuid(Graph, AddedJson->GetStringField(TEXT("nodeGuid"))) : nullptr;
				TestTrue(TEXT("MCP made an integer > integer node"), IsIntegerGreater(Node));
			}

			// Python bridge
			const TSharedPtr<FJsonObject> Bridge = N2CTest::ParseJsonObject(UN2CPythonBridge::SearchBlueprintNodes(TEXT("greater"), true, 100, FString(), true));
			const TSharedPtr<FJsonObject>* Data = nullptr;
			const TArray<FHit> BridgeHits = Bridge.IsValid() && Bridge->TryGetObjectField(TEXT("data"), Data) ? HitsOf(NodesOf(*Data)) : TArray<FHit>();
			const FHit* BridgeHit = FindGreater(BridgeHits);
			if (TestNotNull(FString::Printf(TEXT("bridge search finds integer > integer (%d results)"), BridgeHits.Num()), BridgeHit))
			{
				TestEqual(TEXT("bridge and MCP give the same ID"), BridgeHit->Id, Hit ? Hit->Id : FString());
				const TSharedPtr<FJsonObject> Added = N2CTest::ParseJsonObject(UN2CPythonBridge::AddNodeToGraph(BridgeHit->Name, BridgeHit->Id, 0.f, 600.f));
				const TSharedPtr<FJsonObject>* AddedData = nullptr;
				TestTrue(TEXT("bridge add succeeds"), Added.IsValid() && Added->GetBoolField(TEXT("success")) && Added->TryGetObjectField(TEXT("data"), AddedData));
				const UEdGraphNode* Node = AddedData ? NodeByGuid(Graph, (*AddedData)->GetStringField(TEXT("nodeGuid"))) : nullptr;
				TestTrue(TEXT("bridge made an integer > integer node"), IsIntegerGreater(Node));
			}

			// The bridge's own fields: menu name and category, not the search blob
			if (BridgeHits.Num() > 0 && Data)
			{
				const TSharedPtr<FJsonObject> First = NodesOf(*Data)[0]->AsObject();
				for (const TCHAR* Field : { TEXT("menuName"), TEXT("category"), TEXT("tooltip"), TEXT("keywords") })
				{
					TestTrue(FString::Printf(TEXT("bridge result has %s"), Field), First.IsValid() && First->HasField(Field));
				}
				TestEqual(TEXT("bridge name is the menu name"), First->GetStringField(TEXT("name")), First->GetStringField(TEXT("menuName")));
			}
		});
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
