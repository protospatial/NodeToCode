// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

/**
 * The editor toast the Blueprint tools show after an edit (card 0295). Before it each tool built its own
 * FNotificationInfo with success and error brush names that no UE 5.6-5.8 style defines, so every toast showed the
 * checkerboard placeholder and logged a missing-resource warning.
 */
struct FN2CMcpNotification
{
	/** Icons.SuccessWithColor or Icons.ErrorWithColor from FAppStyle (StarshipCoreStyle, 5.6-5.8) */
	static const FSlateBrush* Icon(bool bSuccess);

	/** A fire-and-forget toast with Text and the success or failure icon, gone after ExpireDuration seconds */
	static void Show(const FText& Text, bool bSuccess = true, float ExpireDuration = 3.0f);
};
