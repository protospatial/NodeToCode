// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "MCP/Tools/Implementations/Blueprint/Analysis/N2CMcpEditorContext.h"
#include "MCP/Tools/Implementations/Blueprint/Analysis/N2CMcpGetOpenBlueprintEditorsTool.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "GraphEditor.h"
#include "K2Node_Composite.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/EngineVersion.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"
#include "Widgets/Docking/SDockTab.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CEditorContextSpec
{
	/** The entry of a JSON array of objects whose Field is Value, or null */
	TSharedPtr<FJsonObject> FindBy(const TArray<TSharedPtr<FJsonValue>>& Array, const FString& Field, const FString& Value)
	{
		for (const TSharedPtr<FJsonValue>& Item : Array)
		{
			const TSharedPtr<FJsonObject> Object = Item.IsValid() ? Item->AsObject() : nullptr;
			FString Found;
			if (Object.IsValid() && Object->TryGetStringField(Field, Found) && Found == Value)
			{
				return Object;
			}
		}
		return nullptr;
	}

	/** A graph made the way the editor makes a function graph, not yet in any of the Blueprint's lists */
	UEdGraph* NewGraph(UBlueprint* Blueprint, const FName Name)
	{
		return FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	}

	/** The asset tab of an editor (the tab a user clicks to switch between open Blueprints) */
	TSharedPtr<SDockTab> AssetTabOf(const TSharedPtr<FBlueprintEditor>& Editor)
	{
		const TSharedPtr<FTabManager> TabManager = Editor.IsValid() ? Editor->GetTabManager() : nullptr;
		return TabManager.IsValid() ? TabManager->GetOwnerTab() : nullptr;
	}

	/** The document tab showing Graph in Editor */
	TSharedPtr<SDockTab> GraphTabOf(const TSharedPtr<FBlueprintEditor>& Editor, UEdGraph* Graph)
	{
		TArray<TSharedPtr<SDockTab>> Tabs;
		if (Editor.IsValid() && Graph)
		{
			Editor->FindOpenTabsContainingDocument(Graph, Tabs);
		}
		return Tabs.Num() > 0 ? Tabs[0] : nullptr;
	}
}

/**
 * get-editor-context and get-open-blueprint-editors (card 0275, review mcp-tools-bp-structure-5 and -6): graphs are
 * classified by where the Blueprint keeps them and by their schema's GetGraphType, so the EventGraph (an UbergraphPages
 * page, not the transient EventGraphs list) is listed and typed Event; every open Blueprint editor is listed, Widget
 * Blueprint editors included, each with its own focused graph; unsaved changes are reported apart from compile status;
 * and get-editor-context follows the focused editor, its graph and its selection.
 */
BEGIN_DEFINE_SPEC(FN2CEditorContextSpec, "NodeToCode.MCP.EditorContext", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CEditorContextSpec)

void FN2CEditorContextSpec::Define()
{
	using namespace N2CEditorContextSpec;

	Describe(TEXT("graph classification"), [this]()
	{
		It(TEXT("types every kind of graph of a transient Blueprint and lists the ones its editor shows"), [this]()
		{
			N2CTest::FTestBlueprint Test(TEXT("BP_N2CEditorContextKinds"));
			UBlueprint* Blueprint = Test.GetBlueprint();
			UEdGraph* EventGraph = Test.EventGraph();
			UEdGraph* Function = Test.AddFunction(TEXT("DoThing"));
			UEdGraph* Macro = Test.AddMacro(TEXT("MyMacro"));
			UK2Node_Composite* Composite = Test.AddCollapsedGraph(EventGraph, TEXT("Collapsed"));
			UEdGraph* Collapsed = Composite ? Composite->BoundGraph.Get() : nullptr;

			UEdGraph* DelegateGraph = NewGraph(Blueprint, TEXT("OnSomething"));
			Blueprint->DelegateSignatureGraphs.Add(DelegateGraph);
			UEdGraph* InterfaceGraph = NewGraph(Blueprint, TEXT("InterfaceFunction"));
			FBPInterfaceDescription Interface;
			Interface.Interface = UInterface::StaticClass();
			Interface.Graphs.Add(InterfaceGraph);
			Blueprint->ImplementedInterfaces.Add(Interface);

			if (!TestTrue(TEXT("the Blueprint was built: ") + FString::Join(Test.Errors(), TEXT("; ")),
				EventGraph && Function && Macro && Collapsed && DelegateGraph && InterfaceGraph))
			{
				return;
			}
			TestTrue(TEXT("the event graph is an UbergraphPages page"), Blueprint->UbergraphPages.Contains(EventGraph));

			using N2CMcpEditorContext::GraphType;
			TestEqual(TEXT("EventGraph"), GraphType(Blueprint, EventGraph), FString(TEXT("Event")));
			TestEqual(TEXT("a function"), GraphType(Blueprint, Function), FString(TEXT("Function")));
			TestEqual(TEXT("a macro"), GraphType(Blueprint, Macro), FString(TEXT("Macro")));
			TestEqual(TEXT("a collapsed graph"), GraphType(Blueprint, Collapsed), FString(TEXT("Collapsed")));
			TestEqual(TEXT("a delegate signature"), GraphType(Blueprint, DelegateGraph), FString(TEXT("Delegate")));
			TestEqual(TEXT("an interface implementation"), GraphType(Blueprint, InterfaceGraph), FString(TEXT("Interface")));
			TestEqual(TEXT("no graph"), GraphType(Blueprint, nullptr), FString(TEXT("Graph")));

			// What get-open-blueprint-editors lists for this Blueprint
			const TArray<TSharedPtr<FJsonValue>> Graphs = FN2CMcpGetOpenBlueprintEditorsTool::CollectBlueprintGraphs(*Blueprint);
			const struct { UEdGraph* Graph; const TCHAR* Type; } Expected[] = {
				{ EventGraph, TEXT("Event") }, { Function, TEXT("Function") }, { Macro, TEXT("Macro") },
				{ InterfaceGraph, TEXT("Interface") }, { DelegateGraph, TEXT("Delegate") } };
			for (const auto& Each : Expected)
			{
				const TSharedPtr<FJsonObject> Entry = FindBy(Graphs, TEXT("name"), Each.Graph->GetName());
				if (TestTrue(FString::Printf(TEXT("%s is listed"), *Each.Graph->GetName()), Entry.IsValid()))
				{
					TestEqual(FString::Printf(TEXT("%s: type"), *Each.Graph->GetName()), Entry->GetStringField(TEXT("type")), FString(Each.Type));
					TestEqual(FString::Printf(TEXT("%s: graphGuid"), *Each.Graph->GetName()), Entry->GetStringField(TEXT("graphGuid")), Each.Graph->GraphGuid.ToString());
				}
			}
			TestFalse(TEXT("a collapsed graph is not listed at the top level"), FindBy(Graphs, TEXT("name"), Collapsed->GetName()).IsValid());
			const TSharedPtr<FJsonObject> DelegateEntry = FindBy(Graphs, TEXT("name"), DelegateGraph->GetName());
			const TSharedPtr<FJsonObject> FunctionEntry = FindBy(Graphs, TEXT("name"), Function->GetName());
			TestTrue(TEXT("a delegate signature is read-only"), DelegateEntry.IsValid() && !DelegateEntry->GetBoolField(TEXT("isEditable")));
			TestTrue(TEXT("a function is editable"), FunctionEntry.IsValid() && FunctionEntry->GetBoolField(TEXT("isEditable")));
			TestEqual(TEXT("every graph once"), Graphs.Num(), N2CMcpEditorContext::TopLevelGraphs(*Blueprint).Num());
		});

		It(TEXT("reports unsaved changes apart from compile status"), [this]()
		{
			N2CTest::FTestBlueprint Test(TEXT("BP_N2CEditorContextDirty"));
			UBlueprint* Blueprint = Test.GetBlueprint();
			Blueprint->Status = BS_UpToDate;
			Test.GetPackage()->SetDirtyFlag(true);
			TestTrue(TEXT("a changed package is dirty"), N2CMcpEditorContext::IsPackageDirty(*Blueprint));
			TestEqual(TEXT("while compiled"), N2CMcpEditorContext::CompileStatus(Blueprint->Status), FString(TEXT("upToDate")));
			Test.GetPackage()->SetDirtyFlag(false);
			Blueprint->Status = BS_Dirty;
			TestFalse(TEXT("a saved package is not dirty"), N2CMcpEditorContext::IsPackageDirty(*Blueprint));
			TestEqual(TEXT("while it needs a compile"), N2CMcpEditorContext::CompileStatus(Blueprint->Status), FString(TEXT("dirty")));
			TestEqual(TEXT("error"), N2CMcpEditorContext::CompileStatus(BS_Error), FString(TEXT("error")));
			TestEqual(TEXT("warnings"), N2CMcpEditorContext::CompileStatus(BS_UpToDateWithWarnings), FString(TEXT("upToDateWithWarnings")));
		});
	});

	Describe(TEXT("get-editor-context"), [this]()
	{
		It(TEXT("answers with the engine version, Play In Editor state and the open editors"), [this]()
		{
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("get-editor-context"), MakeShared<FJsonObject>());
			const TSharedPtr<FJsonObject> Json = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
			if (!TestFalse(TEXT("succeeds"), Result.bIsError) || !TestTrue(TEXT("a JSON object"), Json.IsValid()))
			{
				return;
			}
			TestEqual(TEXT("engineVersion"), Json->GetStringField(TEXT("engineVersion")), FEngineVersion::Current().ToString(EVersionComponent::Patch));
			const TSharedPtr<FJsonObject> Pie = Json->GetObjectField(TEXT("playInEditor"));
			TestEqual(TEXT("playInEditor.active"), Pie->GetBoolField(TEXT("active")), GEditor && GEditor->PlayWorld != nullptr);
			TestEqual(TEXT("openEditorCount"), static_cast<int32>(Json->GetNumberField(TEXT("openEditorCount"))),
				FN2CBlueprintEditorTracker::GetOpenBlueprintEditors().Num());
			TestEqual(TEXT("selectedNodeCount"), static_cast<int32>(Json->GetNumberField(TEXT("selectedNodeCount"))),
				Json->GetArrayField(TEXT("selectedNodes")).Num());
			TestEqual(TEXT("focusedBlueprint is there exactly when a Blueprint editor is focused"), Json->HasField(TEXT("focusedBlueprint")),
				FN2CBlueprintEditorTracker::Get().GetFocusedEditor().IsValid());
		});

		It(TEXT("follows the focused editor, its graph and its selection across an Actor and a Widget Blueprint"), [this]()
		{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
			// UE 5.6 and 5.7 under -nullrhi cannot close a Blueprint editor's window (see N2CTestHelpers.h). This case
			// runs on 5.8; the same behaviour is checked live in a windowed editor on 5.8.
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"));
#else
			UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (!TestNotNull(TEXT("asset editor subsystem"), AssetEditors))
			{
				return;
			}
			FN2CBlueprintEditorTracker& Tracker = FN2CBlueprintEditorTracker::Get();

			N2CTest::FTestBlueprint Actor(TEXT("BP_N2CEditorContextActor"), TEXT("/Game"));
			UEdGraph* ActorGraph = Actor.EventGraph();
			UEdGraphNode* Branch = Actor.AddBranch(ActorGraph);
			UEdGraphNode* Sequence = Actor.AddSequence(ActorGraph);

			UPackage* WidgetPackage = CreatePackage(*FString::Printf(TEXT("/Game/N2CTest_%s/WBP_N2CEditorContext"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			UBlueprint* Widget = FKismetEditorUtilities::CreateBlueprint(UUserWidget::StaticClass(), WidgetPackage,
				TEXT("WBP_N2CEditorContext"), BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass());
			ON_SCOPE_EXIT
			{
				Actor.GetPackage()->SetDirtyFlag(false);
				AssetEditors->CloseAllEditorsForAsset(Actor.GetBlueprint());
				if (Widget)
				{
					AssetEditors->CloseAllEditorsForAsset(Widget);
				}
				WidgetPackage->SetDirtyFlag(false);
				ForEachObjectWithPackage(WidgetPackage, [](UObject* Object)
				{
					Object->ClearFlags(RF_Standalone | RF_Public);
					return true;
				}, false);
			};
			if (!TestTrue(TEXT("the Actor Blueprint was built"), Branch && Sequence) || !TestNotNull(TEXT("the Widget Blueprint"), Widget))
			{
				return;
			}
			UEdGraph* WidgetGraph = Widget->UbergraphPages.Num() > 0 ? Widget->UbergraphPages[0].Get() : nullptr;
			if (!TestNotNull(TEXT("the Widget Blueprint has an event graph"), WidgetGraph))
			{
				return;
			}

			TSharedPtr<IBlueprintEditor> Opened;
			FString OpenError;
			if (!TestTrue(TEXT("the Actor Blueprint opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Actor.GetBlueprint(), Opened, OpenError))
				|| !TestTrue(TEXT("the Widget Blueprint opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Widget, Opened, OpenError)))
			{
				return;
			}
			const TSharedPtr<FBlueprintEditor> ActorEditor = FN2CBlueprintEditorTracker::GetEditorFor(Actor.GetBlueprint());
			const TSharedPtr<FBlueprintEditor> WidgetEditor = FN2CBlueprintEditorTracker::GetEditorFor(Widget);
			if (!TestTrue(TEXT("both are Blueprint editors"), ActorEditor.IsValid() && WidgetEditor.IsValid()))
			{
				return;
			}
			ActorEditor->JumpToHyperlink(ActorGraph, false);
			WidgetEditor->JumpToHyperlink(WidgetGraph, false);

			const auto Context = [this]()
			{
				return N2CTest::ParseJsonObject(N2CTest::ResultText(N2CTest::RunTool(TEXT("get-editor-context"), MakeShared<FJsonObject>())));
			};
			const auto ExpectFocus = [&](const FString& What, const UBlueprint* Blueprint, const UEdGraph* Graph)
			{
				const TSharedPtr<FJsonObject> Json = Context();
				if (!TestTrue(What + TEXT(": a result"), Json.IsValid() && Json->HasField(TEXT("focusedBlueprint")) && Json->HasField(TEXT("focusedGraph"))))
				{
					return Json;
				}
				TestEqual(What + TEXT(": focusedBlueprint.path"), Json->GetObjectField(TEXT("focusedBlueprint"))->GetStringField(TEXT("path")), Blueprint->GetPathName());
				const TSharedPtr<FJsonObject> FocusedGraph = Json->GetObjectField(TEXT("focusedGraph"));
				TestEqual(What + TEXT(": focusedGraph.graphGuid"), FocusedGraph->GetStringField(TEXT("graphGuid")), Graph->GraphGuid.ToString());
				TestEqual(What + TEXT(": focusedGraph.type"), FocusedGraph->GetStringField(TEXT("type")), FString(TEXT("Event")));
				return Json;
			};

			// The Widget Blueprint was opened last
			{
				const TSharedPtr<FJsonObject> Json = ExpectFocus(TEXT("after opening the Widget Blueprint"), Widget, WidgetGraph);
				if (Json.IsValid())
				{
					TestEqual(TEXT("focusedBlueprint.blueprintClass"), Json->GetObjectField(TEXT("focusedBlueprint"))->GetStringField(TEXT("blueprintClass")),
						FString(TEXT("WidgetBlueprint")));
					const TArray<TSharedPtr<FJsonValue>> Editors = Json->GetArrayField(TEXT("openEditors"));
					const TSharedPtr<FJsonObject> ActorEntry = FindBy(Editors, TEXT("assetPath"), Actor.GetBlueprint()->GetPathName());
					const TSharedPtr<FJsonObject> WidgetEntry = FindBy(Editors, TEXT("assetPath"), Widget->GetPathName());
					if (TestTrue(TEXT("both editors are listed"), ActorEntry.IsValid() && WidgetEntry.IsValid()))
					{
						TestTrue(TEXT("the Widget Blueprint is focused"), WidgetEntry->GetBoolField(TEXT("isFocused")) && !ActorEntry->GetBoolField(TEXT("isFocused")));
						TestEqual(TEXT("the Actor editor's own focused graph"), ActorEntry->GetObjectField(TEXT("focusedGraph"))->GetStringField(TEXT("graphGuid")),
							ActorGraph->GraphGuid.ToString());
						TestEqual(TEXT("the Widget editor's toolkit"), WidgetEntry->GetStringField(TEXT("editorName")), FString(TEXT("WidgetBlueprintEditor")));
					}
				}
			}

			// get-open-blueprint-editors lists the Widget Blueprint with its EventGraph typed Event
			{
				const TSharedPtr<FJsonObject> Open = N2CTest::ParseJsonObject(N2CTest::ResultText(N2CTest::RunTool(TEXT("get-open-blueprint-editors"), MakeShared<FJsonObject>())));
				const TSharedPtr<FJsonObject> WidgetEntry = Open.IsValid() ? FindBy(Open->GetArrayField(TEXT("editors")), TEXT("assetPath"), Widget->GetPathName()) : nullptr;
				if (TestTrue(TEXT("get-open-blueprint-editors lists the Widget Blueprint"), WidgetEntry.IsValid()))
				{
					TestEqual(TEXT("its focusedGraphType"), WidgetEntry->GetStringField(TEXT("focusedGraphType")), FString(TEXT("Event")));
					const TSharedPtr<FJsonObject> Page = FindBy(WidgetEntry->GetArrayField(TEXT("graphs")), TEXT("name"), WidgetGraph->GetName());
					TestTrue(TEXT("its EventGraph is listed as Event"), Page.IsValid() && Page->GetStringField(TEXT("type")) == TEXT("Event"));
					TestTrue(TEXT("graph_counts.event_graphs counts it"), WidgetEntry->GetObjectField(TEXT("graph_counts"))->GetNumberField(TEXT("event_graphs")) >= 1);
				}
				const TSharedPtr<FJsonObject> ActorEntry = Open.IsValid() ? FindBy(Open->GetArrayField(TEXT("editors")), TEXT("assetPath"), Actor.GetBlueprint()->GetPathName()) : nullptr;
				TestTrue(TEXT("get-open-blueprint-editors lists the Actor Blueprint with its own focused graph"),
					ActorEntry.IsValid() && ActorEntry->GetStringField(TEXT("focusedGraph")) == ActorGraph->GetName());
			}

			// A click on the Actor Blueprint's tab and in its graph moves the context there
			if (const TSharedPtr<SDockTab> Tab = AssetTabOf(ActorEditor))
			{
				Tab->ActivateInParent(ETabActivationCause::UserClickedOnTab);
			}
			FGlobalTabmanager::Get()->SetActiveTab(GraphTabOf(ActorEditor, ActorGraph));
			Tracker.HandleTabActivated(GraphTabOf(ActorEditor, ActorGraph));
			ExpectFocus(TEXT("after clicking in the Actor Blueprint's graph"), Actor.GetBlueprint(), ActorGraph);

			// The selection in the focused graph, in graph order
			if (const TSharedPtr<SGraphEditor> GraphEditor = ActorEditor->OpenGraphAndBringToFront(ActorGraph))
			{
				GraphEditor->ClearSelectionSet();
				GraphEditor->SetNodeSelection(Sequence, true);
				GraphEditor->SetNodeSelection(Branch, true);
			}
			{
				const TSharedPtr<FJsonObject> Json = Context();
				const TArray<TSharedPtr<FJsonValue>> Selected = Json.IsValid() ? Json->GetArrayField(TEXT("selectedNodes")) : TArray<TSharedPtr<FJsonValue>>();
				TestEqual(TEXT("two nodes selected"), Selected.Num(), 2);
				TestTrue(TEXT("the branch is selected, by its N2C JSON guid"),
					FindBy(Selected, TEXT("nodeGuid"), Branch->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)).IsValid());
				TestTrue(TEXT("the sequence is selected"),
					FindBy(Selected, TEXT("nodeGuid"), Sequence->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)).IsValid());
				TestEqual(TEXT("selectedNodeCount"), Json.IsValid() ? static_cast<int32>(Json->GetNumberField(TEXT("selectedNodeCount"))) : -1, 2);
			}

			// Back to the Widget Blueprint
			Tracker.NotifyEditorActivated(WidgetEditor);
			{
				const TSharedPtr<FJsonObject> Json = ExpectFocus(TEXT("after activating the Widget Blueprint"), Widget, WidgetGraph);
				TestFalse(TEXT("the Actor graph's selection is not reported for the Widget Blueprint"),
					Json.IsValid() && FindBy(Json->GetArrayField(TEXT("selectedNodes")), TEXT("nodeGuid"),
						Branch->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)).IsValid());
			}
#endif
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
