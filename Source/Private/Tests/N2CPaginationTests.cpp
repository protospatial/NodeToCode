// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Utils/N2CMcpContentBrowserUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CPaginationTest, "NodeToCode.Robustness.Pagination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CPaginationTest::RunTest(const FString& Parameters)
{
	int32 Start = -1, End = -1;
	bool bMore = false;

	AddExpectedError(TEXT("beyond available items"), EAutomationExpectedErrorFlags::Contains, 0);
	TestFalse(TEXT("INT32_MAX page is out of range"), FN2CMcpContentBrowserUtils::CalculatePagination(50, MAX_int32, 200, Start, End, bMore));
	TestFalse(TEXT("page that would wrap int32 is out of range"), FN2CMcpContentBrowserUtils::CalculatePagination(50, 10737420, 200, Start, End, bMore));

	TestTrue(TEXT("first page"), FN2CMcpContentBrowserUtils::CalculatePagination(50, 1, 20, Start, End, bMore));
	TestEqual(TEXT("first page start"), Start, 0);
	TestEqual(TEXT("first page end"), End, 20);
	TestTrue(TEXT("first page has more"), bMore);

	TestTrue(TEXT("last page"), FN2CMcpContentBrowserUtils::CalculatePagination(50, 3, 20, Start, End, bMore));
	TestEqual(TEXT("last page start"), Start, 40);
	TestEqual(TEXT("last page end"), End, 50);
	TestFalse(TEXT("last page has no more"), bMore);

	TestTrue(TEXT("empty listing page 1"), FN2CMcpContentBrowserUtils::CalculatePagination(0, 1, 20, Start, End, bMore));
	TestTrue(TEXT("empty listing indices non-negative"), Start >= 0 && End >= 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
