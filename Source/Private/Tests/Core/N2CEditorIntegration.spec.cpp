// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CEditorIntegration.h"
#include "BlueprintEditor.h"
#include "Containers/Ticker.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/App.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"

/**
 * The focused Blueprint editor and the graph overlays follow the user in a real, windowed editor (card 0203, review
 * imp-editor-integration-10, editor-integration-1): two Blueprints opened as the Content Browser opens them, then the
 * user switches between them and into a function graph. After each switch, on the editor's own ticks (the overlay is
 * injected by timers: 0.5 s after an editor opens, 0.1 s after a graph tab comes to the front, and the 2 s poll),
 * GetActiveBlueprintEditor() is the Blueprint the user switched to, get-focused-blueprint reads it, and the graph it
 * shows has an overlay. Closing the focused Blueprint hands the focus to the one left in front.
 *
 * It needs a rendering editor: under -nullrhi (build.ps1 -Test) it only reports that it was skipped. Run it in a
 * windowed editor: UnrealEditor.exe <host>.uproject -ExecCmds="Automation RunTests NodeToCode.Windowed"
 * -TestExit="Automation Test Queue Empty". The headless version of the focus rules (tab events raised directly, UE 5.8)
 * is NodeToCode.Editor.Focus; one overlay per graph for same-GUID graphs is NodeToCode.Editor.OverlayPerGraph.
 */
namespace N2CWindowedIntegrationSpec
{
	FString NameOf(const TSharedPtr<FBlueprintEditor>& Editor)
	{
		const UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
		return Blueprint ? Blueprint->GetName() : FString(TEXT("(none)"));
	}

	TSharedPtr<SDockTab> AssetTabOf(const TSharedPtr<FBlueprintEditor>& Editor)
	{
		const TSharedPtr<FTabManager> TabManager = Editor.IsValid() ? Editor->GetTabManager() : nullptr;
		return TabManager.IsValid() ? TabManager->GetOwnerTab() : nullptr;
	}

	TSharedPtr<SDockTab> GraphTabOf(const TSharedPtr<FBlueprintEditor>& Editor, UEdGraph* Graph)
	{
		TArray<TSharedPtr<SDockTab>> Tabs;
		if (Editor.IsValid() && Graph)
		{
			Editor->FindOpenTabsContainingDocument(Graph, Tabs);
		}
		return Tabs.Num() > 0 ? Tabs[0] : nullptr;
	}

	/** One user action and what must hold, on a later tick, once the editor has caught up with it */
	struct FStep
	{
		FString Name;
		TFunction<void()> Act;
		TFunction<bool()> Settled;
		TFunction<void(const FString&)> Check;
	};
}

BEGIN_DEFINE_SPEC(FN2CWindowedIntegrationSpec, "NodeToCode.Windowed.Integration", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FTestBlueprint> A;
	TUniquePtr<N2CTest::FTestBlueprint> B;
	UEdGraph* FunctionGraph = nullptr;
	TArray<N2CWindowedIntegrationSpec::FStep> Steps;

	static constexpr double StepTimeoutSeconds = 15.0;

	void CloseAll()
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		for (TUniquePtr<N2CTest::FTestBlueprint>* Each : { &A, &B })
		{
			if (Each->IsValid() && AssetEditors)
			{
				(*Each)->GetPackage()->SetDirtyFlag(false);
				AssetEditors->CloseAllEditorsForAsset((*Each)->GetBlueprint());
			}
			Each->Reset();
		}
		FunctionGraph = nullptr;
		Steps.Reset();
	}

	/** Runs Steps[Index] and, once it has settled (or timed out), checks it and runs the next; Done after the last */
	void RunStep(int32 Index, const FDoneDelegate Done)
	{
		if (!Steps.IsValidIndex(Index))
		{
			Done.Execute();
			return;
		}
		const N2CWindowedIntegrationSpec::FStep& Step = Steps[Index];
		Step.Act();
		const double Deadline = FPlatformTime::Seconds() + StepTimeoutSeconds;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, Index, Done, Deadline](float)
		{
			if (!Steps.IsValidIndex(Index))
			{
				return false; // the case timed out and AfterEach cleared it
			}
			const N2CWindowedIntegrationSpec::FStep& Current = Steps[Index];
			const bool bSettled = Current.Settled();
			if (!bSettled && FPlatformTime::Seconds() < Deadline)
			{
				return true;
			}
			if (!bSettled)
			{
				AddError(FString::Printf(TEXT("%s: did not settle within %.0f s"), *Current.Name, StepTimeoutSeconds));
			}
			Current.Check(Current.Name);
			if (HasAnyErrors())
			{
				Done.Execute(); // later steps start from a state this one did not reach
			}
			else
			{
				RunStep(Index + 1, Done);
			}
			return false;
		}));
	}

	/** The Blueprint get-focused-blueprint reports */
	static FString ToolFocusedName()
	{
		const TSharedPtr<FJsonObject> Focused = N2CTest::ParseJsonObject(N2CTest::ResultText(N2CTest::CallTool(TEXT("get-focused-blueprint"), {})));
		const TSharedPtr<FJsonObject>* Metadata = nullptr;
		return Focused.IsValid() && Focused->TryGetObjectField(TEXT("metadata"), Metadata)
			? (*Metadata)->GetStringField(TEXT("name")) : FString(TEXT("(no metadata)"));
	}

END_DEFINE_SPEC(FN2CWindowedIntegrationSpec)

void FN2CWindowedIntegrationSpec::Define()
{
	using namespace N2CWindowedIntegrationSpec;

	if (!FApp::CanEverRender())
	{
		It(TEXT("is skipped without rendering (-nullrhi): it runs in a windowed editor"), [this]()
		{
			AddInfo(TEXT("Skipped under -nullrhi: run NodeToCode.Windowed in a windowed editor (see the spec's header)"));
		});
		return;
	}

	AfterEach([this]()
	{
		CloseAll();
	});

	LatentIt(TEXT("follows the user between two Blueprints and into a function graph: focused editor, MCP focus and overlays"),
		FTimespan::FromSeconds(120), [this](const FDoneDelegate& Done)
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!TestNotNull(TEXT("asset editor subsystem"), AssetEditors))
		{
			Done.Execute();
			return;
		}

		// Under /Game, like the user's Blueprints (and so get-focused-blueprint reads them)
		A = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CWindowedA"), TEXT("/Game"));
		B = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CWindowedB"), TEXT("/Game"));
		FunctionGraph = A->AddFunction(TEXT("WindowedFunction"));
		// A node in each event graph, so neither opens as a data-only Blueprint (class defaults, no graph)
		A->AddCustomEvent(A->EventGraph(), TEXT("WindowedEventA"));
		B->AddCustomEvent(B->EventGraph(), TEXT("WindowedEventB"));
		if (!TestNotNull(TEXT("A's function graph"), FunctionGraph))
		{
			Done.Execute();
			return;
		}

		UBlueprint* BlueprintA = A->GetBlueprint();
		UBlueprint* BlueprintB = B->GetBlueprint();

		const auto EditorOf = [](UBlueprint* Blueprint) { return FN2CBlueprintEditorTracker::GetEditorFor(Blueprint); };
		const auto ShownGraph = [EditorOf](UBlueprint* Blueprint) -> UEdGraph*
		{
			const TSharedPtr<FBlueprintEditor> Editor = EditorOf(Blueprint);
			return Editor.IsValid() ? Editor->GetFocusedGraph() : nullptr;
		};
		// What a user's click raises: the asset tab comes to the front, then the graph tab becomes the active tab
		const auto ClickInto = [EditorOf](UBlueprint* Blueprint, UEdGraph* Graph)
		{
			const TSharedPtr<FBlueprintEditor> Editor = EditorOf(Blueprint);
			if (const TSharedPtr<SDockTab> AssetTab = AssetTabOf(Editor))
			{
				AssetTab->ActivateInParent(ETabActivationCause::UserClickedOnTab);
			}
			if (Editor.IsValid() && Graph)
			{
				Editor->JumpToHyperlink(Graph, false);
				if (const TSharedPtr<SDockTab> GraphTab = GraphTabOf(Editor, Graph))
				{
					GraphTab->ActivateInParent(ETabActivationCause::UserClickedOnTab);
					FGlobalTabmanager::Get()->SetActiveTab(GraphTab);
				}
			}
		};
		// Settled: the user's Blueprint is the focused one and the graph it shows has a live overlay
		const auto FocusedWithOverlay = [ShownGraph](UBlueprint* Blueprint, UEdGraph* Graph)
		{
			return [ShownGraph, Blueprint, Graph]()
			{
				const FN2CEditorIntegration& Integration = FN2CEditorIntegration::Get();
				const TSharedPtr<FBlueprintEditor> Active = Integration.GetActiveBlueprintEditor();
				UEdGraph* Shown = ShownGraph(Blueprint);
				return Active.IsValid() && Active->GetBlueprintObj() == Blueprint && Shown
					&& (!Graph || Shown == Graph) && Integration.HasLiveOverlay(Shown);
			};
		};
		const auto ExpectFocus = [this, ShownGraph](UBlueprint* Blueprint, UEdGraph* Graph)
		{
			return [this, ShownGraph, Blueprint, Graph](const FString& What)
			{
				const FN2CEditorIntegration& Integration = FN2CEditorIntegration::Get();
				const FN2CBlueprintEditorTracker& Tracker = FN2CBlueprintEditorTracker::Get();
				const FString Expected = Blueprint->GetName();
				TestEqual(What + TEXT(": GetActiveBlueprintEditor"), NameOf(Integration.GetActiveBlueprintEditor()), Expected);
				TestEqual(What + TEXT(": the tracker"), NameOf(Tracker.GetFocusedEditor()), Expected);
				UEdGraph* Shown = ShownGraph(Blueprint);
				if (Graph)
				{
					TestTrue(What + TEXT(": the editor shows the graph the user picked"), Shown == Graph);
				}
				TestTrue(What + TEXT(": GetFocusedGraphFromActiveEditor is that graph"), Shown && Integration.GetFocusedGraphFromActiveEditor() == Shown);
				TestTrue(What + TEXT(": that graph has an overlay"), Integration.HasLiveOverlay(Shown));
				TestEqual(What + TEXT(": get-focused-blueprint reads it"), ToolFocusedName(), Expected);
			};
		};

		Steps = {
			{ TEXT("A opened from the Content Browser"),
				[AssetEditors, ClickInto, BlueprintA, this]()
				{
					AssetEditors->OpenEditorForAsset(BlueprintA);
					ClickInto(BlueprintA, A->EventGraph()); // a Blueprint never edited before opens no graph tab
				},
				FocusedWithOverlay(BlueprintA, A->EventGraph()), ExpectFocus(BlueprintA, A->EventGraph()) },
			{ TEXT("B opened from the Content Browser"),
				[AssetEditors, ClickInto, BlueprintB, this]()
				{
					AssetEditors->OpenEditorForAsset(BlueprintB);
					ClickInto(BlueprintB, B->EventGraph());
				},
				FocusedWithOverlay(BlueprintB, B->EventGraph()), ExpectFocus(BlueprintB, B->EventGraph()) },
			{ TEXT("back to A's event graph"),
				[ClickInto, BlueprintA, this]() { ClickInto(BlueprintA, A->EventGraph()); },
				FocusedWithOverlay(BlueprintA, A->EventGraph()), ExpectFocus(BlueprintA, A->EventGraph()) },
			{ TEXT("into A's function graph"),
				[ClickInto, BlueprintA, this]() { ClickInto(BlueprintA, FunctionGraph); },
				FocusedWithOverlay(BlueprintA, FunctionGraph), ExpectFocus(BlueprintA, FunctionGraph) },
			{ TEXT("over to B's event graph"),
				[ClickInto, BlueprintB, this]() { ClickInto(BlueprintB, B->EventGraph()); },
				FocusedWithOverlay(BlueprintB, B->EventGraph()), ExpectFocus(BlueprintB, B->EventGraph()) },
			{ TEXT("B closed"),
				[AssetEditors, BlueprintB, this]()
				{
					B->GetPackage()->SetDirtyFlag(false);
					AssetEditors->CloseAllEditorsForAsset(BlueprintB);
				},
				FocusedWithOverlay(BlueprintA, nullptr),
				[this, BlueprintB, Check = ExpectFocus(BlueprintA, nullptr)](const FString& What)
				{
					TestFalse(What + TEXT(": B's editor is gone"), FN2CBlueprintEditorTracker::GetEditorFor(BlueprintB).IsValid());
					Check(What);
				} },
		};
		RunStep(0, Done);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
