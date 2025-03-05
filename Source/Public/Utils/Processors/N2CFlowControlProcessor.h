// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Select.h"
#include "K2Node_Switch.h"
#include "K2Node_MultiGate.h"
#include "K2Node_DoOnceMultiInput.h"

/**
 * @class FN2CFlowControlProcessor
 * @brief Processor for flow control nodes (sequence, branch, switch, etc.)
 */
class NODETOCODE_API FN2CFlowControlProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CFlowControlProcessor() {}
    
    /** Destructor */
    virtual ~FN2CFlowControlProcessor() {}
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
