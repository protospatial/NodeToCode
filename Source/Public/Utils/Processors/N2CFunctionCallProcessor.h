// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_CallFunction.h"

/**
 * @class FN2CFunctionCallProcessor
 * @brief Processor for function call nodes
 */
class NODETOCODE_API FN2CFunctionCallProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CFunctionCallProcessor() {}
    
    /** Destructor */
    virtual ~FN2CFunctionCallProcessor() {}
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
