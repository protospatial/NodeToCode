// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CNode.h"
#include "Utils/Processors/N2CNodeProcessor.h"

/**
 * @class FN2CNodeProcessorFactory
 * @brief Factory for creating node processors based on node type
 * 
 * Manages the creation and registration of node processors for different
 * node types, implementing the Factory pattern.
 */
class NODETOCODE_API FN2CNodeProcessorFactory
{
public:
    /** Get the singleton instance */
    static FN2CNodeProcessorFactory& Get();
    
    /**
     * Register a processor for a specific node type
     * 
     * @param NodeType The node type to register for
     * @param Processor The processor to register
     */
    void RegisterProcessor(EN2CNodeType NodeType, TSharedPtr<IN2CNodeProcessor> Processor);
    
    /**
     * Get a processor for a specific node type
     * 
     * @param NodeType The node type to get a processor for
     * @return The processor, or nullptr if none is registered
     */
    TSharedPtr<IN2CNodeProcessor> GetProcessor(EN2CNodeType NodeType);
    
private:
    /** Constructor - initializes default processors */
    FN2CNodeProcessorFactory();
    
    /** Map of node types to processors */
    TMap<EN2CNodeType, TSharedPtr<IN2CNodeProcessor>> Processors;
    
    /** Initialize default processors */
    void InitializeDefaultProcessors();
};
