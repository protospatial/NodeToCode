// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "Models/N2CLogging.h"
#include "CoreMinimal.h"
#include "Utils/Processors/N2CBaseNodeProcessor.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

/**
 * @class FN2CVariableProcessor
 * @brief Processor for variable nodes (get/set)
 */
class NODETOCODE_API FN2CVariableProcessor : public FN2CBaseNodeProcessor
{
public:
    /** Constructor */
    FN2CVariableProcessor() { N2C_LOG_FUNCTION_ENTRY(); }
    
    /** Destructor */
    virtual ~FN2CVariableProcessor() {
      N2C_LOG_FUNCTION_ENTRY();
      }
    
protected:
    /**
     * Extract node-specific properties
     * 
     * @param Node The K2Node to process
     * @param OutNodeDef The node definition to populate
     */
    virtual void ExtractNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef) override;
};
