// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Runtime/Launch/Resources/Version.h"

/**
 * Card 0286: open-blueprint-asset and the Python bridge's OpenBlueprint find focus_graph without case, take
 * "ConstructionScript" for the construction script (UserConstructionScript), and fail on a name that matches no
 * graph, listing the Blueprint's graphs, instead of opening the Blueprint with nothing focused (the next
 * add_node_to_graph then landed in whatever graph was focused before). Review ids: python-bridge-24,
 * mcp-tools-contentbrowser-fs-25.
 */
BEGIN_DEFINE_SPEC(FN2COpenBlueprintGraphSpec, "NodeToCode.MCP.OpenBlueprintGraph", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2COpenBlueprintGraphSpec)

void FN2COpenBlueprintGraphSpec::Define()
{
	Describe(TEXT("FindTopLevelGraph"), [this]()
	{
		It(TEXT("matches without case, aliases ConstructionScript and lists the graphs for an unknown name"), [this]()
		{
			N2CTest::FToolSpecBlueprint Actor(TEXT("BP_N2COpenGraph"), TEXT("/Game"));
			UBlueprint* Blueprint = Actor.GetBlueprint();
			UEdGraph* Function = Actor.AddFunction(FName(TEXT("N2COpenGraphFunction")));
			if (!TestNotNull(TEXT("the Blueprint"), Blueprint) || !TestNotNull(TEXT("its function"), Function))
			{
				return;
			}
			UEdGraph* Construction = nullptr;
			for (UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (Graph && Graph->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript)
				{
					Construction = Graph;
				}
			}
			if (!TestNotNull(TEXT("an Actor Blueprint has UserConstructionScript"), Construction))
			{
				return;
			}

			FString Error;
			TestTrue(TEXT("EventGraph"), FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, TEXT("EventGraph"), Error) == Actor.EventGraph());
			TestTrue(TEXT("eventgraph, without case"), FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, TEXT("eventgraph"), Error) == Actor.EventGraph());
			TestTrue(TEXT("a function, without case"), FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, TEXT("n2copengraphfunction"), Error) == Function);
			TestTrue(TEXT("UserConstructionScript"), FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, TEXT("UserConstructionScript"), Error) == Construction);
			TestTrue(TEXT("ConstructionScript names UserConstructionScript"), FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, TEXT("ConstructionScript"), Error) == Construction);
			TestTrue(TEXT("constructionscript too"), FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, TEXT("constructionscript"), Error) == Construction);

			Error.Reset();
			TestNull(TEXT("an unknown name finds nothing"), FN2CMcpBlueprintUtils::FindTopLevelGraph(*Blueprint, TEXT("NoSuchGraph"), Error));
			TestTrue(TEXT("GRAPH_NOT_FOUND: ") + Error, Error.StartsWith(TEXT("GRAPH_NOT_FOUND")));
			TestTrue(TEXT("names the request"), Error.Contains(TEXT("'NoSuchGraph'")));
			TestTrue(TEXT("lists the event graph"), Error.Contains(Actor.EventGraph()->GetName()));
			TestTrue(TEXT("lists the function"), Error.Contains(TEXT("N2COpenGraphFunction")));
			TestTrue(TEXT("lists the construction script"), Error.Contains(TEXT("UserConstructionScript")));
		});
	});

	Describe(TEXT("an unknown focus_graph"), [this]()
	{
		It(TEXT("fails open-blueprint-asset and the bridge's OpenBlueprint, listing the graphs, and opens nothing"), [this]()
		{
			N2CTest::FToolSpecBlueprint Actor(TEXT("BP_N2COpenGraphUnknown"), TEXT("/Game"));
			if (!TestNotNull(TEXT("the Blueprint"), Actor.GetBlueprint()))
			{
				return;
			}

			const FMcpToolCallResult Tool = N2CTest::CallTool(TEXT("open-blueprint-asset"),
				{ { TEXT("asset_path"), Actor.Path() }, { TEXT("focus_graph"), TEXT("NoSuchGraph") }, { TEXT("bring_to_front"), TEXT("false") } });
			const FString ToolText = N2CTest::ResultText(Tool);
			TestTrue(TEXT("the tool fails: ") + ToolText.Left(300), Tool.bIsError);
			TestTrue(TEXT("with GRAPH_NOT_FOUND"), ToolText.Contains(TEXT("GRAPH_NOT_FOUND")));
			TestTrue(TEXT("listing the graphs"), ToolText.Contains(Actor.EventGraph()->GetName()) && ToolText.Contains(TEXT("UserConstructionScript")));

			const TSharedPtr<FJsonObject> Bridge = N2CTest::ParseJsonObject(UN2CPythonBridge::OpenBlueprint(Actor.Path(), TEXT("NoSuchGraph")));
			if (TestTrue(TEXT("the bridge answers JSON"), Bridge.IsValid()))
			{
				TestFalse(TEXT("the bridge fails"), Bridge->GetBoolField(TEXT("success")));
				const FString BridgeError = Bridge->GetStringField(TEXT("error"));
				TestTrue(TEXT("with GRAPH_NOT_FOUND: ") + BridgeError, BridgeError.StartsWith(TEXT("GRAPH_NOT_FOUND")));
				TestTrue(TEXT("listing the graphs"), BridgeError.Contains(Actor.EventGraph()->GetName()));
			}

			TestFalse(TEXT("no Blueprint editor was opened"), FN2CBlueprintEditorTracker::GetEditorFor(Actor.GetBlueprint()).IsValid());
		});
	});

	Describe(TEXT("ConstructionScript"), [this]()
	{
		It(TEXT("focuses UserConstructionScript through open-blueprint-asset and the bridge"), [this]()
		{
			N2CTest::FToolSpecBlueprint Actor(TEXT("BP_N2COpenGraphConstruction"), TEXT("/Game"));
			// Opens the editor on the EventGraph (and has the destructor close it); skips below UE 5.8
			if (!TestNotNull(TEXT("the Blueprint"), Actor.GetBlueprint()) || !Actor.OpenInEditor(*this, Actor.EventGraph()))
			{
				return;
			}
			const TSharedPtr<FBlueprintEditor> Editor = FN2CBlueprintEditorTracker::GetEditorFor(Actor.GetBlueprint());
			if (!TestTrue(TEXT("the editor is open on the EventGraph"), Editor.IsValid() && Editor->GetFocusedGraph() == Actor.EventGraph()))
			{
				return;
			}

			const FMcpToolCallResult Tool = N2CTest::CallTool(TEXT("open-blueprint-asset"),
				{ { TEXT("asset_path"), Actor.Path() }, { TEXT("focus_graph"), TEXT("ConstructionScript") }, { TEXT("bring_to_front"), TEXT("false") } });
			const TSharedPtr<FJsonObject> ToolJson = N2CTest::ParseJsonObject(N2CTest::ResultText(Tool));
			if (TestFalse(TEXT("the tool succeeds: ") + N2CTest::ResultText(Tool).Left(300), Tool.bIsError) && TestTrue(TEXT("a JSON result"), ToolJson.IsValid()))
			{
				TestEqual(TEXT("focused_graph is the graph's name"), ToolJson->GetStringField(TEXT("focused_graph")), UEdGraphSchema_K2::FN_UserConstructionScript.ToString());
			}
			TestTrue(TEXT("the editor shows the construction script"), Editor->GetFocusedGraph()
				&& Editor->GetFocusedGraph()->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript);

			// Back to the EventGraph, then the bridge
			Editor->JumpToHyperlink(Actor.EventGraph(), false);
			const TSharedPtr<FJsonObject> Bridge = N2CTest::ParseJsonObject(UN2CPythonBridge::OpenBlueprint(Actor.Path(), TEXT("constructionscript")));
			const TSharedPtr<FJsonObject>* Data = nullptr;
			if (TestTrue(TEXT("the bridge's OpenBlueprint succeeds"), Bridge.IsValid() && Bridge->GetBoolField(TEXT("success")) && Bridge->TryGetObjectField(TEXT("data"), Data)))
			{
				TestEqual(TEXT("focusedGraph is the graph's name"), (*Data)->GetStringField(TEXT("focusedGraph")), UEdGraphSchema_K2::FN_UserConstructionScript.ToString());
			}
			TestTrue(TEXT("the editor shows the construction script after the bridge"), Editor->GetFocusedGraph()
				&& Editor->GetFocusedGraph()->GetFName() == UEdGraphSchema_K2::FN_UserConstructionScript);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
