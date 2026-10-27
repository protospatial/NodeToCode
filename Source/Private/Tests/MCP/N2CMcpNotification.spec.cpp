// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Utils/N2CMcpNotification.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateBrush.h"

/**
 * Card 0295: the Blueprint tools' toasts (FN2CMcpNotification) use brushes the editor style defines. Before it the
 * tools asked for success and error brush names that no UE 5.6-5.8 style has, and every toast showed the checkerboard
 * default brush. Review ids: mcp-tools-variables-24, mcp-tools-graph-functions-23.
 */
BEGIN_DEFINE_SPEC(FN2CMcpNotificationSpec, "NodeToCode.MCP.Notification", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CMcpNotificationSpec)

void FN2CMcpNotificationSpec::Define()
{
	It(TEXT("uses success and error icons the editor style defines, not its default brush"), [this]()
	{
		const FSlateBrush* const Default = FAppStyle::Get().GetDefaultBrush();
		const FSlateBrush* const Success = FN2CMcpNotification::Icon(true);
		const FSlateBrush* const Error = FN2CMcpNotification::Icon(false);
		TestNotNull(TEXT("a success icon"), Success);
		TestNotNull(TEXT("an error icon"), Error);
		TestTrue(TEXT("the success icon is not the default (checkerboard) brush"), Success != Default);
		TestTrue(TEXT("the error icon is not the default (checkerboard) brush"), Error != Default);
		TestTrue(TEXT("success and error differ"), Success != Error);
		// A brush the style defines is found without a fallback; a missing one comes back as the fallback (nullptr)
		TestTrue(TEXT("the success icon is a defined style brush"), Success != nullptr && FAppStyle::Get().GetOptionalBrush(TEXT("Icons.SuccessWithColor"), nullptr, nullptr) == Success);
		TestTrue(TEXT("the error icon is a defined style brush"), Error != nullptr && FAppStyle::Get().GetOptionalBrush(TEXT("Icons.ErrorWithColor"), nullptr, nullptr) == Error);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
