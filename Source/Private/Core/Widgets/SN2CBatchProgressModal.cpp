// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CBatchProgressModal.h"
#include "Core/N2CTagTypes.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/AppStyle.h"
#include "Models/N2CLogging.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/Widgets/SN2CIconButton.h"
#include "Core/Style/N2CDesignTokens.h"

#define LOCTEXT_NAMESPACE "SN2CBatchProgressModal"

void SN2CBatchProgressModal::Construct(const FArguments& InArgs)
{
	OnCancelRequestedDelegate = InArgs._OnCancelRequested;
	CurrentItemIndex = 0;
	TotalItemCount = 0;
	bIsComplete = false;

	// Calculate minimum width to fit completion text "Complete - X succeeded, Y failed"
	// Assuming 450px is sufficient for the longest expected text
	const float MinModalWidth = 450.0f;
	const float ModalWidth = FMath::Max(InArgs._ModalWidth, MinModalWidth);
	// Max height for the entire modal (header + body + footer)
	const float MaxModalHeight = 400.0f;
	// Max height for the progress list area
	const float MaxListHeight = 200.0f;

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(ModalWidth)
		.MaxDesiredHeight(MaxModalHeight)
		[
			SNew(SN2CPanel)
			.Variant(EN2CPanelVariant::Light)
			.Padding(0)
			[
				SNew(SVerticalBox)

				// Header
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SN2CPanel)
					.Variant(EN2CPanelVariant::Light)
					.Padding(FMargin(FN2CSpacing::XL, 14.0f))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ModalHeader", "Batch Translation in Progress"))
						.Font(FN2CFonts::LargeBold())
						.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
					]
				]

				// Body
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FN2CSpacing::XL)
				[
					SNew(SVerticalBox)

					// Current item text
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, FN2CSpacing::LG)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ProcessingLabel", "Processing: "))
							.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextSecondary))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SAssignNew(CurrentItemText, STextBlock)
							.Text(LOCTEXT("Waiting", "Waiting..."))
							.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentOrange))
							.Font(FN2CFonts::Mono())
						]
					]

					// Progress bar
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, FN2CSpacing::MD)
					[
						SNew(SBox)
						.HeightOverride(FN2CSpacing::SM)
						[
							SNew(SProgressBar)
							.Percent(this, &SN2CBatchProgressModal::GetProgressBarPercent)
							.FillColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentOrange))
							.BackgroundImage(&N2CStyle::GetDarkPanelBorderBrush())
						]
					]

					// Progress count
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, FN2CSpacing::XL)
					[
						SNew(SBox)
						.HAlign(HAlign_Right)
						[
							SAssignNew(ProgressCountText, STextBlock)
							.Text(LOCTEXT("InitialCount", "0 / 0"))
							.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
							.Font(FN2CFonts::Medium())
						]
					]

					// Progress list with max height and scrollbar
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.MaxDesiredHeight(MaxListHeight)
						[
							SNew(SN2CPanel)
							.Variant(EN2CPanelVariant::Dark)
							.Padding(0)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(ProgressListView, SListView<TSharedPtr<FN2CBatchProgressItem>>)
									.ListItemsSource(&ProgressItems)
									.OnGenerateRow(this, &SN2CBatchProgressModal::GenerateProgressRow)
									.SelectionMode(ESelectionMode::None)
								]
							]
						]
					]
				]

				// Footer with cancel button
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SN2CPanel)
					.Variant(EN2CPanelVariant::Light)
					.Padding(FMargin(FN2CSpacing::XL, FN2CSpacing::LG))
					[
						SNew(SBox)
						.HAlign(HAlign_Right)
						[
							SAssignNew(CancelButton, SButton)
							.ButtonStyle(&N2CStyle::GetButtonStyle())
							.ContentPadding(FMargin(FN2CSpacing::XL, FN2CSpacing::SM))
							.OnClicked(this, &SN2CBatchProgressModal::HandleCancelClicked)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("CancelButton", "Cancel"))
							]
						]
					]
				]
			]
		]
	];
}

void SN2CBatchProgressModal::Initialize(const TArray<FString>& GraphNames)
{
	Reset();

	TotalItemCount = GraphNames.Num();

	// Create progress items for all graphs
	for (const FString& Name : GraphNames)
	{
		ProgressItems.Add(MakeShared<FN2CBatchProgressItem>(Name, EN2CBatchProgressItemStatus::Pending));
	}

	// Update count display
	if (ProgressCountText.IsValid())
	{
		ProgressCountText->SetText(FText::Format(
			LOCTEXT("CountFormat", "{0} / {1}"),
			FText::AsNumber(0),
			FText::AsNumber(TotalItemCount)
		));
	}

	// Refresh list view
	if (ProgressListView.IsValid())
	{
		ProgressListView->RequestListRefresh();
	}
}

void SN2CBatchProgressModal::Initialize(const TArray<FN2CTagInfo>& Graphs)
{
	TArray<FString> GraphNames;
	for (const FN2CTagInfo& Graph : Graphs)
	{
		GraphNames.Add(Graph.GraphName);
	}
	Initialize(GraphNames);
	for (int32 Index = 0; Index < Graphs.Num() && Index < ProgressItems.Num(); ++Index)
	{
		// "/Game/Folder/BP_A.BP_A" -> "BP_A"
		ProgressItems[Index]->BlueprintName = FSoftObjectPath(Graphs[Index].BlueprintPath).GetAssetName();
	}
	if (ProgressListView.IsValid())
	{
		ProgressListView->RequestListRefresh();
	}
}

FString SN2CBatchProgressModal::GetItemLabel(const FN2CBatchProgressItem& Item)
{
	return Item.BlueprintName.IsEmpty() ? Item.GraphName : FString::Printf(TEXT("%s (%s)"), *Item.GraphName, *Item.BlueprintName);
}

void SN2CBatchProgressModal::SetProgress(int32 CurrentIndex, int32 TotalCount, const FString& InCurrentGraphName)
{
	CurrentItemIndex = CurrentIndex;
	TotalItemCount = TotalCount;
	CurrentGraphName = InCurrentGraphName;

	// Update current item text (with its Blueprint when the row knows it)
	if (CurrentItemText.IsValid())
	{
		const bool bKnownRow = ProgressItems.IsValidIndex(CurrentIndex) && ProgressItems[CurrentIndex]->GraphName == CurrentGraphName;
		CurrentItemText->SetText(FText::FromString(bKnownRow ? GetItemLabel(*ProgressItems[CurrentIndex]) : CurrentGraphName));
	}

	// Update count display
	if (ProgressCountText.IsValid())
	{
		ProgressCountText->SetText(FText::Format(
			LOCTEXT("CountFormat", "{0} / {1}"),
			FText::AsNumber(CurrentIndex + 1),
			FText::AsNumber(TotalCount)
		));
	}

	// The row of this item only: same-named graphs in other Blueprints keep their own status (card 0489)
	if (ProgressItems.IsValidIndex(CurrentIndex))
	{
		ProgressItems[CurrentIndex]->Status = EN2CBatchProgressItemStatus::InProgress;
	}

	// Refresh list view
	if (ProgressListView.IsValid())
	{
		ProgressListView->RequestListRefresh();
	}
}

void SN2CBatchProgressModal::MarkItemComplete(const FString& GraphName, bool bSuccess, const FString& ErrorMessage)
{
	bool bFound = false;
	for (TSharedPtr<FN2CBatchProgressItem>& Item : ProgressItems)
	{
		if (Item->GraphName == GraphName)
		{
			Item->Status = bSuccess ? EN2CBatchProgressItemStatus::Completed : EN2CBatchProgressItemStatus::Failed;
			Item->ErrorMessage = bSuccess ? FString() : ErrorMessage;
			bFound = true;
			UE_LOG(LogNodeToCode, Verbose, TEXT("[SN2CBatchProgressModal] MarkItemComplete: Found '%s', set to %s"),
				*GraphName, bSuccess ? TEXT("Completed") : TEXT("Failed"));
			break;
		}
	}

	if (!bFound)
	{
		// One line, not the whole item list (card 0271)
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CBatchProgressModal] MarkItemComplete: '%s' is not one of the %d items"),
			*GraphName, ProgressItems.Num());
	}

	// Refresh list view
	if (ProgressListView.IsValid())
	{
		ProgressListView->RequestListRefresh();
	}
}

void SN2CBatchProgressModal::MarkItemCompleteAt(int32 ItemIndex, bool bSuccess, const FString& ErrorMessage)
{
	if (!ProgressItems.IsValidIndex(ItemIndex))
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CBatchProgressModal] MarkItemCompleteAt: item %d is not one of the %d items"),
			ItemIndex, ProgressItems.Num());
		return;
	}
	FN2CBatchProgressItem& Item = *ProgressItems[ItemIndex];
	Item.Status = bSuccess ? EN2CBatchProgressItemStatus::Completed : EN2CBatchProgressItemStatus::Failed;
	Item.ErrorMessage = bSuccess ? FString() : ErrorMessage;
	UE_LOG(LogNodeToCode, Verbose, TEXT("[SN2CBatchProgressModal] MarkItemCompleteAt: item %d '%s' set to %s"),
		ItemIndex, *GetItemLabel(Item), bSuccess ? TEXT("Completed") : TEXT("Failed"));
	if (ProgressListView.IsValid())
	{
		ProgressListView->RequestListRefresh();
	}
}

FString SN2CBatchProgressModal::GetItemErrorMessage(const FString& GraphName) const
{
	for (const TSharedPtr<FN2CBatchProgressItem>& Item : ProgressItems)
	{
		if (Item.IsValid() && Item->GraphName == GraphName)
		{
			return Item->ErrorMessage;
		}
	}
	return FString();
}

void SN2CBatchProgressModal::SetResult(const FN2CBatchTranslationResult& Result)
{
	bIsComplete = true;

	// Update current item text to show completion
	if (CurrentItemText.IsValid())
	{
		CurrentItemText->SetText(FText::Format(
			LOCTEXT("Complete", "Complete - {0} succeeded, {1} failed"),
			FText::AsNumber(Result.SuccessCount),
			FText::AsNumber(Result.FailureCount)
		));
	}

	// Update count to show final numbers
	if (ProgressCountText.IsValid())
	{
		ProgressCountText->SetText(FText::Format(
			LOCTEXT("CountFormat", "{0} / {1}"),
			FText::AsNumber(Result.TotalCount),
			FText::AsNumber(Result.TotalCount)
		));
	}

	// Update cancel button to show "Close"
	if (CancelButton.IsValid())
	{
		CancelButton->SetContent(
			SNew(STextBlock)
			.Text(LOCTEXT("CloseButton", "Close"))
		);
	}
}

void SN2CBatchProgressModal::Reset()
{
	ProgressItems.Empty();
	CurrentItemIndex = 0;
	TotalItemCount = 0;
	CurrentGraphName = FString();
	bIsComplete = false;

	if (CurrentItemText.IsValid())
	{
		CurrentItemText->SetText(LOCTEXT("Waiting", "Waiting..."));
	}

	if (ProgressCountText.IsValid())
	{
		ProgressCountText->SetText(LOCTEXT("InitialCount", "0 / 0"));
	}

	if (CancelButton.IsValid())
	{
		CancelButton->SetContent(
			SNew(STextBlock)
			.Text(LOCTEXT("CancelButton", "Cancel"))
		);
	}

	if (ProgressListView.IsValid())
	{
		ProgressListView->RequestListRefresh();
	}
}

float SN2CBatchProgressModal::GetProgressPercent() const
{
	if (TotalItemCount <= 0)
	{
		return 0.0f;
	}
	return static_cast<float>(CurrentItemIndex + 1) / static_cast<float>(TotalItemCount);
}

FReply SN2CBatchProgressModal::HandleCancelClicked()
{
	OnCancelRequestedDelegate.ExecuteIfBound();
	return FReply::Handled();
}

TSharedRef<ITableRow> SN2CBatchProgressModal::GenerateProgressRow(TSharedPtr<FN2CBatchProgressItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Capture Item by value (shared pointer) so status updates are reflected
	TSharedPtr<FN2CBatchProgressItem> ItemPtr = Item;

	return SNew(STableRow<TSharedPtr<FN2CBatchProgressItem>>, OwnerTable)
		.Padding(FMargin(FN2CSpacing::MD, FN2CSpacing::XS))
		.ToolTipText_Lambda([ItemPtr]() { return FText::FromString(ItemPtr->ErrorMessage); })
		[
			SNew(SHorizontalBox)
			// Status icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, FN2CSpacing::MD, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(FN2CSizing::IconMD)
				.HeightOverride(FN2CSizing::IconMD)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					// Use attribute binding to dynamically query current status
					.Image_Lambda([ItemPtr]() { return GetStatusBrush(ItemPtr->Status); })
					.ColorAndOpacity_Lambda([this, ItemPtr]() { return GetStatusColor(ItemPtr->Status); })
					.ToolTipText_Lambda([ItemPtr]() { return GetStatusLabel(ItemPtr->Status); })
				]
			]
			// Graph name, and under it why the item failed
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text_Lambda([ItemPtr]() { return FText::FromString(GetItemLabel(*ItemPtr)); })
					.Font(FN2CFonts::MonoSmall())
					// Use attribute binding to dynamically query current status
					.ColorAndOpacity_Lambda([this, ItemPtr]() { return GetItemTextColor(ItemPtr->Status); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, FN2CSpacing::XS, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text_Lambda([ItemPtr]() { return FText::FromString(ItemPtr->ErrorMessage); })
					.AutoWrapText(true)
					.Font(FN2CFonts::MonoSmall())
					.ColorAndOpacity_Lambda([this, ItemPtr]() { return GetStatusColor(ItemPtr->Status); })
					.Visibility_Lambda([ItemPtr]()
					{
						return ItemPtr->Status == EN2CBatchProgressItemStatus::Failed && !ItemPtr->ErrorMessage.IsEmpty()
							? EVisibility::Visible : EVisibility::Collapsed;
					})
				]
			]
		];
}

TOptional<float> SN2CBatchProgressModal::GetProgressBarPercent() const
{
	if (TotalItemCount <= 0)
	{
		return 0.0f;
	}
	return static_cast<float>(CurrentItemIndex + 1) / static_cast<float>(TotalItemCount);
}

const FSlateBrush* SN2CBatchProgressModal::GetStatusBrush(EN2CBatchProgressItemStatus Status)
{
	switch (Status)
	{
	case EN2CBatchProgressItemStatus::Completed:
		return FAppStyle::GetBrush("Icons.Check");
	case EN2CBatchProgressItemStatus::InProgress:
		return FAppStyle::GetBrush("Icons.ArrowRight");
	case EN2CBatchProgressItemStatus::Failed:
		return FAppStyle::GetBrush("Icons.X");
	case EN2CBatchProgressItemStatus::Pending:
	default:
		return FAppStyle::GetBrush("Icons.FilledCircle");
	}
}

FText SN2CBatchProgressModal::GetStatusLabel(EN2CBatchProgressItemStatus Status)
{
	switch (Status)
	{
	case EN2CBatchProgressItemStatus::Completed:
		return LOCTEXT("StatusCompleted", "Completed");
	case EN2CBatchProgressItemStatus::InProgress:
		return LOCTEXT("StatusInProgress", "In progress");
	case EN2CBatchProgressItemStatus::Failed:
		return LOCTEXT("StatusFailed", "Failed");
	case EN2CBatchProgressItemStatus::Pending:
	default:
		return LOCTEXT("StatusPending", "Pending");
	}
}

FSlateColor SN2CBatchProgressModal::GetStatusColor(EN2CBatchProgressItemStatus Status) const
{
	switch (Status)
	{
	case EN2CBatchProgressItemStatus::Completed:
		return N2C::UI::Linear(N2C::UI::Colors().AccentGreen);
	case EN2CBatchProgressItemStatus::InProgress:
		return N2C::UI::Linear(N2C::UI::Colors().AccentOrange);
	case EN2CBatchProgressItemStatus::Failed:
		return N2C::UI::Linear(N2C::UI::Colors().AccentRed);
	case EN2CBatchProgressItemStatus::Pending:
	default:
		return N2C::UI::Linear(N2C::UI::Colors().TextMuted);
	}
}

FSlateColor SN2CBatchProgressModal::GetItemTextColor(EN2CBatchProgressItemStatus Status) const
{
	switch (Status)
	{
	case EN2CBatchProgressItemStatus::InProgress:
		return N2C::UI::Linear(N2C::UI::Colors().TextPrimary);
	case EN2CBatchProgressItemStatus::Completed:
	case EN2CBatchProgressItemStatus::Failed:
	case EN2CBatchProgressItemStatus::Pending:
	default:
		return N2C::UI::Linear(N2C::UI::Colors().TextSecondary);
	}
}

#undef LOCTEXT_NAMESPACE
