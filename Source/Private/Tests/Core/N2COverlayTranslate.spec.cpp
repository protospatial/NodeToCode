// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/N2CEditorIntegration.h"
#include "Core/N2CEditorWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Widgets/Docking/SDockTab.h"

/**
 * Translate on the N2C bar with the Node to Code window closed (card 0521). SN2CGraphOverlay::OnTranslateClicked calls
 * FN2CEditorIntegration::RequestOverlayTranslation, whose only listener is the window's SN2CMainWindow. Before the fix
 * the request was broadcast to nobody, so the click did nothing; now the window is opened first and then receives the
 * request. Headless: the open step is replaced by a listener standing in for the window, and one case opens the real
 * NodeToCodeEditor tab (it needs Slate) with a Blueprint path that does not exist, so no provider is called.
 */
BEGIN_DEFINE_SPEC(FN2COverlayTranslateSpec, "NodeToCode.Editor.OverlayTranslate", N2C_TEST_FLAGS)

	FN2CEditorIntegration* Integration = nullptr;
	FDelegateHandle Listener;
	int32 Received = 0;
	int32 Opened = 0;
	FGuid ReceivedGuid;
	FString ReceivedName;
	FString ReceivedPath;

	void Listen()
	{
		Listener = Integration->OnOverlayTranslationRequested.AddLambda(
			[this](const FGuid& Guid, const FString& Name, const FString& Path)
			{
				++Received;
				ReceivedGuid = Guid;
				ReceivedName = Name;
				ReceivedPath = Path;
			});
	}

	static TSharedPtr<SDockTab> WindowTab()
	{
		return FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(SN2CEditorWindow::TabId));
	}

END_DEFINE_SPEC(FN2COverlayTranslateSpec)

void FN2COverlayTranslateSpec::Define()
{
	BeforeEach([this]()
	{
		Integration = &FN2CEditorIntegration::Get();
		Received = 0;
		Opened = 0;
		ReceivedGuid.Invalidate();
		ReceivedName.Reset();
		ReceivedPath.Reset();
	});

	AfterEach([this]()
	{
		Integration->OnOverlayTranslationRequested.Remove(Listener);
		Listener.Reset();
		Integration->OpenWindowForOverlayTranslation = nullptr;
	});

	It(TEXT("opens the window when it is closed, and the window gets the request"), [this]()
	{
		if (!TestFalse(TEXT("no Node to Code window listens before the click"), Integration->OnOverlayTranslationRequested.IsBound()))
		{
			return;
		}
		// The window: building it subscribes, as SN2CMainWindow's constructor does
		Integration->OpenWindowForOverlayTranslation = [this]() { ++Opened; Listen(); };

		const FGuid Guid = FGuid::NewGuid();
		TestTrue(TEXT("the request is delivered"), Integration->RequestOverlayTranslation(Guid, TEXT("EventGraph"), TEXT("/Game/N2CTest/BP_Overlay")));
		TestEqual(TEXT("the window was opened once"), Opened, 1);
		TestEqual(TEXT("the window got the request once"), Received, 1);
		TestEqual(TEXT("for the clicked graph"), ReceivedGuid, Guid);
		TestEqual(TEXT("with its name"), ReceivedName, FString(TEXT("EventGraph")));
		TestEqual(TEXT("and its Blueprint"), ReceivedPath, FString(TEXT("/Game/N2CTest/BP_Overlay")));
	});

	It(TEXT("does not open the window again while it is open"), [this]()
	{
		Listen();
		Integration->OpenWindowForOverlayTranslation = [this]() { ++Opened; };
		TestTrue(TEXT("the request is delivered"), Integration->RequestOverlayTranslation(FGuid::NewGuid(), TEXT("EventGraph"), TEXT("/Game/N2CTest/BP_Overlay")));
		TestEqual(TEXT("the open window is not opened again"), Opened, 0);
		TestEqual(TEXT("it got the request"), Received, 1);
	});

	It(TEXT("says so when the window cannot be opened"), [this]()
	{
		if (!TestFalse(TEXT("no Node to Code window listens before the click"), Integration->OnOverlayTranslationRequested.IsBound()))
		{
			return;
		}
		Integration->OpenWindowForOverlayTranslation = [this]() { ++Opened; };
		AddExpectedMessage(TEXT("Could not open the Node to Code window to translate EventGraph"), EAutomationExpectedMessageFlags::Contains, 1);
		TestFalse(TEXT("nothing received the request"), Integration->RequestOverlayTranslation(FGuid::NewGuid(), TEXT("EventGraph"), TEXT("/Game/N2CTest/BP_Overlay")));
		TestEqual(TEXT("opening was tried once"), Opened, 1);
	});

	It(TEXT("opens the real Node to Code tab when no window is open"), [this]()
	{
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Skipped: Slate is not initialized"));
			return;
		}
		if (!TestFalse(TEXT("no Node to Code window is open before the click"), WindowTab().IsValid() || Integration->OnOverlayTranslationRequested.IsBound()))
		{
			return;
		}
		ON_SCOPE_EXIT
		{
			if (const TSharedPtr<SDockTab> Tab = WindowTab())
			{
				Tab->RequestCloseTab();
			}
			Integration->SetTranslationInProgress(false);
		};
		// The window resolves the graph first; a Blueprint that does not exist fails there, before any provider call
		AddExpectedMessage(TEXT("N2CTestMissing"), EAutomationExpectedMessageFlags::Contains, 0);
		const bool bDelivered = Integration->RequestOverlayTranslation(FGuid::NewGuid(), TEXT("EventGraph"), TEXT("/Game/N2CTestMissing/BP_None.BP_None"));
		TestTrue(TEXT("the Node to Code tab was opened"), WindowTab().IsValid());
		TestTrue(TEXT("its window received the request"), bDelivered);
		TestFalse(TEXT("the failed translation does not leave the bars blocked"), Integration->IsAnyTranslationInProgress());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
