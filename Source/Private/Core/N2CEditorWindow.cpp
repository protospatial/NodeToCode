// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CEditorWindow.h"
#include "Core/N2CWidgetContainer.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "LLM/N2CLLMModule.h"
#include "Utils/N2CLogger.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace
{
struct FN2CRawResponseViewItem
{
    FN2CRawResponseRecord Record;
    FString DisplayText;
};

struct FN2CRawResponseViewerState
{
    TArray<TSharedPtr<FN2CRawResponseViewItem>> Items;
    TSharedPtr<FN2CRawResponseViewItem> SelectedItem;
    TWeakPtr<SListView<TSharedPtr<FN2CRawResponseViewItem>>> ResponseList;
    int32 ActiveTabIndex = 1; // Response by default.
    bool bRetryInFlight = false;
    FString StatusText;
};

FString GetRawResponseProviderDisplayName(EN2CLLMProvider Provider)
{
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        return ProviderEnum->GetDisplayNameTextByValue(static_cast<int64>(Provider)).ToString();
    }
    return UEnum::GetValueAsString(Provider);
}

void RefreshN2CRawResponseViewer(
    const TSharedRef<FN2CRawResponseViewerState>& State,
    int32 PreferredRequestId = INDEX_NONE)
{
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (!LLMModule)
    {
        return;
    }

    const int32 PreviouslySelectedRequestId =
        State->SelectedItem.IsValid() ? State->SelectedItem->Record.RequestId : INDEX_NONE;
    const int32 RequestIdToSelect =
        PreferredRequestId != INDEX_NONE ? PreferredRequestId : PreviouslySelectedRequestId;

    State->Items.Reset();
    State->SelectedItem.Reset();

    for (const FN2CRawResponseRecord& Record : LLMModule->GetRawResponseHistory())
    {
        TSharedPtr<FN2CRawResponseViewItem> Item = MakeShared<FN2CRawResponseViewItem>();
        Item->Record = Record;
        Item->DisplayText = FString::Printf(
            TEXT("#%d  %s  |  %s"),
            Record.RequestId,
            *Record.RequestLabel,
            *GetRawResponseProviderDisplayName(Record.Provider));
        State->Items.Add(Item);

        if (Record.RequestId == RequestIdToSelect)
        {
            State->SelectedItem = Item;
        }
    }

    if (!State->SelectedItem.IsValid() && !State->Items.IsEmpty())
    {
        State->SelectedItem = State->Items[0];
    }

    if (const TSharedPtr<SListView<TSharedPtr<FN2CRawResponseViewItem>>> List = State->ResponseList.Pin())
    {
        List->RequestListRefresh();
        if (State->SelectedItem.IsValid())
        {
            List->SetSelection(State->SelectedItem, ESelectInfo::Direct);
            List->RequestScrollIntoView(State->SelectedItem);
        }
    }
}

FReply OpenRawResponseViewer()
{
    UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
    if (!LLMModule || !FSlateApplication::IsInitialized())
    {
        return FReply::Handled();
    }

    if (LLMModule->GetRawResponseHistory().IsEmpty())
    {
        return FReply::Handled();
    }

    const TSharedRef<FN2CRawResponseViewerState> State =
        MakeShared<FN2CRawResponseViewerState>();
    RefreshN2CRawResponseViewer(State);

    TSharedPtr<SListView<TSharedPtr<FN2CRawResponseViewItem>>> ResponseList;
    TSharedPtr<SWindow> ViewerWindow;

    SAssignNew(ViewerWindow, SWindow)
        .Title(NSLOCTEXT("NodeToCode", "RawResponsesWindowTitle", "Raw LLM Requests / Responses"))
        .ClientSize(FVector2D(1120.0f, 700.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(true);

    ViewerWindow->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(10.0f)
        [
            SNew(SSplitter)
            + SSplitter::Slot()
            .Value(0.28f)
            [
                SAssignNew(ResponseList, SListView<TSharedPtr<FN2CRawResponseViewItem>>)
                .ListItemsSource(&State->Items)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow_Lambda([](
                    TSharedPtr<FN2CRawResponseViewItem> Item,
                    const TSharedRef<STableViewBase>& OwnerTable)
                {
                    return SNew(STableRow<TSharedPtr<FN2CRawResponseViewItem>>, OwnerTable)
                        .Padding(FMargin(8.0f, 6.0f))
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            [
                                SNew(STextBlock)
                                .Text(Item.IsValid()
                                    ? FText::FromString(Item->DisplayText)
                                    : FText::GetEmpty())
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 2.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text_Lambda([Item]()
                                {
                                    if (!Item.IsValid())
                                    {
                                        return FText::GetEmpty();
                                    }

                                    FString Details = FString::Printf(
                                        TEXT("%s%s%s  |  %s"),
                                        *Item->Record.Timestamp,
                                        Item->Record.Model.IsEmpty() ? TEXT("") : TEXT("  |  "),
                                        *Item->Record.Model,
                                        Item->Record.bParsedSuccessfully ? TEXT("Parsed") : TEXT("Parse failed"));

                                    if (Item->Record.RetriedFromRequestId > 0)
                                    {
                                        Details += FString::Printf(
                                            TEXT("  |  Retry of #%d"),
                                            Item->Record.RetriedFromRequestId);
                                    }
                                    return FText::FromString(Details);
                                })
                            ]
                        ];
                })
                .OnSelectionChanged_Lambda([State](
                    TSharedPtr<FN2CRawResponseViewItem> Item,
                    ESelectInfo::Type)
                {
                    if (Item.IsValid())
                    {
                        State->SelectedItem = Item;
                    }
                })
            ]
            + SSplitter::Slot()
            .Value(0.72f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SNew(SButton)
                        .Text(NSLOCTEXT("NodeToCode", "RawRequestTab", "Request"))
                        .IsEnabled_Lambda([State]()
                        {
                            return State->ActiveTabIndex != 0;
                        })
                        .OnClicked_Lambda([State]()
                        {
                            State->ActiveTabIndex = 0;
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(NSLOCTEXT("NodeToCode", "RawResponseTab", "Response"))
                        .IsEnabled_Lambda([State]()
                        {
                            return State->ActiveTabIndex != 1;
                        })
                        .OnClicked_Lambda([State]()
                        {
                            State->ActiveTabIndex = 1;
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SNew(SWidgetSwitcher)
                    .WidgetIndex_Lambda([State]()
                    {
                        return State->ActiveTabIndex;
                    })
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SMultiLineEditableTextBox)
                        .IsReadOnly(true)
                        .Text_Lambda([State]()
                        {
                            return State->SelectedItem.IsValid()
                                ? FText::FromString(State->SelectedItem->Record.RawRequest)
                                : FText::GetEmpty();
                        })
                    ]
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SMultiLineEditableTextBox)
                        .IsReadOnly(true)
                        .Text_Lambda([State]()
                        {
                            return State->SelectedItem.IsValid()
                                ? FText::FromString(State->SelectedItem->Record.FormattedResponse)
                                : FText::GetEmpty();
                        })
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text_Lambda([State]()
                    {
                        return FText::FromString(State->StatusText);
                    })
                    .AutoWrapText(true)
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .HAlign(HAlign_Right)
        .Padding(10.0f, 0.0f, 10.0f, 10.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "ResendRawRequest", "Resend & Re-parse"))
                .ToolTipText(NSLOCTEXT(
                    "NodeToCode",
                    "ResendRawRequestToolTip",
                    "Replay the exact captured request body using the same active provider/model, then parse the new response through the normal Node to Code parser. The original history entry is preserved."))
                .IsEnabled_Lambda([State]()
                {
                    return State->SelectedItem.IsValid() &&
                           !State->SelectedItem->Record.RawRequest.IsEmpty() &&
                           !State->bRetryInFlight;
                })
                .OnClicked_Lambda([State, ViewerWindow]()
                {
                    if (!State->SelectedItem.IsValid())
                    {
                        return FReply::Handled();
                    }

                    const int32 RequestId = State->SelectedItem->Record.RequestId;
                    State->bRetryInFlight = true;
                    State->StatusText = FString::Printf(
                        TEXT("Resending request #%d and waiting for a new response..."),
                        RequestId);

                    const TWeakPtr<SWindow> WeakViewerWindow = ViewerWindow;
                    const bool bStarted = UN2CLLMModule::Get()->ResendRawRequest(
                        RequestId,
                        [State, WeakViewerWindow](bool bSuccess)
                        {
                            State->bRetryInFlight = false;
                            State->StatusText = bSuccess
                                ? TEXT("Retry completed and parsed successfully.")
                                : TEXT("Retry completed but the new response did not parse successfully.");

                            if (!WeakViewerWindow.IsValid())
                            {
                                return;
                            }

                            const TArray<FN2CRawResponseRecord>& History =
                                UN2CLLMModule::Get()->GetRawResponseHistory();
                            const int32 NewRequestId = History.IsEmpty()
                                ? INDEX_NONE
                                : History.Last().RequestId;
                            RefreshN2CRawResponseViewer(State, NewRequestId);
                        });

                    if (!bStarted)
                    {
                        State->bRetryInFlight = false;
                        State->StatusText =
                            TEXT("Unable to resend this request. The captured body may be unavailable or the active provider/model no longer matches it.");
                    }

                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "CloseRawResponses", "Close"))
                .OnClicked_Lambda([ViewerWindow]()
                {
                    ViewerWindow->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]
    );

    State->ResponseList = ResponseList;
    if (ResponseList.IsValid() && State->SelectedItem.IsValid())
    {
        ResponseList->SetSelection(State->SelectedItem, ESelectInfo::Direct);
    }

    FSlateApplication::Get().AddModalWindow(
        ViewerWindow.ToSharedRef(),
        FSlateApplication::Get().GetActiveTopLevelWindow(),
        false);

    return FReply::Handled();
}
}

const FName SN2CEditorWindow::TabId(TEXT("NodeToCodeEditor"));
TWeakPtr<SDockTab> SN2CEditorWindow::ActiveTab;

void SN2CEditorWindow::RegisterTabSpawner()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        TabId,
        FOnSpawnTab::CreateStatic(&SN2CEditorWindow::SpawnTab))
        .SetDisplayName(NSLOCTEXT("NodeToCode", "TabTitle", "Node to Code"))
        .SetMenuType(ETabSpawnerMenuType::Hidden)
        .SetIcon(FSlateIcon("NodeToCodeStyle", "NodeToCode.ToolbarButton"));

    FN2CLogger::Get().Log(TEXT("Registered Node to Code tab spawner"), EN2CLogSeverity::Info);
}

void SN2CEditorWindow::UnregisterTabSpawner()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
    FN2CLogger::Get().Log(TEXT("Unregistered Node to Code tab spawner"), EN2CLogSeverity::Info);
}

TSharedRef<SDockTab> SN2CEditorWindow::SpawnTab(const FSpawnTabArgs& Args)
{
    // Check if we already have an active tab
    if (TSharedPtr<SDockTab> ExistingTab = ActiveTab.Pin())
    {
        // Bring the existing tab to front
        ExistingTab->DrawAttention();
        return ExistingTab.ToSharedRef();
    }

    // Create new tab
    TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        // Add OnTabClosed handler immediately during construction
        .OnTabClosed_Static(&SN2CEditorWindow::OnTabClosed);

    // Store the active tab reference before creating content
    // This prevents potential recursive spawning
    ActiveTab = SpawnedTab;

    TSharedRef<SN2CEditorWindow> EditorWindow = SNew(SN2CEditorWindow);
    SpawnedTab->SetContent(EditorWindow);

    return SpawnedTab;
}

void SN2CEditorWindow::OnTabClosed(TSharedRef<SDockTab> ClosedTab)
{
    // Clear the active tab reference
    if (ActiveTab.Pin() == ClosedTab)
    {
        ActiveTab.Reset();
    }
}

void SN2CEditorWindow::Construct(const FArguments& InArgs)
{
    // 1) Load the widget blueprint
    FString AssetPath = TEXT("/Script/Blutility.EditorUtilityWidgetBlueprint'/NodeToCode/UI/NodeToCodeUI.NodeToCodeUI'");
    UEditorUtilityWidgetBlueprint* EditorBP = LoadObject<UEditorUtilityWidgetBlueprint>(nullptr, *AssetPath);
    if (!EditorBP)
    {
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to load NodeToCodeUI blueprint at path: %s"), 
            *AssetPath));
        return;
    }

    // 2) Manually create the widget (instead of SpawnAndRegisterTab)
    UClass* WidgetClass = EditorBP->GeneratedClass;
    if (!WidgetClass || !WidgetClass->IsChildOf(UEditorUtilityWidget::StaticClass()))
    {
        FN2CLogger::Get().LogError(TEXT("Loaded blueprint is not a valid Editor Utility Widget class"));
        return;
    }

    // Use our persistent container as the outer
    UEditorUtilityWidget* EditorWidget = NewObject<UEditorUtilityWidget>(
        UN2CWidgetContainer::Get(),
        WidgetClass
    );
    if (!EditorWidget)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to create Editor Utility Widget instance"));
        return;
    }

    // 3) Add a lightweight native session toolbar above the existing Editor Utility Widget.
    // This keeps the binary UI assets untouched while exposing reliable progress/raw-response
    // state shared by both single-graph and full-Blueprint translation paths.
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SBorder)
            .Padding(FMargin(8.0f, 4.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SCircularThrobber)
                    .Visibility_Lambda([]()
                    {
                        return UN2CLLMModule::Get()->IsWorkInProgress()
                            ? EVisibility::Visible
                            : EVisibility::Collapsed;
                    })
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([]()
                    {
                        const UN2CLLMModule* LLMModule = UN2CLLMModule::Get();
                        const int32 InFlight = LLMModule->GetInFlightRequestCount();
                        if (InFlight > 0)
                        {
                            return FText::FromString(FString::Printf(
                                TEXT("Processing %d request%s..."),
                                InFlight,
                                InFlight == 1 ? TEXT("") : TEXT("s")));
                        }

                        if (LLMModule->GetSystemStatus() == EN2CSystemStatus::Initializing)
                        {
                            return NSLOCTEXT("NodeToCode", "InitializingLLM", "Initializing LLM...");
                        }
                        if (LLMModule->GetSystemStatus() == EN2CSystemStatus::Error)
                        {
                            return NSLOCTEXT("NodeToCode", "TranslationCompletedWithErrors", "Translation completed with errors");
                        }
                        return NSLOCTEXT("NodeToCode", "TranslationReady", "Ready");
                    })
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SButton)
                    .Text_Lambda([]()
                    {
                        return FText::FromString(FString::Printf(
                            TEXT("Raw Responses (%d)"),
                            UN2CLLMModule::Get()->GetRawResponseCount()));
                    })
                    .ToolTipText(NSLOCTEXT(
                        "NodeToCode",
                        "RawResponsesToolTip",
                        "View the exact request body and formatted provider response for any request in the current translation session, and optionally replay it."))
                    .IsEnabled_Lambda([]()
                    {
                        return UN2CLLMModule::Get()->GetRawResponseCount() > 0;
                    })
                    .OnClicked_Lambda([]()
                    {
                        return OpenRawResponseViewer();
                    })
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [
            EditorWidget->TakeWidget()
        ]
    ];

    FN2CLogger::Get().Log(TEXT("Successfully created and embedded NodeToCodeUI widget"), EN2CLogSeverity::Info);
}
