// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/N2CTagTypes.h"
#include "Core/Widgets/SN2CBatchProgressModal.h"
#include "Framework/Application/SlateApplication.h"

/**
 * The batch progress dialog (card 0489, and the BatchModal spec card 0202 left on 0259). Gate 0389 translated EventGraph
 * of BP_G and a function N2CFn in each of BP_A..BP_G, with the last three failing: every N2CFn update landed on the first
 * N2CFn row, because rows were found by graph name. Rows are now addressed by the orchestrator's item index and labelled
 * with their Blueprint. The final and cancelled states stay with card 0259.
 */
BEGIN_DEFINE_SPEC(FN2CBatchModalSpec, "NodeToCode.UI.BatchModal", N2C_TEST_FLAGS)

	TSharedPtr<SN2CBatchProgressModal> Modal;

	/** The gate 0389 batch: BP_G's EventGraph, then N2CFn in BP_A..BP_G */
	static TArray<FN2CTagInfo> GateBatch()
	{
		TArray<FN2CTagInfo> Graphs;
		auto Add = [&Graphs](const TCHAR* Graph, const TCHAR* Blueprint)
		{
			FN2CTagInfo Info;
			Info.GraphName = Graph;
			Info.GraphGuid = FGuid::NewGuid().ToString();
			Info.BlueprintPath = FString::Printf(TEXT("/Game/N2CTest/Batch/%s.%s"), Blueprint, Blueprint);
			Graphs.Add(Info);
		};
		Add(TEXT("EventGraph"), TEXT("BP_G"));
		for (const TCHAR* Blueprint : { TEXT("BP_A"), TEXT("BP_B"), TEXT("BP_C"), TEXT("BP_D"), TEXT("BP_E"), TEXT("BP_F"), TEXT("BP_G") })
		{
			Add(TEXT("N2CFn"), Blueprint);
		}
		return Graphs;
	}

	EN2CBatchProgressItemStatus StatusAt(int32 Index) const
	{
		return Modal->GetItems()[Index]->Status;
	}

END_DEFINE_SPEC(FN2CBatchModalSpec)

void FN2CBatchModalSpec::Define()
{
	BeforeEach([this]()
	{
		Modal.Reset();
		if (FSlateApplication::IsInitialized())
		{
			Modal = SNew(SN2CBatchProgressModal);
		}
	});

	AfterEach([this]()
	{
		Modal.Reset();
	});

	It(TEXT("lists each graph with its Blueprint, in batch order"), [this]()
	{
		if (!Modal.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		Modal->Initialize(GateBatch());
		const TArray<TSharedPtr<FN2CBatchProgressItem>>& Items = Modal->GetItems();
		if (!TestEqual(TEXT("eight rows"), Items.Num(), 8)) { return; }
		TestEqual(TEXT("first row"), SN2CBatchProgressModal::GetItemLabel(*Items[0]), FString(TEXT("EventGraph (BP_G)")));
		TestEqual(TEXT("second row"), SN2CBatchProgressModal::GetItemLabel(*Items[1]), FString(TEXT("N2CFn (BP_A)")));
		TestEqual(TEXT("last row"), SN2CBatchProgressModal::GetItemLabel(*Items[7]), FString(TEXT("N2CFn (BP_G)")));
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("row %d starts pending"), Index), StatusAt(Index) == EN2CBatchProgressItemStatus::Pending);
		}
		FN2CBatchProgressItem NoBlueprint(TEXT("EventGraph"));
		TestEqual(TEXT("a row without a Blueprint shows the graph name"), SN2CBatchProgressModal::GetItemLabel(NoBlueprint), FString(TEXT("EventGraph")));
	});

	It(TEXT("gives each same-named graph its own status in a batch with mixed results (gate 0389)"), [this]()
	{
		if (!Modal.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		const TArray<FN2CTagInfo> Graphs = GateBatch();
		Modal->Initialize(Graphs);
		const FString Unauthorized = TEXT("Anthropic request failed with HTTP 401 (Auth): invalid x-api-key");

		// As the orchestrator reports them: progress for item i, then its result; items 5 to 7 fail
		for (int32 Index = 0; Index < Graphs.Num(); ++Index)
		{
			Modal->SetProgress(Index, Graphs.Num(), Graphs[Index].GraphName);
			TestTrue(FString::Printf(TEXT("item %d is in progress"), Index), StatusAt(Index) == EN2CBatchProgressItemStatus::InProgress);
			for (int32 Later = Index + 1; Later < Graphs.Num(); ++Later)
			{
				TestTrue(FString::Printf(TEXT("while item %d runs, item %d is still pending"), Index, Later), StatusAt(Later) == EN2CBatchProgressItemStatus::Pending);
			}
			const bool bSuccess = Index < 5;
			Modal->MarkItemCompleteAt(Index, bSuccess, bSuccess ? FString() : Unauthorized);
		}

		for (int32 Index = 0; Index < Graphs.Num(); ++Index)
		{
			const FN2CBatchProgressItem& Item = *Modal->GetItems()[Index];
			const bool bSuccess = Index < 5;
			TestTrue(FString::Printf(TEXT("%s has its own result"), *SN2CBatchProgressModal::GetItemLabel(Item)),
				Item.Status == (bSuccess ? EN2CBatchProgressItemStatus::Completed : EN2CBatchProgressItemStatus::Failed));
			TestEqual(FString::Printf(TEXT("%s has its own error"), *SN2CBatchProgressModal::GetItemLabel(Item)), Item.ErrorMessage,
				bSuccess ? FString() : Unauthorized);
		}
	});

	It(TEXT("refuses an item index that is not a row, and changes nothing"), [this]()
	{
		if (!Modal.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		Modal->Initialize(GateBatch());
		AddExpectedMessage(TEXT("MarkItemCompleteAt: item 8 is not one of the 8 items"), EAutomationExpectedMessageFlags::Contains, 1);
		Modal->MarkItemCompleteAt(8, false, TEXT("x"));
		for (int32 Index = 0; Index < 8; ++Index)
		{
			TestTrue(FString::Printf(TEXT("row %d unchanged"), Index), StatusAt(Index) == EN2CBatchProgressItemStatus::Pending);
		}
	});

	It(TEXT("says Cancelling... after Cancel, then ends with the unfinished rows Skipped (card 0255)"), [this]()
	{
		if (!Modal.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		const TArray<FN2CTagInfo> Graphs = GateBatch();
		Modal->Initialize(Graphs);
		Modal->SetProgress(0, Graphs.Num(), Graphs[0].GraphName);
		Modal->MarkItemCompleteAt(0, true);
		Modal->SetProgress(1, Graphs.Num(), Graphs[1].GraphName);

		Modal->SetCancelling();
		TestTrue(TEXT("cancelling"), Modal->IsCancelling());
		TestFalse(TEXT("not complete yet"), Modal->IsComplete());
		TestEqual(TEXT("the status line"), Modal->GetStatusText().ToString(), FString(TEXT("Cancelling...")));

		FN2CBatchTranslationResult Result;
		Result.TotalCount = Graphs.Num();
		Result.SuccessCount = 1;
		Result.SkippedCount = Graphs.Num() - 1;
		Result.bCancelled = true;
		Modal->SetResult(Result);
		TestTrue(TEXT("complete"), Modal->IsComplete());
		TestFalse(TEXT("no longer cancelling"), Modal->IsCancelling());
		TestEqual(TEXT("the summary"), Modal->GetStatusText().ToString(), FString(TEXT("Cancelled - 1 succeeded, 0 failed, 7 skipped")));
		TestTrue(TEXT("the finished row keeps its result"), StatusAt(0) == EN2CBatchProgressItemStatus::Completed);
		for (int32 Index = 1; Index < Graphs.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("row %d is skipped"), Index), StatusAt(Index) == EN2CBatchProgressItemStatus::Skipped);
		}
		TestEqual(TEXT("a skipped row's label"), SN2CBatchProgressModal::GetStatusLabel(EN2CBatchProgressItemStatus::Skipped).ToString(),
			FString(TEXT("Skipped (cancelled)")));

		Modal->Reset();
		TestFalse(TEXT("Reset clears the cancelled state"), Modal->IsCancelling() || Modal->IsComplete());
	});

	It(TEXT("still marks a single translation by name"), [this]()
	{
		if (!Modal.IsValid()) { AddInfo(TEXT("Slate is not initialized; skipping")); return; }
		Modal->Initialize(TArray<FString>{ TEXT("EventGraph") });
		Modal->SetProgress(0, 1, TEXT("EventGraph"));
		Modal->MarkItemComplete(TEXT("EventGraph"), true);
		TestTrue(TEXT("the one row completed"), StatusAt(0) == EN2CBatchProgressItemStatus::Completed);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
