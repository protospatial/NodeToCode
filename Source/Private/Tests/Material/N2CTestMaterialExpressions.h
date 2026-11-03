// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "N2CTestMaterialExpressions.generated.h"

/**
 * A material expression class for the material capture tests only (card 0451, NodeToCode.Material.Capture): it stands in
 * for a plugin's expression whose input is not an FExpressionInput property (it overrides GetInput), so capture's
 * reflection fallback names the input by its index with an UNMAPPED_PIN warning. Transient keeps it out of saved
 * assets; it has no palette entry.
 */
UCLASS(Transient)
class UN2CTestMaterialExpressionUnmapped : public UMaterialExpression
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual FExpressionInput* GetInput(int32 InputIndex) override
	{
		return InputIndex == 0 ? &Hidden : nullptr;
	}
#endif

	/** An input the class supplies through GetInput only: no UPROPERTY, so reflection cannot name it */
	FExpressionInput Hidden;
};
