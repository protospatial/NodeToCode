// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CNodeProcessor.h"
#include "Utils/N2CLogger.h"

/**
 * @class FN2CBaseNodeProcessor
 * @brief Base implementation for node processors
 * 
 * Provides common functionality for all node processors.
 */
class NODETOCODE_API FN2CBaseNodeProcessor : public IN2CNodeProcessor
{
public:
    /** Constructor */
    FN2CBaseNodeProcessor() {}
    
    /** Destructor */
    virtual ~FN2CBaseNodeProcessor() {}
    
    /**
     * Process a node and extract its properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     * @return True if processing succeeded
     */
    virtual bool Process(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

    /**
     * For a node that names an input action asset (Enhanced Input), member_name is the asset's name and member_parent
     * its package path. Process calls it when the node-specific properties left member_name empty.
     */
    static void ExtractInputActionReference(const UK2Node* Node, FN2CNodeDefinition& OutNodeDef);
};
