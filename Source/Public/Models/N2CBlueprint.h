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

    FN2CVersion() : Value(TEXT("1.0.0")) { N2C_LOG_FUNCTION_ENTRY(); }
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

    FN2CMetadata() : Name(TEXT("")), BlueprintType(EN2CBlueprintType::Normal), BlueprintClass(TEXT("")) { N2C_LOG_FUNCTION_ENTRY(); }
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
        N2C_LOG_FUNCTION_ENTRY();
    }
};

/** Native UEdGraphPin::LinkedTo edge. GUIDs remain available even for skipped knot/hidden nodes. */
USTRUCT(BlueprintType)
struct FN2CDirectLink
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourceNodeGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourcePinGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetNodeGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetPinGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourceNodeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourcePinId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetNodeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetPinId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourceNodeName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourceNodeClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourcePinName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SourceDirection;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetNodeName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetNodeClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetPinName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TargetDirection;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsExecution = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bDeprecated = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bDisplayAsMutableRef = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bAllowFriendlyName = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bSavePinIfOrphaned = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bUseBackwardsCompatForEmptyAutogeneratedValue = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bSourceHidden = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bTargetHidden = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bSourceIsKnot = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bTargetIsKnot = false;
};

/** Full native pin identity and metadata, including hidden and split pins. */
USTRUCT(BlueprintType)
struct FN2CGraphPin
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NodeGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString PinGuid;
    /** Native RigVM pin path; GUIDs are deterministically derived from it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NativePath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NodeId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString PinId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NodeName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NodeClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString PinName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString FriendlyName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Direction;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Category;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SubCategory;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SubCategoryObject;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SubCategoryMemberParent;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SubCategoryMemberName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ContainerType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ValueTerminalCategory;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ValueTerminalSubCategory;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ValueTerminalSubCategoryObject;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString PinToolTip;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString AutogeneratedDefaultValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultObjectPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultTextValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString PersistentGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ParentPinGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ReferencePassThroughNodeGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ReferencePassThroughPinGuid;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> SubPinGuids;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsExecution = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bDeprecated = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bDisplayAsMutableRef = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bAllowFriendlyName = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bSavePinIfOrphaned = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bUseBackwardsCompatForEmptyAutogeneratedValue = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bConnected = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bHidden = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bNotConnectable = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bOrphaned = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsReference = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsConst = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsWeakPointer = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsUObjectWrapper = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bSerializeAsSinglePrecisionFloat = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bAdvancedView = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bDefaultValueIsReadOnly = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bDefaultValueIsIgnored = false;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TypePath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;

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
    FString PinToolTip;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString AutogeneratedDefaultValue;

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
        N2C_LOG_FUNCTION_ENTRY();
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TypePath;

    /** Struct comment */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    /** List of struct members */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CStructMember> Members;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;

    FN2CStruct()
        : Name(TEXT(""))
        , Comment(TEXT(""))
    {
        N2C_LOG_FUNCTION_ENTRY();
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TypePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DisplayName;

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
    FString PinToolTip;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString AutogeneratedDefaultValue;

    /** Optional comment/tooltip */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    /** Native RigVM variable identity and flags not covered by the normalized fields. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;

    FN2CVariable()
        : Name(TEXT(""))
        , Type(EN2CStructMemberType::Int)
        , TypeName(TEXT(""))
        , KeyType(EN2CStructMemberType::Int)
        , KeyTypeName(TEXT(""))
        , DefaultValue(TEXT(""))
        , Comment(TEXT(""))
    {
        N2C_LOG_FUNCTION_ENTRY();
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
        N2C_LOG_FUNCTION_ENTRY();
    }
};

/** A reflected native object or asset subobject that has no normalized Blueprint model. */
USTRUCT(BlueprintType)
struct FN2CNativeObject
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Kind;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ObjectPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NativeClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ParentPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 Order = INDEX_NONE;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int64 Value = 0;
	
    /** Enum value comment (if any) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;

    FN2CEnumValue()
        : Name(TEXT(""))
        , Comment(TEXT(""))
    {
        N2C_LOG_FUNCTION_ENTRY();
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString TypePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnderlyingType;

    /** Enum comment */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    /** List of enum values */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CEnumValue> Values;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;

    FN2CEnum()
        : Name(TEXT(""))
        , Comment(TEXT(""))
    {
        N2C_LOG_FUNCTION_ENTRY();
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
    Enum           UMETA(DisplayName = "Enum"),
    /** A RigVM model graph, including Control Rig graphs. */
    RigVM          UMETA(DisplayName = "RigVM")
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
    /** Stable UEdGraph GUID or deterministic path-derived RigVM graph identity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphGuid;
    /** Native UObject path or RigVM model object path, distinguishing same-named graphs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GraphPath;
    /** Native graph UObject class (for example URigVMGraph). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString NativeClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> NativeProperties;
    /** Number of UEdGraphNode or RigVM node entries in the source graph. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 NativeNodeCount = 0;
    /** Native nodes not represented in the normalized NodeToCode node list. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 SkippedNodeCount = 0;
    /** Distinct native classes represented by SkippedNodeCount. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> SkippedNodeClasses;
    /** True only when all native nodes are absent or non-executable comments. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bNoExecutableNodes = false;

    /** Array of all nodes in this graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CNodeDefinition> Nodes;

    /** Lossy normalized summaries for legacy consumers; Connections is authoritative for physical topology. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CFlows Flows;

    /** Every native pin in the graph, including hidden, split, orphaned and knot pins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CGraphPin> Pins;

    /** Authoritative lossless native pin edges; flows remains for legacy consumers. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CDirectLink> Connections;

    /** Explicit diagnostics for malformed or unrepresentable native pin links. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> ConnectionErrors;

    /** Links or pins which could not be represented or had unexpected directions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int32 UnresolvedConnectionCount = 0;

    /** Local variables declared for this graph (e.g. function-local variables) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CVariable> LocalVariables;

    FN2CGraph()
        : GraphType(EN2CGraphType::EventGraph)
    {
        N2C_LOG_FUNCTION_ENTRY();
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

    /** Native class ancestry and interfaces required to reproduce the generated class contract. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ParentClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString GeneratedClassPath;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> ImplementedInterfacePaths;

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

    /** Reflected snapshots for Blueprint-specific data such as widgets and rig hierarchies. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CNativeObject> NativeObjects;

    FN2CBlueprint()
    {
        N2C_LOG_FUNCTION_ENTRY();
        // Version is automatically initialized to "1.0.0" by FN2CVersion constructor
    }

    /** Validates the Blueprint structure and its enums */
    bool IsValid() const;
};
