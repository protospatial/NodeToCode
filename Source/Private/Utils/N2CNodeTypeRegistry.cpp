// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CNodeTypeRegistry.h"

#include "K2Node.h"
#include "K2Node_EventNodeInterface.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

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

bool FN2CNodeTypeRegistry::FindOwnMapping(const UClass* Class, EN2CNodeType& OutType) const
{
    if (const EN2CNodeType* ByClass = ClassMappings.Find(Class))
    {
        OutType = *ByClass;
        return true;
    }
    if (const EN2CNodeType* ByName = ClassNameMappings.Find(FName(*GetBaseNodeType(Class->GetName()))))
    {
        OutType = *ByName;
        return true;
    }
    return false;
}

EN2CNodeType FN2CNodeTypeRegistry::GetNodeType(const UK2Node* Node)
{
    if (!Node)
    {
        return EN2CNodeType::Unknown;
    }

    // The nearest mapped ancestor wins: BreakStruct before its base StructOperation and that before Variable,
    // SetFieldsInStruct before MakeStruct, a CallFunction subclass with no mapping of its own is a CallFunction
    EN2CNodeType Type = EN2CNodeType::Unknown;
    bool bMapped = false;
    for (const UClass* Class = Node->GetClass(); Class && !bMapped; Class = Class->GetSuperClass())
    {
        bMapped = FindOwnMapping(Class, Type);
    }

    if (!bMapped)
    {
        // Interfaces are not in the super-class chain (IsA never matches them, translation-core-15)
        return Node->GetClass()->ImplementsInterface(UK2Node_EventNodeInterface::StaticClass())
            ? EN2CNodeType::Event
            : EN2CNodeType::Unknown;
    }

    if (Type == EN2CNodeType::Variable || Type == EN2CNodeType::VariableGet || Type == EN2CNodeType::VariableSet)
    {
        if (const UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
        {
            return DetermineVariableNodeType(VarNode);
        }
    }
    return Type;
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

    // Classes the IsA chain mapped before card 0234 that had no class-name mapping
    RegisterNodeType(FName(TEXT("EditablePinBase")), EN2CNodeType::EditablePinBase);
    RegisterNodeType(FName(TEXT("ForEachElementInEnum")), EN2CNodeType::ForEachElementInEnum);
    RegisterNodeType(FName(TEXT("GetDataTableRow")), EN2CNodeType::GetDataTableRow);
    RegisterNodeType(FName(TEXT("GetInputAxisKeyValue")), EN2CNodeType::GetInputAxisKeyValue);
    RegisterNodeType(FName(TEXT("GetInputAxisValue")), EN2CNodeType::GetInputAxisValue);
    RegisterNodeType(FName(TEXT("GetInputVectorAxisValue")), EN2CNodeType::GetInputVectorAxisValue);
    RegisterNodeType(FName(TEXT("Select")), EN2CNodeType::Select);

    // Engine and Enhanced Input nodes that used to fall through to CallFunction (translation-core-15). Mapped by
    // name, so NodeToCode needs no dependency on the InputBlueprintNodes module.
    RegisterNodeType(FName(TEXT("EnhancedInputAction")), EN2CNodeType::InputAction);
    RegisterNodeType(FName(TEXT("GetInputActionValue")), EN2CNodeType::GetInputAxisValue);
    RegisterNodeType(FName(TEXT("MapForEach")), EN2CNodeType::ForEachLoop);
    RegisterNodeType(FName(TEXT("SetForEach")), EN2CNodeType::ForEachLoop);
    RegisterNodeType(FName(TEXT("GenericToText")), EN2CNodeType::GenericToText);
}

EN2CNodeType FN2CNodeTypeRegistry::DetermineVariableNodeType(const UK2Node_Variable* Node)
{
    if (!Node)
    {
        return EN2CNodeType::Variable;
    }

    // Access first (translation-core-17): a get and a set of the same variable never share a type
    const bool bGet = Node->IsA<UK2Node_VariableGet>();
    const bool bSet = Node->IsA<UK2Node_VariableSet>();

    // Then scope. A function parameter is a local of its function whose property has CPF_Parm; the reference's own
    // scope still decides when the property does not resolve (an unresolved variable keeps its get or set type).
    const FProperty* VariableProperty = Node->GetPropertyForVariable();
    if (VariableProperty && VariableProperty->HasAnyPropertyFlags(CPF_Parm))
    {
        return bSet ? EN2CNodeType::LocalVariableSet : EN2CNodeType::FunctionParameter;
    }
    if (Node->VariableReference.IsLocalScope())
    {
        return bGet ? EN2CNodeType::LocalVariableGet
            : bSet ? EN2CNodeType::LocalVariableSet
            : EN2CNodeType::LocalFunctionVariable;
    }
    return bGet ? EN2CNodeType::VariableGet
        : bSet ? EN2CNodeType::VariableSet
        : EN2CNodeType::Variable;
}
