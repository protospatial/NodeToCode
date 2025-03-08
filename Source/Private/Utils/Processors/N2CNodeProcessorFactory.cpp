// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/Processors/N2CNodeProcessorFactory.h"

#include <Utils/Processors/N2CArrayProcessor.h>
#include <Utils/Processors/N2CCastProcessor.h>

#include "Utils/Processors/N2CFunctionCallProcessor.h"
#include "Utils/N2CLogger.h"
#include "Utils/Processors/N2CDelegateProcessor.h"
#include "Utils/Processors/N2CEventProcessor.h"
#include "Utils/Processors/N2CFlowControlProcessor.h"
#include "Utils/Processors/N2CFunctionEntryProcessor.h"
#include "Utils/Processors/N2CVariableProcessor.h"
#include "Utils/Processors/N2CStructProcessor.h"

class FN2CStructProcessor;

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
    // Create shared instances of each processor type
    TSharedPtr<FN2CFunctionCallProcessor> FunctionCallProcessor = MakeShared<FN2CFunctionCallProcessor>();
    TSharedPtr<FN2CVariableProcessor> VariableProcessor = MakeShared<FN2CVariableProcessor>();
    TSharedPtr<FN2CEventProcessor> EventProcessor = MakeShared<FN2CEventProcessor>();
    TSharedPtr<FN2CStructProcessor> StructProcessor = MakeShared<FN2CStructProcessor>();
    TSharedPtr<FN2CFlowControlProcessor> FlowControlProcessor = MakeShared<FN2CFlowControlProcessor>();
    TSharedPtr<FN2CDelegateProcessor> DelegateProcessor = MakeShared<FN2CDelegateProcessor>();
    TSharedPtr<FN2CFunctionEntryProcessor> FunctionEntryProcessor = MakeShared<FN2CFunctionEntryProcessor>();
    TSharedPtr<FN2CArrayProcessor> ArrayProcessor = MakeShared<FN2CArrayProcessor>();
    TSharedPtr<FN2CCastProcessor> CastProcessor = MakeShared<FN2CCastProcessor>();
    
    // Register function call processors
    RegisterProcessor(EN2CNodeType::CallFunction, FunctionCallProcessor);
    RegisterProcessor(EN2CNodeType::CallArrayFunction, FunctionCallProcessor);
    RegisterProcessor(EN2CNodeType::CallDataTableFunction, FunctionCallProcessor);
    RegisterProcessor(EN2CNodeType::CallFunctionOnMember, FunctionCallProcessor);
    RegisterProcessor(EN2CNodeType::CallMaterialParameterCollection, FunctionCallProcessor);
    RegisterProcessor(EN2CNodeType::CallParentFunction, FunctionCallProcessor);
    RegisterProcessor(EN2CNodeType::GetDataTableRow, FunctionCallProcessor);
    
    // Register variable processors
    RegisterProcessor(EN2CNodeType::Variable, VariableProcessor);
    RegisterProcessor(EN2CNodeType::VariableGet, VariableProcessor);
    RegisterProcessor(EN2CNodeType::VariableSet, VariableProcessor);
    RegisterProcessor(EN2CNodeType::VariableSetRef, VariableProcessor);
    RegisterProcessor(EN2CNodeType::LocalVariable, VariableProcessor);
    RegisterProcessor(EN2CNodeType::LocalVariableGet, VariableProcessor);
    RegisterProcessor(EN2CNodeType::LocalVariableSet, VariableProcessor);
    RegisterProcessor(EN2CNodeType::FunctionParameter, VariableProcessor);
    RegisterProcessor(EN2CNodeType::LocalFunctionVariable, VariableProcessor);
    RegisterProcessor(EN2CNodeType::MakeVariable, VariableProcessor);
    RegisterProcessor(EN2CNodeType::TemporaryVariable, VariableProcessor);
    RegisterProcessor(EN2CNodeType::SetVariableOnPersistentFrame, VariableProcessor);
    
    // Register event processors
    RegisterProcessor(EN2CNodeType::Event, EventProcessor);
    RegisterProcessor(EN2CNodeType::CustomEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::ActorBoundEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::ComponentBoundEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputAction, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputActionEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputAxisEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputAxisKeyEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputKey, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputKeyEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputTouch, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputTouchEvent, EventProcessor);
    RegisterProcessor(EN2CNodeType::InputVectorAxisEvent, EventProcessor);
    
    // Register struct processors
    RegisterProcessor(EN2CNodeType::MakeStruct, StructProcessor);
    RegisterProcessor(EN2CNodeType::BreakStruct, StructProcessor);
    RegisterProcessor(EN2CNodeType::SetFieldsInStruct, StructProcessor);
    RegisterProcessor(EN2CNodeType::StructMemberGet, StructProcessor);
    RegisterProcessor(EN2CNodeType::StructMemberSet, StructProcessor);
    RegisterProcessor(EN2CNodeType::StructOperation, StructProcessor);
    
    // Register flow control processors
    RegisterProcessor(EN2CNodeType::Sequence, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::Branch, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::Select, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::Switch, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::SwitchInt, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::SwitchString, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::SwitchEnum, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::SwitchName, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::MultiGate, FlowControlProcessor);
    RegisterProcessor(EN2CNodeType::DoOnceMultiInput, FlowControlProcessor);
    
    // Register delegate processors
    RegisterProcessor(EN2CNodeType::CallDelegate, DelegateProcessor);
    RegisterProcessor(EN2CNodeType::CreateDelegate, DelegateProcessor);
    RegisterProcessor(EN2CNodeType::AddDelegate, DelegateProcessor);
    RegisterProcessor(EN2CNodeType::ClearDelegate, DelegateProcessor);
    RegisterProcessor(EN2CNodeType::RemoveDelegate, DelegateProcessor);
    RegisterProcessor(EN2CNodeType::AssignDelegate, DelegateProcessor);
    RegisterProcessor(EN2CNodeType::DelegateSet, DelegateProcessor);
    RegisterProcessor(EN2CNodeType::BaseMCDelegate, DelegateProcessor);
    
    // Register function entry/result processors
    RegisterProcessor(EN2CNodeType::FunctionEntry, FunctionEntryProcessor);
    RegisterProcessor(EN2CNodeType::FunctionResult, FunctionEntryProcessor);
    RegisterProcessor(EN2CNodeType::FunctionTerminator, FunctionEntryProcessor);
    RegisterProcessor(EN2CNodeType::MacroInstance, FunctionEntryProcessor);
    
    // Register array and container processors
    RegisterProcessor(EN2CNodeType::MakeArray, ArrayProcessor);
    RegisterProcessor(EN2CNodeType::MakeMap, ArrayProcessor);
    RegisterProcessor(EN2CNodeType::MakeSet, ArrayProcessor);
    RegisterProcessor(EN2CNodeType::MakeContainer, ArrayProcessor);
    RegisterProcessor(EN2CNodeType::GetArrayItem, ArrayProcessor);
    
    // Register cast processors
    RegisterProcessor(EN2CNodeType::DynamicCast, CastProcessor);
    RegisterProcessor(EN2CNodeType::ClassDynamicCast, CastProcessor);
    RegisterProcessor(EN2CNodeType::CastByteToEnum, CastProcessor);
}
