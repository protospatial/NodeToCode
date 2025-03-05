// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_CastByteToEnum.h"

/**
 * @class FN2CCastProcessor
 * @brief Processor for cast-related nodes
 */
class NODETOCODE_API FN2CCastProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CCastProcessor() {}
    
    /** Destructor */
    virtual ~FN2CCastProcessor() {}
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
