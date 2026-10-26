// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CCapture.h"
#include "Core/N2CEditorIntegration.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/Implementations/Blueprint/Analysis/N2CMcpGetFocusedBlueprintTool.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CBlueprintByPathSpec
{
	/** The K2 nodes of a graph: what a capture describes (knots aside, which these graphs do not use) */
	int32 K2NodeCount(const UEdGraph* Graph)
	{
		int32 Count = 0;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			Count += Cast<UK2Node>(Node) != nullptr ? 1 : 0;
		}
		return Count;
	}

	FString Condensed(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		if (Object.IsValid())
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}
		return Text;
	}

	/** The graph objects of an N2C JSON document, by name */
	TMap<FString, TSharedPtr<FJsonObject>> GraphsByName(const TSharedPtr<FJsonObject>& Document)
	{
		TMap<FString, TSharedPtr<FJsonObject>> Graphs;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Document.IsValid() && Document->TryGetArrayField(TEXT("graphs"), Array))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Array)
			{
				const TSharedPtr<FJsonObject> Graph = Value->AsObject();
				FString Name;
				if (Graph.IsValid() && Graph->TryGetStringField(TEXT("name"), Name))
				{
					Graphs.Add(Name, Graph);
				}
			}
		}
		return Graphs;
	}

	int32 NodesIn(const TSharedPtr<FJsonObject>& Graph)
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		return Graph.IsValid() && Graph->TryGetArrayField(TEXT("nodes"), Nodes) ? Nodes->Num() : -1;
	}

	/** A delegate signature graph, as My Blueprint > Add Event Dispatcher makes one (without the variable) */
	UEdGraph* AddDelegateSignature(UBlueprint* Blueprint, const FName Name)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		Graph->bEditable = false;
		Schema->CreateDefaultNodesForGraph(*Graph);
		Schema->CreateFunctionGraphTerminators(*Graph, static_cast<UClass*>(nullptr));
		Schema->AddExtraFunctionFlags(Graph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
		Schema->MarkFunctionEntryAsEditable(Graph, true);
		Blueprint->DelegateSignatureGraphs.Add(Graph);
		return Graph;
	}

	/** A second event graph page with no nodes at all */
	UEdGraph* AddEmptyEventGraph(UBlueprint* Blueprint, const FName Name)
	{
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		Blueprint->UbergraphPages.Add(Graph);
		return Graph;
	}
}

/**
 * get-blueprint-n2cjson and list-blueprint-graphs (card 0157, review rec-agentic-landscape-2): any Blueprint read by
 * asset path with no Blueprint editor open, on Blueprints built in code. Every graph kind is listed and read by name
 * and GUID, every graph at once, errors carry codes, results are marked untrusted, a document over the token limit
 * goes to a file, the Python bridge reads the same, and the output is get-focused-blueprint's for the same graph.
 * find-nodes-in-graph, which now serializes its capture once (imp-mcp-tools-graph-functions-12), returns the nodes of
 * that same document.
 */
BEGIN_DEFINE_SPEC(FN2CBlueprintByPathSpec, "NodeToCode.MCP.BlueprintByPath", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FTestBlueprint> Test;
	UEdGraph* Function = nullptr;
	UEdGraph* Macro = nullptr;
	UEdGraph* Delegate = nullptr;
	UEdGraph* Empty = nullptr;
	UK2Node_Composite* Collapsed = nullptr;

	FString Path() const { return Test->GetBlueprint()->GetPathName(); }

	TSharedPtr<FJsonObject> Args(const FString& BlueprintPath, const FString& GraphName = FString(), const FString& GraphGuid = FString())
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("blueprintPath"), BlueprintPath);
		if (!GraphName.IsEmpty())
		{
			Json->SetStringField(TEXT("graphName"), GraphName);
		}
		if (!GraphGuid.IsEmpty())
		{
			Json->SetStringField(TEXT("graphGuid"), GraphGuid);
		}
		return Json;
	}

	/** A successful result's text, or empty (with a test error) */
	FString SuccessText(const FString& What, const FMcpToolCallResult& Result)
	{
		const FString Text = N2CTest::ResultText(Result);
		TestFalse(FString::Printf(TEXT("%s succeeds (%s)"), *What, *Text.Left(300)), Result.bIsError);
		return Result.bIsError ? FString() : Text;
	}

	/** An error result's text starts with Code */
	void ExpectError(const FString& What, const FMcpToolCallResult& Result, const FString& Code)
	{
		const FString Text = N2CTest::ResultText(Result);
		TestTrue(FString::Printf(TEXT("%s fails"), *What), Result.bIsError);
		TestTrue(FString::Printf(TEXT("%s: error starts with %s (got: %s)"), *What, *Code, *Text.Left(300)), Text.StartsWith(Code));
	}

	/** How many graphs of the test Blueprint hold a node (the five above and the Actor's UserConstructionScript) */
	int32 GraphsWithNodesCount() const
	{
		int32 Count = 0;
		for (const UEdGraph* Graph : N2CMcpBlueprintExport::GraphsToCapture(*Test->GetBlueprint()))
		{
			Count += N2CBlueprintByPathSpec::K2NodeCount(Graph) > 0 ? 1 : 0;
		}
		return Count;
	}

	/** Every graph of the test Blueprint that holds nodes, with its kind */
	TArray<TPair<UEdGraph*, FString>> GraphsWithNodes() const
	{
		return {
			{ Test->EventGraph(), TEXT("event") }, { Function, TEXT("function") }, { Macro, TEXT("macro") },
			{ Collapsed->BoundGraph.Get(), TEXT("collapsed") }, { Delegate, TEXT("delegate") } };
	}
END_DEFINE_SPEC(FN2CBlueprintByPathSpec)

void FN2CBlueprintByPathSpec::Define()
{
	using namespace N2CBlueprintByPathSpec;

	BeforeEach([this]()
	{
		// Under /Game: capture follows only user graphs there, and open-blueprint-asset accepts the path
		Test = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CByPath"), TEXT("/Game"));
		Function = Test->AddFunction(FName(TEXT("N2CByPathFunction")));
		Macro = Test->AddMacro(FName(TEXT("N2CByPathMacro")));
		Collapsed = Test->AddCollapsedGraph(Test->EventGraph(), FName(TEXT("N2CByPathCollapsed")));
		Delegate = AddDelegateSignature(Test->GetBlueprint(), FName(TEXT("N2CByPathDispatcher")));
		Empty = AddEmptyEventGraph(Test->GetBlueprint(), FName(TEXT("N2CByPathEmpty")));

		// The function prints a string; a custom event in the event graph calls the function
		UK2Node_CallFunction* Print = Test->AddCall(Function, UKismetSystemLibrary::StaticClass(), FName(TEXT("PrintString")));
		Test->Link(Test->FunctionEntry(Function), UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
		UK2Node_CustomEvent* Event = Test->AddCustomEvent(Test->EventGraph(), FName(TEXT("N2CByPathEvent")));
		UK2Node_CallFunction* CallFunction = Test->AddSelfCall(Test->EventGraph(), FName(TEXT("N2CByPathFunction")));
		Test->Link(Event, UEdGraphSchema_K2::PN_Then, CallFunction, UEdGraphSchema_K2::PN_Execute);

		TestTrue(TEXT("test graphs built: ") + FString::Join(Test->Errors(), TEXT("; ")),
			Test->Errors().Num() == 0 && Function && Macro && Collapsed && Delegate && Empty && Print && Event && CallFunction);
	});

	AfterEach([this]()
	{
		Function = Macro = Delegate = Empty = nullptr;
		Collapsed = nullptr;
		Test.Reset();
	});

	It(TEXT("works on a Blueprint no editor has open"), [this]()
	{
		TestFalse(TEXT("no Blueprint editor edits the test Blueprint"), FN2CBlueprintEditorTracker::GetEditorFor(Test->GetBlueprint()).IsValid());
		const UEdGraph* Focused = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
		TestTrue(TEXT("no graph of the test Blueprint is focused"), !Focused || FBlueprintEditorUtils::FindBlueprintForGraph(Focused) != Test->GetBlueprint());
	});

	Describe(TEXT("list-blueprint-graphs"), [this]()
	{
		It(TEXT("lists every graph with its GraphGuid, kind, node count and read-only flag"), [this]()
		{
			const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(SuccessText(TEXT("list"), N2CTest::RunToolUnmarked(TEXT("list-blueprint-graphs"), Args(Path()))));
			if (!TestTrue(TEXT("a JSON object"), Json.IsValid()))
			{
				return;
			}
			TestEqual(TEXT("blueprintPath"), Json->GetStringField(TEXT("blueprintPath")), Path());
			TestEqual(TEXT("blueprintName"), Json->GetStringField(TEXT("blueprintName")), FString(TEXT("BP_N2CByPath")));

			TMap<FString, TSharedPtr<FJsonObject>> Listed;
			for (const TSharedPtr<FJsonValue>& Value : Json->GetArrayField(TEXT("graphs")))
			{
				Listed.Add(Value->AsObject()->GetStringField(TEXT("name")), Value->AsObject());
			}
			TArray<UEdGraph*> All;
			Test->GetBlueprint()->GetAllGraphs(All);
			TestEqual(TEXT("one entry per graph of GetAllGraphs"), Listed.Num(), All.Num());
			TestEqual(TEXT("graphCount"), static_cast<int32>(Json->GetNumberField(TEXT("graphCount"))), All.Num());

			TArray<TPair<UEdGraph*, FString>> Expected = GraphsWithNodes();
			Expected.Add({ Empty, TEXT("event") });
			for (const TPair<UEdGraph*, FString>& Graph : Expected)
			{
				const TSharedPtr<FJsonObject>* Entry = Listed.Find(Graph.Key->GetName());
				if (!TestNotNull(FString::Printf(TEXT("%s is listed"), *Graph.Key->GetName()), Entry))
				{
					continue;
				}
				const FString Name = Graph.Key->GetName();
				TestEqual(Name + TEXT(" graphGuid"), (*Entry)->GetStringField(TEXT("graphGuid")), Graph.Key->GraphGuid.ToString());
				TestEqual(Name + TEXT(" kind"), (*Entry)->GetStringField(TEXT("kind")), Graph.Value);
				TestEqual(Name + TEXT(" nodeCount"), static_cast<int32>((*Entry)->GetNumberField(TEXT("nodeCount"))), Graph.Key->Nodes.Num());
				TestEqual(Name + TEXT(" readOnly"), (*Entry)->GetBoolField(TEXT("readOnly")), Graph.Key == Delegate);
			}
			const TSharedPtr<FJsonObject>* CollapsedEntry = Listed.Find(Collapsed->BoundGraph->GetName());
			TestTrue(TEXT("the collapsed graph names its parent graph"), CollapsedEntry
				&& (*CollapsedEntry)->GetStringField(TEXT("parentGraph")) == Test->EventGraph()->GetName());
			TestEqual(TEXT("the empty page has no nodes"), Empty->Nodes.Num(), 0);
		});

		It(TEXT("refuses a missing or unknown blueprintPath with a code"), [this]()
		{
			ExpectError(TEXT("no path"), N2CTest::RunToolUnmarked(TEXT("list-blueprint-graphs"), MakeShared<FJsonObject>()), TEXT("INVALID_ARGUMENT"));
			ExpectError(TEXT("unknown path"), N2CTest::RunToolUnmarked(TEXT("list-blueprint-graphs"), Args(TEXT("/Game/N2CTest/NoSuch/BP_Nope"))), TEXT("ASSET_NOT_FOUND"));
		});
	});

	Describe(TEXT("get-blueprint-n2cjson"), [this]()
	{
		It(TEXT("reads each graph kind by name and by GUID, with the nodes the graph holds"), [this]()
		{
			for (const TPair<UEdGraph*, FString>& Graph : GraphsWithNodes())
			{
				const FString Name = Graph.Key->GetName();
				const TSharedPtr<FJsonObject> ByGuidArgs = Args(Path(), FString(), Graph.Key->GraphGuid.ToString());
				ByGuidArgs->SetNumberField(TEXT("depth"), 0);
				const FString ByGuid = SuccessText(Name + TEXT(" by GUID"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), ByGuidArgs));
				const TSharedPtr<FJsonObject> ByNameArgs = Args(Path(), Name);
				ByNameArgs->SetNumberField(TEXT("depth"), 0);
				const FString ByName = SuccessText(Name + TEXT(" by name"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), ByNameArgs));
				TestEqual(Name + TEXT(": by name and by GUID read the same document"), ByName, ByGuid);

				const TSharedPtr<FJsonObject> Document = N2CTest::ParseJsonObject(ByGuid);
				if (!TestTrue(Name + TEXT(" parses"), Document.IsValid()))
				{
					continue;
				}
				TestEqual(Name + TEXT(" N2C JSON version"), Document->GetStringField(TEXT("version")), FString(TEXT("2.0.0")));
				const TMap<FString, TSharedPtr<FJsonObject>> Graphs = GraphsByName(Document);
				// At depth 0 the graph alone, with the collapsed graphs it holds (always part of it, card 0235)
				int32 CollapsedGraphs = 0;
				for (const UEdGraphNode* Node : Graph.Key->Nodes)
				{
					CollapsedGraphs += Cast<UK2Node_Composite>(Node) ? 1 : 0;
				}
				TestEqual(Name + TEXT(": one graph and its collapsed graphs at depth 0"), Graphs.Num(), 1 + CollapsedGraphs);
				TestEqual(Name + TEXT(": node count"), NodesIn(Graphs.FindRef(Name)), K2NodeCount(Graph.Key));
				TestTrue(Name + TEXT(": has the guids table"), Document->HasTypedField<EJson::Object>(TEXT("guids")));
			}
		});

		It(TEXT("reads every graph that holds nodes into one document when no graph is named"), [this]()
		{
			const FString Text = SuccessText(TEXT("every graph"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path())));
			const TSharedPtr<FJsonObject> Document = N2CTest::ParseJsonObject(Text);
			const TArray<TSharedPtr<FJsonValue>>* GraphArray = nullptr;
			if (!TestTrue(TEXT("parses, with graphs"), Document.IsValid() && Document->TryGetArrayField(TEXT("graphs"), GraphArray)))
			{
				return;
			}
			const TMap<FString, TSharedPtr<FJsonObject>> Graphs = GraphsByName(Document);
			TestEqual(TEXT("each graph once"), Graphs.Num(), GraphArray->Num());
			for (const TPair<UEdGraph*, FString>& Graph : GraphsWithNodes())
			{
				TestEqual(Graph.Key->GetName() + TEXT(": all its nodes"), NodesIn(Graphs.FindRef(Graph.Key->GetName())), K2NodeCount(Graph.Key));
			}
			TestFalse(TEXT("the empty page is left out"), Graphs.Contains(Empty->GetName()));
			TestTrue(TEXT("the construction script too"), Graphs.Contains(UEdGraphSchema_K2::FN_UserConstructionScript.ToString()));
			TestEqual(TEXT("every graph with nodes"), Graphs.Num(), GraphsWithNodesCount());

			// Short IDs are unique across the document, so the GUID table has one entry per node
			TSet<FString> Ids;
			int32 NodeTotal = 0;
			for (const TPair<FString, TSharedPtr<FJsonObject>>& Graph : Graphs)
			{
				for (const TSharedPtr<FJsonValue>& Node : Graph.Value->GetArrayField(TEXT("nodes")))
				{
					Ids.Add(Node->AsObject()->GetObjectField(TEXT("ids"))->GetStringField(TEXT("short")));
					++NodeTotal;
				}
			}
			TestEqual(TEXT("node IDs unique across graphs"), Ids.Num(), NodeTotal);
			TestEqual(TEXT("guids.nodes has every node"), Document->GetObjectField(TEXT("guids"))->GetObjectField(TEXT("nodes"))->Values.Num(), NodeTotal);
		});

		It(TEXT("returns compact JSON by default and the indented document with compact false"), [this]()
		{
			const FString Compact = SuccessText(TEXT("compact"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), Function->GetName())));
			const TSharedPtr<FJsonObject> PrettyArgs = Args(Path(), Function->GetName());
			PrettyArgs->SetBoolField(TEXT("compact"), false);
			const FString Pretty = SuccessText(TEXT("indented"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), PrettyArgs));
			TestFalse(TEXT("compact has no newline"), Compact.Contains(TEXT("\n")));
			TestTrue(TEXT("indented has newlines"), Pretty.Contains(TEXT("\n")));
			TestTrue(TEXT("compact is shorter"), Compact.Len() < Pretty.Len());
			TestEqual(TEXT("the same document"), Condensed(N2CTest::ParseJsonObject(Compact)), Condensed(N2CTest::ParseJsonObject(Pretty)));
		});

		It(TEXT("returns what the editor-integration capture gives get-focused-blueprint for the same graph"), [this]()
		{
			// The path get-focused-blueprint took before it became a wrapper: collect, capture with the ghost events,
			// validate, then its result builder
			for (const TPair<UEdGraph*, FString>& Graph : GraphsWithNodes())
			{
				TArray<UK2Node*> Nodes;
				FN2CCapture Capture;
				FN2CCaptureOptions Options;
				Options.bIncludeGhostNodes = true;
				if (!TestTrue(Graph.Key->GetName() + TEXT(": old path captures"), FN2CEditorIntegration::Get().CollectNodesFromGraph(Graph.Key, Nodes)
					&& FN2CEditorIntegration::Get().TranslateNodesToN2CBlueprintWithMaps(Nodes, Capture, Options)))
				{
					continue;
				}
				const TSharedPtr<FJsonObject> PrettyArgs = Args(Path(), FString(), Graph.Key->GraphGuid.ToString());
				PrettyArgs->SetBoolField(TEXT("compact"), false);
				TestEqual(Graph.Key->GetName() + TEXT(": the same text"),
					SuccessText(Graph.Key->GetName(), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), PrettyArgs)),
					FN2CMcpGetFocusedBlueprintTool::BuildResultJson(Capture));
			}
		});

		It(TEXT("returns the text get-focused-blueprint returns when the same graph is focused"), [this]()
		{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
			// UE 5.6 and 5.7 under -nullrhi cannot close a Blueprint editor window (see NodeToCode.Editor.Focus); the
			// case above covers the same capture on every engine
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"));
#else
			UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (!TestNotNull(TEXT("asset editor subsystem"), AssetEditors))
			{
				return;
			}
			ON_SCOPE_EXIT
			{
				Test->GetPackage()->SetDirtyFlag(false);
				AssetEditors->CloseAllEditorsForAsset(Test->GetBlueprint());
			};
			TSharedPtr<IBlueprintEditor> Opened;
			FString OpenError;
			if (!TestTrue(TEXT("opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Test->GetBlueprint(), Opened, OpenError)))
			{
				return;
			}
			const TSharedPtr<FBlueprintEditor> Editor = FN2CBlueprintEditorTracker::GetEditorFor(Test->GetBlueprint());
			if (!TestTrue(TEXT("the editor is tracked"), Editor.IsValid()))
			{
				return;
			}
			for (UEdGraph* Graph : TArray<UEdGraph*>{ Test->EventGraph(), Function })
			{
				Editor->JumpToHyperlink(Graph, false);
				if (!TestTrue(Graph->GetName() + TEXT(" is focused"), FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor() == Graph))
				{
					continue;
				}
				const FString Focused = SuccessText(TEXT("get-focused-blueprint"), N2CTest::RunToolUnmarked(TEXT("get-focused-blueprint"), MakeShared<FJsonObject>()));
				const TSharedPtr<FJsonObject> PrettyArgs = Args(Path(), Graph->GetName());
				PrettyArgs->SetBoolField(TEXT("compact"), false);
				TestEqual(Graph->GetName() + TEXT(": compact false is get-focused-blueprint's text"),
					SuccessText(TEXT("by path"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), PrettyArgs)), Focused);
				TestEqual(Graph->GetName() + TEXT(": compact is the same document"),
					Condensed(N2CTest::ParseJsonObject(SuccessText(TEXT("compact"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), Graph->GetName()))))),
					Condensed(N2CTest::ParseJsonObject(Focused)));
			}
#endif
		});

		It(TEXT("gives the same IDs on every read and keeps nothing between reads"), [this]()
		{
			const FString First = SuccessText(TEXT("first"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), Function->GetName())));
			SuccessText(TEXT("every graph"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path())));
			SuccessText(TEXT("event graph"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), TEXT("EventGraph"))));
			TestEqual(TEXT("the function reads the same after other reads"), SuccessText(TEXT("again"),
				N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), Function->GetName()))), First);
		});

		It(TEXT("refuses bad arguments, unknown Blueprints and graphs, and an empty graph, with codes"), [this]()
		{
			ExpectError(TEXT("no blueprintPath"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), MakeShared<FJsonObject>()), TEXT("INVALID_ARGUMENT"));
			ExpectError(TEXT("unknown path"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(TEXT("/Game/N2CTest/NoSuch/BP_Nope"))), TEXT("ASSET_NOT_FOUND"));
			ExpectError(TEXT("unknown path with a graph"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(TEXT("/Game/N2CTest/NoSuch/BP_Nope"), TEXT("EventGraph"))), TEXT("ASSET_NOT_FOUND"));
			ExpectError(TEXT("unknown graph name"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), TEXT("NoSuchGraph"))), TEXT("GRAPH_NOT_FOUND"));
			ExpectError(TEXT("unknown graph GUID"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), FString(), FGuid::NewGuid().ToString())), TEXT("GRAPH_NOT_FOUND"));
			ExpectError(TEXT("a graph with no nodes"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), Empty->GetName())), TEXT("EMPTY_GRAPH"));
			for (const double Depth : { -1.0, 6.0, 1.5 })
			{
				const TSharedPtr<FJsonObject> DepthArgs = Args(Path(), Function->GetName());
				DepthArgs->SetNumberField(TEXT("depth"), Depth);
				ExpectError(FString::Printf(TEXT("depth %g"), Depth), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), DepthArgs), TEXT("INVALID_ARGUMENT"));
			}
		});

		It(TEXT("follows depth into the functions a graph calls"), [this]()
		{
			const TSharedPtr<FJsonObject> Depth0 = Args(Path(), TEXT("EventGraph"));
			Depth0->SetNumberField(TEXT("depth"), 0);
			const TSharedPtr<FJsonObject> Depth1 = Args(Path(), TEXT("EventGraph"));
			Depth1->SetNumberField(TEXT("depth"), 1);
			const TMap<FString, TSharedPtr<FJsonObject>> At0 = GraphsByName(N2CTest::ParseJsonObject(SuccessText(TEXT("depth 0"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Depth0))));
			const TMap<FString, TSharedPtr<FJsonObject>> At1 = GraphsByName(N2CTest::ParseJsonObject(SuccessText(TEXT("depth 1"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Depth1))));
			TestFalse(TEXT("depth 0: the event graph only"), At0.Contains(Function->GetName()));
			TestTrue(TEXT("depth 1: the called function too"), At1.Contains(Function->GetName()));
			TestTrue(TEXT("depth 1: and the collapsed graph"), At1.Contains(Collapsed->BoundGraph->GetName()));
		});

		It(TEXT("marks its results and list-blueprint-graphs' as untrusted Blueprint data"), [this]()
		{
			FN2CMcpToolManager& Tools = FN2CMcpToolManager::Get();
			for (const TCHAR* Name : { TEXT("get-blueprint-n2cjson"), TEXT("list-blueprint-graphs") })
			{
				const bool bWasRegistered = Tools.IsToolRegistered(Name);
				if (!bWasRegistered)
				{
					TestTrue(FString::Printf(TEXT("%s registered"), Name), Tools.RegisterTool(N2CTest::FindRegisteredTool(Name)));
				}
				const FString Text = N2CTest::ResultText(Tools.ExecuteTool(Name, Args(Path(), Function->GetName())));
				const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(Text);
				TestTrue(FString::Printf(TEXT("%s: a JSON object with untrusted_content"), Name), Json.IsValid()
					&& Json->Values.Num() > 1 && Json->HasField(FN2CMcpUntrustedContent::MarkerField));
				TestTrue(FString::Printf(TEXT("%s: untrusted_content is the first key"), Name),
					Text.TrimStart().StartsWith(FString::Printf(TEXT("{\"%s\""), FN2CMcpUntrustedContent::MarkerField)));
				if (!bWasRegistered)
				{
					Tools.UnregisterTool(Name);
				}
				const FMcpToolDefinition Definition = N2CTest::FindRegisteredTool(Name)->GetDefinition();
				TestTrue(FString::Printf(TEXT("%s: readOnlyHint"), Name), Definition.Annotations.IsReadOnly());
				TestTrue(FString::Printf(TEXT("%s: in BlueprintDiscoveryTools"), Name), Definition.Toolset == &N2CMcpToolsets::BlueprintDiscovery);
			}
		});

		Describe(TEXT("files"), [this]()
		{
			It(TEXT("writes a document over the token limit to a file whose JSON parses, and toFile writes any document"), [this]()
			{
				N2CTest::FScratchDir Scratch(TEXT("N2CByPathExports"));
				N2CMcpBlueprintExport::SetExportDirectoryForTests(Scratch.Path);
				ON_SCOPE_EXIT { N2CMcpBlueprintExport::SetExportDirectoryForTests(FString()); };

				// A function with a long chain of Print String calls: well over 20000 estimated tokens
				UEdGraph* Large = Test->AddFunction(FName(TEXT("N2CByPathLarge")));
				UEdGraphNode* Previous = Test->FunctionEntry(Large);
				for (int32 Index = 0; Index < 120; ++Index)
				{
					UK2Node_CallFunction* Print = Test->AddCall(Large, UKismetSystemLibrary::StaticClass(), FName(TEXT("PrintString")));
					Test->Link(Previous, UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
					Previous = Print;
				}
				if (!TestEqual(TEXT("built: ") + FString::Join(Test->Errors(), TEXT("; ")), Test->Errors().Num(), 0))
				{
					return;
				}

				const TSharedPtr<FJsonObject> LargeArgs = Args(Path(), Large->GetName());
				LargeArgs->SetNumberField(TEXT("depth"), 0);
				const TSharedPtr<FJsonObject> Result = N2CTest::ParseJsonObject(SuccessText(TEXT("large"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), LargeArgs)));
				if (!TestTrue(TEXT("the result names a file"), Result.IsValid() && Result->HasTypedField<EJson::String>(TEXT("path"))))
				{
					return;
				}
				TestEqual(TEXT("reason"), Result->GetStringField(TEXT("reason")), FString(TEXT("size")));
				TestTrue(TEXT("estimatedTokens over the limit"), Result->GetNumberField(TEXT("estimatedTokens")) > N2CMcpBlueprintExport::MaxInlineTokens);
				TestEqual(TEXT("nodeCount"), static_cast<int32>(Result->GetNumberField(TEXT("nodeCount"))), 121);
				const FString FilePath = Result->GetStringField(TEXT("path"));
				TestTrue(TEXT("inside the export folder"), FPaths::IsUnderDirectory(FilePath, Scratch.Path));
				TestEqual(TEXT("bytes"), static_cast<int64>(Result->GetNumberField(TEXT("bytes"))), IFileManager::Get().FileSize(*FilePath));
				FString FileText;
				TestTrue(TEXT("the file reads"), FFileHelper::LoadFileToString(FileText, *FilePath));
				const TMap<FString, TSharedPtr<FJsonObject>> Graphs = GraphsByName(N2CTest::ParseJsonObject(FileText));
				TestEqual(TEXT("the file's JSON parses with every node"), NodesIn(Graphs.FindRef(Large->GetName())), 121);

				// The same read again replaces the file; toFile writes a small document too
				const TSharedPtr<FJsonObject> Again = N2CTest::ParseJsonObject(SuccessText(TEXT("again"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), LargeArgs)));
				TestEqual(TEXT("the same file"), Again.IsValid() ? Again->GetStringField(TEXT("path")) : FString(), FilePath);
				const TSharedPtr<FJsonObject> SmallArgs = Args(Path(), Function->GetName());
				SmallArgs->SetBoolField(TEXT("toFile"), true);
				const TSharedPtr<FJsonObject> Small = N2CTest::ParseJsonObject(SuccessText(TEXT("toFile"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), SmallArgs)));
				TestTrue(TEXT("toFile: reason toFile"), Small.IsValid() && Small->GetStringField(TEXT("reason")) == TEXT("toFile"));
				TArray<FString> Files;
				IFileManager::Get().FindFiles(Files, *(Scratch.Path / TEXT("*")), true, false);
				TestEqual(TEXT("two files, no temporary left"), Files.Num(), 2);
			});
		});
	});

	Describe(TEXT("the Python bridge"), [this]()
	{
		It(TEXT("lists and reads by path as the tools do"), [this]()
		{
			const TSharedPtr<FJsonObject> Listed = N2CTest::ParseJsonObject(UN2CPythonBridge::ListBlueprintGraphs(Path()));
			TestTrue(TEXT("ListBlueprintGraphs succeeds"), Listed.IsValid() && Listed->GetBoolField(TEXT("success")));
			const TSharedPtr<FJsonObject> ToolList = N2CTest::ParseJsonObject(N2CTest::ResultText(N2CTest::RunToolUnmarked(TEXT("list-blueprint-graphs"), Args(Path()))));
			TestEqual(TEXT("ListBlueprintGraphs data is the tool's result"),
				Listed.IsValid() ? Condensed(Listed->GetObjectField(TEXT("data"))) : FString(), Condensed(ToolList));

			const TSharedPtr<FJsonObject> Read = N2CTest::ParseJsonObject(UN2CPythonBridge::GetBlueprintN2CJson(Path(), FString(), Function->GraphGuid.ToString()));
			const TSharedPtr<FJsonObject> ToolRead = N2CTest::ParseJsonObject(N2CTest::ResultText(N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), Args(Path(), FString(), Function->GraphGuid.ToString()))));
			if (TestTrue(TEXT("GetBlueprintN2CJson succeeds"), Read.IsValid() && Read->GetBoolField(TEXT("success"))))
			{
				const TSharedPtr<FJsonObject> Data = Read->GetObjectField(TEXT("data"));
				TestEqual(TEXT("n2c_json is the tool's document"), Condensed(Data->GetObjectField(TEXT("n2c_json"))), Condensed(ToolRead));
				TestEqual(TEXT("nodeCount"), static_cast<int32>(Data->GetNumberField(TEXT("nodeCount"))), K2NodeCount(Function));
				TestEqual(TEXT("target graph"), Data->GetObjectField(TEXT("target"))->GetStringField(TEXT("graphName")), Function->GetName());
			}

			const TSharedPtr<FJsonObject> All = N2CTest::ParseJsonObject(UN2CPythonBridge::GetBlueprintN2CJson(Path()));
			TestTrue(TEXT("every graph with nodes"), All.IsValid() && All->GetBoolField(TEXT("success"))
				&& static_cast<int32>(All->GetObjectField(TEXT("data"))->GetNumberField(TEXT("graphCount"))) == GraphsWithNodesCount());

			const TSharedPtr<FJsonObject> Missing = N2CTest::ParseJsonObject(UN2CPythonBridge::GetBlueprintN2CJson(TEXT("/Game/N2CTest/NoSuch/BP_Nope")));
			TestTrue(TEXT("unknown path: ASSET_NOT_FOUND"), Missing.IsValid() && !Missing->GetBoolField(TEXT("success"))
				&& Missing->GetStringField(TEXT("error")).StartsWith(TEXT("ASSET_NOT_FOUND")));
			const TSharedPtr<FJsonObject> NoGraph = N2CTest::ParseJsonObject(UN2CPythonBridge::GetBlueprintN2CJson(Path(), TEXT("NoSuchGraph")));
			TestTrue(TEXT("unknown graph: GRAPH_NOT_FOUND"), NoGraph.IsValid() && !NoGraph->GetBoolField(TEXT("success"))
				&& NoGraph->GetStringField(TEXT("error")).StartsWith(TEXT("GRAPH_NOT_FOUND")));
		});
	});

	Describe(TEXT("find-nodes-in-graph"), [this]()
	{
		It(TEXT("returns the matching nodes exactly as the graph's document has them"), [this]()
		{
			const TSharedPtr<FJsonObject> FindArgs = Args(Path(), Function->GetName());
			TArray<TSharedPtr<FJsonValue>> Terms;
			Terms.Add(MakeShared<FJsonValueString>(TEXT("PrintString")));
			Terms.Add(MakeShared<FJsonValueString>(TEXT("FunctionEntry")));
			FindArgs->SetArrayField(TEXT("searchTerms"), Terms);
			const TSharedPtr<FJsonObject> Found = N2CTest::ParseJsonObject(SuccessText(TEXT("find"), N2CTest::RunToolUnmarked(TEXT("find-nodes-in-graph"), FindArgs)));
			const TSharedPtr<FJsonObject> ReadArgs = Args(Path(), Function->GetName());
			ReadArgs->SetNumberField(TEXT("depth"), 0);
			const TMap<FString, TSharedPtr<FJsonObject>> Graphs = GraphsByName(N2CTest::ParseJsonObject(
				SuccessText(TEXT("read"), N2CTest::RunToolUnmarked(TEXT("get-blueprint-n2cjson"), ReadArgs))));
			if (!TestTrue(TEXT("both parse"), Found.IsValid() && Graphs.Contains(Function->GetName())))
			{
				return;
			}
			TMap<FString, FString> DocumentNodes;
			for (const TSharedPtr<FJsonValue>& Node : Graphs[Function->GetName()]->GetArrayField(TEXT("nodes")))
			{
				DocumentNodes.Add(Node->AsObject()->GetObjectField(TEXT("ids"))->GetStringField(TEXT("short")), Condensed(Node->AsObject()));
			}
			const TArray<TSharedPtr<FJsonValue>>& Nodes = Found->GetArrayField(TEXT("nodes"));
			TestEqual(TEXT("both nodes found"), Nodes.Num(), 2);
			for (const TSharedPtr<FJsonValue>& Node : Nodes)
			{
				const FString Id = Node->AsObject()->GetObjectField(TEXT("ids"))->GetStringField(TEXT("short"));
				TestEqual(Id + TEXT(" is the document's node"), Condensed(Node->AsObject()), DocumentNodes.FindRef(Id));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
