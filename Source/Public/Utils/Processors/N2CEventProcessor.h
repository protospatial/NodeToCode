// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ActorBoundEvent.h"
#include "K2Node_ComponentBoundEvent.h"

/**
 * @class FN2CEventProcessor
 * @brief Processor for event nodes
 */
class NODETOCODE_API FN2CEventProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CEventProcessor() {}
    
    /** Destructor */
    virtual ~FN2CEventProcessor() {}
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
