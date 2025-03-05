// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CNode.h"
#include "K2Node.h"

/**
 * @class IN2CNodeProcessor
 * @brief Interface for node processors that handle specific node types
 * 
 * Implements the Strategy pattern for node processing, allowing specialized
 * processors for different node types.
 */
class NODETOCODE_API IN2CNodeProcessor
{
public:
    /** Virtual destructor */
    virtual ~IN2CNodeProcessor() {}
    
    /**
     * Process a node and extract its properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     * @return True if processing succeeded
     */
    virtual bool Process(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) = 0;
};
