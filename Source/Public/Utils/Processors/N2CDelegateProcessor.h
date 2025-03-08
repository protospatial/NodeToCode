// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_CallDelegate.h"

/**
 * @class FN2CDelegateProcessor
 * @brief Processor for delegate-related nodes
 */
class NODETOCODE_API FN2CDelegateProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CDelegateProcessor() {}
    
    /** Destructor */
    virtual ~FN2CDelegateProcessor() {}
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
