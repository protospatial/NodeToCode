// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Widgets/SN2CTagCategoryTree.h"
#include "Core/Style/N2CStyle.h"
#include "Core/N2CTagManager.h"
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

void SN2CTagCategoryTree::Construct(const FArguments& InArgs)
{
	OnSelectionChangedDelegate = InArgs._OnSelectionChanged;

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

void SN2CTagCategoryTree::RefreshData()
{
	BuildTreeData();

	// Apply any existing filter
	if (!CurrentSearchFilter.IsEmpty())
	{
		SetSearchFilter(CurrentSearchFilter);
	}
	else
	{
		FilteredRootItems = RootItems;
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();

		// Expand all categories by default
		for (const TSharedPtr<FN2CTreeItem>& Item : FilteredRootItems)
		{
			TreeView->SetItemExpansion(Item, true);
		}
	}
}

void SN2CTagCategoryTree::BuildTreeData()
{
	RootItems.Empty();
	AllItems.Empty();

	UN2CTagManager& TagManager = UN2CTagManager::Get();
	TArray<FString> Categories = TagManager.GetAllCategories();

	for (const FString& Category : Categories)
	{
		// Create category item
		TSharedPtr<FN2CTreeItem> CategoryItem = MakeShared<FN2CTreeItem>();
		CategoryItem->Name = Category;
		CategoryItem->ItemType = EN2CTreeItemType::Category;
		CategoryItem->bIsExpanded = true;

		// Get tags in this category
		TArray<FN2CTaggedBlueprintGraph> TaggedGraphs = TagManager.GetTagsInCategory(Category);

		// Group by tag name and count
		TMap<FString, int32> TagCounts;
		for (const FN2CTaggedBlueprintGraph& TaggedGraph : TaggedGraphs)
		{
			int32& Count = TagCounts.FindOrAdd(TaggedGraph.Tag);
			Count++;
		}

		// Create tag items
		int32 CategoryTotalCount = 0;
		for (const auto& Pair : TagCounts)
		{
			TSharedPtr<FN2CTreeItem> TagItem = MakeShared<FN2CTreeItem>();
			TagItem->Name = Pair.Key;
			TagItem->Category = Category;
			TagItem->GraphCount = Pair.Value;
			TagItem->ItemType = EN2CTreeItemType::Tag;
			TagItem->Parent = CategoryItem;

			CategoryItem->Children.Add(TagItem);
			AllItems.Add(TagItem);
			CategoryTotalCount += Pair.Value;
		}

		// Sort tags alphabetically
		CategoryItem->Children.Sort([](const TSharedPtr<FN2CTreeItem>& A, const TSharedPtr<FN2CTreeItem>& B)
		{
			return A->Name < B->Name;
		});

		CategoryItem->GraphCount = CategoryTotalCount;
		RootItems.Add(CategoryItem);
		AllItems.Add(CategoryItem);
	}

	// Sort categories alphabetically
	RootItems.Sort([](const TSharedPtr<FN2CTreeItem>& A, const TSharedPtr<FN2CTreeItem>& B)
	{
		return A->Name < B->Name;
	});
}

void SN2CTagCategoryTree::SetSearchFilter(const FString& SearchText)
{
	CurrentSearchFilter = SearchText;
	FilteredRootItems.Empty();

	if (SearchText.IsEmpty())
	{
		FilteredRootItems = RootItems;
	}
	else
	{
		// Filter items that match the search text
		for (const TSharedPtr<FN2CTreeItem>& Category : RootItems)
		{
			TSharedPtr<FN2CTreeItem> FilteredCategory = MakeShared<FN2CTreeItem>();
			FilteredCategory->Name = Category->Name;
			FilteredCategory->ItemType = EN2CTreeItemType::Category;
			FilteredCategory->bIsExpanded = true;

			bool bCategoryMatches = Category->Name.Contains(SearchText, ESearchCase::IgnoreCase);
			int32 FilteredCount = 0;

			for (const TSharedPtr<FN2CTreeItem>& TagItem : Category->Children)
			{
				if (bCategoryMatches || TagItem->Name.Contains(SearchText, ESearchCase::IgnoreCase))
				{
					TSharedPtr<FN2CTreeItem> FilteredTag = MakeShared<FN2CTreeItem>();
					FilteredTag->Name = TagItem->Name;
					FilteredTag->Category = TagItem->Category;
					FilteredTag->GraphCount = TagItem->GraphCount;
					FilteredTag->ItemType = EN2CTreeItemType::Tag;
					FilteredTag->Parent = FilteredCategory;
					FilteredCategory->Children.Add(FilteredTag);
					FilteredCount += TagItem->GraphCount;
				}
			}

			if (bCategoryMatches || FilteredCategory->Children.Num() > 0)
			{
				FilteredCategory->GraphCount = FilteredCount;
				FilteredRootItems.Add(FilteredCategory);
			}
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();

		// Expand all when filtering
		for (const TSharedPtr<FN2CTreeItem>& Item : FilteredRootItems)
		{
			TreeView->SetItemExpansion(Item, true);
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
	for (const TSharedPtr<FN2CTreeItem>& CategoryItem : FilteredRootItems)
	{
		if (CategoryItem->Name == InCategory)
		{
			// Expand the category
			if (TreeView.IsValid())
			{
				TreeView->SetItemExpansion(CategoryItem, true);
			}

			for (const TSharedPtr<FN2CTreeItem>& TagItem : CategoryItem->Children)
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
	for (const TSharedPtr<FN2CTreeItem>& CategoryItem : FilteredRootItems)
	{
		if (CategoryItem->Name == Category)
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
	// Build display text with count
	FText DisplayText = FText::Format(LOCTEXT("ItemDisplayFormat", "{0} ({1})"), FText::FromString(Item->Name), FText::AsNumber(Item->GraphCount));

	// Create the icon widget with dynamic binding for categories
	TSharedRef<SWidget> IconWidget = Item->IsCategory()
		? StaticCastSharedRef<SWidget>(
			SNew(SImage)
			.Image_Lambda([Item]() -> const FSlateBrush*
			{
				return Item->bIsExpanded
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
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

void SN2CTagCategoryTree::OnSelectionChanged(TSharedPtr<FN2CTreeItem> Item, ESelectInfo::Type SelectInfo)
{
	SelectedItem = Item;
	OnSelectionChangedDelegate.ExecuteIfBound();
}

void SN2CTagCategoryTree::OnExpansionChanged(TSharedPtr<FN2CTreeItem> Item, bool bIsExpanded)
{
	if (Item.IsValid())
	{
		Item->bIsExpanded = bIsExpanded;
	}
}

bool SN2CTagCategoryTree::ItemMatchesFilter(const TSharedPtr<FN2CTreeItem>& Item) const
{
	if (CurrentSearchFilter.IsEmpty())
	{
		return true;
	}
	return Item->Name.Contains(CurrentSearchFilter, ESearchCase::IgnoreCase);
}

#undef LOCTEXT_NAMESPACE
