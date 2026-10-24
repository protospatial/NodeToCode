// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Core/Widgets/N2CMainWindowWidget.h"
#include "Core/Widgets/N2CTagManagerWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "UObject/Package.h"

/**
 * The UMG wrappers UN2CMainWindowWidget and UN2CTagManagerWidget are kept (unsupported) so user Editor Utility Widgets
 * that embed them keep loading. Their Slate widgets subscribe to live editor delegates in Construct, so in the UMG
 * designer (IsDesignTime) the wrappers must build a placeholder instead of SN2CMainWindow or SN2CTagManager.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CLegacyWidgetWrapperDesignTimeTest, "NodeToCode.Robustness.LegacyWidgetWrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CLegacyWidgetWrapperDesignTimeTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddInfo(TEXT("Slate is not initialized; skipping"));
		return true;
	}

	static const FName TagManagerType(TEXT("SN2CTagManager"));
	static const FName MainWindowType(TEXT("SN2CMainWindow"));

	// Design time: placeholders only
	{
		UN2CTagManagerWidget* Wrapper = NewObject<UN2CTagManagerWidget>(GetTransientPackage());
		Wrapper->SetDesignerFlags(EWidgetDesignFlags::Designing);
		const TSharedRef<SWidget> Built = Wrapper->TakeWidget();
		TestNotEqual(TEXT("Tag Manager wrapper builds no SN2CTagManager in the designer"), Built->GetType(), TagManagerType);
		TestEqual(TEXT("Tag Manager wrapper returns 0 selected graphs in the designer"), Wrapper->GetSelectedCount(), 0);
		Wrapper->ReleaseSlateResources(true);
	}
	{
		UN2CMainWindowWidget* Wrapper = NewObject<UN2CMainWindowWidget>(GetTransientPackage());
		Wrapper->SetDesignerFlags(EWidgetDesignFlags::Designing);
		const TSharedRef<SWidget> Built = Wrapper->TakeWidget();
		TestNotEqual(TEXT("Main Window wrapper builds no SN2CMainWindow in the designer"), Built->GetType(), MainWindowType);
		TestFalse(TEXT("Main Window wrapper reports no visible viewer in the designer"), Wrapper->IsTranslationViewerVisible());
		Wrapper->ReleaseSlateResources(true);
	}

	// Outside the designer the Tag Manager wrapper still builds the real widget
	{
		UN2CTagManagerWidget* Wrapper = NewObject<UN2CTagManagerWidget>(GetTransientPackage());
		const TSharedRef<SWidget> Built = Wrapper->TakeWidget();
		TestEqual(TEXT("Tag Manager wrapper builds SN2CTagManager outside the designer"), Built->GetType(), TagManagerType);
		Wrapper->ReleaseSlateResources(true);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
