// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

/**
 * @file N2CNode.h
 * @brief Node-related data type definitions for the Node to Code plugin
 */

#pragma once

#include "CoreMinimal.h"
#include "N2CPin.h"
#include "Utils/N2CLogger.h"
#include "N2CNode.generated.h"

/**
 * @enum EN2CNodeType
 * @brief Defines the specific type of Blueprint node
 */
UENUM(BlueprintType)
enum class EN2CNodeType : uint8
{

    // Function Calls
    CallFunction         UMETA(DisplayName = "call_function"),
    CallArrayFunction    UMETA(DisplayName = "call_array_function"),
    CallDataTableFunction UMETA(DisplayName = "call_datatable_function"),
    CallDelegate        UMETA(DisplayName = "call_delegate"),
    CallFunctionOnMember UMETA(DisplayName = "call_function_on_member"),
    CallMaterialParameterCollection UMETA(DisplayName = "call_material_parameter_collection"),
    CallParentFunction  UMETA(DisplayName = "call_parent_function"),
    GenericToText       UMETA(DisplayName = "generic_to_text"),
    GetDataTableRow     UMETA(DisplayName = "get_datatable_row"),
    FunctionEntry       UMETA(DisplayName = "function_entry"),
    FunctionResult      UMETA(DisplayName = "function_result"),
    FunctionTerminator  UMETA(DisplayName = "function_terminator"),

    // Variables
    VariableSet         UMETA(DisplayName = "variable_set"),
    VariableGet         UMETA(DisplayName = "variable_get"),
    VariableSetRef      UMETA(DisplayName = "variable_set_ref"),
    Variable            UMETA(DisplayName = "variable"),
    LocalVariable       UMETA(DisplayName = "local_variable"),
    LocalVariableSet    UMETA(DisplayName = "local_variable_set"),
    LocalVariableGet    UMETA(DisplayName = "local_variable_get"),
    FunctionParameter   UMETA(DisplayName = "function_parameter"),
    LocalFunctionVariable UMETA(DisplayName = "local_function_variable"),
    MakeVariable        UMETA(DisplayName = "make_variable"),
    TemporaryVariable   UMETA(DisplayName = "temporary_variable"),
    SetVariableOnPersistentFrame UMETA(DisplayName = "set_variable_on_persistent_frame"),
    
    // Events
    Event               UMETA(DisplayName = "event"),
    CustomEvent         UMETA(DisplayName = "custom_event"),
    ActorBoundEvent     UMETA(DisplayName = "actor_bound_event"),
    ComponentBoundEvent UMETA(DisplayName = "component_bound_event"),
    GeneratedBoundEvent UMETA(DisplayName = "generated_bound_event"),
    GetInputAxisKeyValue UMETA(DisplayName = "get_input_axis_key_value"),
    GetInputAxisValue   UMETA(DisplayName = "get_input_axis_value"),
    GetInputVectorAxisValue UMETA(DisplayName = "get_input_vector_axis_value"),
    EventNodeInterface  UMETA(DisplayName = "event_node_interface"),
    InputAction         UMETA(DisplayName = "input_action"),
    InputActionEvent    UMETA(DisplayName = "input_action_event"),
    InputAxisEvent      UMETA(DisplayName = "input_axis_event"),
    InputAxisKeyEvent   UMETA(DisplayName = "input_axis_key_event"),
    InputKey            UMETA(DisplayName = "input_key"),
    InputKeyEvent       UMETA(DisplayName = "input_key_event"),
    InputTouch          UMETA(DisplayName = "input_touch"),
    InputTouchEvent     UMETA(DisplayName = "input_touch_event"),
    InputVectorAxisEvent UMETA(DisplayName = "input_vector_axis_event"),

    // Flow Control
    ForEachLoop         UMETA(DisplayName = "for_each_loop"),
    ForEachElementInEnum UMETA(DisplayName = "for_each_element_in_enum"),
    WhileLoop           UMETA(DisplayName = "while_loop"),
    ForLoop             UMETA(DisplayName = "for_loop"),
    Sequence            UMETA(DisplayName = "sequence"),
    Branch              UMETA(DisplayName = "branch"),
    Select              UMETA(DisplayName = "select"),
    Gate                UMETA(DisplayName = "gate"),
    MultiGate           UMETA(DisplayName = "multi_gate"),
    DoOnceMultiInput    UMETA(DisplayName = "do_once_multi_input"),
    DoOnce              UMETA(DisplayName = "do_once"),
    Knot                UMETA(DisplayName = "knot"),
    Tunnel              UMETA(DisplayName = "tunnel"),
    TunnelBoundary      UMETA(DisplayName = "tunnel_boundary"),

    // Switches
    Switch              UMETA(DisplayName = "switch"),
    SwitchInt           UMETA(DisplayName = "switch_int"),
    SwitchString        UMETA(DisplayName = "switch_string"),
    SwitchEnum          UMETA(DisplayName = "switch_enum"),
    SwitchName          UMETA(DisplayName = "switch_name"),

    // Structs and Objects
    MakeStruct          UMETA(DisplayName = "make_struct"),
    BreakStruct         UMETA(DisplayName = "break_struct"),
    SetFieldsInStruct   UMETA(DisplayName = "set_fields_in_struct"),
    StructMemberGet     UMETA(DisplayName = "struct_member_get"),
    StructMemberSet     UMETA(DisplayName = "struct_member_set"),
    StructOperation     UMETA(DisplayName = "struct_operation"),

    // Containers
    MakeArray           UMETA(DisplayName = "make_array"),
    MakeMap             UMETA(DisplayName = "make_map"),
    MakeSet             UMETA(DisplayName = "make_set"),
    MakeContainer       UMETA(DisplayName = "make_container"),
    GetArrayItem        UMETA(DisplayName = "get_array_item"),

    // Casting and Conversion
    DynamicCast         UMETA(DisplayName = "dynamic_cast"),
    ClassDynamicCast    UMETA(DisplayName = "class_dynamic_cast"),
    CastByteToEnum      UMETA(DisplayName = "cast_byte_to_enum"),
    ConvertAsset        UMETA(DisplayName = "convert_asset"),
    EditablePinBase     UMETA(DisplayName = "editable_pin_base"),
    ExternalGraphInterface UMETA(DisplayName = "external_graph_interface"),

    // Delegates
    AddDelegate         UMETA(DisplayName = "add_delegate"),
    CreateDelegate      UMETA(DisplayName = "create_delegate"),
    ClearDelegate       UMETA(DisplayName = "clear_delegate"),
    RemoveDelegate      UMETA(DisplayName = "remove_delegate"),
    AssignDelegate      UMETA(DisplayName = "assign_delegate"),
    DelegateSet         UMETA(DisplayName = "delegate_set"),

    // Async/Latent
    AsyncAction         UMETA(DisplayName = "async_action"),
    BaseAsyncTask       UMETA(DisplayName = "base_async_task"),

    // Components
    AddComponent        UMETA(DisplayName = "add_component"),
    AddComponentByClass UMETA(DisplayName = "add_component_by_class"),
    AddPinInterface     UMETA(DisplayName = "add_pin_interface"),

    // Misc Utility
    ConstructObjectFromClass UMETA(DisplayName = "construct_object_from_class"),
    GenericCreateObject UMETA(DisplayName = "generic_create_object"),
    Timeline            UMETA(DisplayName = "timeline"),
    SpawnActor          UMETA(DisplayName = "spawn_actor"),
    SpawnActorFromClass UMETA(DisplayName = "spawn_actor_from_class"),
    FormatText          UMETA(DisplayName = "format_text"),
    GetClassDefaults    UMETA(DisplayName = "get_class_defaults"),
    GetSubsystem        UMETA(DisplayName = "get_subsystem"),
    LoadAsset           UMETA(DisplayName = "load_asset"),
    Copy                UMETA(DisplayName = "copy"),
    Comment             UMETA(DisplayName = "comment"),
    
    // Math/Logic
    BitmaskLiteral      UMETA(DisplayName = "bitmask_literal"),
    EnumEquality        UMETA(DisplayName = "enum_equality"),
    EnumInequality      UMETA(DisplayName = "enum_inequality"),
    EnumLiteral         UMETA(DisplayName = "enum_literal"),
    GetEnumeratorName   UMETA(DisplayName = "get_enumerator_name"),
    GetEnumeratorNameAsString UMETA(DisplayName = "get_enumerator_name_as_string"),
    GetNumEnumEntries   UMETA(DisplayName = "get_num_enum_entries"),
    MathExpression      UMETA(DisplayName = "math_expression"),
    EaseFunction        UMETA(DisplayName = "ease_function"),
    CommutativeAssociativeBinaryOperator UMETA(DisplayName = "commutative_associative_binary_operator"),
    PureAssignmentStatement UMETA(DisplayName = "pure_assignment_statement"),
    AssignmentStatement UMETA(DisplayName = "assignment_statement"),
    
    // Special Types
    Self                UMETA(DisplayName = "self"),
    Composite           UMETA(DisplayName = "composite"),
    DeadClass           UMETA(DisplayName = "dead_class"),
    Literal             UMETA(DisplayName = "literal"),
    Message             UMETA(DisplayName = "interface_message"),
    PromotableOperator  UMETA(DisplayName = "promotable_operator"),
    MacroInstance       UMETA(DisplayName = "macro_instance"),
    BaseMCDelegate      UMETA(DisplayName = "base_mc_delegate"),
};

/**
 * @struct FN2CNodeDefinition
 * @brief Defines a single node within a Blueprint graph for code generation
 */
USTRUCT(BlueprintType)
struct FN2CNodeDefinition
{
    GENERATED_BODY()

    /** Short ID for node references (e.g. "N1") */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ID;

    /** The specific node type for code generation */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CNodeType NodeType;

    /** Display name or function name */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Class/scope containing the function/variable */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString MemberParent;

    /** Gets a cleaned version of the MemberParent name without common prefixes/suffixes */
    FString GetCleanMemberParent() const
    {
        FString CleanName = MemberParent;
        
        // Remove SKEL_ prefix if present
        if (CleanName.StartsWith(TEXT("SKEL_")))
        {
            CleanName.RightChopInline(5);
        }
        
        // Remove _C suffix if present (indicates Blueprint class)
        if (CleanName.EndsWith(TEXT("_C")))
        {
            CleanName.LeftChopInline(2);
        }
        
        return CleanName;
    }

    /** Function/variable name being accessed */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString MemberName;

    /** Comment that was added to the node */
    FString Comment;

    /** Node behavior flags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    uint8 bPure:1;        // Pure vs Impure function

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    uint8 bLatent:1;      // Latent/async operation

    /** Input/Output parameters */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CPinDefinition> InputPins;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CPinDefinition> OutputPins;

    FN2CNodeDefinition()
        : ID(TEXT(""))
        , NodeType(EN2CNodeType::CallFunction)
        , Name(TEXT(""))
        , MemberParent(TEXT(""))
        , MemberName(TEXT(""))
        , Comment(TEXT(""))
        , bPure(false)
        , bLatent(false)
    {
    }

    /** Validates the node definition and its enums */
    bool IsValid() const;
};
