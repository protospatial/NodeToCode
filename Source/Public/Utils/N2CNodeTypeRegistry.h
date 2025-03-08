// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CNode.h"
#include "K2Node.h"

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

/**
 * @class FN2CNodeTypeRegistry
 * @brief Registry for mapping Blueprint node classes to N2CNodeType
 * 
 * Provides a centralized, data-driven approach to node type determination
 * that replaces the large if-else chains and switch statements.
 */
class NODETOCODE_API FN2CNodeTypeRegistry
{
public:
    /** Get the singleton instance */
    static FN2CNodeTypeRegistry& Get();
    
    /** Register node type mapping by class name */
    void RegisterNodeType(const FName& ClassName, EN2CNodeType NodeType);
    
    /** Register node type mapping by class pointer */
    void RegisterNodeClass(const UClass* Class, EN2CNodeType NodeType);
    
    /** Get node type for a specific node */
    EN2CNodeType GetNodeType(const UK2Node* Node);
    
    /** Determine variable node type */
    EN2CNodeType DetermineVariableNodeType(const UK2Node_Variable* Node);
    
private:
    /** Constructor - initializes default mappings */
    FN2CNodeTypeRegistry();
    
    /** Mappings from class names to node types */
    TMap<FName, EN2CNodeType> ClassNameMappings;
    
    /** Mappings from class pointers to node types */
    TMap<const UClass*, EN2CNodeType> ClassMappings;
    
    /** Initialize default mappings */
    void InitializeDefaultMappings();
    
    /** Get base node type from class name (strips K2Node_ prefix) */
    FString GetBaseNodeType(const FString& ClassName);
    
    /** Try to determine node type based on inheritance */
    bool MapFromInheritance(const UK2Node* Node, EN2CNodeType& OutType);
};
