// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "N2CTestNodeClasses.generated.h"

/**
 * Node classes for the capture tests only (card 0234, NodeToCode.Capture.Golden "NodeTypes"); they stand in for a
 * plugin's node classes N2C has never heard of. Neither adds a menu action (UK2Node::GetMenuActions is empty and
 * UK2Node_CallFunction does not override it), so they never appear in the Blueprint editor, and Transient keeps them
 * out of saved assets.
 */

/** A UK2Node subclass with no N2C mapping and no mapped ancestor: captured as "Unknown" with its "class" */
UCLASS(Transient)
class UK2Node_N2CTestUnmapped : public UK2Node
{
	GENERATED_BODY()

public:
	/** One exec input and one exec output, so the node sits in an exec chain */
	virtual void AllocateDefaultPins() override;
};

/** A UK2Node_CallFunction subclass with no mapping of its own: captured as "CallFunction", its nearest mapped ancestor */
UCLASS(Transient)
class UK2Node_N2CTestCallFunction : public UK2Node_CallFunction
{
	GENERATED_BODY()
};
