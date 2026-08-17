// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CTranslationHistoryWindow.h"

#include "Core/N2CTranslationHistory.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "LLM/N2CLLMModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace N2CTranslationHistoryWindowPrivate
{
struct FBatchItem
{
    FN2CTranslationBatchInfo Batch;
};

struct FRequestItem
{
    FN2CRawResponseRecord Record;
};

struct FViewerState
{
    TArray<TSharedPtr<FBatchItem>> Batches;
    TArray<TSharedPtr<FRequestItem>> Requests;
    TSharedPtr<FBatchItem> SelectedBatch;
    TSharedPtr<FRequestItem> SelectedRequest;
    TWeakPtr<SListView<TSharedPtr<FBatchItem>>> BatchList;
    TWeakPtr<SListView<TSharedPtr<FRequestItem>>> RequestList;
    int32 ActiveTabIndex = 0;
    FString StatusText;
};

FString ProviderDisplayName(EN2CLLMProvider Provider)
{
    if (const UEnum* ProviderEnum = StaticEnum<EN2CLLMProvider>())
    {
        return ProviderEnum->GetDisplayNameTextByValue(static_cast<int64>(Provider)).ToString();
    }
    return UEnum::GetValueAsString(Provider);
}

void RefreshRequests(const TSharedRef<FViewerState>& State)
{
    State->Requests.Reset();
    State->SelectedRequest.Reset();
    State->StatusText.Empty();

    if (State->SelectedBatch.IsValid())
    {
        TArray<FN2CRawResponseRecord> Records;
        if (FN2CTranslationHistory::LoadRequestHistory(State->SelectedBatch->Batch.DirectoryPath, Records))
        {
            for (FN2CRawResponseRecord& Record : Records)
            {
                TSharedPtr<FRequestItem> Item = MakeShared<FRequestItem>();
                Item->Record = MoveTemp(Record);
                State->Requests.Add(Item);
            }
        }
        else
        {
            State->StatusText = TEXT("This translation predates persisted request history. The batch folder can still be opened.");
        }
    }

    if (!State->Requests.IsEmpty())
    {
        State->SelectedRequest = State->Requests[0];
    }

    if (const TSharedPtr<SListView<TSharedPtr<FRequestItem>>> List = State->RequestList.Pin())
    {
        List->RequestListRefresh();
        if (State->SelectedRequest.IsValid())
        {
            List->SetSelection(State->SelectedRequest, ESelectInfo::Direct);
        }
        else
        {
            List->ClearSelection();
        }
    }
}

void RefreshBatches(const TSharedRef<FViewerState>& State)
{
    const FString PreviouslySelectedPath = State->SelectedBatch.IsValid()
        ? State->SelectedBatch->Batch.DirectoryPath
        : FString();

    State->Batches.Reset();
    State->SelectedBatch.Reset();

    TArray<FN2CTranslationBatchInfo> Batches;
    FN2CTranslationHistory::EnumerateBatches(
        UN2CLLMModule::Get()->GetTranslationBaseDirectory(),
        Batches);

    for (FN2CTranslationBatchInfo& Batch : Batches)
    {
        TSharedPtr<FBatchItem> Item = MakeShared<FBatchItem>();
        Item->Batch = MoveTemp(Batch);
        if (!PreviouslySelectedPath.IsEmpty() && Item->Batch.DirectoryPath == PreviouslySelectedPath)
        {
            State->SelectedBatch = Item;
        }
        State->Batches.Add(Item);
    }

    if (!State->SelectedBatch.IsValid() && !State->Batches.IsEmpty())
    {
        State->SelectedBatch = State->Batches[0];
    }

    if (const TSharedPtr<SListView<TSharedPtr<FBatchItem>>> List = State->BatchList.Pin())
    {
        List->RequestListRefresh();
        if (State->SelectedBatch.IsValid())
        {
            List->SetSelection(State->SelectedBatch, ESelectInfo::Direct);
        }
    }

    RefreshRequests(State);
}
}

void FN2CTranslationHistoryWindow::Open()
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    using namespace N2CTranslationHistoryWindowPrivate;

    const TSharedRef<FViewerState> State = MakeShared<FViewerState>();
    TSharedPtr<SListView<TSharedPtr<FBatchItem>>> BatchList;
    TSharedPtr<SListView<TSharedPtr<FRequestItem>>> RequestList;
    TSharedPtr<SWindow> Window;

    SAssignNew(Window, SWindow)
        .Title(NSLOCTEXT("NodeToCode", "PreviousTranslationsTitle", "View Previous Translations"))
        .ClientSize(FVector2D(1280.0f, 760.0f))
        .SupportsMinimize(true)
        .SupportsMaximize(true);

    Window->SetContent(
        SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        .Padding(10.0f)
        [
            SNew(SSplitter)
            + SSplitter::Slot()
            .Value(0.30f)
            [
                SAssignNew(BatchList, SListView<TSharedPtr<FBatchItem>>)
                .ListItemsSource(&State->Batches)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow_Lambda([](TSharedPtr<FBatchItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
                {
                    return SNew(STableRow<TSharedPtr<FBatchItem>>, OwnerTable)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(Item.IsValid() ? FText::FromString(Item->Batch.DisplayName) : FText::GetEmpty())
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
                                const FString Suffix = Item->Batch.bHasRequestHistory
                                    ? TEXT("request history available")
                                    : TEXT("legacy batch");
                                return FText::FromString(FString::Printf(
                                    TEXT("%s  |  %s"),
                                    *Item->Batch.Timestamp.ToString(TEXT("%Y-%m-%d %H:%M:%S")),
                                    *Suffix));
                            })
                        ]
                    ];
                })
                .OnSelectionChanged_Lambda([State](TSharedPtr<FBatchItem> Item, ESelectInfo::Type)
                {
                    State->SelectedBatch = Item;
                    RefreshRequests(State);
                })
            ]
            + SSplitter::Slot()
            .Value(0.25f)
            [
                SAssignNew(RequestList, SListView<TSharedPtr<FRequestItem>>)
                .ListItemsSource(&State->Requests)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow_Lambda([](TSharedPtr<FRequestItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
                {
                    return SNew(STableRow<TSharedPtr<FRequestItem>>, OwnerTable)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text_Lambda([Item]()
                            {
                                if (!Item.IsValid())
                                {
                                    return FText::GetEmpty();
                                }
                                return FText::FromString(FString::Printf(
                                    TEXT("#%d  %s"),
                                    Item->Record.RequestId,
                                    *Item->Record.RequestLabel));
                            })
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
                                return FText::FromString(FString::Printf(
                                    TEXT("%s  |  %s  |  %s"),
                                    *ProviderDisplayName(Item->Record.Provider),
                                    *Item->Record.Model,
                                    Item->Record.bParsedSuccessfully ? TEXT("Parsed") : TEXT("Parse failed")));
                            })
                        ]
                    ];
                })
                .OnSelectionChanged_Lambda([State](TSharedPtr<FRequestItem> Item, ESelectInfo::Type)
                {
                    State->SelectedRequest = Item;
                })
            ]
            + SSplitter::Slot()
            .Value(0.45f)
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
                        .Text(NSLOCTEXT("NodeToCode", "PreviousRequestTab", "Request"))
                        .IsEnabled_Lambda([State]() { return State->ActiveTabIndex != 0; })
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
                        .Text(NSLOCTEXT("NodeToCode", "PreviousResponseTab", "Response"))
                        .IsEnabled_Lambda([State]() { return State->ActiveTabIndex != 1; })
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
                    .WidgetIndex_Lambda([State]() { return State->ActiveTabIndex; })
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SMultiLineEditableTextBox)
                        .IsReadOnly(true)
                        .Text_Lambda([State]()
                        {
                            return State->SelectedRequest.IsValid()
                                ? FText::FromString(State->SelectedRequest->Record.RawRequest)
                                : FText::GetEmpty();
                        })
                    ]
                    + SWidgetSwitcher::Slot()
                    [
                        SNew(SMultiLineEditableTextBox)
                        .IsReadOnly(true)
                        .Text_Lambda([State]()
                        {
                            return State->SelectedRequest.IsValid()
                                ? FText::FromString(State->SelectedRequest->Record.FormattedResponse)
                                : FText::GetEmpty();
                        })
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text_Lambda([State]() { return FText::FromString(State->StatusText); })
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(10.0f, 0.0f, 10.0f, 10.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "RefreshPreviousTranslations", "Refresh"))
                .OnClicked_Lambda([State]()
                {
                    RefreshBatches(State);
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "OpenPreviousTranslationFolder", "Open Batch Folder"))
                .IsEnabled_Lambda([State]() { return State->SelectedBatch.IsValid(); })
                .OnClicked_Lambda([State]()
                {
                    if (State->SelectedBatch.IsValid())
                    {
                        FPlatformProcess::ExploreFolder(*State->SelectedBatch->Batch.DirectoryPath);
                    }
                    return FReply::Handled();
                })
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(STextBlock)
                .Text_Lambda([State]()
                {
                    return State->SelectedBatch.IsValid()
                        ? FText::FromString(State->SelectedBatch->Batch.DirectoryPath)
                        : FText::GetEmpty();
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SButton)
                .Text(NSLOCTEXT("NodeToCode", "ClosePreviousTranslations", "Close"))
                .OnClicked_Lambda([Window]()
                {
                    Window->RequestDestroyWindow();
                    return FReply::Handled();
                })
            ]
        ]);

    State->BatchList = BatchList;
    State->RequestList = RequestList;
    RefreshBatches(State);

    FSlateApplication::Get().AddWindow(Window.ToSharedRef(), true);
}
