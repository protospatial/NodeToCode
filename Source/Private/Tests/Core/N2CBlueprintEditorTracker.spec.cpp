// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CGraphOverlayInjector.h"
#include "MCP/Python/N2CPythonBridge.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "BlueprintEditor.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Runtime/Launch/Resources/Version.h"

namespace N2CEditorFocusSpec
{
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

	FString NameOf(const TSharedPtr<FBlueprintEditor>& Editor)
	{
		const UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
		return Blueprint ? Blueprint->GetName() : FString(TEXT("(none)"));
	}
}

/**
 * The focused Blueprint editor follows the user (card 0150, review editor-integration-1, editor-integration-9,
 * imp-mcp-tools-graph-functions-11, imp-python-bridge-14): it is the Blueprint editor last activated through the tab
 * and focus events a click raises, not the last one the overlay injector touched. Opening an already open Blueprint
 * through a tool returns and focuses that Blueprint's own editor, and closing the focused editor falls back to the
 * most recently activated one still open. The MCP tools and the Python bridge read the same focus.
 */
BEGIN_DEFINE_SPEC(FN2CEditorFocusSpec, "NodeToCode.Editor.Focus", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CEditorFocusSpec)

void FN2CEditorFocusSpec::Define()
{
	using namespace N2CEditorFocusSpec;

	Describe(TEXT("without opening an editor"), [this]()
	{
		It(TEXT("finds no editor for no Blueprint, for a Blueprint that is not open, or for a tab outside every editor"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CFocusNotOpen"));
			TestFalse(TEXT("GetEditorFor(nullptr)"), FN2CBlueprintEditorTracker::GetEditorFor(nullptr).IsValid());
			TestFalse(TEXT("GetEditorFor a Blueprint with no editor"), FN2CBlueprintEditorTracker::GetEditorFor(Blueprint.GetBlueprint()).IsValid());
			TestFalse(TEXT("FindEditorForTab(nullptr)"), FN2CBlueprintEditorTracker::FindEditorForTab(nullptr).IsValid());

			const TSharedRef<SDockTab> LooseTab = SNew(SDockTab);
			TestFalse(TEXT("FindEditorForTab of a tab in no editor"), FN2CBlueprintEditorTracker::FindEditorForTab(LooseTab).IsValid());

			FN2CBlueprintEditorTracker& Tracker = FN2CBlueprintEditorTracker::Get();
			const TSharedPtr<FBlueprintEditor> Before = Tracker.GetFocusedEditor();
			TestFalse(TEXT("activating a tab outside every Blueprint editor changes nothing"), Tracker.HandleTabActivated(LooseTab).IsValid());
			TestTrue(TEXT("the focus stays where it was"), Tracker.GetFocusedEditor() == Before);
		});
	});

	Describe(TEXT("with three Blueprint editors open"), [this]()
	{
		It(TEXT("follows tab clicks, tool opens and closes, and the MCP tools and the bridge read the same Blueprint"), [this]()
		{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
			// UE 5.6 and 5.7 under -nullrhi cannot close a Blueprint editor's window (closing it, or a later Slate
			// layout pass while it is open, asks the null native window for its restored size: a fatal error in
			// GenericWindow.cpp). This case runs on 5.8; the same behaviour is checked live on the others.
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"));
#else
			UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (!TestNotNull(TEXT("asset editor subsystem"), AssetEditors))
			{
				return;
			}
			FN2CBlueprintEditorTracker& Tracker = FN2CBlueprintEditorTracker::Get();

			// Under /Game so open-blueprint-asset accepts their paths
			N2CTest::FTestBlueprint A(TEXT("BP_N2CFocusA"), TEXT("/Game"));
			N2CTest::FTestBlueprint B(TEXT("BP_N2CFocusB"), TEXT("/Game"));
			N2CTest::FTestBlueprint C(TEXT("BP_N2CFocusC"), TEXT("/Game"));
			N2CTest::FTestBlueprint* const All[] = { &A, &B, &C };
			ON_SCOPE_EXIT
			{
				for (N2CTest::FTestBlueprint* Each : All)
				{
					Each->GetPackage()->SetDirtyFlag(false);
					AssetEditors->CloseAllEditorsForAsset(Each->GetBlueprint());
				}
			};

			// Opening each through the tool path returns that Blueprint's own editor and focuses it
			TMap<const N2CTest::FTestBlueprint*, TSharedPtr<FBlueprintEditor>> Editors;
			for (N2CTest::FTestBlueprint* Each : All)
			{
				TSharedPtr<IBlueprintEditor> Opened;
				FString OpenError;
				if (!TestTrue(FString::Printf(TEXT("%s opens: %s"), *Each->GetBlueprint()->GetName(), *OpenError),
					FN2CMcpBlueprintUtils::OpenBlueprintEditor(Each->GetBlueprint(), Opened, OpenError)))
				{
					return;
				}
				const TSharedPtr<FBlueprintEditor> Editor = FN2CBlueprintEditorTracker::GetEditorFor(Each->GetBlueprint());
				if (!TestTrue(TEXT("GetEditorFor finds the editor just opened"), Editor.IsValid()))
				{
					return;
				}
				TestTrue(TEXT("OpenBlueprintEditor returns that Blueprint's editor"), StaticCastSharedPtr<FBlueprintEditor>(Opened) == Editor);
				TestTrue(TEXT("the editor edits that Blueprint"), Editor->GetBlueprintObj() == Each->GetBlueprint());
				TestEqual(TEXT("the editor just opened is focused"), NameOf(Tracker.GetFocusedEditor()), Each->GetBlueprint()->GetName());
				Editor->JumpToHyperlink(Each->EventGraph(), false);
				Editors.Add(Each, Editor);
			}
			TestEqual(TEXT("three distinct editors"), TSet<TSharedPtr<FBlueprintEditor>>(
				{ Editors[&A], Editors[&B], Editors[&C] }).Num(), 3);
			TestTrue(TEXT("GetOpenBlueprintEditors lists all three"),
				FN2CBlueprintEditorTracker::GetOpenBlueprintEditors().Contains(Editors[&A])
				&& FN2CBlueprintEditorTracker::GetOpenBlueprintEditors().Contains(Editors[&B])
				&& FN2CBlueprintEditorTracker::GetOpenBlueprintEditors().Contains(Editors[&C]));

			for (N2CTest::FTestBlueprint* Each : All)
			{
				const TSharedPtr<SDockTab> AssetTab = AssetTabOf(Editors[Each]);
				const TSharedPtr<SDockTab> GraphTab = GraphTabOf(Editors[Each], Each->EventGraph());
				TestTrue(FString::Printf(TEXT("%s has an asset tab"), *Each->GetBlueprint()->GetName()), AssetTab.IsValid());
				TestTrue(FString::Printf(TEXT("%s has an event graph tab"), *Each->GetBlueprint()->GetName()), GraphTab.IsValid());
				TestTrue(TEXT("FindEditorForTab maps the asset tab to its editor"), FN2CBlueprintEditorTracker::FindEditorForTab(AssetTab) == Editors[Each]);
				TestTrue(TEXT("FindEditorForTab maps the graph tab to its editor"), FN2CBlueprintEditorTracker::FindEditorForTab(GraphTab) == Editors[Each]);
				AddInfo(FString::Printf(TEXT("%s: asset tab in front %d, window %p"), *Each->GetBlueprint()->GetName(),
					AssetTab.IsValid() && AssetTab->IsForeground(), AssetTab.IsValid() ? AssetTab->GetParentWindow().Get() : nullptr));
			}

			// What a click raises: on an asset tab, SDockTab::ActivateInParent(UserClickedOnTab) (its tab well then
			// broadcasts OnTabForegrounded when the tab was behind another); inside a graph, the tab stack's
			// FGlobalTabmanager::SetActiveTab (OnActiveTabChanged)
			const auto ClickAssetTab = [&](N2CTest::FTestBlueprint& Which)
			{
				if (const TSharedPtr<SDockTab> Tab = AssetTabOf(Editors[&Which]))
				{
					Tab->ActivateInParent(ETabActivationCause::UserClickedOnTab);
				}
			};
			const auto ClickInGraph = [&](N2CTest::FTestBlueprint& Which)
			{
				FGlobalTabmanager::Get()->SetActiveTab(GraphTabOf(Editors[&Which], Which.EventGraph()));
			};
			const auto ExpectFocus = [&](const FString& What, N2CTest::FTestBlueprint& Which)
			{
				const FString Expected = Which.GetBlueprint()->GetName();
				TestEqual(What + TEXT(": tracked editor"), NameOf(Tracker.GetFocusedEditor()), Expected);
				TestEqual(What + TEXT(": FN2CEditorIntegration::GetActiveBlueprintEditor"), NameOf(FN2CEditorIntegration::Get().GetActiveBlueprintEditor()), Expected);
				TestTrue(What + TEXT(": focused graph is that Blueprint's"), FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor() == Which.EventGraph());

				// The MCP tools read the same Blueprint
				const TSharedPtr<FJsonObject> Focused = N2CTest::ParseJsonObject(N2CTest::ResultText(N2CTest::CallTool(TEXT("get-focused-blueprint"), {})));
				const TSharedPtr<FJsonObject>* Metadata = nullptr;
				TestEqual(What + TEXT(": get-focused-blueprint reads"), Focused.IsValid() && Focused->TryGetObjectField(TEXT("metadata"), Metadata)
					? (*Metadata)->GetStringField(TEXT("name")) : FString(TEXT("(no metadata)")), Expected);
				const TSharedPtr<FJsonObject> Open = N2CTest::ParseJsonObject(N2CTest::ResultText(N2CTest::CallTool(TEXT("get-open-blueprint-editors"), {})));
				int32 FocusedCount = 0;
				FString FocusedName;
				const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
				if (Open.IsValid() && Open->TryGetArrayField(TEXT("editors"), List))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *List)
					{
						bool bFocused = false;
						if (Entry->AsObject()->TryGetBoolField(TEXT("isFocused"), bFocused) && bFocused)
						{
							++FocusedCount;
							FocusedName = Entry->AsObject()->GetStringField(TEXT("blueprintName"));
						}
					}
				}
				TestEqual(What + TEXT(": get-open-blueprint-editors marks one editor focused"), FocusedCount, 1);
				TestEqual(What + TEXT(": get-open-blueprint-editors marks it"), FocusedName, Expected);

				// So does the Python bridge
				const TSharedPtr<FJsonObject> Bridge = N2CTest::ParseJsonObject(UN2CPythonBridge::GetFocusedBlueprintJson());
				const TSharedPtr<FJsonObject>* Data = nullptr;
				TestTrue(What + TEXT(": the bridge reads ") + Expected, Bridge.IsValid() && Bridge->TryGetObjectField(TEXT("data"), Data)
					&& (*Data)->GetStringField(TEXT("name")) == Expected);
			};

			// C was opened last
			ExpectFocus(TEXT("after opening C"), C);

			// Clicking another Blueprint's asset tab, then inside its graph, moves the focus there. Each click on its
			// own is checked with the click inside the graph; the asset tab click alone where it brings a hidden tab
			// to the front (the tabs share a tab well)
			for (N2CTest::FTestBlueprint* Target : { &A, &B, &C, &A })
			{
				const bool bWasBehind = AssetTabOf(Editors[Target]).IsValid() && !AssetTabOf(Editors[Target])->IsForeground();
				ClickAssetTab(*Target);
				if (bWasBehind)
				{
					ExpectFocus(FString::Printf(TEXT("asset tab of %s clicked"), *Target->GetBlueprint()->GetName()), *Target);
				}
				ClickInGraph(*Target);
				ExpectFocus(FString::Printf(TEXT("clicked in the graph of %s"), *Target->GetBlueprint()->GetName()), *Target);
			}

			// A tab in an editor whose asset tab is behind another (a background rebuild) does not take the focus;
			// clicking a tab outside every Blueprint editor does not either
			if (AssetTabOf(Editors[&B]).IsValid() && !AssetTabOf(Editors[&B])->IsForeground())
			{
				TestFalse(TEXT("a background editor's graph tab does not take the focus"),
					Tracker.HandleTabActivated(GraphTabOf(Editors[&B], B.EventGraph())).IsValid());
				ExpectFocus(TEXT("after a background tab event"), A);
			}
			else
			{
				AddInfo(TEXT("The asset tabs are each in front of their own window here: the background-tab case needs a shared tab well"));
			}
			Tracker.HandleTabActivated(SNew(SDockTab));
			ExpectFocus(TEXT("after a tab outside every editor"), A);

			// The overlay upkeep no longer moves the focus: a foregrounded graph tab of the focused editor's Blueprint
			// keeps it, and the injection loops run without changing it
			FN2CGraphOverlayInjector::Get().InjectForFocusedGraph(Editors[&C]);
			ExpectFocus(TEXT("after injecting C's overlay"), A);

			// Opening an already open Blueprint through a tool returns its own editor and focuses it (card 0145 saw
			// the graph go into the previously focused Blueprint's editor)
			{
				TSharedPtr<IBlueprintEditor> Reopened;
				FString OpenError;
				TestTrue(TEXT("B re-opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(B.GetBlueprint(), Reopened, OpenError));
				TestTrue(TEXT("re-opening B returns B's editor"), StaticCastSharedPtr<FBlueprintEditor>(Reopened) == Editors[&B]);
				ExpectFocus(TEXT("after re-opening B"), B);
			}
			{
				// B's asset tab already in front while another Blueprint has the focus (two editor windows, the user
				// last clicked in C's): re-opening B raises no tab event, and must still return and focus B's editor
				Tracker.NotifyEditorActivated(Editors[&C]);
				TestTrue(TEXT("B's asset tab is in front"), AssetTabOf(Editors[&B])->IsForeground());
				TestEqual(TEXT("C has the focus"), NameOf(Tracker.GetFocusedEditor()), C.GetBlueprint()->GetName());
				TSharedPtr<IBlueprintEditor> Reopened;
				FString OpenError;
				TestTrue(TEXT("B re-opens while in front: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(B.GetBlueprint(), Reopened, OpenError));
				TestTrue(TEXT("re-opening B in front returns B's editor, not the focused C's"), StaticCastSharedPtr<FBlueprintEditor>(Reopened) == Editors[&B]);
				ExpectFocus(TEXT("after re-opening B in front"), B);
			}
			{
				const FString Result = N2CTest::ResultText(N2CTest::CallTool(TEXT("open-blueprint-asset"),
					{ { TEXT("asset_path"), C.GetBlueprint()->GetPathName() }, { TEXT("focus_graph"), TEXT("EventGraph") } }));
				TestTrue(TEXT("open-blueprint-asset on C succeeds: ") + Result.Left(200), Result.Contains(C.GetBlueprint()->GetName()));
				ExpectFocus(TEXT("after open-blueprint-asset C"), C);
			}
			{
				const TSharedPtr<FJsonObject> Bridge = N2CTest::ParseJsonObject(UN2CPythonBridge::OpenBlueprint(A.GetBlueprint()->GetPathName(), A.EventGraph()->GetName()));
				const TSharedPtr<FJsonObject>* Data = nullptr;
				if (TestTrue(TEXT("the bridge's OpenBlueprint on A succeeds"), Bridge.IsValid() && Bridge->TryGetObjectField(TEXT("data"), Data)))
				{
					TestEqual(TEXT("OpenBlueprint reports the graph the editor shows"), (*Data)->GetStringField(TEXT("focusedGraph")), A.EventGraph()->GetName());
				}
				ExpectFocus(TEXT("after the bridge opened A"), A);
				TestTrue(TEXT("C's editor still shows C's graph"), Editors[&C]->GetFocusedGraph() == C.EventGraph());
				TestTrue(TEXT("B's editor still shows B's graph"), Editors[&B]->GetFocusedGraph() == B.EventGraph());
			}

			// No activation recorded (editors opened before the tracker started): the open Blueprint editor with the
			// latest IAssetEditorInstance::GetLastActivationTime()
			{
				Tracker.Shutdown();
				const TSharedPtr<FBlueprintEditor> Fallback = Tracker.GetFocusedEditor();
				Tracker.Initialize();
				double LatestTime = -1.0;
				for (N2CTest::FTestBlueprint* Each : All)
				{
					LatestTime = FMath::Max(LatestTime, Editors[Each]->GetLastActivationTime());
				}
				if (TestTrue(TEXT("with no history the fallback is an open Blueprint editor"), Fallback.IsValid()
					&& (Fallback == Editors[&A] || Fallback == Editors[&B] || Fallback == Editors[&C])))
				{
					TestEqual(TEXT("the fallback is the latest activated"), Fallback->GetLastActivationTime(), LatestTime);
				}
				Tracker.NotifyEditorActivated(Editors[&A]);
			}

			// Closing the focused editor's tab brings another asset tab to the front: that Blueprint, now in view,
			// takes the focus
			A.GetPackage()->SetDirtyFlag(false);
			AssetEditors->CloseAllEditorsForAsset(A.GetBlueprint());
			TestFalse(TEXT("A's editor is gone"), FN2CBlueprintEditorTracker::GetEditorFor(A.GetBlueprint()).IsValid());
			N2CTest::FTestBlueprint* InFront = AssetTabOf(Editors[&B])->IsForeground() ? &B : &C;
			N2CTest::FTestBlueprint* Behind = InFront == &B ? &C : &B;
			TestTrue(TEXT("one of B and C is in front after A closed"), AssetTabOf(Editors[InFront])->IsForeground());
			ExpectFocus(TEXT("after closing A"), *InFront);

			// The focused editor closing without any tab coming to the front (it was behind another): the focus falls
			// back to the most recently activated editor still open. The stale pointer is never returned
			Tracker.NotifyEditorActivated(Editors[Behind]);
			TestFalse(TEXT("the editor about to close is behind another"), AssetTabOf(Editors[Behind])->IsForeground());
			Behind->GetPackage()->SetDirtyFlag(false);
			AssetEditors->CloseAllEditorsForAsset(Behind->GetBlueprint());
			ExpectFocus(TEXT("after the focused editor closed behind another"), *InFront);

			InFront->GetPackage()->SetDirtyFlag(false);
			AssetEditors->CloseAllEditorsForAsset(InFront->GetBlueprint());
			if (FN2CBlueprintEditorTracker::GetOpenBlueprintEditors().IsEmpty())
			{
				TestFalse(TEXT("no focused editor once no Blueprint editor is open"), Tracker.GetFocusedEditor().IsValid());
			}
#endif
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
