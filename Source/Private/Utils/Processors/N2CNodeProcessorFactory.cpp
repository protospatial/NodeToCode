// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CNodeProcessorFactory.h"
#include "Utils/Processors/N2CFunctionCallProcessor.h"
#include "Utils/N2CLogger.h"

FN2CNodeProcessorFactory& FN2CNodeProcessorFactory::Get()
{
    static FN2CNodeProcessorFactory Instance;
    return Instance;
}

FN2CNodeProcessorFactory::FN2CNodeProcessorFactory()
{
    InitializeDefaultProcessors();
}

void FN2CNodeProcessorFactory::RegisterProcessor(EN2CNodeType NodeType, TSharedPtr<IN2CNodeProcessor> Processor)
{
    if (Processor.IsValid())
    {
        Processors.Add(NodeType, Processor);
    }
}

TSharedPtr<IN2CNodeProcessor> FN2CNodeProcessorFactory::GetProcessor(EN2CNodeType NodeType)
{
    // Try to find a specific processor for this node type
    if (TSharedPtr<IN2CNodeProcessor>* FoundProcessor = Processors.Find(NodeType))
    {
        return *FoundProcessor;
    }
    
    // If no specific processor is found, return the default processor (CallFunction)
    if (TSharedPtr<IN2CNodeProcessor>* DefaultProcessor = Processors.Find(EN2CNodeType::CallFunction))
    {
        return *DefaultProcessor;
    }
    
    // If no default processor is found, return nullptr
    return nullptr;
}

void FN2CNodeProcessorFactory::InitializeDefaultProcessors()
{
    // Register the function call processor for CallFunction node type
    RegisterProcessor(EN2CNodeType::CallFunction, MakeShared<FN2CFunctionCallProcessor>());
    
    // Use the same processor for similar function call types
    RegisterProcessor(EN2CNodeType::CallArrayFunction, MakeShared<FN2CFunctionCallProcessor>());
    RegisterProcessor(EN2CNodeType::CallDataTableFunction, MakeShared<FN2CFunctionCallProcessor>());
    RegisterProcessor(EN2CNodeType::CallDelegate, MakeShared<FN2CFunctionCallProcessor>());
    RegisterProcessor(EN2CNodeType::CallFunctionOnMember, MakeShared<FN2CFunctionCallProcessor>());
    RegisterProcessor(EN2CNodeType::CallMaterialParameterCollection, MakeShared<FN2CFunctionCallProcessor>());
    RegisterProcessor(EN2CNodeType::CallParentFunction, MakeShared<FN2CFunctionCallProcessor>());
    
    // TODO: Add more specialized processors for other node types
}
