// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CTaggedGraphsList.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CGraphStateTypes.h"
#include "Widgets/SOverlay.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"
#include "Core/Style/N2CStyle.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/Style/N2CDesignTokens.h"
#include "Misc/Paths.h"
#include "Models/N2CLogging.h"

#define LOCTEXT_NAMESPACE "SN2CTaggedGraphsList"

const FName SN2CTaggedGraphsList::Column_Checkbox = SN2CGraphListRow::Column_Checkbox;
const FName SN2CTaggedGraphsList::Column_GraphName = SN2CGraphListRow::Column_GraphName;
const FName SN2CTaggedGraphsList::Column_Blueprint = SN2CGraphListRow::Column_Blueprint;
const FName SN2CTaggedGraphsList::Column_Context = SN2CGraphListRow::Column_Context;

void SN2CTaggedGraphsList::Construct(const FArguments& InArgs)
{
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;
	OnGraphDoubleClickedDelegate = InArgs._OnGraphDoubleClicked;
	OnSingleTranslateRequestedDelegate = InArgs._OnSingleTranslateRequested;
	OnSingleJsonExportRequestedDelegate = InArgs._OnSingleJsonExportRequested;
	OnViewTranslationRequestedDelegate = InArgs._OnViewTranslationRequested;
	OnRefreshTokensRequestedDelegate = InArgs._OnRefreshTokensRequested;
	OnRemoveRequestedDelegate = InArgs._OnRemoveRequested;

	// Subscribe to token estimation service events
	FN2CTokenEstimationService& TokenService = FN2CTokenEstimationService::Get();
	ModelChangedHandle = TokenService.OnModelChanged.AddSP(this, &SN2CTaggedGraphsList::OnModelChanged);
	CacheInvalidatedHandle = TokenService.OnCacheInvalidated.AddSP(this, &SN2CTaggedGraphsList::OnCacheInvalidated);
	EstimatesStaleHandle = TokenService.OnEstimatesStale.AddSP(this, &SN2CTaggedGraphsList::OnEstimatesStale);

	// Create header row
	// Note: We use a button wrapper for the select-all checkbox because SHeaderRow
	// intercepts mouse clicks for column sorting, preventing the checkbox's OnCheckStateChanged
	// from being triggered. The button ensures our click handler is called.
	HeaderRow = SNew(SHeaderRow)
		.Style(&N2CStyle::GetHeaderRowStyle())
		+ SHeaderRow::Column(Column_Checkbox)
		.DefaultLabel(FText::GetEmpty())
		.FixedWidth(30.0f)
		.HeaderContent()
		[
			SNew(SButton)
			.ButtonStyle(&N2CStyle::GetNoBorderStyle())
			.ContentPadding(0.0f)
			.OnClicked(this, &SN2CTaggedGraphsList::OnSelectAllClicked)
			[
				SNew(SCheckBox)
				.Style(&N2CStyle::GetCheckBoxStyle())
				.IsChecked(this, &SN2CTaggedGraphsList::GetSelectAllCheckboxState)
				// HitTestInvisible makes the checkbox not respond to clicks
				// while still appearing enabled - the parent button handles the click
				.Visibility(EVisibility::HitTestInvisible)
			]
		]
		+ SHeaderRow::Column(Column_GraphName)
		.DefaultLabel(LOCTEXT("GraphNameHeader", "Graph Name"))
		.FillWidth(1.0f)
		.SortMode(this, &SN2CTaggedGraphsList::GetSortMode, Column_GraphName)
		.OnSort(this, &SN2CTaggedGraphsList::OnSortModeChanged)
		+ SHeaderRow::Column(Column_Blueprint)
		.DefaultLabel(LOCTEXT("BlueprintHeader", "Blueprint"))
		.FillWidth(1.0f)
		.SortMode(this, &SN2CTaggedGraphsList::GetSortMode, Column_Blueprint)
		.OnSort(this, &SN2CTaggedGraphsList::OnSortModeChanged)
		+ SHeaderRow::Column(Column_Context)
		.DefaultLabel(LOCTEXT("ContextHeader", "Context"))
		.ManualWidth(120.0f)
		.SortMode(this, &SN2CTaggedGraphsList::GetSortMode, Column_Context)
		.OnSort(this, &SN2CTaggedGraphsList::OnSortModeChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		// Header path display
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0, 4.0f)
		[
			SNew(SN2CPanel)
			.Variant(EN2CPanelVariant::Light)
			.Padding(FN2CSpacing::XS)
			[
				SAssignNew(HeaderPathText, STextBlock)
				.Text(GetHeaderText())
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
			]
		]
		// List view
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SN2CPanel)
			.Variant(EN2CPanelVariant::Light)
			.Padding(0)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SAssignNew(ListView, SListView<TSharedPtr<FN2CGraphListItem>>)
					.ListItemsSource(&FilteredItems)
					.OnGenerateRow(this, &SN2CTaggedGraphsList::OnGenerateRow)
					// Multi selection is the checked set (card 0267): Shift and Ctrl click, arrows, Space and Ctrl+A
					// select, and the checkboxes follow (HandleListSelectionChanged)
					.SelectionMode(ESelectionMode::Multi)
					.OnSelectionChanged(this, &SN2CTaggedGraphsList::HandleListSelectionChanged)
					.OnMouseButtonDoubleClick(this, &SN2CTaggedGraphsList::HandleRowDoubleClicked)
					.OnContextMenuOpening(this, &SN2CTaggedGraphsList::MakeContextMenu)
					.HeaderRow(HeaderRow)
				]
				// Says why the list is empty: nothing selected, or no graph matches
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FN2CSpacing::MD)
				[
					SNew(STextBlock)
					.Text(this, &SN2CTaggedGraphsList::GetEmptyText)
					.Visibility_Lambda([this]()
					{
						return FilteredItems.Num() == 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					.AutoWrapText(true)
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
				]
			]
		]
	];
}

FString SN2CTaggedGraphsList::GetGraphKey(const FN2CTagInfo& Info)
{
	const FN2CGraphKey Key = FN2CGraphKey::FromStrings(Info.BlueprintPath, Info.GraphGuid);
	return Key.IsValid() ? Key.ToString() : Info.BlueprintPath + TEXT(":") + Info.GraphGuid;
}

TArray<FN2CTagInfo> SN2CTaggedGraphsList::UniqueGraphs(const TArray<FN2CTagInfo>& Infos)
{
	TArray<FN2CTagInfo> Unique;
	TSet<FString> Seen;
	for (const FN2CTagInfo& Info : Infos)
	{
		bool bAlreadySeen = false;
		Seen.Add(GetGraphKey(Info), &bAlreadySeen);
		if (!bAlreadySeen)
		{
			Unique.Add(Info);
		}
	}
	return Unique;
}

void SN2CTaggedGraphsList::SetGraphs(const TArray<FN2CTagInfo>& InTagInfos, bool bKeepChecks)
{
	// The graphs checked before, so a refresh of the same view (after a batch, a tag change) keeps them checked
	TSet<FString> Checked;
	if (bKeepChecks)
	{
		for (const TSharedPtr<FN2CGraphListItem>& Old : AllItems)
		{
			if (Old->bIsSelected)
			{
				Checked.Add(GetGraphKey(Old->TagInfo));
			}
		}
	}

	AllItems.Empty();

	// One row per graph: the category and search views get one entry per (graph, tag), and a graph listed twice was
	// translated twice by Select All + Batch Translate (ui-widgets-code-editor-14)
	TMap<FString, TSharedPtr<FN2CGraphListItem>> ByGraph;
	for (const FN2CTagInfo& TagInfo : InTagInfos)
	{
		const FString GraphKey = GetGraphKey(TagInfo);
		if (const TSharedPtr<FN2CGraphListItem>* Existing = ByGraph.Find(GraphKey))
		{
			(*Existing)->Tags.Add(TagInfo);
			continue;
		}

		TSharedPtr<FN2CGraphListItem> Item = MakeShared<FN2CGraphListItem>();
		Item->TagInfo = TagInfo;
		Item->Tags.Add(TagInfo);
		Item->bIsSelected = Checked.Contains(GraphKey);
		Item->bIsStarred = false;
		ByGraph.Add(GraphKey, Item);

		// Check if a translation exists for this graph (of this Blueprint: copies share graph GUIDs)
		const FN2CGraphKey Key = FN2CGraphKey::FromStrings(TagInfo.BlueprintPath, TagInfo.GraphGuid);
		if (Key.IsValid())
		{
			Item->bHasTranslation = UN2CGraphStateManager::Get().HasTranslation(Key);
		}
		else
		{
			Item->bHasTranslation = false;
		}

		AllItems.Add(Item);
	}

	// Sort by graph name
	AllItems.Sort([](const TSharedPtr<FN2CGraphListItem>& A, const TSharedPtr<FN2CGraphListItem>& B)
	{
		return A->TagInfo.GraphName < B->TagInfo.GraphName;
	});

	ApplyFilters();

	// Refresh token estimates for all items
	RefreshTokenEstimates();
}

void SN2CTaggedGraphsList::SetView(EN2CGraphsListView InView, const FString& InCategory, const FString& InTag)
{
	View = InView;
	ViewCategory = InCategory;
	ViewTag = InTag;
	UpdateHeader();
}

FText SN2CTaggedGraphsList::GetHeaderText() const
{
	const FText Search = FText::FromString(CurrentSearchFilter);
	FText Text;
	switch (View)
	{
	case EN2CGraphsListView::Prompt:
		return LOCTEXT("SelectTagPrompt", "Select a category or tag");
	case EN2CGraphsListView::Search:
		return FText::Format(LOCTEXT("SearchHeader", "Search results for \"{0}\""), Search);
	case EN2CGraphsListView::Category:
		Text = FText::Format(LOCTEXT("CategoryPathFormat", "Category: {0}"), FText::FromString(ViewCategory));
		break;
	case EN2CGraphsListView::Tag:
		Text = FText::Format(LOCTEXT("TagPathFormat", "{0} > {1}"), FText::FromString(ViewCategory), FText::FromString(ViewTag));
		break;
	}
	// A category or tag picked during a search lists its graphs that match the search
	return CurrentSearchFilter.IsEmpty()
		? Text
		: FText::Format(LOCTEXT("ScopedSearchHeader", "{0}, matching \"{1}\""), Text, Search);
}

FText SN2CTaggedGraphsList::GetEmptyText() const
{
	if (View == EN2CGraphsListView::Prompt)
	{
		return LOCTEXT("EmptyPrompt", "Select a category or tag to list its graphs.");
	}
	if (!CurrentSearchFilter.IsEmpty())
	{
		return FText::Format(LOCTEXT("EmptySearch", "No graphs match \"{0}\"."), FText::FromString(CurrentSearchFilter));
	}
	return View == EN2CGraphsListView::Category
		? LOCTEXT("EmptyCategory", "No graphs in this category.")
		: LOCTEXT("EmptyTag", "No graphs have this tag.");
}

void SN2CTaggedGraphsList::UpdateHeader()
{
	if (HeaderPathText.IsValid())
	{
		HeaderPathText->SetText(GetHeaderText());
	}
}

void SN2CTaggedGraphsList::SetSearchFilter(const FString& SearchText)
{
	CurrentSearchFilter = SearchText;
	UpdateHeader();
	ApplyFilters();
}

TArray<FN2CTagInfo> SN2CTaggedGraphsList::GetSelectedGraphs() const
{
	TArray<FN2CTagInfo> Selected;
	for (const TSharedPtr<FN2CGraphListItem>& Item : AllItems)
	{
		if (Item->bIsSelected)
		{
			Selected.Add(Item->TagInfo);
		}
	}
	// Rows are one per graph already; a batch must never get a graph twice
	return UniqueGraphs(Selected);
}

TArray<FN2CTagInfo> SN2CTaggedGraphsList::GetSelectedTagEntries() const
{
	TArray<FN2CTagInfo> Entries;
	for (const TSharedPtr<FN2CGraphListItem>& Item : AllItems)
	{
		if (Item->bIsSelected)
		{
			Entries.Append(Item->Tags);
		}
	}
	return Entries;
}

int32 SN2CTaggedGraphsList::GetSelectedCount() const
{
	int32 Count = 0;
	for (const TSharedPtr<FN2CGraphListItem>& Item : AllItems)
	{
		if (Item->bIsSelected)
		{
			Count++;
		}
	}
	return Count;
}

void SN2CTaggedGraphsList::SelectAll()
{
	for (TSharedPtr<FN2CGraphListItem>& Item : FilteredItems)
	{
		Item->bIsSelected = true;
	}
	SyncListSelection();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
	OnSelectionChangedDelegate.ExecuteIfBound();
}

void SN2CTaggedGraphsList::DeselectAll()
{
	for (TSharedPtr<FN2CGraphListItem>& Item : AllItems)
	{
		Item->bIsSelected = false;
	}
	SyncListSelection();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
	OnSelectionChangedDelegate.ExecuteIfBound();
}

FN2CTagInfo SN2CTaggedGraphsList::GetDoubleClickedGraph() const
{
	return LastDoubleClickedGraph;
}

FN2CTagInfo SN2CTaggedGraphsList::GetTranslateRequestedGraph() const
{
	return LastTranslateRequestedGraph;
}

FN2CTagInfo SN2CTaggedGraphsList::GetJsonExportRequestedGraph() const
{
	return LastJsonExportRequestedGraph;
}

FN2CTagInfo SN2CTaggedGraphsList::GetViewTranslationRequestedGraph() const
{
	return LastViewTranslationRequestedGraph;
}

TSharedRef<ITableRow> SN2CTaggedGraphsList::OnGenerateRow(TSharedPtr<FN2CGraphListItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// A multi-column row: its cells are the header's columns (card 0267)
	return SNew(SN2CGraphListRow, OwnerTable)
		.Item(Item)
		.OnCheckboxChanged(this, &SN2CTaggedGraphsList::HandleRowCheckboxChanged)
		.OnTranslateClicked(this, &SN2CTaggedGraphsList::HandleRowTranslateClicked)
		.OnJsonExportClicked(this, &SN2CTaggedGraphsList::HandleRowJsonExportClicked)
		.OnViewTranslationClicked(this, &SN2CTaggedGraphsList::HandleRowViewTranslationClicked)
		.OnRefreshClicked(this, &SN2CTaggedGraphsList::HandleRowRefreshClicked);
}

void SN2CTaggedGraphsList::HandleRowCheckboxChanged(TSharedPtr<FN2CGraphListItem> Item)
{
	// The row set bIsSelected: the list's selection follows
	SyncListSelection();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
	OnSelectionChangedDelegate.ExecuteIfBound();
}

void SN2CTaggedGraphsList::SyncListSelection()
{
	if (!ListView.IsValid())
	{
		return;
	}
	TGuardValue<bool> Guard(bSyncingSelection, true);
	ListView->ClearSelection();
	for (const TSharedPtr<FN2CGraphListItem>& Item : FilteredItems)
	{
		if (Item->bIsSelected)
		{
			ListView->SetItemSelection(Item, true, ESelectInfo::Direct);
		}
	}
}

void SN2CTaggedGraphsList::HandleListSelectionChanged(TSharedPtr<FN2CGraphListItem> Item, ESelectInfo::Type SelectInfo)
{
	if (bSyncingSelection || !ListView.IsValid())
	{
		return;
	}
	// A click, Shift or Ctrl click, the arrows, Space or Ctrl+A: the shown rows' checks become the selection (rows
	// hidden by the search keep theirs)
	for (const TSharedPtr<FN2CGraphListItem>& Shown : FilteredItems)
	{
		Shown->bIsSelected = ListView->IsItemSelected(Shown);
	}
	OnSelectionChangedDelegate.ExecuteIfBound();
}

EColumnSortMode::Type SN2CTaggedGraphsList::GetSortMode(const FName Column) const
{
	return Column == SortColumn ? SortMode : EColumnSortMode::None;
}

void SN2CTaggedGraphsList::OnSortModeChanged(EColumnSortPriority::Type Priority, const FName& Column, EColumnSortMode::Type Mode)
{
	SetSort(Column, Mode);
}

void SN2CTaggedGraphsList::SetSort(const FName& Column, EColumnSortMode::Type Mode)
{
	SortColumn = Column;
	SortMode = Mode;
	SortItems(FilteredItems, SortColumn, SortMode);
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SN2CTaggedGraphsList::SortItems(TArray<TSharedPtr<FN2CGraphListItem>>& Items, const FName& Column, EColumnSortMode::Type Mode)
{
	if (Mode == EColumnSortMode::None || Column.IsNone())
	{
		return;
	}
	const bool bAscending = Mode == EColumnSortMode::Ascending;
	auto ByName = [](const TSharedPtr<FN2CGraphListItem>& A, const TSharedPtr<FN2CGraphListItem>& B)
	{
		return A->TagInfo.GraphName.Compare(B->TagInfo.GraphName, ESearchCase::IgnoreCase);
	};
	Items.StableSort([&](const TSharedPtr<FN2CGraphListItem>& A, const TSharedPtr<FN2CGraphListItem>& B)
	{
		int32 Order = 0;
		if (Column == SN2CGraphListRow::Column_Blueprint)
		{
			Order = A->GetBlueprintDisplayName().Compare(B->GetBlueprintDisplayName(), ESearchCase::IgnoreCase);
		}
		else if (Column == SN2CGraphListRow::Column_Context)
		{
			Order = A->EstimatedTokens == B->EstimatedTokens ? 0 : (A->EstimatedTokens < B->EstimatedTokens ? -1 : 1);
		}
		if (Order == 0)
		{
			Order = ByName(A, B);
		}
		return bAscending ? Order < 0 : Order > 0;
	});
}

TSharedPtr<FN2CGraphListItem> SN2CTaggedGraphsList::GetContextItem() const
{
	const TArray<TSharedPtr<FN2CGraphListItem>> Selected = ListView.IsValid() ? ListView->GetSelectedItems() : TArray<TSharedPtr<FN2CGraphListItem>>();
	return Selected.Num() > 0 ? Selected[0] : nullptr;
}

TSharedPtr<SWidget> SN2CTaggedGraphsList::MakeContextMenu()
{
	const TSharedPtr<FN2CGraphListItem> Item = GetContextItem();
	if (!Item.IsValid())
	{
		return nullptr;
	}
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection*/ true, nullptr);
	Menu.AddMenuEntry(LOCTEXT("MenuTranslate", "Translate"), LOCTEXT("MenuTranslateTip", "Translate this graph"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SN2CTaggedGraphsList::HandleRowTranslateClicked, Item)));
	Menu.AddMenuEntry(LOCTEXT("MenuView", "View Translation"), LOCTEXT("MenuViewTip", "Open the saved translation"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SN2CTaggedGraphsList::HandleRowViewTranslationClicked, Item),
			FCanExecuteAction::CreateLambda([Item]() { return Item->bHasTranslation; })));
	Menu.AddMenuEntry(LOCTEXT("MenuJson", "Export JSON"), LOCTEXT("MenuJsonTip", "Export this graph as N2C JSON"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SN2CTaggedGraphsList::HandleRowJsonExportClicked, Item)));
	Menu.AddMenuEntry(LOCTEXT("MenuOpen", "Open in Blueprint"), LOCTEXT("MenuOpenTip", "Open the graph in its Blueprint editor"), FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SN2CTaggedGraphsList::HandleRowDoubleClicked, Item)));
	if (OnRemoveRequestedDelegate.IsBound())
	{
		Menu.AddMenuSeparator();
		Menu.AddMenuEntry(LOCTEXT("MenuRemove", "Remove Tag"), LOCTEXT("MenuRemoveTip", "Remove the selected graphs' tags in this view"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this]() { OnRemoveRequestedDelegate.ExecuteIfBound(); })));
	}
	return Menu.MakeWidget();
}

void SN2CTaggedGraphsList::OnSelectAllChanged(ECheckBoxState NewState)
{
	bool bSelect = (NewState == ECheckBoxState::Checked);
	for (TSharedPtr<FN2CGraphListItem>& Item : FilteredItems)
	{
		Item->bIsSelected = bSelect;
	}
	SyncListSelection();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
	OnSelectionChangedDelegate.ExecuteIfBound();
}

FReply SN2CTaggedGraphsList::OnSelectAllClicked()
{
	UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] OnSelectAllClicked called"));

	// Toggle selection: if all are selected, deselect all; otherwise select all
	ECheckBoxState CurrentState = GetSelectAllCheckboxState();
	UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] Current checkbox state: %d (0=Unchecked, 1=Checked, 2=Undetermined)"), (int32)CurrentState);

	bool bShouldSelectAll = (CurrentState != ECheckBoxState::Checked);
	UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] Should select all: %s, FilteredItems count: %d"),
		bShouldSelectAll ? TEXT("true") : TEXT("false"), FilteredItems.Num());

	for (TSharedPtr<FN2CGraphListItem>& Item : FilteredItems)
	{
		Item->bIsSelected = bShouldSelectAll;
	}
	SyncListSelection();

	if (ListView.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] Calling RequestListRefresh"));
		ListView->RequestListRefresh();
	}
	else
	{
		UE_LOG(LogNodeToCode, Warning, TEXT("[SN2CTaggedGraphsList] ListView is not valid!"));
	}

	OnSelectionChangedDelegate.ExecuteIfBound();
	UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] OnSelectAllClicked complete"));

	return FReply::Handled();
}

void SN2CTaggedGraphsList::HandleRowDoubleClicked(TSharedPtr<FN2CGraphListItem> Item)
{
	if (Item.IsValid())
	{
		LastDoubleClickedGraph = Item->TagInfo;
		OnGraphDoubleClickedDelegate.ExecuteIfBound();
	}
}

ECheckBoxState SN2CTaggedGraphsList::GetSelectAllCheckboxState() const
{
	if (FilteredItems.Num() == 0)
	{
		return ECheckBoxState::Unchecked;
	}

	int32 SelectedCount = 0;
	for (const TSharedPtr<FN2CGraphListItem>& Item : FilteredItems)
	{
		if (Item->bIsSelected)
		{
			SelectedCount++;
		}
	}

	if (SelectedCount == 0)
	{
		return ECheckBoxState::Unchecked;
	}
	else if (SelectedCount == FilteredItems.Num())
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Undetermined;
}

void SN2CTaggedGraphsList::ApplyFilters()
{
	FilteredItems.Empty();

	for (const TSharedPtr<FN2CGraphListItem>& Item : AllItems)
	{
		if (ItemPassesFilters(Item))
		{
			FilteredItems.Add(Item);
		}
	}

	// The header's sort order, and the list's selection is the checked rows that are shown
	SortItems(FilteredItems, SortColumn, SortMode);
	SyncListSelection();
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

bool SN2CTaggedGraphsList::ItemPassesFilters(const TSharedPtr<FN2CGraphListItem>& Item) const
{
	// Search filter
	if (!CurrentSearchFilter.IsEmpty())
	{
		const bool bMatchesGraph = Item->TagInfo.GraphName.Contains(CurrentSearchFilter, ESearchCase::IgnoreCase);
		const bool bMatchesBlueprint = Item->TagInfo.BlueprintPath.Contains(CurrentSearchFilter, ESearchCase::IgnoreCase);
		// The search box says "Search tags or graphs": a graph also matches by one of its tags or their category
		const bool bMatchesTag = Item->Tags.ContainsByPredicate([this](const FN2CTagInfo& TagEntry)
		{
			return TagEntry.Tag.Contains(CurrentSearchFilter, ESearchCase::IgnoreCase)
				|| TagEntry.Category.Contains(CurrentSearchFilter, ESearchCase::IgnoreCase);
		});
		if (!bMatchesGraph && !bMatchesBlueprint && !bMatchesTag)
		{
			return false;
		}
	}

	return true;
}

void SN2CTaggedGraphsList::UpdateSelectionDisplay()
{
	// This could update a selection count display if added to the UI
}

void SN2CTaggedGraphsList::HandleRowTranslateClicked(TSharedPtr<FN2CGraphListItem> Item)
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] Translate clicked for graph: %s"), *Item->TagInfo.GraphName);
		LastTranslateRequestedGraph = Item->TagInfo;
		OnSingleTranslateRequestedDelegate.ExecuteIfBound();
	}
}

void SN2CTaggedGraphsList::HandleRowJsonExportClicked(TSharedPtr<FN2CGraphListItem> Item)
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] JSON export clicked for graph: %s"), *Item->TagInfo.GraphName);
		LastJsonExportRequestedGraph = Item->TagInfo;
		OnSingleJsonExportRequestedDelegate.ExecuteIfBound();
	}
}

void SN2CTaggedGraphsList::HandleRowViewTranslationClicked(TSharedPtr<FN2CGraphListItem> Item)
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] View translation clicked for graph: %s"), *Item->TagInfo.GraphName);
		LastViewTranslationRequestedGraph = Item->TagInfo;
		OnViewTranslationRequestedDelegate.ExecuteIfBound();
	}
}

void SN2CTaggedGraphsList::HandleRowRefreshClicked(TSharedPtr<FN2CGraphListItem> Item)
{
	if (Item.IsValid())
	{
		UE_LOG(LogNodeToCode, Log, TEXT("[SN2CTaggedGraphsList] Refresh clicked for graph: %s"), *Item->TagInfo.GraphName);

		// Invalidate this specific item's cache and refresh
		FN2CTokenEstimationService::Get().InvalidateCache(Item->TagInfo.GraphGuid);
		RefreshTokenEstimate(Item);

		// Notify external handlers if bound
		OnRefreshTokensRequestedDelegate.ExecuteIfBound(Item);
	}
}

SN2CTaggedGraphsList::~SN2CTaggedGraphsList()
{
	// Unsubscribe from token estimation service events
	if (ModelChangedHandle.IsValid())
	{
		FN2CTokenEstimationService::Get().OnModelChanged.Remove(ModelChangedHandle);
	}
	if (CacheInvalidatedHandle.IsValid())
	{
		FN2CTokenEstimationService::Get().OnCacheInvalidated.Remove(CacheInvalidatedHandle);
	}
	if (EstimatesStaleHandle.IsValid())
	{
		FN2CTokenEstimationService::Get().OnEstimatesStale.Remove(EstimatesStaleHandle);
	}
}

void SN2CTaggedGraphsList::OnModelChanged()
{
	// Model changed, refresh all token estimates
	RefreshTokenEstimates();
}

void SN2CTaggedGraphsList::OnCacheInvalidated()
{
	// Cache was invalidated, refresh all token estimates
	RefreshTokenEstimates();
}

void SN2CTaggedGraphsList::OnEstimatesStale(const FSoftObjectPath& Blueprint)
{
	if (EstimatesStaleTimer.IsValid())
	{
		UnRegisterActiveTimer(EstimatesStaleTimer.ToSharedRef());
	}
	EstimatesStaleTimer = RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[WeakThis = TWeakPtr<SN2CTaggedGraphsList>(SharedThis(this))](double, float)
		{
			if (TSharedPtr<SN2CTaggedGraphsList> This = WeakThis.Pin())
			{
				This->EstimatesStaleTimer.Reset();
				This->RefreshTokenEstimates();
			}
			return EActiveTimerReturnType::Stop;
		}));
}

void SN2CTaggedGraphsList::RefreshTokenEstimates()
{
	FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();

	for (auto& Item : AllItems)
	{
		// Input with the request's prompt plus the expected output, and the prompt's share of the context (card 0220)
		Item->EstimatedTokens = Service.GetTokenEstimate(Item->TagInfo);
		Item->EstimatedCost = Service.CalculateCost(Item->EstimatedTokens);
		Item->ContextUsagePercent = FMath::Clamp(Service.GetContextUsage(Item->EstimatedTokens), 0.0f, 1.0f);
		Item->bTokenEstimateValid = true;
	}

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

void SN2CTaggedGraphsList::RefreshTokenEstimate(TSharedPtr<FN2CGraphListItem> Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();

	Item->EstimatedTokens = Service.GetTokenEstimate(Item->TagInfo);
	Item->EstimatedCost = Service.CalculateCost(Item->EstimatedTokens);
	Item->ContextUsagePercent = FMath::Clamp(Service.GetContextUsage(Item->EstimatedTokens), 0.0f, 1.0f);
	Item->bTokenEstimateValid = true;

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
