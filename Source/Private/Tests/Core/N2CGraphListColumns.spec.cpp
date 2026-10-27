// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/Widgets/SN2CTaggedGraphsList.h"
#include "Core/Widgets/SN2CGraphListRow.h"
#include "Framework/Application/SlateApplication.h"

// The list's view, to drive its selection the way a click or Ctrl+A does
N2C_TEST_PRIVATE_MEMBER(FN2CGraphListColumnsView, TSharedPtr<SListView<TSharedPtr<FN2CGraphListItem>>> SN2CTaggedGraphsList::*,
	&SN2CTaggedGraphsList::ListView)

/**
 * The tagged graphs list's columns (card 0267). Before, rows were a plain STableRow around a fixed-width box, so cells
 * did not follow the header and nothing sorted; selection was off and only the checkboxes picked rows. Checks:
 * SortItems orders a fixture list by graph name, Blueprint and token count, both ways, ties by name; and on a live
 * list, the header sort reorders the shown rows, and selecting rows in the list checks exactly those rows (and
 * checking a row selects it).
 */
BEGIN_DEFINE_SPEC(FN2CGraphListColumnsSpec, "NodeToCode.UI.GraphListColumns", N2C_TEST_FLAGS)

	static FN2CTagInfo Info(const TCHAR* Graph, const TCHAR* Blueprint)
	{
		FN2CTagInfo Tag;
		Tag.Tag = TEXT("Sort");
		Tag.Category = TEXT("Test");
		Tag.GraphName = Graph;
		Tag.GraphGuid = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		Tag.BlueprintPath = FString::Printf(TEXT("/Game/N2CTest/%s.%s"), Blueprint, Blueprint);
		return Tag;
	}

	static TArray<TSharedPtr<FN2CGraphListItem>> Fixture()
	{
		const TPair<const TCHAR*, const TCHAR*> Rows[] = {
			{ TEXT("tick"), TEXT("BP_Zed") }, { TEXT("BeginPlay"), TEXT("BP_Alpha") }, { TEXT("Attack"), TEXT("BP_Mid") },
			{ TEXT("move"), TEXT("BP_Alpha") } };
		const int32 Tokens[] = { 50, 300, 300, 10 };
		TArray<TSharedPtr<FN2CGraphListItem>> Items;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Rows); ++Index)
		{
			TSharedPtr<FN2CGraphListItem> Item = MakeShared<FN2CGraphListItem>();
			Item->TagInfo = Info(Rows[Index].Key, Rows[Index].Value);
			Item->EstimatedTokens = Tokens[Index];
			Items.Add(Item);
		}
		return Items;
	}

	static FString Names(const TArray<TSharedPtr<FN2CGraphListItem>>& Items)
	{
		TArray<FString> Out;
		for (const TSharedPtr<FN2CGraphListItem>& Item : Items)
		{
			Out.Add(Item->TagInfo.GraphName);
		}
		return FString::Join(Out, TEXT(","));
	}

END_DEFINE_SPEC(FN2CGraphListColumnsSpec)

void FN2CGraphListColumnsSpec::Define()
{
	It(TEXT("sorts rows by name, Blueprint and tokens, both ways"), [this]()
	{
		TArray<TSharedPtr<FN2CGraphListItem>> Items = Fixture();
		SN2CTaggedGraphsList::SortItems(Items, SN2CGraphListRow::Column_GraphName, EColumnSortMode::Ascending);
		TestEqual(TEXT("by name, ascending, ignoring case"), Names(Items), FString(TEXT("Attack,BeginPlay,move,tick")));
		SN2CTaggedGraphsList::SortItems(Items, SN2CGraphListRow::Column_GraphName, EColumnSortMode::Descending);
		TestEqual(TEXT("by name, descending"), Names(Items), FString(TEXT("tick,move,BeginPlay,Attack")));
		SN2CTaggedGraphsList::SortItems(Items, SN2CGraphListRow::Column_Blueprint, EColumnSortMode::Ascending);
		TestEqual(TEXT("by Blueprint, ties by name"), Names(Items), FString(TEXT("BeginPlay,move,Attack,tick")));
		SN2CTaggedGraphsList::SortItems(Items, SN2CGraphListRow::Column_Context, EColumnSortMode::Descending);
		TestEqual(TEXT("by tokens, descending, ties by name descending"), Names(Items), FString(TEXT("BeginPlay,Attack,tick,move")));
		const FString Before = Names(Items);
		SN2CTaggedGraphsList::SortItems(Items, SN2CGraphListRow::Column_GraphName, EColumnSortMode::None);
		TestEqual(TEXT("no sort leaves the order"), Names(Items), Before);
	});

	It(TEXT("sorts the shown rows from the header and keeps selection and checks the same set"), [this]()
	{
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Slate is not initialized; skipping"));
			return;
		}
		const TSharedRef<SN2CTaggedGraphsList> List = SNew(SN2CTaggedGraphsList);
		List->SetGraphs({ Info(TEXT("tick"), TEXT("BP_Zed")), Info(TEXT("BeginPlay"), TEXT("BP_Alpha")), Info(TEXT("Attack"), TEXT("BP_Mid")) });
		List->SetSort(SN2CGraphListRow::Column_GraphName, EColumnSortMode::Ascending);
		TestEqual(TEXT("the header sort orders the shown rows"), Names(List->GetShownItems()), FString(TEXT("Attack,BeginPlay,tick")));

		const TSharedPtr<SListView<TSharedPtr<FN2CGraphListItem>>> View = (*List).*N2CTest::GetPrivateMember(N2CTest::FN2CGraphListColumnsView());
		if (!TestTrue(TEXT("the list view"), View.IsValid()))
		{
			return;
		}
		// Selecting in the list (a click, Shift or Ctrl click, Ctrl+A) checks those rows
		View->SetItemSelection(List->GetShownItems()[0], true, ESelectInfo::OnMouseClick);
		View->SetItemSelection(List->GetShownItems()[2], true, ESelectInfo::OnMouseClick);
		TestEqual(TEXT("two rows checked"), List->GetSelectedCount(), 2);
		TestTrue(TEXT("Attack is checked"), List->GetShownItems()[0]->bIsSelected);
		TestFalse(TEXT("BeginPlay is not"), List->GetShownItems()[1]->bIsSelected);

		// Select All from the header checks every row, and the list selects them too
		List->SelectAll();
		TestEqual(TEXT("the list selects every checked row"), View->GetNumItemsSelected(), 3);
		List->DeselectAll();
		TestEqual(TEXT("and none after Deselect All"), View->GetNumItemsSelected(), 0);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
