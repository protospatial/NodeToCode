// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Python/N2CPythonTranslation.h"

/**
 * UN2CPythonTranslation (card 0176), the batch table behind NodeToCodeTools' translation tools, on every engine: a
 * refused start returns the translate-batch tool's error and keeps no batch, and unknown ids are reported as such.
 * Translation, progress and cancel against a stub provider are NodeToCode.Toolsets.PythonTests (UE 5.8).
 */
BEGIN_DEFINE_SPEC(FN2CPythonTranslationSpec, "NodeToCode.Python.Translation", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CPythonTranslationSpec)

void FN2CPythonTranslationSpec::Define()
{
	Describe(TEXT("StartBatch"), [this]()
	{
		It(TEXT("refuses a call with neither targets nor a tag and keeps no batch"), [this]()
		{
			const FString Before = UN2CPythonTranslation::GetBatchStatus(FString()).BatchId;
			FString Error;
			const FString Id = UN2CPythonTranslation::StartBatch({}, {}, FString(), FString(), FString(), FString(),
				FString(), Error);
			TestTrue(TEXT("no id"), Id.IsEmpty());
			TestTrue(TEXT("INVALID_ARGUMENT"), Error.Contains(TEXT("INVALID_ARGUMENT")));
			TestEqual(TEXT("the latest batch is unchanged"), UN2CPythonTranslation::GetBatchStatus(FString()).BatchId, Before);
		});

		It(TEXT("refuses a missing Blueprint with the target's error"), [this]()
		{
			FString Error;
			const FString Id = UN2CPythonTranslation::StartBatch({ TEXT("/Game/N2CTestNoSuchFolder/BP_Missing") },
				{ TEXT("EventGraph") }, FString(), FString(), FString(), FString(), FString(), Error);
			TestTrue(TEXT("no id"), Id.IsEmpty());
			TestTrue(TEXT("NO_VALID_ITEMS"), Error.Contains(TEXT("NO_VALID_ITEMS")));
			TestTrue(TEXT("names the target"), Error.Contains(TEXT("BP_Missing")));
		});

		It(TEXT("refuses an unknown provider before anything is sent"), [this]()
		{
			FString Error;
			const FString Id = UN2CPythonTranslation::StartBatch({ TEXT("/Game/N2CTestNoSuchFolder/BP_Missing") },
				{}, FString(), FString(), FString(), TEXT("acme"), FString(), Error);
			TestTrue(TEXT("no id"), Id.IsEmpty());
			TestTrue(TEXT("names the provider"), Error.Contains(TEXT("acme")));
		});
	});

	Describe(TEXT("unknown batches"), [this]()
	{
		It(TEXT("has no status for an unknown id"), [this]()
		{
			TestTrue(TEXT("empty status"), UN2CPythonTranslation::GetBatchStatus(TEXT("no-such-batch")).BatchId.IsEmpty());
		});

		It(TEXT("cannot cancel an unknown id"), [this]()
		{
			TestFalse(TEXT("not cancelled"), UN2CPythonTranslation::CancelBatch(TEXT("no-such-batch")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
