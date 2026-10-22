// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

/**
 * @file N2CBlueprint.h
 * @brief Blueprint-related data type definitions for the Node to Code plugin
 */

#pragma once

#include "CoreMinimal.h"
#include "N2CNode.h"
#include "Utils/N2CLogger.h"
#include "N2CBlueprint.generated.h"

/**
 * @struct FN2CVersion
 * @brief Version information for N2CStruct format
 */
USTRUCT(BlueprintType)
struct FN2CVersion
{
    GENERATED_BODY()

    /** Version string, always "1.0.0" in current spec */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Value;

    FN2CVersion() : Value(TEXT("1.0.0")) {}
};

/**
 * @enum EN2CBlueprintType
 * @brief Defines the type of Blueprint being processed
 */
UENUM(BlueprintType)
enum class EN2CBlueprintType : uint8
{
    /** Normal blueprint. */
    Normal  UMETA(DisplayName = "Blueprint Class"),
    /** Blueprint that is const during execution (no state graph and methods cannot modify member variables). */
    Const   UMETA(DisplayName = "Const Blueprint Class"),
    /** Blueprint that serves as a container for macros to be used in other blueprints. */
    MacroLibrary UMETA(DisplayName = "Blueprint Macro Library"),
    /** Blueprint that serves as an interface to be implemented by other blueprints. */
    Interface UMETA(DisplayName = "Blueprint Interface"),
    /** Blueprint that handles level scripting. */
    LevelScript UMETA(DisplayName = "Level Blueprint"),
    /** Blueprint that serves as a container for functions to be used in other blueprints. */
    FunctionLibrary UMETA(DisplayName = "Blueprint Function Library"),
};

/**
 * @struct FN2CMetadata
 * @brief Required metadata about the Blueprint
 */
USTRUCT(BlueprintType)
struct FN2CMetadata
{
    GENERATED_BODY()

    /** Name of the Blueprint */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Type of the Blueprint */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CBlueprintType BlueprintType;

    /** The Blueprint class this graph belongs to */                                                                                                                                                                                 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")                                                                                                                                                             
    FString BlueprintClass; 

    FN2CMetadata() : Name(TEXT("")), BlueprintType(EN2CBlueprintType::Normal), BlueprintClass(TEXT("")) {}
};

/**
 * @struct FN2CFlows 
 * @brief Contains all execution and data flow connections between nodes
 */
USTRUCT(BlueprintType)
struct FN2CFlows
{
    GENERATED_BODY()

    /** Execution array. Each entry is a chain like "N1->N2->N3" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> Execution;

    /** Data connections: a mapping from "N1.P4" to "N2.P3" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> Data;

    FN2CFlows()
    {
    }
};

/**
 * @enum EN2CStructMemberType
 * @brief Defines the type of a struct member
 */
UENUM(BlueprintType)
enum class EN2CStructMemberType : uint8
{
    Bool        UMETA(DisplayName = "Boolean"),
    Byte        UMETA(DisplayName = "Byte"),
    Int         UMETA(DisplayName = "Integer"),
    Float       UMETA(DisplayName = "Float"),
    String      UMETA(DisplayName = "String"),
    Name        UMETA(DisplayName = "Name"),
    Text        UMETA(DisplayName = "Text"),
    Vector      UMETA(DisplayName = "Vector"),
    Vector2D    UMETA(DisplayName = "Vector2D"),
    Rotator     UMETA(DisplayName = "Rotator"),
    Transform   UMETA(DisplayName = "Transform"),
    Class       UMETA(DisplayName = "Class"),
    Object      UMETA(DisplayName = "Object"),
    Struct      UMETA(DisplayName = "Struct"),
    Enum        UMETA(DisplayName = "Enum"),
    Array       UMETA(DisplayName = "Array"),
    Set         UMETA(DisplayName = "Set"),
    Map         UMETA(DisplayName = "Map"),
    Custom      UMETA(DisplayName = "Custom")
};

/**
 * @struct FN2CStructMember
 * @brief Represents a single member of a struct
 */
USTRUCT(BlueprintType)
struct FN2CStructMember
{
    GENERATED_BODY()

    /** Member name */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Member type */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CStructMemberType Type;

    /** Type name - required for structs, enums, objects, classes, etc. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TypeName;

    /** Container flags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsArray = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsSet = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsMap = false;

    /** Key type for maps */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CStructMemberType KeyType;

    /** Key type name for maps (if needed) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString KeyTypeName;

    /** Default value as string (if any) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultValue;

    /** Member comment (if any) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    FN2CStructMember()
        : Name(TEXT(""))
        , Type(EN2CStructMemberType::Int)
        , TypeName(TEXT(""))
        , KeyType(EN2CStructMemberType::Int)
        , KeyTypeName(TEXT(""))
        , DefaultValue(TEXT(""))
        , Comment(TEXT(""))
    {
    }
};

/**
 * @struct FN2CStruct
 * @brief Represents a blueprint-defined struct
 */
USTRUCT(BlueprintType)
struct FN2CStruct
{
    GENERATED_BODY()

    /** Struct name */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Struct comment */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    /** List of struct members */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CStructMember> Members;

    FN2CStruct()
        : Name(TEXT(""))
        , Comment(TEXT(""))
    {
    }

    /** Validates the struct definition */
    bool IsValid() const;
};

/**
 * @struct FN2CVariable
 * @brief Represents a Blueprint variable declaration
 */
USTRUCT(BlueprintType)
struct FN2CVariable
{
    GENERATED_BODY()

    /** Variable name */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Variable type (basic category) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CStructMemberType Type;

    /** Full type name for objects/structs/enums/classes */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TypeName;

    /** Container flags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsArray = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsSet = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsMap = false;

    /** Map key type if this variable is a Map */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CStructMemberType KeyType;

    /** Map key type name for complex keys */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString KeyTypeName;

    /** Default value as string (best-effort) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultValue;

    /** Optional comment/tooltip */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    FN2CVariable()
        : Name(TEXT(""))
        , Type(EN2CStructMemberType::Int)
        , TypeName(TEXT(""))
        , KeyType(EN2CStructMemberType::Int)
        , KeyTypeName(TEXT(""))
        , DefaultValue(TEXT(""))
        , Comment(TEXT(""))
    {
    }
};

/**
 * @struct FN2CComponentOverride
 * @brief Represents a Blueprint component instance and its overridden default properties
 */
USTRUCT(BlueprintType)
struct FN2CComponentOverride
{
    GENERATED_BODY()

    /** Component instance name on the Blueprint (variable name on the actor) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ComponentName;

    /** Component class name (e.g. StaticMeshComponent) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ComponentClassName;

    /** Optional attach parent component/variable name */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString AttachParentName;

    /** Properties on this component whose defaults were overridden in the Blueprint */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CVariable> OverriddenProperties;

    FN2CComponentOverride()
        : ComponentName(TEXT(""))
        , ComponentClassName(TEXT(""))
        , AttachParentName(TEXT(""))
    {
    }
};

/**
 * @struct FN2CEnumValue
 * @brief Represents a single value in an enum
 */
USTRUCT(BlueprintType)
struct FN2CEnumValue
{
    GENERATED_BODY()

    /** Enum value name */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;
	
    /** Enum value comment (if any) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    FN2CEnumValue()
        : Name(TEXT(""))
        , Comment(TEXT(""))
    {
    }
};

/**
 * @struct FN2CEnum
 * @brief Represents a blueprint-defined enum
 */
USTRUCT(BlueprintType)
struct FN2CEnum
{
    GENERATED_BODY()

    /** Enum name */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Enum comment */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    /** List of enum values */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CEnumValue> Values;

    FN2CEnum()
        : Name(TEXT(""))
        , Comment(TEXT(""))
    {
    }

    /** Validates the enum definition */
    bool IsValid() const;
};

/**
 * @enum EN2CGraphType
 * @brief Defines the type of Blueprint graph
 */
UENUM(BlueprintType)
enum class EN2CGraphType : uint8
{
    /** The main event graph */
    EventGraph       UMETA(DisplayName = "Event Graph"),
    /** A user-created function */
    Function        UMETA(DisplayName = "Function"),
    /** A collapsed graph/composite node */
    Composite       UMETA(DisplayName = "Composite"),
    /** A macro graph */
    Macro          UMETA(DisplayName = "Macro"),
    /** A construction script */
    Construction    UMETA(DisplayName = "Construction Script"),
    /** An animation graph */
    Animation      UMETA(DisplayName = "Animation"),
    /** A synthetic class skeleton graph (no nodes, only class-level structure) */
    ClassItSelf  UMETA(DisplayName = "Class Skeleton"),
    /** A struct definition */
    Struct         UMETA(DisplayName = "Struct"),
    /** An enum definition */
    Enum           UMETA(DisplayName = "Enum")
};

/**
 * @struct FN2CGraph
 * @brief Represents a single graph within the Blueprint
 */
USTRUCT(BlueprintType)
struct FN2CGraph
{
    GENERATED_BODY()

    /** Name of the graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** Type of graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CGraphType GraphType;

    /** Array of all nodes in this graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CNodeDefinition> Nodes;

    /** Execution and data flow connections for this graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CFlows Flows;

    /** Local variables declared for this graph (e.g. function-local variables) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CVariable> LocalVariables;

    FN2CGraph()
        : GraphType(EN2CGraphType::EventGraph)
    {
    }

    /** Validates the graph structure */
    bool IsValid() const;
};

/**
 * @struct FN2CBlueprint
 * @brief Top-level container for Blueprint graph data
 */
USTRUCT(BlueprintType)
struct FN2CBlueprint
{
    GENERATED_BODY()

    /** Version information (always "1.0.0" in current spec) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CVersion Version;

    /** Required metadata about the Blueprint */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CMetadata Metadata;

    /** Array of all graphs in the Blueprint */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CGraph> Graphs;

    /** Array of all structs used in the Blueprint */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CStruct> Structs;

    /** Array of all enums used in the Blueprint */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CEnum> Enums;

    /** Array of declared Blueprint variables */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CVariable> Variables;

    /** Array of Blueprint components and their overridden default properties */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CComponentOverride> Components;

    FN2CBlueprint()
    {
        // Version is automatically initialized to "1.0.0" by FN2CVersion constructor
    }

    /** Validates the Blueprint structure and its enums */
    bool IsValid() const;
};
