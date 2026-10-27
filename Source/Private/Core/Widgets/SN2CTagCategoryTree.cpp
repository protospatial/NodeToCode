// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CTagCategoryTree.h"
#include "Core/Style/N2CStyle.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CGraphStateTypes.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Core/Style/N2CUIColors.h"
#include "Core/Widgets/SN2CPanel.h"
#include "Core/Style/N2CDesignTokens.h"

#define LOCTEXT_NAMESPACE "SN2CTagCategoryTree"

namespace N2CTagCategoryTree
{
	/** A tag item's cache key: tags are per category */
	FString TagKey(const FString& Category, const FString& Tag)
	{
		return Category + TEXT("\n") + Tag;
	}

	/** One graph, whatever tags it has: a Blueprint and its copies share graph GUIDs, so the key names both */
	FString GraphKey(const FN2CTaggedBlueprintGraph& Graph)
	{
		return FN2CGraphKey(Graph.OwningBlueprint, Graph.GraphGuid).ToString();
	}
}

void SN2CTagCategoryTree::Construct(const FArguments& InArgs)
{
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;
	TagSource = InArgs._TagSource;

	ChildSlot
	[
		SNew(SN2CPanel)
		.Variant(EN2CPanelVariant::Light)
		.Padding(FN2CSpacing::XS)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(4.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CategoriesHeader", "CATEGORIES"))
				.Font(FAppStyle::GetFontStyle("SmallFontBold"))
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextMuted))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FN2CTreeItem>>)
				.TreeItemsSource(&FilteredRootItems)
				.OnGenerateRow(this, &SN2CTagCategoryTree::OnGenerateRow)
				.OnGetChildren(this, &SN2CTagCategoryTree::OnGetChildren)
				.OnSelectionChanged(this, &SN2CTagCategoryTree::OnSelectionChanged)
				.OnExpansionChanged(this, &SN2CTagCategoryTree::OnExpansionChanged)
				.SelectionMode(ESelectionMode::Single)
			]
		]
	];

	// Build initial tree data
	RefreshData();
}

TArray<FN2CTaggedBlueprintGraph> SN2CTagCategoryTree::GetTags() const
{
	return TagSource ? TagSource() : UN2CGraphStateManager::Get().GetAllTags();
}

void SN2CTagCategoryTree::RefreshData()
{
	BuildTreeData();

	// The selected tag or category is gone (its last tag was removed): nothing is selected any more
	if (SelectedItem.IsValid() && !AllItems.Contains(SelectedItem))
	{
		SelectedItem.Reset();
		if (TreeView.IsValid())
		{
			TreeView->ClearSelection();
		}
	}

	ApplyFilter();
}

void SN2CTagCategoryTree::BuildTreeData()
{
	using namespace N2CTagCategoryTree;

	// Group the (graph, tag) pairs. Counts are of distinct graphs, so a graph with two tags in one category counts
	// once for the category
	struct FTagGroup
	{
		FString Name;
		TSet<FString> Graphs;
	};
	struct FCategoryGroup
	{
		FString Name;
		TSet<FString> Graphs;
		TMap<FString, FTagGroup> Tags;
	};
	TMap<FString, FCategoryGroup> Groups;
	for (const FN2CTaggedBlueprintGraph& Tagged : GetTags())
	{
		const FString Category = FN2CTagEntry::NormalizeCategory(Tagged.Category);
		const FString Graph = GraphKey(Tagged);
		FCategoryGroup& CategoryGroup = Groups.FindOrAdd(Category);
		if (CategoryGroup.Name.IsEmpty())
		{
			CategoryGroup.Name = Category;
		}
		CategoryGroup.Graphs.Add(Graph);
		FTagGroup& TagGroup = CategoryGroup.Tags.FindOrAdd(Tagged.Tag);
		if (TagGroup.Name.IsEmpty())
		{
			TagGroup.Name = Tagged.Tag;
		}
		TagGroup.Graphs.Add(Graph);
	}

	// Reuse the item of each category and tag that is still there: the tree keeps its selection and expansion
	// because the pointers it holds stay the same (ui-widgets-code-editor-13)
	TMap<FString, TSharedPtr<FN2CTreeItem>> NewCategoryItems;
	TMap<FString, TSharedPtr<FN2CTreeItem>> NewTagItems;
	RootItems.Reset();
	AllItems.Reset();

	for (const TPair<FString, FCategoryGroup>& CategoryPair : Groups)
	{
		TSharedPtr<FN2CTreeItem> CategoryItem = CategoryItems.FindRef(CategoryPair.Key);
		if (!CategoryItem.IsValid())
		{
			CategoryItem = MakeShared<FN2CTreeItem>();
			CategoryItem->ItemType = EN2CTreeItemType::Category;
			CategoryItem->bIsExpanded = true; // a new category starts expanded
		}
		CategoryItem->Name = CategoryPair.Value.Name;
		CategoryItem->GraphCount = CategoryPair.Value.Graphs.Num();
		CategoryItem->Children.Reset();

		for (const TPair<FString, FTagGroup>& TagPair : CategoryPair.Value.Tags)
		{
			const FString Key = TagKey(CategoryPair.Key, TagPair.Key);
			TSharedPtr<FN2CTreeItem> TagItem = TagItems.FindRef(Key);
			if (!TagItem.IsValid())
			{
				TagItem = MakeShared<FN2CTreeItem>();
				TagItem->ItemType = EN2CTreeItemType::Tag;
			}
			TagItem->Name = TagPair.Value.Name;
			TagItem->Category = CategoryItem->Name;
			TagItem->GraphCount = TagPair.Value.Graphs.Num();
			TagItem->Parent = CategoryItem;

			CategoryItem->Children.Add(TagItem);
			AllItems.Add(TagItem);
			NewTagItems.Add(Key, TagItem);
		}

		// Sort tags alphabetically
		CategoryItem->Children.Sort([](const TSharedPtr<FN2CTreeItem>& A, const TSharedPtr<FN2CTreeItem>& B)
		{
			return A->Name < B->Name;
		});

		RootItems.Add(CategoryItem);
		AllItems.Add(CategoryItem);
		NewCategoryItems.Add(CategoryPair.Key, CategoryItem);
	}

	CategoryItems = MoveTemp(NewCategoryItems);
	TagItems = MoveTemp(NewTagItems);

	// Sort categories alphabetically
	RootItems.Sort([](const TSharedPtr<FN2CTreeItem>& A, const TSharedPtr<FN2CTreeItem>& B)
	{
		return A->Name < B->Name;
	});
}

void SN2CTagCategoryTree::SetSearchFilter(const FString& SearchText)
{
	CurrentSearchFilter = SearchText;
	ApplyFilter();
}

void SN2CTagCategoryTree::ApplyFilter()
{
	FilteredRootItems.Reset();
	FilteredChildren.Reset();

	if (CurrentSearchFilter.IsEmpty())
	{
		FilteredRootItems = RootItems;
	}
	else
	{
		// The same items, fewer of them: a category is shown when its name or one of its tags matches
		for (const TSharedPtr<FN2CTreeItem>& Category : RootItems)
		{
			const bool bCategoryMatches = Category->Name.Contains(CurrentSearchFilter, ESearchCase::IgnoreCase);
			TArray<TSharedPtr<FN2CTreeItem>> Shown;
			for (const TSharedPtr<FN2CTreeItem>& TagItem : Category->Children)
			{
				if (bCategoryMatches || TagItem->Name.Contains(CurrentSearchFilter, ESearchCase::IgnoreCase))
				{
					Shown.Add(TagItem);
				}
			}

			if (bCategoryMatches || Shown.Num() > 0)
			{
				FilteredRootItems.Add(Category);
				FilteredChildren.Add(Category, MoveTemp(Shown));
			}
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		ApplyTreeState();
	}
}

void SN2CTagCategoryTree::ApplyTreeState()
{
	TGuardValue<bool> Guard(bApplyingTreeState, true);
	const bool bSearching = !CurrentSearchFilter.IsEmpty();

	// Expand everything while searching; otherwise each category as the user left it
	for (const TSharedPtr<FN2CTreeItem>& Item : FilteredRootItems)
	{
		TreeView->SetItemExpansion(Item, bSearching || Item->bIsExpanded);
	}

	// Show the selection again once its item is shown (a search that hid it was cleared). SetSelection notifies
	// with ESelectInfo::Direct, which OnSelectionChanged does not forward
	if (SelectedItem.IsValid() && !TreeView->IsItemSelected(SelectedItem))
	{
		const TSharedPtr<FN2CTreeItem> Root = SelectedItem->IsTag() ? SelectedItem->Parent.Pin() : SelectedItem;
		bool bShown = Root.IsValid() && FilteredRootItems.Contains(Root);
		if (bShown && bSearching && SelectedItem->IsTag())
		{
			const TArray<TSharedPtr<FN2CTreeItem>>* Shown = FilteredChildren.Find(Root);
			bShown = Shown && Shown->Contains(SelectedItem);
		}
		if (bShown)
		{
			TreeView->SetSelection(SelectedItem, ESelectInfo::Direct);
		}
	}
}

TSharedPtr<FN2CTreeItem> SN2CTagCategoryTree::GetSelectedItem() const
{
	return SelectedItem;
}

bool SN2CTagCategoryTree::IsSelectedCategory() const
{
	return SelectedItem.IsValid() && SelectedItem->IsCategory();
}

bool SN2CTagCategoryTree::IsSelectedTag() const
{
	return SelectedItem.IsValid() && SelectedItem->IsTag();
}

FString SN2CTagCategoryTree::GetSelectedTag() const
{
	if (IsSelectedTag())
	{
		return SelectedItem->Name;
	}
	return FString();
}

FString SN2CTagCategoryTree::GetSelectedCategory() const
{
	if (!SelectedItem.IsValid())
	{
		return FString();
	}

	if (SelectedItem->IsCategory())
	{
		return SelectedItem->Name;
	}

	return SelectedItem->Category;
}

void SN2CTagCategoryTree::SelectTag(const FString& InTag, const FString& InCategory)
{
	// FString == ignores case, as tag matching does everywhere else
	const FString Category = FN2CTagEntry::NormalizeCategory(InCategory);
	for (const TSharedPtr<FN2CTreeItem>& CategoryItem : FilteredRootItems)
	{
		if (CategoryItem->Name == Category)
		{
			// Expand the category
			CategoryItem->bIsExpanded = true;
			if (TreeView.IsValid())
			{
				TGuardValue<bool> Guard(bApplyingTreeState, true);
				TreeView->SetItemExpansion(CategoryItem, true);
			}

			TArray<TSharedPtr<FN2CTreeItem>> Shown;
			OnGetChildren(CategoryItem, Shown);
			for (const TSharedPtr<FN2CTreeItem>& TagItem : Shown)
			{
				if (TagItem->Name == InTag)
				{
					SelectedItem = TagItem;
					if (TreeView.IsValid())
					{
						TreeView->SetSelection(TagItem);
					}
					return;
				}
			}
		}
	}
}

void SN2CTagCategoryTree::SelectCategory(const FString& Category)
{
	const FString Wanted = FN2CTagEntry::NormalizeCategory(Category);
	for (const TSharedPtr<FN2CTreeItem>& CategoryItem : FilteredRootItems)
	{
		if (CategoryItem->Name == Wanted)
		{
			SelectedItem = CategoryItem;
			if (TreeView.IsValid())
			{
				TreeView->SetSelection(CategoryItem);
			}
			return;
		}
	}
}

void SN2CTagCategoryTree::ClearSelection()
{
	SelectedItem.Reset();
	if (TreeView.IsValid())
	{
		TreeView->ClearSelection();
	}
}

TSharedRef<ITableRow> SN2CTagCategoryTree::OnGenerateRow(TSharedPtr<FN2CTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	// The text and the folder icon read the item every frame: a refresh updates the item in place, so its row stays
	const TAttribute<FText> DisplayText = TAttribute<FText>::CreateLambda([Item]()
	{
		return FText::Format(LOCTEXT("ItemDisplayFormat", "{0} ({1})"), FText::FromString(Item->Name), FText::AsNumber(Item->GraphCount));
	});

	// Create the icon widget with dynamic binding for categories
	TSharedRef<SWidget> IconWidget = Item->IsCategory()
		? StaticCastSharedRef<SWidget>(
			SNew(SImage)
			.Image_Lambda([this, Item]() -> const FSlateBrush*
			{
				return TreeView.IsValid() && TreeView->IsItemExpanded(Item)
					? FAppStyle::GetBrush("Icons.FolderOpen")
					: FAppStyle::GetBrush("Icons.FolderClosed");
			})
			.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
		)
		: StaticCastSharedRef<SWidget>(
			SNew(SImage)
			.Image(FAppStyle::GetBrush("GraphEditor.Bookmark"))
			.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::AccentGold))
		);

	return SNew(STableRow<TSharedPtr<FN2CTreeItem>>, OwnerTable)
		.Style(&N2CStyle::GetTableRowStyle())
		.Padding(FMargin(Item->IsTag() ? 16.0f : 0.0f, 2.0f, 0.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				IconWidget
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(DisplayText)
				.Font(Item->IsCategory() ? FAppStyle::GetFontStyle("NormalFontBold") : FAppStyle::GetFontStyle("NormalFont"))
				.ColorAndOpacity(N2C::UI::Bind(&FN2CUIColors::TextPrimary))
			]
		];
}

void SN2CTagCategoryTree::OnGetChildren(TSharedPtr<FN2CTreeItem> Item, TArray<TSharedPtr<FN2CTreeItem>>& OutChildren)
{
	if (!Item.IsValid())
	{
		return;
	}
	if (!CurrentSearchFilter.IsEmpty())
	{
		if (const TArray<TSharedPtr<FN2CTreeItem>>* Shown = FilteredChildren.Find(Item))
		{
			OutChildren = *Shown;
			return;
		}
	}
	OutChildren = Item->Children;
}

void SN2CTagCategoryTree::OnSelectionChanged(TSharedPtr<FN2CTreeItem> Item, ESelectInfo::Type SelectInfo)
{
	// Direct comes from code: SelectTag, SelectCategory and ClearSelection set SelectedItem themselves, and the tree
	// sends Direct with nothing selected when a refresh, a search or a collapse hides the selected item. Forwarding
	// that emptied the graphs list a frame after every refresh (ui-widgets-code-editor-13)
	if (SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	SelectedItem = Item;
	OnSelectionChangedDelegate.ExecuteIfBound();
}

void SN2CTagCategoryTree::OnExpansionChanged(TSharedPtr<FN2CTreeItem> Item, bool bIsExpanded)
{
	// Remember only the user's own expand or collapse, not a search's expand-all
	if (Item.IsValid() && !bApplyingTreeState && CurrentSearchFilter.IsEmpty())
	{
		Item->bIsExpanded = bIsExpanded;
	}
}

#undef LOCTEXT_NAMESPACE
