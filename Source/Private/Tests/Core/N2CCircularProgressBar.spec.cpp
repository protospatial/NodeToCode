// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/Widgets/SN2CCircularProgressBar.h"
#include "Tests/N2CTestHelpers.h"

/**
 * The context ring's drawn value (card 0522): the Percent attribute, clamped, unless the docs-shot harness pinned every
 * ring to one value. The harness pins rings empty so that a system prompt edit (which changes the request's share of
 * the context window) does not change the screenshots. No renderer needed.
 */
BEGIN_DEFINE_SPEC(FN2CCircularProgressBarSpec, "NodeToCode.UI.CircularProgressBar", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CCircularProgressBarSpec)

void FN2CCircularProgressBarSpec::Define()
{
	AfterEach([]()
	{
		SN2CCircularProgressBar::SetPinnedPercentForCapture(TOptional<float>());
	});

	It(TEXT("draws its Percent, clamped to 0-1, when nothing is pinned"), [this]()
	{
		float Value = 0.3f;
		const TSharedRef<SN2CCircularProgressBar> Ring = SNew(SN2CCircularProgressBar).Percent_Lambda([&Value]() { return Value; });
		TestFalse(TEXT("nothing pinned by default"), SN2CCircularProgressBar::GetPinnedPercentForCapture().IsSet());
		TestEqual(TEXT("follows the attribute"), Ring->GetDrawnPercent(), 0.3f);
		Value = 0.6f;
		TestEqual(TEXT("follows a new value"), Ring->GetDrawnPercent(), 0.6f);
		Value = 1.7f;
		TestEqual(TEXT("clamped above"), Ring->GetDrawnPercent(), 1.0f);
		Value = -0.2f;
		TestEqual(TEXT("clamped below"), Ring->GetDrawnPercent(), 0.0f);
	});

	It(TEXT("draws the pinned value whatever its Percent says, and its Percent again once unpinned"), [this]()
	{
		const TSharedRef<SN2CCircularProgressBar> Small = SNew(SN2CCircularProgressBar).Percent(0.02f);
		const TSharedRef<SN2CCircularProgressBar> Large = SNew(SN2CCircularProgressBar).Percent(0.9f);
		SN2CCircularProgressBar::SetPinnedPercentForCapture(0.0f);
		TestEqual(TEXT("a small ring draws the pin"), Small->GetDrawnPercent(), 0.0f);
		TestEqual(TEXT("a large ring draws the pin"), Large->GetDrawnPercent(), 0.0f);
		SN2CCircularProgressBar::SetPinnedPercentForCapture(0.25f);
		TestEqual(TEXT("another pin"), Large->GetDrawnPercent(), 0.25f);
		SN2CCircularProgressBar::SetPinnedPercentForCapture(TOptional<float>());
		TestEqual(TEXT("unpinned: its own value again"), Large->GetDrawnPercent(), 0.9f);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
