// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"

/**
 * @class FN2CFunctionEntryProcessor
 * @brief Processor for function entry/result and macro nodes
 */
class NODETOCODE_API FN2CFunctionEntryProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CFunctionEntryProcessor() {}
    
    /** Destructor */
    virtual ~FN2CFunctionEntryProcessor() {}
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
