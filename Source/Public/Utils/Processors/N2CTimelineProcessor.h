// Copyright (c) 2025 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"

/**
 * @class FN2CTimelineProcessor
 * @brief Timeline nodes (card 0239): member_name is the timeline, member_parent its Blueprint, and "timeline" summarises
 *        its template (length, loop, autoplay, track names and kinds). Latent like every Timeline (the base processor).
 */
class NODETOCODE_API FN2CTimelineProcessor : public FN2CBaseNodeProcessor
{
protected:
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};

/**
 * @class FN2CAsyncTaskProcessor
 * @brief Async proxy nodes (UK2Node_BaseAsyncTask: AI MoveTo, async actions, Play Montage; card 0239): member_parent is
 *        the proxy factory class and member_name its factory function, the call the node stands for. Latent (the base
 *        processor).
 */
class NODETOCODE_API FN2CAsyncTaskProcessor : public FN2CBaseNodeProcessor
{
protected:
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
