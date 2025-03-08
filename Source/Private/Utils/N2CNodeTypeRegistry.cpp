// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CNodeTypeRegistry.h"
#include "Utils/N2CLogger.h"

// Include all K2Node types
#include "K2Node_ActorBoundEvent.h"
#include "K2Node_AddComponent.h"
#include "K2Node_AddComponentByClass.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_AddPinInterface.h"
#include "K2Node_AssignDelegate.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_AsyncAction.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_BitmaskLiteral.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallDataTableFunction.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallFunctionOnMember.h"
#include "K2Node_CallMaterialParameterCollectionFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_CastByteToEnum.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Composite.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_ConvertAsset.h"
#include "K2Node_Copy.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DeadClass.h"
#include "K2Node_DelegateSet.h"
#include "K2Node_DoOnceMultiInput.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EaseFunction.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_EnumInequality.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_Event.h"
#include "K2Node_EventNodeInterface.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_ExternalGraphInterface.h"
#include "K2Node_ForEachElementInEnum.h"
#include "K2Node_FormatText.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_GenericCreateObject.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_GetClassDefaults.h"
#include "K2Node_GetDataTableRow.h"
#include "K2Node_GetEnumeratorName.h"
#include "K2Node_GetEnumeratorNameAsString.h"
#include "K2Node_GetInputAxisKeyValue.h"
#include "K2Node_GetInputAxisValue.h"
#include "K2Node_GetInputVectorAxisValue.h"
#include "K2Node_GetNumEnumEntries.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputAction.h"
#include "K2Node_InputActionEvent.h"
#include "K2Node_InputAxisEvent.h"
#include "K2Node_InputAxisKeyEvent.h"
#include "K2Node_InputKey.h"
#include "K2Node_InputKeyEvent.h"
#include "K2Node_InputTouch.h"
#include "K2Node_InputTouchEvent.h"
#include "K2Node_InputVectorAxisEvent.h"
#include "K2Node_Knot.h"
#include "K2Node_Literal.h"
#include "K2Node_LoadAsset.h"
#include "K2Node_LocalVariable.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeContainer.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MakeVariable.h"
#include "K2Node_MathExpression.h"
#include "K2Node_Message.h"
#include "K2Node_MultiGate.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_PureAssignmentStatement.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SetFieldsInStruct.h"
#include "K2Node_SetVariableOnPersistentFrame.h"
#include "K2Node_SpawnActor.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_StructMemberGet.h"
#include "K2Node_StructMemberSet.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Switch.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_TunnelBoundary.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_VariableSetRef.h"

FN2CNodeTypeRegistry& FN2CNodeTypeRegistry::Get()
{
    static FN2CNodeTypeRegistry Instance;
    return Instance;
}

FN2CNodeTypeRegistry::FN2CNodeTypeRegistry()
{
    InitializeDefaultMappings();
}

void FN2CNodeTypeRegistry::RegisterNodeType(const FName& ClassName, EN2CNodeType NodeType)
{
    ClassNameMappings.Add(ClassName, NodeType);
}

void FN2CNodeTypeRegistry::RegisterNodeClass(const UClass* Class, EN2CNodeType NodeType)
{
    if (Class)
    {
        ClassMappings.Add(Class, NodeType);
    }
}

EN2CNodeType FN2CNodeTypeRegistry::GetNodeType(const UK2Node* Node)
{
    if (!Node)
    {
        return EN2CNodeType::CallFunction; // Default
    }
    
    // Try setting make struct type first since MakeStruct is considered a variable
    // before being considered MakeStruct
    if (const UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
    {
        FName ClassName = FName(*GetBaseNodeType(Node->GetClass()->GetName()));
        if (ClassNameMappings.Contains(ClassName))
        {
            return ClassNameMappings[ClassName];
        }
    }

    // Try setting variable type first
    if (const UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
    {
        return DetermineVariableNodeType(VarNode);
    }
    
    // Try direct class mapping
    if (ClassMappings.Contains(Node->GetClass()))
    {
        return ClassMappings[Node->GetClass()];
    }
    
    // Try class name mapping
    FName ClassName = FName(*GetBaseNodeType(Node->GetClass()->GetName()));
    if (ClassNameMappings.Contains(ClassName))
    {
        return ClassNameMappings[ClassName];
    }
    
    // Fall back to inheritance-based mapping
    EN2CNodeType OutType = EN2CNodeType::CallFunction;
    MapFromInheritance(Node, OutType);
    return OutType;
}

FString FN2CNodeTypeRegistry::GetBaseNodeType(const FString& ClassName)
{
    static const FString Prefix = TEXT("K2Node_");
    if (ClassName.StartsWith(Prefix))
    {
        return ClassName.RightChop(Prefix.Len());
    }
    return ClassName;
}

void FN2CNodeTypeRegistry::InitializeDefaultMappings()
{
    // Function Calls
    RegisterNodeType(FName(TEXT("CallFunction")), EN2CNodeType::CallFunction);
    RegisterNodeType(FName(TEXT("CallArrayFunction")), EN2CNodeType::CallArrayFunction);
    RegisterNodeType(FName(TEXT("CallDataTableFunction")), EN2CNodeType::CallDataTableFunction);
    RegisterNodeType(FName(TEXT("CallDelegate")), EN2CNodeType::CallDelegate);
    RegisterNodeType(FName(TEXT("CallFunctionOnMember")), EN2CNodeType::CallFunctionOnMember);
    RegisterNodeType(FName(TEXT("CallMaterialParameterCollectionFunction")), EN2CNodeType::CallMaterialParameterCollection);
    RegisterNodeType(FName(TEXT("CallParentFunction")), EN2CNodeType::CallParentFunction);
    RegisterNodeType(FName(TEXT("FunctionEntry")), EN2CNodeType::FunctionEntry);
    RegisterNodeType(FName(TEXT("FunctionResult")), EN2CNodeType::FunctionResult);
    RegisterNodeType(FName(TEXT("FunctionTerminator")), EN2CNodeType::FunctionTerminator);

    // Variables
    RegisterNodeType(FName(TEXT("Variable")), EN2CNodeType::Variable);
    RegisterNodeType(FName(TEXT("VariableGet")), EN2CNodeType::VariableGet);
    RegisterNodeType(FName(TEXT("VariableSet")), EN2CNodeType::VariableSet);
    RegisterNodeType(FName(TEXT("VariableSetRef")), EN2CNodeType::VariableSetRef);
    RegisterNodeType(FName(TEXT("LocalVariable")), EN2CNodeType::LocalVariable);
    RegisterNodeType(FName(TEXT("MakeVariable")), EN2CNodeType::MakeVariable);
    RegisterNodeType(FName(TEXT("TemporaryVariable")), EN2CNodeType::TemporaryVariable);
    RegisterNodeType(FName(TEXT("SetVariableOnPersistentFrame")), EN2CNodeType::SetVariableOnPersistentFrame);

    // Events
    RegisterNodeType(FName(TEXT("Event")), EN2CNodeType::Event);
    RegisterNodeType(FName(TEXT("CustomEvent")), EN2CNodeType::CustomEvent);
    RegisterNodeType(FName(TEXT("ActorBoundEvent")), EN2CNodeType::ActorBoundEvent);
    RegisterNodeType(FName(TEXT("ComponentBoundEvent")), EN2CNodeType::ComponentBoundEvent);
    RegisterNodeType(FName(TEXT("InputAction")), EN2CNodeType::InputAction);
    RegisterNodeType(FName(TEXT("InputActionEvent")), EN2CNodeType::InputActionEvent);
    RegisterNodeType(FName(TEXT("InputAxisEvent")), EN2CNodeType::InputAxisEvent);
    RegisterNodeType(FName(TEXT("InputAxisKeyEvent")), EN2CNodeType::InputAxisKeyEvent);
    RegisterNodeType(FName(TEXT("InputKey")), EN2CNodeType::InputKey);
    RegisterNodeType(FName(TEXT("InputKeyEvent")), EN2CNodeType::InputKeyEvent);
    RegisterNodeType(FName(TEXT("InputTouch")), EN2CNodeType::InputTouch);
    RegisterNodeType(FName(TEXT("InputTouchEvent")), EN2CNodeType::InputTouchEvent);
    RegisterNodeType(FName(TEXT("InputVectorAxisEvent")), EN2CNodeType::InputVectorAxisEvent);

    // Flow Control
    RegisterNodeType(FName(TEXT("ExecutionSequence")), EN2CNodeType::Sequence);
    RegisterNodeType(FName(TEXT("IfThenElse")), EN2CNodeType::Branch);
    RegisterNodeType(FName(TEXT("DoOnceMultiInput")), EN2CNodeType::DoOnceMultiInput);
    RegisterNodeType(FName(TEXT("MultiGate")), EN2CNodeType::MultiGate);
    RegisterNodeType(FName(TEXT("Knot")), EN2CNodeType::Knot);
    RegisterNodeType(FName(TEXT("Tunnel")), EN2CNodeType::Tunnel);
    RegisterNodeType(FName(TEXT("TunnelBoundary")), EN2CNodeType::TunnelBoundary);

    // Switches
    RegisterNodeType(FName(TEXT("Switch")), EN2CNodeType::Switch);
    RegisterNodeType(FName(TEXT("SwitchInteger")), EN2CNodeType::SwitchInt);
    RegisterNodeType(FName(TEXT("SwitchString")), EN2CNodeType::SwitchString);
    RegisterNodeType(FName(TEXT("SwitchEnum")), EN2CNodeType::SwitchEnum);
    RegisterNodeType(FName(TEXT("SwitchName")), EN2CNodeType::SwitchName);

    // Structs and Objects
    RegisterNodeType(FName(TEXT("MakeStruct")), EN2CNodeType::MakeStruct);
    RegisterNodeType(FName(TEXT("BreakStruct")), EN2CNodeType::BreakStruct);
    RegisterNodeType(FName(TEXT("SetFieldsInStruct")), EN2CNodeType::SetFieldsInStruct);
    RegisterNodeType(FName(TEXT("StructMemberGet")), EN2CNodeType::StructMemberGet);
    RegisterNodeType(FName(TEXT("StructMemberSet")), EN2CNodeType::StructMemberSet);
    RegisterNodeType(FName(TEXT("StructOperation")), EN2CNodeType::StructOperation);

    // Containers
    RegisterNodeType(FName(TEXT("MakeArray")), EN2CNodeType::MakeArray);
    RegisterNodeType(FName(TEXT("MakeMap")), EN2CNodeType::MakeMap);
    RegisterNodeType(FName(TEXT("MakeSet")), EN2CNodeType::MakeSet);
    RegisterNodeType(FName(TEXT("MakeContainer")), EN2CNodeType::MakeContainer);
    RegisterNodeType(FName(TEXT("GetArrayItem")), EN2CNodeType::GetArrayItem);

    // Casting and Conversion
    RegisterNodeType(FName(TEXT("DynamicCast")), EN2CNodeType::DynamicCast);
    RegisterNodeType(FName(TEXT("ClassDynamicCast")), EN2CNodeType::ClassDynamicCast);
    RegisterNodeType(FName(TEXT("CastByteToEnum")), EN2CNodeType::CastByteToEnum);
    RegisterNodeType(FName(TEXT("ConvertAsset")), EN2CNodeType::ConvertAsset);

    // Delegates
    RegisterNodeType(FName(TEXT("AddDelegate")), EN2CNodeType::AddDelegate);
    RegisterNodeType(FName(TEXT("CreateDelegate")), EN2CNodeType::CreateDelegate);
    RegisterNodeType(FName(TEXT("ClearDelegate")), EN2CNodeType::ClearDelegate);
    RegisterNodeType(FName(TEXT("RemoveDelegate")), EN2CNodeType::RemoveDelegate);
    RegisterNodeType(FName(TEXT("AssignDelegate")), EN2CNodeType::AssignDelegate);
    RegisterNodeType(FName(TEXT("DelegateSet")), EN2CNodeType::DelegateSet);

    // Async/Latent
    RegisterNodeType(FName(TEXT("AsyncAction")), EN2CNodeType::AsyncAction);
    RegisterNodeType(FName(TEXT("BaseAsyncTask")), EN2CNodeType::BaseAsyncTask);

    // Components
    RegisterNodeType(FName(TEXT("AddComponent")), EN2CNodeType::AddComponent);
    RegisterNodeType(FName(TEXT("AddComponentByClass")), EN2CNodeType::AddComponentByClass);
    RegisterNodeType(FName(TEXT("AddPinInterface")), EN2CNodeType::AddPinInterface);

    // Misc Utility
    RegisterNodeType(FName(TEXT("ConstructObjectFromClass")), EN2CNodeType::ConstructObjectFromClass);
    RegisterNodeType(FName(TEXT("GenericCreateObject")), EN2CNodeType::GenericCreateObject);
    RegisterNodeType(FName(TEXT("Timeline")), EN2CNodeType::Timeline);
    RegisterNodeType(FName(TEXT("SpawnActor")), EN2CNodeType::SpawnActor);
    RegisterNodeType(FName(TEXT("SpawnActorFromClass")), EN2CNodeType::SpawnActorFromClass);
    RegisterNodeType(FName(TEXT("FormatText")), EN2CNodeType::FormatText);
    RegisterNodeType(FName(TEXT("GetClassDefaults")), EN2CNodeType::GetClassDefaults);
    RegisterNodeType(FName(TEXT("GetSubsystem")), EN2CNodeType::GetSubsystem);
    RegisterNodeType(FName(TEXT("LoadAsset")), EN2CNodeType::LoadAsset);
    RegisterNodeType(FName(TEXT("Copy")), EN2CNodeType::Copy);

    // Math/Logic
    RegisterNodeType(FName(TEXT("BitmaskLiteral")), EN2CNodeType::BitmaskLiteral);
    RegisterNodeType(FName(TEXT("EnumEquality")), EN2CNodeType::EnumEquality);
    RegisterNodeType(FName(TEXT("EnumInequality")), EN2CNodeType::EnumInequality);
    RegisterNodeType(FName(TEXT("EnumLiteral")), EN2CNodeType::EnumLiteral);
    RegisterNodeType(FName(TEXT("GetEnumeratorName")), EN2CNodeType::GetEnumeratorName);
    RegisterNodeType(FName(TEXT("GetEnumeratorNameAsString")), EN2CNodeType::GetEnumeratorNameAsString);
    RegisterNodeType(FName(TEXT("GetNumEnumEntries")), EN2CNodeType::GetNumEnumEntries);
    RegisterNodeType(FName(TEXT("MathExpression")), EN2CNodeType::MathExpression);
    RegisterNodeType(FName(TEXT("EaseFunction")), EN2CNodeType::EaseFunction);
    RegisterNodeType(FName(TEXT("CommutativeAssociativeBinaryOperator")), EN2CNodeType::CommutativeAssociativeBinaryOperator);
    RegisterNodeType(FName(TEXT("PureAssignmentStatement")), EN2CNodeType::PureAssignmentStatement);
    RegisterNodeType(FName(TEXT("AssignmentStatement")), EN2CNodeType::AssignmentStatement);

    // Special Types
    RegisterNodeType(FName(TEXT("Self")), EN2CNodeType::Self);
    RegisterNodeType(FName(TEXT("Composite")), EN2CNodeType::Composite);
    RegisterNodeType(FName(TEXT("DeadClass")), EN2CNodeType::DeadClass);
    RegisterNodeType(FName(TEXT("Literal")), EN2CNodeType::Literal);
    RegisterNodeType(FName(TEXT("Message")), EN2CNodeType::Message);
    RegisterNodeType(FName(TEXT("PromotableOperator")), EN2CNodeType::PromotableOperator);
    RegisterNodeType(FName(TEXT("MacroInstance")), EN2CNodeType::MacroInstance);
    RegisterNodeType(FName(TEXT("BaseMCDelegate")), EN2CNodeType::BaseMCDelegate);
    
    // Register class mappings for common base classes
    RegisterNodeClass(UK2Node_CallFunction::StaticClass(), EN2CNodeType::CallFunction);
    RegisterNodeClass(UK2Node_Event::StaticClass(), EN2CNodeType::Event);
    RegisterNodeClass(UK2Node_Variable::StaticClass(), EN2CNodeType::Variable);
}

bool FN2CNodeTypeRegistry::MapFromInheritance(const UK2Node* Node, EN2CNodeType& OutType)
{
    // Handle common base classes
    if (Node->IsA<UK2Node_CallFunction>())
    {
        OutType = EN2CNodeType::CallFunction;
        return true;
    }
    
    if (Node->IsA<UK2Node_Event>())
    {
        OutType = EN2CNodeType::Event;
        return true;
    }

    if (Node->IsA<UK2Node_MakeStruct>())
    {
        OutType = EN2CNodeType::MakeStruct;
        return true;
    }
    
    if (Node->IsA<UK2Node_Variable>())
    {
        OutType = DetermineVariableNodeType(Cast<UK2Node_Variable>(Node));
        return true;
    }

    if (Node->IsA<UK2Node_VariableSetRef>())
    {
        OutType = EN2CNodeType::VariableSetRef;
        return true;
    }

    if (Node->IsA<UK2Node_ActorBoundEvent>())
    {
        OutType = EN2CNodeType::ActorBoundEvent;
        return true;
    }

    if (Node->IsA<UK2Node_AddComponent>())
    {
        OutType = EN2CNodeType::AddComponent;
        return true;
    }

    if (Node->IsA<UK2Node_AddComponentByClass>())
    {
        OutType = EN2CNodeType::AddComponentByClass;
        return true;
    }

    if (Node->IsA<UK2Node_AddDelegate>())
    {
        OutType = EN2CNodeType::AddDelegate;
        return true;
    }

    if (Node->IsA<UK2Node_AddPinInterface>())
    {
        OutType = EN2CNodeType::AddPinInterface;
        return true;
    }

    if (Node->IsA<UK2Node_AssignDelegate>())
    {
        OutType = EN2CNodeType::AssignDelegate;
        return true;
    }

    if (Node->IsA<UK2Node_AssignmentStatement>())
    {
        OutType = EN2CNodeType::AssignmentStatement;
        return true;
    }

    if (Node->IsA<UK2Node_AsyncAction>())
    {
        OutType = EN2CNodeType::AsyncAction;
        return true;
    }

    if (Node->IsA<UK2Node_BaseAsyncTask>())
    {
        OutType = EN2CNodeType::BaseAsyncTask;
        return true;
    }

    if (Node->IsA<UK2Node_BaseMCDelegate>())
    {
        OutType = EN2CNodeType::BaseMCDelegate;
        return true;
    }

    if (Node->IsA<UK2Node_BitmaskLiteral>())
    {
        OutType = EN2CNodeType::BitmaskLiteral;
        return true;
    }

    if (Node->IsA<UK2Node_BreakStruct>())
    {
        OutType = EN2CNodeType::BreakStruct;
        return true;
    }

    if (Node->IsA<UK2Node_CallArrayFunction>())
    {
        OutType = EN2CNodeType::CallArrayFunction;
        return true;
    }

    if (Node->IsA<UK2Node_CallDataTableFunction>())
    {
        OutType = EN2CNodeType::CallDataTableFunction;
        return true;
    }

    if (Node->IsA<UK2Node_CallDelegate>())
    {
        OutType = EN2CNodeType::CallDelegate;
        return true;
    }

    if (Node->IsA<UK2Node_CallFunctionOnMember>())
    {
        OutType = EN2CNodeType::CallFunctionOnMember;
        return true;
    }

    if (Node->IsA<UK2Node_CallMaterialParameterCollectionFunction>())
    {
        OutType = EN2CNodeType::CallMaterialParameterCollection;
        return true;
    }

    if (Node->IsA<UK2Node_CallParentFunction>())
    {
        OutType = EN2CNodeType::CallParentFunction;
        return true;
    }

    if (Node->IsA<UK2Node_CastByteToEnum>())
    {
        OutType = EN2CNodeType::CastByteToEnum;
        return true;
    }

    if (Node->IsA<UK2Node_ClassDynamicCast>())
    {
        OutType = EN2CNodeType::ClassDynamicCast;
        return true;
    }

    if (Node->IsA<UK2Node_ClearDelegate>())
    {
        OutType = EN2CNodeType::ClearDelegate;
        return true;
    }

    if (Node->IsA<UK2Node_CommutativeAssociativeBinaryOperator>())
    {
        OutType = EN2CNodeType::CommutativeAssociativeBinaryOperator;
        return true;
    }

    if (Node->IsA<UK2Node_ComponentBoundEvent>())
    {
        OutType = EN2CNodeType::ComponentBoundEvent;
        return true;
    }

    if (Node->IsA<UK2Node_Composite>())
    {
        OutType = EN2CNodeType::Composite;
        return true;
    }

    if (Node->IsA<UK2Node_ConstructObjectFromClass>())
    {
        OutType = EN2CNodeType::ConstructObjectFromClass;
        return true;
    }

    if (Node->IsA<UK2Node_ConvertAsset>())
    {
        OutType = EN2CNodeType::ConvertAsset;
        return true;
    }

    if (Node->IsA<UK2Node_Copy>())
    {
        OutType = EN2CNodeType::Copy;
        return true;
    }

    if (Node->IsA<UK2Node_CreateDelegate>())
    {
        OutType = EN2CNodeType::CreateDelegate;
        return true;
    }

    if (Node->IsA<UK2Node_CustomEvent>())
    {
        OutType = EN2CNodeType::CustomEvent;
        return true;
    }
    
    if (Node->IsA<UK2Node_DeadClass>())
    {
        OutType = EN2CNodeType::DeadClass;
        return true;
    }

    if (Node->IsA<UK2Node_DelegateSet>())
    {
        OutType = EN2CNodeType::DelegateSet;
        return true;
    }

    if (Node->IsA<UK2Node_DoOnceMultiInput>())
    {
        OutType = EN2CNodeType::DoOnceMultiInput;
        return true;
    }

    if (Node->IsA<UK2Node_DynamicCast>())
    {
        OutType = EN2CNodeType::DynamicCast;
        return true;
    }

    if (Node->IsA<UK2Node_EaseFunction>())
    {
        OutType = EN2CNodeType::EaseFunction;
        return true;
    }

    if (Node->IsA<UK2Node_EditablePinBase>())
    {
        OutType = EN2CNodeType::EditablePinBase;
        return true;
    }

    if (Node->IsA<UK2Node_EnumEquality>())
    {
        OutType = EN2CNodeType::EnumEquality;
        return true;
    }

    if (Node->IsA<UK2Node_EnumInequality>())
    {
        OutType = EN2CNodeType::EnumInequality;
        return true;
    }

    if (Node->IsA<UK2Node_EnumLiteral>())
    {
        OutType = EN2CNodeType::EnumLiteral;
        return true;
    }

    if (Node->IsA<UK2Node_EventNodeInterface>())
    {
        OutType = EN2CNodeType::EventNodeInterface;
        return true;
    }

    if (Node->IsA<UK2Node_ExecutionSequence>())
    {
        OutType = EN2CNodeType::Sequence;
        return true;
    }

    if (Node->IsA<UK2Node_ExternalGraphInterface>())
    {
        OutType = EN2CNodeType::ExternalGraphInterface;
        return true;
    }

    if (Node->IsA<UK2Node_ForEachElementInEnum>())
    {
        OutType = EN2CNodeType::ForEachElementInEnum;
        return true;
    }

    if (Node->IsA<UK2Node_FormatText>())
    {
        OutType = EN2CNodeType::FormatText;
        return true;
    }

    if (Node->IsA<UK2Node_FunctionEntry>())
    {
        OutType = EN2CNodeType::FunctionEntry;
        return true;
    }

    if (Node->IsA<UK2Node_FunctionResult>())
    {
        OutType = EN2CNodeType::FunctionResult;
        return true;
    }

    if (Node->IsA<UK2Node_FunctionTerminator>())
    {
        OutType = EN2CNodeType::FunctionTerminator;
        return true;
    }

    if (Node->IsA<UK2Node_GenericCreateObject>())
    {
        OutType = EN2CNodeType::GenericCreateObject;
        return true;
    }

    if (Node->IsA<UK2Node_GetArrayItem>())
    {
        OutType = EN2CNodeType::GetArrayItem;
        return true;
    }

    if (Node->IsA<UK2Node_GetClassDefaults>())
    {
        OutType = EN2CNodeType::GetClassDefaults;
        return true;
    }

    if (Node->IsA<UK2Node_GetDataTableRow>())
    {
        OutType = EN2CNodeType::GetDataTableRow;
        return true;
    }

    if (Node->IsA<UK2Node_GetEnumeratorName>())
    {
        OutType = EN2CNodeType::GetEnumeratorName;
        return true;
    }

    if (Node->IsA<UK2Node_GetEnumeratorNameAsString>())
    {
        OutType = EN2CNodeType::GetEnumeratorNameAsString;
        return true;
    }

    if (Node->IsA<UK2Node_GetInputAxisKeyValue>())
    {
        OutType = EN2CNodeType::GetInputAxisKeyValue;
        return true;
    }

    if (Node->IsA<UK2Node_GetInputAxisValue>())
    {
        OutType = EN2CNodeType::GetInputAxisValue;
        return true;
    }

    if (Node->IsA<UK2Node_GetInputVectorAxisValue>())
    {
        OutType = EN2CNodeType::GetInputVectorAxisValue;
        return true;
    }

    if (Node->IsA<UK2Node_GetNumEnumEntries>())
    {
        OutType = EN2CNodeType::GetNumEnumEntries;
        return true;
    }

    if (Node->IsA<UK2Node_GetSubsystem>())
    {
        OutType = EN2CNodeType::GetSubsystem;
        return true;
    }

    if (Node->IsA<UK2Node_IfThenElse>())
    {
        OutType = EN2CNodeType::Branch;
        return true;
    }

    if (Node->IsA<UK2Node_InputAction>())
    {
        OutType = EN2CNodeType::InputAction;
        return true;
    }

    if (Node->IsA<UK2Node_InputActionEvent>())
    {
        OutType = EN2CNodeType::InputActionEvent;
        return true;
    }

    if (Node->IsA<UK2Node_InputAxisEvent>())
    {
        OutType = EN2CNodeType::InputAxisEvent;
        return true;
    }

    if (Node->IsA<UK2Node_InputAxisKeyEvent>())
    {
        OutType = EN2CNodeType::InputAxisKeyEvent;
        return true;
    }

    if (Node->IsA<UK2Node_InputKey>())
    {
        OutType = EN2CNodeType::InputKey;
        return true;
    }

    if (Node->IsA<UK2Node_InputKeyEvent>())
    {
        OutType = EN2CNodeType::InputKeyEvent;
        return true;
    }

    if (Node->IsA<UK2Node_InputTouch>())
    {
        OutType = EN2CNodeType::InputTouch;
        return true;
    }

    if (Node->IsA<UK2Node_InputTouchEvent>())
    {
        OutType = EN2CNodeType::InputTouchEvent;
        return true;
    }

    if (Node->IsA<UK2Node_InputVectorAxisEvent>())
    {
        OutType = EN2CNodeType::InputVectorAxisEvent;
        return true;
    }

    if (Node->IsA<UK2Node_Knot>())
    {
        OutType = EN2CNodeType::Knot;
        return true;
    }

    if (Node->IsA<UK2Node_Literal>())
    {
        OutType = EN2CNodeType::Literal;
        return true;
    }

    if (Node->IsA<UK2Node_LoadAsset>())
    {
        OutType = EN2CNodeType::LoadAsset;
        return true;
    }

    if (Node->IsA<UK2Node_MacroInstance>())
    {
        OutType = EN2CNodeType::MacroInstance;
        return true;
    }

    if (Node->IsA<UK2Node_MakeArray>())
    {
        OutType = EN2CNodeType::MakeArray;
        return true;
    }

    if (Node->IsA<UK2Node_MakeContainer>())
    {
        OutType = EN2CNodeType::MakeContainer;
        return true;
    }

    if (Node->IsA<UK2Node_MakeMap>())
    {
        OutType = EN2CNodeType::MakeMap;
        return true;
    }

    if (Node->IsA<UK2Node_MakeSet>())
    {
        OutType = EN2CNodeType::MakeSet;
        return true;
    }
    
    if (Node->IsA<UK2Node_MakeVariable>())
    {
        OutType = EN2CNodeType::MakeVariable;
        return true;
    }

    if (Node->IsA<UK2Node_MathExpression>())
    {
        OutType = EN2CNodeType::MathExpression;
        return true;
    }

    if (Node->IsA<UK2Node_Message>())
    {
        OutType = EN2CNodeType::Message;
        return true;
    }

    if (Node->IsA<UK2Node_MultiGate>())
    {
        OutType = EN2CNodeType::MultiGate;
        return true;
    }

    if (Node->IsA<UK2Node_PromotableOperator>())
    {
        OutType = EN2CNodeType::PromotableOperator;
        return true;
    }

    if (Node->IsA<UK2Node_PureAssignmentStatement>())
    {
        OutType = EN2CNodeType::PureAssignmentStatement;
        return true;
    }

    if (Node->IsA<UK2Node_RemoveDelegate>())
    {
        OutType = EN2CNodeType::RemoveDelegate;
        return true;
    }

    if (Node->IsA<UK2Node_Select>())
    {
        OutType = EN2CNodeType::Select;
        return true;
    }

    if (Node->IsA<UK2Node_Self>())
    {
        OutType = EN2CNodeType::Self;
        return true;
    }

    if (Node->IsA<UK2Node_SetFieldsInStruct>())
    {
        OutType = EN2CNodeType::SetFieldsInStruct;
        return true;
    }

    if (Node->IsA<UK2Node_SetVariableOnPersistentFrame>())
    {
        OutType = EN2CNodeType::SetVariableOnPersistentFrame;
        return true;
    }

    if (Node->IsA<UK2Node_SpawnActor>())
    {
        OutType = EN2CNodeType::SpawnActor;
        return true;
    }

    if (Node->IsA<UK2Node_SpawnActorFromClass>())
    {
        OutType = EN2CNodeType::SpawnActorFromClass; 
        return true;
    }

    if (Node->IsA<UK2Node_StructMemberGet>())
    {
        OutType = EN2CNodeType::StructMemberGet;
        return true;
    }

    if (Node->IsA<UK2Node_StructMemberSet>())
    {
        OutType = EN2CNodeType::StructMemberSet;
        return true;
    }

    if (Node->IsA<UK2Node_StructOperation>())
    {
        OutType = EN2CNodeType::StructOperation;
        return true;
    }

    if (Node->IsA<UK2Node_Switch>())
    {
        OutType = EN2CNodeType::Switch;
        return true;
    }

    if (Node->IsA<UK2Node_SwitchEnum>())
    {
        OutType = EN2CNodeType::SwitchEnum;
        return true;
    }

    if (Node->IsA<UK2Node_SwitchInteger>())
    {
        OutType = EN2CNodeType::SwitchInt;
        return true;
    }

    if (Node->IsA<UK2Node_SwitchName>())
    {
        OutType = EN2CNodeType::SwitchName;
        return true;
    }

    if (Node->IsA<UK2Node_SwitchString>())
    {
        OutType = EN2CNodeType::SwitchString;
        return true;
    }

    if (Node->IsA<UK2Node_TemporaryVariable>())
    {
        OutType = EN2CNodeType::TemporaryVariable;
        return true;
    }

    if (Node->IsA<UK2Node_Timeline>())
    {
        OutType = EN2CNodeType::Timeline;
        return true;
    }

    if (Node->IsA<UK2Node_Tunnel>())
    {
        OutType = EN2CNodeType::Tunnel;
        return true;
    }

    if (Node->IsA<UK2Node_TunnelBoundary>())
    {
        OutType = EN2CNodeType::TunnelBoundary;
        return true;
    }
    
    // Default to call function type for unknown types
    OutType = EN2CNodeType::CallFunction;
    return false;
}

EN2CNodeType FN2CNodeTypeRegistry::DetermineVariableNodeType(const UK2Node_Variable* Node)
{
    if (!Node)
    {
        return EN2CNodeType::Variable;
    }

    // Get variable property safely
    FProperty* VariableProperty = Node->GetPropertyForVariable();
    if (!VariableProperty)
    {
        return EN2CNodeType::Variable;
    }

    // Get Blueprint class safely
    UClass* VarBP = Node->GetBlueprintClassFromNode();
    if (!VarBP)
    {
        return EN2CNodeType::Variable;
    }

    // Get scope safely
    UStruct const* VariableScope = Node->VariableReference.GetMemberScope(VarBP);

    // Check if variable is a function parameter
    if (VariableProperty && VariableProperty->HasAnyPropertyFlags(CPF_Parm))
    {
        return EN2CNodeType::FunctionParameter;
    }

    // Check if variable is a local function variable
    if (VariableProperty && !VariableProperty->HasAnyPropertyFlags(CPF_Parm) && 
        Node->VariableReference.IsLocalScope())
    {
        return EN2CNodeType::LocalFunctionVariable;
    }
        
    if (Node->IsA<UK2Node_VariableGet>())
    {
        // Check if local variable get
        if (VariableScope != nullptr)
        {
            return EN2CNodeType::LocalVariableGet;
        }
        return EN2CNodeType::VariableGet;
    }
    
    if (Node->IsA<UK2Node_VariableSet>())
    {
        // Check if local variable set
        if (VariableScope != nullptr)
        {
            return EN2CNodeType::LocalVariableSet;
        }
        return EN2CNodeType::VariableSet;
    }

    return EN2CNodeType::Variable;
}
