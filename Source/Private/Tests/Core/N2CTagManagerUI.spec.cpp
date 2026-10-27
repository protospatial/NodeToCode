// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/Widgets/SN2CTagCategoryTree.h"
#include "Core/Widgets/SN2CTagManager.h"
#include "Core/Widgets/SN2CTaggedGraphsList.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Tests/N2CTestHelpers.h"
#include "Widgets/Views/STreeView.h"

// The tag manager's tree and list, and the tree's STreeView (ticked by hand: the widgets are not in a window)
N2C_TEST_PRIVATE_MEMBER(FN2CTagUiTree, TSharedPtr<SN2CTagCategoryTree> SN2CTagManager::*, &SN2CTagManager::CategoryTree)
N2C_TEST_PRIVATE_MEMBER(FN2CTagUiList, TSharedPtr<SN2CTaggedGraphsList> SN2CTagManager::*, &SN2CTagManager::GraphsList)
N2C_TEST_PRIVATE_MEMBER(FN2CTagUiTreeView, TSharedPtr<STreeView<TSharedPtr<FN2CTreeItem>>> SN2CTagCategoryTree::*,
	&SN2CTagCategoryTree::TreeView)
N2C_TEST_PRIVATE_MEMBER(FN2CTagUiTreeRoots, TArray<TSharedPtr<FN2CTreeItem>> SN2CTagCategoryTree::*,
	&SN2CTagCategoryTree::RootItems)

/**
 * The Tag Manager's tree and graphs list (card 0266), on a headless SN2CTagManager fed from an in-memory tag source
 * (no graph state is read or written).
 * - ui-widgets-code-editor-13: the tree rebuilt every item on refresh and search, so on its next tick STreeView dropped
 *   the selection and the list emptied itself. The selected tag must still be selected, and its graphs listed, after
 *   RefreshData, a search keystroke and the tree's tick.
 * - ui-widgets-code-editor-14: category and search views listed a graph once per tag, so Select All + Batch
 *   Translate sent it twice. Each graph must be listed once, and the selection hold each graph once.
 * - ui-widgets-code-editor-33: the header read "Category: Search Results" and "Category: ".
 * - imp-ui-widgets-code-editor-21: checked graphs stay checked when the same view refreshes (after a batch).
 */
BEGIN_DEFINE_SPEC(FN2CTagManagerUISpec, "NodeToCode.UI.TagManager", N2C_TEST_FLAGS)

	/** What the tag source returns: every (graph, tag) pair */
	TSharedPtr<TArray<FN2CTaggedBlueprintGraph>> Tags;
	TSharedPtr<SN2CTagManager> Manager;

	const FGuid Fire = FGuid(0x11111111, 0x22222222, 0x33333333, 0x44444444);
	const FGuid Reload = FGuid(0x55555555, 0x66666666, 0x77777777, 0x88888888);
	const FGuid Patrol = FGuid(0x99999999, 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC);

	void AddTag(const FGuid& Guid, const TCHAR* GraphName, const TCHAR* Blueprint, const TCHAR* Tag, const TCHAR* Category)
	{
		FN2CTaggedBlueprintGraph Tagged;
		Tagged.GraphGuid = Guid;
		Tagged.GraphName = GraphName;
		Tagged.OwningBlueprint = FSoftObjectPath(Blueprint);
		Tagged.Tag = Tag;
		Tagged.Category = Category;
		Tags->Add(Tagged);
	}

	SN2CTagCategoryTree& Tree() const { return *((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CTagUiTree())); }
	SN2CTaggedGraphsList& List() const { return *((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CTagUiList())); }
	STreeView<TSharedPtr<FN2CTreeItem>>& TreeView() const
	{
		return *(Tree().*N2CTest::GetPrivateMember(N2CTest::FN2CTagUiTreeView()));
	}

	/** One frame of the tree: where STreeView drops selected items it no longer holds and says so */
	void TickTree() const
	{
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(300.0, 400.0), FSlateLayoutTransform());
		TreeView().Tick(Geometry, FSlateApplication::Get().GetCurrentTime(), 1.0f / 60.0f);
	}

	/** The graph names listed, sorted and joined: "Fire, Reload" */
	FString ListedNames() const
	{
		TArray<FString> Names;
		for (const TSharedPtr<FN2CGraphListItem>& Item : List().GetShownItems())
		{
			Names.Add(Item->TagInfo.GraphName);
		}
		Names.Sort();
		return FString::Join(Names, TEXT(", "));
	}

	/** The tree's item for a tag, or null */
	TSharedPtr<FN2CTreeItem> FindTagItem(const FString& Category, const FString& Tag) const
	{
		for (const TSharedPtr<FN2CTreeItem>& Root : Tree().*N2CTest::GetPrivateMember(N2CTest::FN2CTagUiTreeRoots()))
		{
			if (Root->Name == Category)
			{
				for (const TSharedPtr<FN2CTreeItem>& Child : Root->Children)
				{
					if (Child->Name == Tag)
					{
						return Child;
					}
				}
			}
		}
		return nullptr;
	}

END_DEFINE_SPEC(FN2CTagManagerUISpec)

void FN2CTagManagerUISpec::Define()
{
	BeforeEach([this]()
	{
		// Fire is tagged Combat and Weapons, both in Gameplay; Reload has Combat; Patrol has AI in Default
		Tags = MakeShared<TArray<FN2CTaggedBlueprintGraph>>();
		AddTag(Fire, TEXT("Fire"), TEXT("/Game/N2CTestUI/BP_Rifle.BP_Rifle"), TEXT("Combat"), TEXT("Gameplay"));
		AddTag(Fire, TEXT("Fire"), TEXT("/Game/N2CTestUI/BP_Rifle.BP_Rifle"), TEXT("Weapons"), TEXT("Gameplay"));
		AddTag(Reload, TEXT("Reload"), TEXT("/Game/N2CTestUI/BP_Rifle.BP_Rifle"), TEXT("Combat"), TEXT("Gameplay"));
		AddTag(Patrol, TEXT("Patrol"), TEXT("/Game/N2CTestUI/BP_Guard.BP_Guard"), TEXT("AI"), TEXT("Default"));

		const TSharedPtr<TArray<FN2CTaggedBlueprintGraph>> Source = Tags;
		Manager = SNew(SN2CTagManager)
			.ShowActionBar(false)
			.TagSource([Source]() { return *Source; });
	});

	AfterEach([this]()
	{
		Manager.Reset();
		Tags.Reset();
	});

	Describe(TEXT("each graph once"), [this]()
	{
		It(TEXT("lists a graph with two tags in the category once, with both tags"), [this]()
		{
			Manager->SelectCategory(TEXT("Gameplay"));
			TestEqual(TEXT("Fire and Reload, each once"), ListedNames(), FString(TEXT("Fire, Reload")));

			const TSharedPtr<FN2CGraphListItem>* FireRow = List().GetShownItems().FindByPredicate(
				[](const TSharedPtr<FN2CGraphListItem>& Item) { return Item->TagInfo.GraphName == TEXT("Fire"); });
			TestTrue(TEXT("Fire's row carries both of its tags"), FireRow && (*FireRow)->Tags.Num() == 2);

			const TSharedPtr<FN2CTreeItem> Gameplay = Tree().GetSelectedItem();
			TestTrue(TEXT("the category counts two graphs, not three tags"), Gameplay.IsValid() && Gameplay->GraphCount == 2);
		});

		It(TEXT("lists each graph once in the search results"), [this]()
		{
			Manager->SetSearchFilter(TEXT("Rifle"));
			TestEqual(TEXT("the Rifle's graphs, each once"), ListedNames(), FString(TEXT("Fire, Reload")));
		});

		It(TEXT("finds graphs by a tag name in the search"), [this]()
		{
			Manager->SetSearchFilter(TEXT("weap"));
			TestEqual(TEXT("Fire, by its Weapons tag"), ListedNames(), FString(TEXT("Fire")));
		});

		It(TEXT("selects each graph once for a batch after Select All, and removes every tag of the row"), [this]()
		{
			Manager->SelectCategory(TEXT("Gameplay"));
			List().SelectAll();
			const TArray<FN2CTagInfo> Selected = Manager->GetSelectedGraphs();
			TestEqual(TEXT("two graphs selected"), Selected.Num(), 2);
			TestEqual(TEXT("no graph twice"), SN2CTaggedGraphsList::UniqueGraphs(Selected).Num(), Selected.Num());
			TestEqual(TEXT("Remove Selected gets the three (graph, tag) entries"), Manager->GetSelectedTagEntries().Num(), 3);
		});

		It(TEXT("keeps a Blueprint's copy apart: same graph GUID, another Blueprint"), [this]()
		{
			AddTag(Fire, TEXT("Fire"), TEXT("/Game/N2CTestUI/BP_RifleCopy.BP_RifleCopy"), TEXT("Combat"), TEXT("Gameplay"));
			Manager->RefreshData();
			Manager->SelectTag(TEXT("Combat"), TEXT("Gameplay"));
			TestEqual(TEXT("both Fire graphs and Reload"), ListedNames(), FString(TEXT("Fire, Fire, Reload")));
		});
	});

	Describe(TEXT("selection across refreshes"), [this]()
	{
		It(TEXT("keeps the selected tag selected, and its graphs listed, after RefreshData and the tree's tick"), [this]()
		{
			Manager->SelectTag(TEXT("Combat"), TEXT("Gameplay"));
			TickTree();
			const TSharedPtr<FN2CTreeItem> Before = FindTagItem(TEXT("Gameplay"), TEXT("Combat"));

			// A tag added elsewhere, then the refresh a batch or a tag event makes
			AddTag(Patrol, TEXT("Patrol"), TEXT("/Game/N2CTestUI/BP_Guard.BP_Guard"), TEXT("Stealth"), TEXT("Default"));
			Manager->RefreshData();
			TickTree();

			TestTrue(TEXT("the tag is still selected"), Tree().IsSelectedTag() && Tree().GetSelectedTag() == TEXT("Combat"));
			TestTrue(TEXT("the tree item was updated in place"), Before.IsValid() && FindTagItem(TEXT("Gameplay"), TEXT("Combat")) == Before);
			TestTrue(TEXT("the tree still shows it selected"), Before.IsValid() && TreeView().IsItemSelected(Before));
			TestEqual(TEXT("its graphs are still listed"), ListedNames(), FString(TEXT("Fire, Reload")));
			TestEqual(TEXT("the header names the tag"), List().GetHeaderText().ToString(), FString(TEXT("Gameplay > Combat")));
		});

		It(TEXT("keeps the selection through a search and shows it again when the search is cleared"), [this]()
		{
			Manager->SelectTag(TEXT("Combat"), TEXT("Gameplay"));
			TickTree();

			Manager->SetSearchFilter(TEXT("Pat"));
			TickTree();
			TestEqual(TEXT("the first keystroke lists the matches"), ListedNames(), FString(TEXT("Patrol")));
			TestEqual(TEXT("the header says it is a search"), List().GetHeaderText().ToString(), FString(TEXT("Search results for \"Pat\"")));

			Manager->SetSearchFilter(FString());
			TickTree();
			const TSharedPtr<FN2CTreeItem> Combat = FindTagItem(TEXT("Gameplay"), TEXT("Combat"));
			TestTrue(TEXT("the tag is selected again"), Combat.IsValid() && TreeView().IsItemSelected(Combat) && Tree().GetSelectedItem() == Combat);
			TestEqual(TEXT("its graphs are listed again"), ListedNames(), FString(TEXT("Fire, Reload")));
		});

		It(TEXT("keeps checked graphs checked when the same view refreshes"), [this]()
		{
			Manager->SelectTag(TEXT("Combat"), TEXT("Gameplay"));
			List().SelectAll();
			Manager->RefreshData();
			TickTree();
			TestEqual(TEXT("both graphs are still checked"), Manager->GetSelectedCount(), 2);
		});

		It(TEXT("clears the selection when its tag is gone, and prompts"), [this]()
		{
			Manager->SelectTag(TEXT("AI"), TEXT("Default"));
			Tags->RemoveAll([](const FN2CTaggedBlueprintGraph& Tagged) { return Tagged.Tag == TEXT("AI"); });
			Manager->RefreshData();
			TickTree();
			TestFalse(TEXT("nothing is selected"), Tree().GetSelectedItem().IsValid());
			TestEqual(TEXT("the list is empty"), List().GetShownItems().Num(), 0);
			TestEqual(TEXT("the header prompts"), List().GetHeaderText().ToString(), FString(TEXT("Select a category or tag")));
		});
	});

	Describe(TEXT("header"), [this]()
	{
		It(TEXT("names the view and explains an empty list"), [this]()
		{
			TestEqual(TEXT("nothing selected"), List().GetHeaderText().ToString(), FString(TEXT("Select a category or tag")));
			Manager->SelectCategory(TEXT("Gameplay"));
			TestEqual(TEXT("a category"), List().GetHeaderText().ToString(), FString(TEXT("Category: Gameplay")));
			Manager->SetSearchFilter(TEXT("zzz"));
			TestEqual(TEXT("no match"), List().GetEmptyText().ToString(), FString(TEXT("No graphs match \"zzz\".")));
			TestEqual(TEXT("an empty search result"), List().GetShownItems().Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
