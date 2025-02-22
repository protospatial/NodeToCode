// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Utils/N2CNodeTypeHelper.h"

#include "Utils/N2CLogger.h"

void FN2CNodeTypeHelper::DetermineNodeType(const UK2Node* Node, EN2CNodeType& OutType)
{
    
    
    // Default to function call
    OutType = EN2CNodeType::CallFunction;

    if (!Node)
    {
        return;
    }

    // Try setting make struct type first since MakeStruct is considered a variable
    // before being considered MakeStruct
    if (const UK2Node_MakeStruct* MakeStructNode = Cast<UK2Node_MakeStruct>(Node))
    {
        if (MapFromClassName(Node->GetClass()->GetName(), OutType))
        {
            return;
        }
    }

    // Try setting variable type first
    if (const UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
    {
        OutType = DetermineVariableNodeType(VarNode);

        return;
    }
    
    // Try mapping from class name second
    if (MapFromClassName(Node->GetClass()->GetName(), OutType))
    {
        return;
    }

    // Fall back to inheritance-based mapping
    MapFromInheritance(Node, OutType);
}

bool FN2CNodeTypeHelper::MapFromClassName(const FString& ClassName, EN2CNodeType& OutType)
{
    // Strip K2Node_ prefix
    FString BaseType = GetBaseNodeType(ClassName);
    
    // Direct mappings
    static const TMap<FString, EN2CNodeType> DirectMappings = {
        // Function Calls
        { TEXT("CallFunction"), EN2CNodeType::CallFunction },
        { TEXT("CallArrayFunction"), EN2CNodeType::CallArrayFunction },
        { TEXT("CallDataTableFunction"), EN2CNodeType::CallDataTableFunction },
        { TEXT("CallDelegate"), EN2CNodeType::CallDelegate },
        { TEXT("CallFunctionOnMember"), EN2CNodeType::CallFunctionOnMember },
        { TEXT("CallMaterialParameterCollectionFunction"), EN2CNodeType::CallMaterialParameterCollection },
        { TEXT("CallParentFunction"), EN2CNodeType::CallParentFunction },
        { TEXT("FunctionEntry"), EN2CNodeType::FunctionEntry },
        { TEXT("FunctionResult"), EN2CNodeType::FunctionResult },
        { TEXT("FunctionTerminator"), EN2CNodeType::FunctionTerminator },

        // Variables
        { TEXT("Variable"), EN2CNodeType::Variable },
        { TEXT("VariableGet"), EN2CNodeType::VariableGet },
        { TEXT("VariableSet"), EN2CNodeType::VariableSet },
        { TEXT("VariableSetRef"), EN2CNodeType::VariableSetRef },
        { TEXT("LocalVariable"), EN2CNodeType::LocalVariable },
        { TEXT("MakeVariable"), EN2CNodeType::MakeVariable },
        { TEXT("TemporaryVariable"), EN2CNodeType::TemporaryVariable },
        { TEXT("SetVariableOnPersistentFrame"), EN2CNodeType::SetVariableOnPersistentFrame },

        // Events
        { TEXT("Event"), EN2CNodeType::Event },
        { TEXT("CustomEvent"), EN2CNodeType::CustomEvent },
        { TEXT("ActorBoundEvent"), EN2CNodeType::ActorBoundEvent },
        { TEXT("ComponentBoundEvent"), EN2CNodeType::ComponentBoundEvent },
        { TEXT("InputAction"), EN2CNodeType::InputAction },
        { TEXT("InputActionEvent"), EN2CNodeType::InputActionEvent },
        { TEXT("InputAxisEvent"), EN2CNodeType::InputAxisEvent },
        { TEXT("InputAxisKeyEvent"), EN2CNodeType::InputAxisKeyEvent },
        { TEXT("InputKey"), EN2CNodeType::InputKey },
        { TEXT("InputKeyEvent"), EN2CNodeType::InputKeyEvent },
        { TEXT("InputTouch"), EN2CNodeType::InputTouch },
        { TEXT("InputTouchEvent"), EN2CNodeType::InputTouchEvent },
        { TEXT("InputVectorAxisEvent"), EN2CNodeType::InputVectorAxisEvent },

        // Flow Control
        { TEXT("ExecutionSequence"), EN2CNodeType::Sequence },
        { TEXT("IfThenElse"), EN2CNodeType::Branch },
        { TEXT("DoOnceMultiInput"), EN2CNodeType::DoOnceMultiInput },
        { TEXT("MultiGate"), EN2CNodeType::MultiGate },
        { TEXT("Knot"), EN2CNodeType::Knot },
        { TEXT("Tunnel"), EN2CNodeType::Tunnel },
        { TEXT("TunnelBoundary"), EN2CNodeType::TunnelBoundary },

        // Switches
        { TEXT("Switch"), EN2CNodeType::Switch },
        { TEXT("SwitchInteger"), EN2CNodeType::SwitchInt },
        { TEXT("SwitchString"), EN2CNodeType::SwitchString },
        { TEXT("SwitchEnum"), EN2CNodeType::SwitchEnum },
        { TEXT("SwitchName"), EN2CNodeType::SwitchName },

        // Structs and Objects
        { TEXT("MakeStruct"), EN2CNodeType::MakeStruct },
        { TEXT("BreakStruct"), EN2CNodeType::BreakStruct },
        { TEXT("SetFieldsInStruct"), EN2CNodeType::SetFieldsInStruct },
        { TEXT("StructMemberGet"), EN2CNodeType::StructMemberGet },
        { TEXT("StructMemberSet"), EN2CNodeType::StructMemberSet },
        { TEXT("StructOperation"), EN2CNodeType::StructOperation },

        // Containers
        { TEXT("MakeArray"), EN2CNodeType::MakeArray },
        { TEXT("MakeMap"), EN2CNodeType::MakeMap },
        { TEXT("MakeSet"), EN2CNodeType::MakeSet },
        { TEXT("MakeContainer"), EN2CNodeType::MakeContainer },
        { TEXT("GetArrayItem"), EN2CNodeType::GetArrayItem },

        // Casting and Conversion
        { TEXT("DynamicCast"), EN2CNodeType::DynamicCast },
        { TEXT("ClassDynamicCast"), EN2CNodeType::ClassDynamicCast },
        { TEXT("CastByteToEnum"), EN2CNodeType::CastByteToEnum },
        { TEXT("ConvertAsset"), EN2CNodeType::ConvertAsset },

        // Delegates
        { TEXT("AddDelegate"), EN2CNodeType::AddDelegate },
        { TEXT("CreateDelegate"), EN2CNodeType::CreateDelegate },
        { TEXT("ClearDelegate"), EN2CNodeType::ClearDelegate },
        { TEXT("RemoveDelegate"), EN2CNodeType::RemoveDelegate },
        { TEXT("AssignDelegate"), EN2CNodeType::AssignDelegate },
        { TEXT("DelegateSet"), EN2CNodeType::DelegateSet },

        // Async/Latent
        { TEXT("AsyncAction"), EN2CNodeType::AsyncAction },
        { TEXT("BaseAsyncTask"), EN2CNodeType::BaseAsyncTask },

        // Components
        { TEXT("AddComponent"), EN2CNodeType::AddComponent },
        { TEXT("AddComponentByClass"), EN2CNodeType::AddComponentByClass },
        { TEXT("AddPinInterface"), EN2CNodeType::AddPinInterface },

        // Misc Utility
        { TEXT("ConstructObjectFromClass"), EN2CNodeType::ConstructObjectFromClass },
        { TEXT("GenericCreateObject"), EN2CNodeType::GenericCreateObject },
        { TEXT("Timeline"), EN2CNodeType::Timeline },
        { TEXT("SpawnActor"), EN2CNodeType::SpawnActor },
        { TEXT("SpawnActorFromClass"), EN2CNodeType::SpawnActorFromClass },
        { TEXT("FormatText"), EN2CNodeType::FormatText },
        { TEXT("GetClassDefaults"), EN2CNodeType::GetClassDefaults },
        { TEXT("GetSubsystem"), EN2CNodeType::GetSubsystem },
        { TEXT("LoadAsset"), EN2CNodeType::LoadAsset },
        { TEXT("Copy"), EN2CNodeType::Copy },

        // Math/Logic
        { TEXT("BitmaskLiteral"), EN2CNodeType::BitmaskLiteral },
        { TEXT("EnumEquality"), EN2CNodeType::EnumEquality },
        { TEXT("EnumInequality"), EN2CNodeType::EnumInequality },
        { TEXT("EnumLiteral"), EN2CNodeType::EnumLiteral },
        { TEXT("GetEnumeratorName"), EN2CNodeType::GetEnumeratorName },
        { TEXT("GetEnumeratorNameAsString"), EN2CNodeType::GetEnumeratorNameAsString },
        { TEXT("GetNumEnumEntries"), EN2CNodeType::GetNumEnumEntries },
        { TEXT("MathExpression"), EN2CNodeType::MathExpression },
        { TEXT("EaseFunction"), EN2CNodeType::EaseFunction },
        { TEXT("CommutativeAssociativeBinaryOperator"), EN2CNodeType::CommutativeAssociativeBinaryOperator },
        { TEXT("PureAssignmentStatement"), EN2CNodeType::PureAssignmentStatement },
        { TEXT("AssignmentStatement"), EN2CNodeType::AssignmentStatement },

        // Special Types
        { TEXT("Self"), EN2CNodeType::Self },
        { TEXT("Composite"), EN2CNodeType::Composite },
        { TEXT("DeadClass"), EN2CNodeType::DeadClass },
        { TEXT("Literal"), EN2CNodeType::Literal },
        { TEXT("Message"), EN2CNodeType::Message },
        { TEXT("PromotableOperator"), EN2CNodeType::PromotableOperator },
        { TEXT("MacroInstance"), EN2CNodeType::MacroInstance },
        { TEXT("BaseMCDelegate"), EN2CNodeType::BaseMCDelegate }
    };

    if (const EN2CNodeType* Type = DirectMappings.Find(BaseType))
    {
        OutType = *Type;
        return true;
    }

    return false;
}

bool FN2CNodeTypeHelper::MapFromInheritance(const UK2Node* Node, EN2CNodeType& OutType)
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
        const UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
        UClass* VarBP = Node->GetBlueprintClassFromNode();
        UStruct const* VariableScope = VarNode->VariableReference.GetMemberScope(VarBP);
        
        if (Node->IsA<UK2Node_VariableGet>())
        {
            // Check if local variable get
            if (VariableScope != nullptr)
            {
                OutType = EN2CNodeType::LocalVariableGet;
                FString LogMessage = FString::Printf(TEXT("This is a local variable get"));
                FN2CLogger::Get().Log(LogMessage, EN2CLogSeverity::Debug);
            }
            else
            {
                OutType = EN2CNodeType::VariableGet;
            }
        }
        else if (Node->IsA<UK2Node_VariableSet>())
        {
            // Check if local variable set
            if (VariableScope != nullptr)
            {
                OutType = EN2CNodeType::LocalVariableSet;
                OutType = EN2CNodeType::LocalVariableGet;
                FString LogMessage = FString::Printf(TEXT("This is a local variable set"));
                FN2CLogger::Get().Log(LogMessage, EN2CLogSeverity::Debug);
            }
            else
            {
                OutType = EN2CNodeType::VariableSet;
            }
        }
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

FString FN2CNodeTypeHelper::GetBaseNodeType(const FString& ClassName)
{
    static const FString Prefix = TEXT("K2Node_");
    if (ClassName.StartsWith(Prefix))
    {
        return ClassName.RightChop(Prefix.Len());
    }
    return ClassName;
}

EN2CNodeType FN2CNodeTypeHelper::DetermineVariableNodeType(const UK2Node_Variable* Node)
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
