// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Core/N2CBlueprintEditorTracker.h"
#include "Core/N2CGraphOverlayInjector.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "BlueprintEditor.h"
#include "Containers/Ticker.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Docking/TabManager.h"
#include "GraphEditor.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"

/**
 * Graph overlays go into the exact graph editor that shows their graph, once, quietly, and come out again (card 0293,
 * review editor-integration-10, editor-integration-18, imp-editor-integration-2; card 0500), and the injector's own
 * events keep it at one per graph editor as tabs switch (card 0344, FN2CGraphOverlayInjector). Headless Blueprint
 * editors, so these run on UE 5.8 (N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR); the windowed check of the same rules is
 * NodeToCode.Windowed.Integration.
 */
namespace N2COverlayInjectionTest
{
	/** Overlays of ours anywhere below Widget */
	int32 CountOverlays(const TSharedRef<SWidget>& Widget)
	{
		static const FName OverlayType(TEXT("SN2CGraphOverlay"));
		int32 Count = Widget->GetType() == OverlayType ? 1 : 0;
		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				Count += CountOverlays(Children->GetChildAt(Index));
			}
		}
		return Count;
	}

	/** Collects LogNodeToCode lines at Warning or worse while it lives */
	class FWarningCapture : public FOutputDevice
	{
	public:
		TArray<FString> Lines;

		FWarningCapture() { GLog->AddOutputDevice(this); }
		virtual ~FWarningCapture() override { GLog->RemoveOutputDevice(this); }

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Level = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			if (Category == FName(TEXT("LogNodeToCode")) && Level <= ELogVerbosity::Warning)
			{
				Lines.Add(Message);
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
	};

	/** A Blueprint with its event graph and a function graph open in their own document tabs */
	struct FOpenBlueprint
	{
		TUniquePtr<N2CTest::FTestBlueprint> Blueprint;
		TSharedPtr<FBlueprintEditor> Editor;
		UEdGraph* EventGraph = nullptr;
		UEdGraph* FunctionGraph = nullptr;
		TSharedPtr<SDockTab> EventTab;
		TSharedPtr<SDockTab> FunctionTab;

		~FOpenBlueprint()
		{
			if (Blueprint.IsValid())
			{
				Blueprint->GetPackage()->SetDirtyFlag(false);
				if (UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
				{
					AssetEditors->CloseAllEditorsForAsset(Blueprint->GetBlueprint());
				}
			}
		}

		static TSharedPtr<SDockTab> TabOf(const TSharedPtr<FBlueprintEditor>& Editor, UEdGraph* Graph)
		{
			TArray<TSharedPtr<SDockTab>> Tabs;
			Editor->FindOpenTabsContainingDocument(Graph, Tabs);
			return Tabs.Num() > 0 ? Tabs[0] : nullptr;
		}

		/** bOnlyPlaceholders: the event graph keeps only the editor's greyed-out default events, as in a new Blueprint */
		bool Open(FAutomationTestBase& Test, const TCHAR* Name, bool bOnlyPlaceholders = false)
		{
			Blueprint = MakeUnique<N2CTest::FTestBlueprint>(Name, TEXT("/Game"));
			EventGraph = Blueprint->EventGraph();
			FunctionGraph = Blueprint->AddFunction(TEXT("OverlayFunction"));
			if (!bOnlyPlaceholders)
			{
				Blueprint->AddCustomEvent(EventGraph, TEXT("OverlayEvent"));
			}
			TSharedPtr<IBlueprintEditor> Opened;
			FString OpenError;
			if (!Test.TestTrue(FString::Printf(TEXT("%s opens: %s"), Name, *OpenError),
				FN2CMcpBlueprintUtils::OpenBlueprintEditor(Blueprint->GetBlueprint(), Opened, OpenError)))
			{
				return false;
			}
			Editor = StaticCastSharedPtr<FBlueprintEditor>(Opened);
			Editor->JumpToHyperlink(EventGraph, false);
			Editor->JumpToHyperlink(FunctionGraph, false);
			EventTab = TabOf(Editor, EventGraph);
			FunctionTab = TabOf(Editor, FunctionGraph);
			return Test.TestTrue(TEXT("the event graph and the function graph each have a tab"),
				EventTab.IsValid() && FunctionTab.IsValid() && EventTab != FunctionTab);
		}
	};
}

BEGIN_DEFINE_SPEC(FN2COverlayInjectionSpec, "NodeToCode.Editor.OverlayInjection", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2COverlayInjectionSpec)

void FN2COverlayInjectionSpec::Define()
{
	using namespace N2COverlayInjectionTest;

#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
	It(TEXT("is skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"), [this]()
	{
		AddInfo(TEXT("Skipped below UE 5.8: the windowed NodeToCode.Windowed.Integration checks overlays on every engine"));
	});
#else
	It(TEXT("puts a graph's overlay only into the graph editor that shows that graph, once"), [this]()
	{
		FOpenBlueprint Open;
		if (!Open.Open(*this, TEXT("BP_N2COverlayExact")))
		{
			return;
		}
		FN2CGraphOverlayInjector& Injector = FN2CGraphOverlayInjector::Get();

		// The function graph's overlay offered to the event graph's tab: refused (review editor-integration-10, where the
		// fallback put a graph's overlay into another graph's tab)
		TestFalse(TEXT("a tab showing another graph does not take this graph's overlay"),
			Injector.InjectIntoTab(Open.EventTab, Open.FunctionGraph, Open.Editor));
		TestFalse(TEXT("the function graph has no overlay"), Injector.HasLiveOverlay(Open.FunctionGraph));
		TestEqual(TEXT("no overlay in the event graph's tab"), CountOverlays(Open.EventTab->GetContent()), 0);

		// The graph editor that shows the graph, found exactly
		const TSharedPtr<SGraphEditor> FunctionEditor = FN2CGraphOverlayInjector::FindGraphEditorFor(Open.Editor, Open.FunctionGraph);
		if (!TestTrue(TEXT("FindGraphEditorFor finds the function graph's editor"), FunctionEditor.IsValid()))
		{
			return;
		}
		TestTrue(TEXT("it is the function tab's content"), StaticCastSharedRef<SWidget>(FunctionEditor.ToSharedRef()) == Open.FunctionTab->GetContent());
		TestTrue(TEXT("it shows the function graph"), FunctionEditor->GetCurrentGraph() == Open.FunctionGraph);

		// Injected once, however often it is asked
		TestTrue(TEXT("the function tab takes its own graph's overlay"),
			Injector.InjectIntoTab(Open.FunctionTab, Open.FunctionGraph, Open.Editor));
		TestTrue(TEXT("asked again: still has one"), Injector.InjectIntoTab(Open.FunctionTab, Open.FunctionGraph, Open.Editor));
		TestTrue(TEXT("InjectForFocusedGraph on the focused function graph"), Injector.InjectForFocusedGraph(Open.Editor));
		TestTrue(TEXT("the function graph has an overlay"), Injector.HasLiveOverlay(Open.FunctionGraph));
		TestEqual(TEXT("exactly one overlay in the function graph's editor"), CountOverlays(Open.FunctionTab->GetContent()), 1);
		TestEqual(TEXT("still none in the event graph's tab"), CountOverlays(Open.EventTab->GetContent()), 0);
	});

	It(TEXT("builds and injects overlays without logging a warning"), [this]()
	{
		FOpenBlueprint Open;
		FOpenBlueprint Fresh; // only placeholder events: the overlay's token estimate captured them and warned
		if (!Open.Open(*this, TEXT("BP_N2COverlayQuiet")) || !Fresh.Open(*this, TEXT("BP_N2COverlayQuietFresh"), true))
		{
			return;
		}
		TestTrue(TEXT("the fresh Blueprint's event graph holds placeholder events only"),
			Fresh.EventGraph->Nodes.Num() > 0 && !Fresh.EventGraph->Nodes.ContainsByPredicate(
				[](const UEdGraphNode* Node) { return Node && !Node->IsAutomaticallyPlacedGhostNode(); }));
		FN2CGraphOverlayInjector& Injector = FN2CGraphOverlayInjector::Get();

		// Building and injecting overlays, and refusing a wrong tab, logs no warning (editor-integration-18, card 0500:
		// SN2CGraphOverlay::Construct wrote eight Warning traces per overlay and FindGraphEditorOverlay one more)
		TArray<FString> Warnings;
		{
			FWarningCapture Capture;
			Injector.InjectIntoTab(Open.EventTab, Open.EventGraph, Open.Editor);
			Injector.InjectIntoTab(Open.FunctionTab, Open.FunctionGraph, Open.Editor);
			Injector.InjectIntoTab(Open.EventTab, Open.FunctionGraph, Open.Editor);
			Injector.InjectForFocusedGraph(Open.Editor);
			Injector.InjectIntoTab(Fresh.EventTab, Fresh.EventGraph, Fresh.Editor);
			Warnings = Capture.Lines;
		}
		TestTrue(TEXT("both graphs have overlays"), Injector.HasLiveOverlay(Open.EventGraph) && Injector.HasLiveOverlay(Open.FunctionGraph));
		TestTrue(TEXT("the fresh Blueprint's event graph has an overlay"), Injector.HasLiveOverlay(Fresh.EventGraph));
		TestEqual(FString::Printf(TEXT("LogNodeToCode warnings while injecting overlays: %s"), *FString::Join(Warnings, TEXT(" | "))),
			Warnings.Num(), 0);
	});

	It(TEXT("takes the overlays out of the graph editors on shutdown"), [this]()
	{
		FOpenBlueprint Open;
		if (!Open.Open(*this, TEXT("BP_N2COverlayRemoved")))
		{
			return;
		}
		FN2CGraphOverlayInjector& Injector = FN2CGraphOverlayInjector::Get();
		Injector.InjectIntoTab(Open.EventTab, Open.EventGraph, Open.Editor);
		Injector.InjectIntoTab(Open.FunctionTab, Open.FunctionGraph, Open.Editor);
		TestEqual(TEXT("an overlay in each graph editor"),
			CountOverlays(Open.EventTab->GetContent()) + CountOverlays(Open.FunctionTab->GetContent()), 2);

		// What Shutdown runs: the slots come out of the engine's graph editors (no widget of an unloaded module stays)
		Injector.RemoveInjectedOverlays();
		TestEqual(TEXT("no overlay left in the event graph's editor"), CountOverlays(Open.EventTab->GetContent()), 0);
		TestEqual(TEXT("no overlay left in the function graph's editor"), CountOverlays(Open.FunctionTab->GetContent()), 0);
		TestFalse(TEXT("HasLiveOverlay is false afterwards"), Injector.HasLiveOverlay(Open.EventGraph) || Injector.HasLiveOverlay(Open.FunctionGraph));

		// And they come back on the next injection (the module keeps running in this editor)
		TestTrue(TEXT("re-injected after removal"), Injector.InjectIntoTab(Open.EventTab, Open.EventGraph, Open.Editor));
		TestEqual(TEXT("one overlay again"), CountOverlays(Open.EventTab->GetContent()), 1);
	});

	// The injector on its own (card 0344): the tab events and timers it subscribes to, nothing called by hand. Switching
	// tabs twice leaves exactly one overlay in each graph editor, and switching back injects nothing new
	LatentIt(TEXT("injects exactly one overlay per graph editor as the user switches tabs, from its own events"), FTimespan::FromSeconds(60.0),
		[this](const FDoneDelegate& Done)
	{
		struct FState
		{
			FOpenBlueprint Open;
			int32 Step = 0;
			double StepStart = 0.0;
			int32 CountBefore = 0;
			int32 CountAfterSecondSwitch = 0;
			TWeakPtr<SWidget> EventOverlay;
			TWeakPtr<SWidget> FunctionOverlay;
		};
		const TSharedRef<FState> State = MakeShared<FState>();
		FN2CGraphOverlayInjector& Injector = FN2CGraphOverlayInjector::Get();
		Injector.RemoveInjectedOverlays();
		State->CountBefore = Injector.GetInjectionCount();
		if (!State->Open.Open(*this, TEXT("BP_N2COverlaySwitch")))
		{
			Done.Execute();
			return;
		}
		const bool bOnlyOurs = FN2CBlueprintEditorTracker::GetOpenBlueprintEditors().Num() == 1;

		// What a click on a document tab does: bring it to the front of its well (OnTabForegrounded) and make it the
		// active tab (the Blueprint editor then focuses that graph)
		const auto Click = [](const TSharedPtr<SDockTab>& Tab)
		{
			Tab->ActivateInParent(ETabActivationCause::UserClickedOnTab);
			FGlobalTabmanager::Get()->SetActiveTab(Tab);
		};
		const auto FirstOverlay = [](const TSharedRef<SWidget>& Widget)
		{
			TFunction<TSharedPtr<SWidget>(const TSharedRef<SWidget>&)> Find = [&Find](const TSharedRef<SWidget>& Each) -> TSharedPtr<SWidget>
			{
				if (Each->GetType() == FName(TEXT("SN2CGraphOverlay")))
				{
					return Each;
				}
				if (FChildren* Children = Each->GetChildren())
				{
					for (int32 Index = 0; Index < Children->Num(); ++Index)
					{
						if (TSharedPtr<SWidget> Found = Find(Children->GetChildAt(Index)))
						{
							return Found;
						}
					}
				}
				return nullptr;
			};
			return Find(Widget);
		};

		Click(State->Open.EventTab);
		State->StepStart = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[this, State, Done, Click, FirstOverlay, bOnlyOurs](float) -> bool
		{
			FN2CGraphOverlayInjector& Injector = FN2CGraphOverlayInjector::Get();
			FOpenBlueprint& Open = State->Open;
			const double Waited = FPlatformTime::Seconds() - State->StepStart;
			const auto Finish = [&]() { Done.Execute(); return false; };

			if (State->Step == 0) // first switch: to the event graph
			{
				if (!Injector.HasLiveOverlay(Open.EventGraph))
				{
					if (Waited < 10.0)
					{
						return true;
					}
					AddError(TEXT("the event graph got no overlay within 10 s of its tab coming to the front"));
					return Finish();
				}
				Click(Open.FunctionTab);
				State->Step = 1;
				State->StepStart = FPlatformTime::Seconds();
				return true;
			}
			if (State->Step == 1) // second switch: to the function graph
			{
				if (!Injector.HasLiveOverlay(Open.FunctionGraph))
				{
					if (Waited < 10.0)
					{
						return true;
					}
					AddError(TEXT("the function graph got no overlay within 10 s of its tab coming to the front"));
					return Finish();
				}
				State->CountAfterSecondSwitch = Injector.GetInjectionCount();
				State->EventOverlay = FirstOverlay(Open.EventTab->GetContent());
				State->FunctionOverlay = FirstOverlay(Open.FunctionTab->GetContent());
				Click(Open.EventTab);
				State->Step = 2;
				State->StepStart = FPlatformTime::Seconds();
				return true;
			}
			// Back to the event graph: wait past the tab event's timer and one poll pass, then nothing may have changed
			if (Waited < 2.5)
			{
				return true;
			}
			TestEqual(TEXT("exactly one overlay in the event graph's editor"), CountOverlays(Open.EventTab->GetContent()), 1);
			TestEqual(TEXT("exactly one overlay in the function graph's editor"), CountOverlays(Open.FunctionTab->GetContent()), 1);
			TestEqual(TEXT("switching back injected nothing"), Injector.GetInjectionCount(), State->CountAfterSecondSwitch);
			const TSharedPtr<SWidget> EventOverlay = FirstOverlay(Open.EventTab->GetContent());
			TestTrue(TEXT("the event graph keeps the overlay it got first"), EventOverlay.IsValid() && EventOverlay == State->EventOverlay.Pin());
			const TSharedPtr<SWidget> FunctionOverlay = FirstOverlay(Open.FunctionTab->GetContent());
			TestTrue(TEXT("the function graph keeps the overlay it got first"), FunctionOverlay.IsValid() && FunctionOverlay == State->FunctionOverlay.Pin());
			if (bOnlyOurs)
			{
				TestEqual(TEXT("two overlays injected in all: one per graph editor"), Injector.GetInjectionCount() - State->CountBefore, 2);
			}
			else
			{
				AddInfo(TEXT("Another Blueprint editor is open: the total injection count is not checked"));
			}
			return Finish();
		}));
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
