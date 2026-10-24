// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"

/**
 * FN2CMcpContentBrowserUtils::CalculatePagination, which read-content-browser-path uses to slice a listing. The
 * indices it returns are used to index the listing, so they must stay in [0, TotalItems] for any page and page size
 * a client can send. Review id: mcp-tools-contentbrowser-fs-2 (int32 overflow gave a negative start index).
 * NodeToCode.Robustness.Pagination keeps the original p0-6 regression case.
 */
BEGIN_DEFINE_SPEC(FN2CPaginationSpec, "NodeToCode.Pagination", N2C_TEST_FLAGS)

	int32 Start = -1;
	int32 End = -1;
	bool bMore = false;

	bool Page(int32 Total, int32 PageNumber, int32 PageSize)
	{
		Start = -1;
		End = -1;
		bMore = true;
		return FN2CMcpContentBrowserUtils::CalculatePagination(Total, PageNumber, PageSize, Start, End, bMore);
	}

	void TestIndicesInRange(const FString& What, int32 Total)
	{
		TestTrue(FString::Printf(TEXT("%s: 0 <= start (%d) <= end (%d) <= total (%d)"), *What, Start, End, Total),
			Start >= 0 && Start <= End && End <= FMath::Max(Total, 0));
	}

END_DEFINE_SPEC(FN2CPaginationSpec)

void FN2CPaginationSpec::Define()
{
	It(TEXT("slices the first, middle and last pages"), [this]()
	{
		TestTrue(TEXT("page 1"), Page(50, 1, 20));
		TestEqual(TEXT("page 1 start"), Start, 0);
		TestEqual(TEXT("page 1 end"), End, 20);
		TestTrue(TEXT("page 1 has more"), bMore);

		TestTrue(TEXT("page 2"), Page(50, 2, 20));
		TestEqual(TEXT("page 2 start"), Start, 20);
		TestEqual(TEXT("page 2 end"), End, 40);
		TestTrue(TEXT("page 2 has more"), bMore);

		TestTrue(TEXT("page 3"), Page(50, 3, 20));
		TestEqual(TEXT("page 3 start"), Start, 40);
		TestEqual(TEXT("page 3 end"), End, 50);
		TestFalse(TEXT("page 3 is the last"), bMore);

		TestTrue(TEXT("exact fit"), Page(40, 2, 20));
		TestEqual(TEXT("exact fit end"), End, 40);
		TestFalse(TEXT("exact fit is the last"), bMore);
	});

	It(TEXT("refuses a page past the end"), [this]()
	{
		AddExpectedErrorPlain(TEXT("is beyond available items"), EAutomationExpectedErrorFlags::Contains, 0);
		TestFalse(TEXT("page 4 of 50 by 20"), Page(50, 4, 20));
		TestFalse(TEXT("page 2 of 20 by 20"), Page(20, 2, 20));
		TestFalse(TEXT("page 2 of 1 by 1"), Page(1, 2, 1));
	});

	It(TEXT("refuses pages whose start index would overflow int32"), [this]()
	{
		AddExpectedErrorPlain(TEXT("is beyond available items"), EAutomationExpectedErrorFlags::Contains, 0);
		TestFalse(TEXT("INT32_MAX page"), Page(50, MAX_int32, 200));
		// (10737420 - 1) * 200 is just past INT32_MAX: wrapped negative before the int64 fix
		TestFalse(TEXT("first page that wraps at page size 200"), Page(50, 10737420, 200));
		TestFalse(TEXT("INT32_MAX page and page size"), Page(50, MAX_int32, MAX_int32));
		TestFalse(TEXT("INT32_MAX page of INT32_MAX items"), Page(MAX_int32, MAX_int32, 200));
	});

	It(TEXT("keeps the end index in range when page size is huge"), [this]()
	{
		TestTrue(TEXT("page 1 with INT32_MAX page size"), Page(10, 1, MAX_int32));
		TestIndicesInRange(TEXT("INT32_MAX page size"), 10);
		TestEqual(TEXT("end is the item count"), End, 10);
		TestFalse(TEXT("nothing more"), bMore);

		TestTrue(TEXT("last page of INT32_MAX items"), Page(MAX_int32, 2, MAX_int32 / 2 + 1));
		TestIndicesInRange(TEXT("INT32_MAX items"), MAX_int32);
	});

	It(TEXT("returns an empty, in-range slice for an empty listing"), [this]()
	{
		TestTrue(TEXT("page 1 of nothing"), Page(0, 1, 20));
		TestEqual(TEXT("start"), Start, 0);
		TestEqual(TEXT("end"), End, 0);
		TestFalse(TEXT("nothing more"), bMore);

		// Any page of an empty listing is empty rather than an error, and must not index the listing
		Page(0, MAX_int32, 200);
		TestIndicesInRange(TEXT("INT32_MAX page of nothing"), 0);
	});

	It(TEXT("refuses zero and negative page numbers and page sizes"), [this]()
	{
		AddExpectedErrorPlain(TEXT("Invalid pagination parameters"), EAutomationExpectedErrorFlags::Contains, 0);
		TestFalse(TEXT("page 0"), Page(50, 0, 20));
		TestFalse(TEXT("page -1"), Page(50, -1, 20));
		TestFalse(TEXT("page INT32_MIN"), Page(50, MIN_int32, 20));
		TestFalse(TEXT("page size 0"), Page(50, 1, 0));
		TestFalse(TEXT("page size -20"), Page(50, 1, -20));
		TestFalse(TEXT("page size INT32_MIN"), Page(50, 2, MIN_int32));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
