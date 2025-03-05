// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeContainer.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_GetArrayItem.h"

/**
 * @class FN2CArrayProcessor
 * @brief Processor for array and container-related nodes
 */
class NODETOCODE_API FN2CArrayProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CArrayProcessor() {}
    
    /** Destructor */
    virtual ~FN2CArrayProcessor() {}
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
