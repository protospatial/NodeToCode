// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

/**
 * @file N2CBlueprint.h
 * @brief Blueprint-related data type definitions for the Node to Code plugin
 */

#pragma once

#include "CoreMinimal.h"
#include "N2CNode.h"
#include "N2CJsonContract.h"
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

    /** Version string: N2CJsonContract::Version ("2.0.0") for a capture; FromJson maps 1.x documents to it */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Value;

    FN2CVersion() : Value(N2CJsonContract::Version) {}
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
    /**
     * A "blueprint_type" this build does not know (a later 2.x document), read by FN2CSerializer::FromJson and kept
     * with its text in FN2CMetadata::UnknownBlueprintType so ToJson writes it back unchanged (card 0445). Never captured.
     */
    Unknown UMETA(DisplayName = "Unknown"),
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

    /** The "blueprint_type" text as read when BlueprintType is Unknown (card 0445); ToJson writes it in place of "Unknown" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnknownBlueprintType;

    /**
     * Fields of "metadata" this build does not know, by name, each value as condensed JSON text, in the order read.
     * ToJson writes them back after the known fields (card 0445).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> UnknownFields;

    FN2CMetadata() : Name(TEXT("")), BlueprintType(EN2CBlueprintType::Normal), BlueprintClass(TEXT("")) {}
};

/**
 * @struct FN2CDataFlowTargets
 * @brief The input pins one output pin feeds ("N2.P1", ...), in link order, each once
 *
 * From PR #7 by Yury Rakhuba (FN2CInputConnectors), as an ordered array so the JSON is stable.
 */
USTRUCT(BlueprintType)
struct FN2CDataFlowTargets
{
    GENERATED_BODY()

    /** Input pin refs ("N2.P1"); written as one string when there is one, else as an array */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> Targets;

    bool operator==(const FN2CDataFlowTargets& Other) const { return Targets == Other.Targets; }
};

/**
 * @struct FN2CFlows 
 * @brief Contains all execution and data flow connections between nodes
 */
USTRUCT(BlueprintType)
struct FN2CFlows
{
    GENERATED_BODY()

    /**
     * Exec links, one per link in capture order: exec output pin ref -> exec input pin ref ("N3.P5->N4.P1"). Documents
     * read from N2C JSON 1.x (and 2.0 builds before card 0136) keep their node-only entries ("N1->N2")
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> Execution;

    /** Data connections: output pin ref ("N1.P4") -> every input pin it feeds ("N2.P3", "N5.P1"); each edge once */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FN2CDataFlowTargets> Data;

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
    /** The construction script (the function graph UEdGraphSchema_K2::FN_UserConstructionScript) */
    Construction    UMETA(DisplayName = "Construction Script"),
    /** An animation graph or an animation state machine graph (GT_Animation, GT_StateMachine) */
    Animation      UMETA(DisplayName = "Animation"),
    /** A struct definition */
    Struct         UMETA(DisplayName = "Struct"),
    /** An enum definition */
    Enum           UMETA(DisplayName = "Enum"),
    /**
     * A "graph_type" this build does not know (a later 2.x document), read by FN2CSerializer::FromJson and kept with
     * its text in FN2CGraph::UnknownGraphType so ToJson writes it back unchanged (card 0445). Never captured.
     */
    Unknown        UMETA(DisplayName = "Unknown")
};

/**
 * @struct FN2CCommentBox
 * @brief A comment box (UEdGraphNode_Comment) of a graph: its text and the captured nodes inside it (card 0235)
 */
USTRUCT(BlueprintType)
struct FN2CCommentBox
{
    GENERATED_BODY()

    /** The comment's text, as the user wrote it. Written as "text". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Text;

    /**
     * IDs of the graph's captured nodes inside the box (a node whose top-left corner lies within it), in capture order.
     * Written as "nodes", only when there are any.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> NodeIDs;

    bool operator==(const FN2CCommentBox& Other) const { return Text == Other.Text && NodeIDs == Other.NodeIDs; }
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

    /**
     * The Blueprint this graph belongs to, set only for a nested graph from a Blueprint other than the captured one
     * (two Blueprints can each have a function of the same name). Written as the optional "owner_blueprint".
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString OwnerBlueprint;

    /** Array of all nodes in this graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CNodeDefinition> Nodes;

    /** Execution and data flow connections for this graph */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CFlows Flows;

    /** The graph's comment boxes, in graph order (card 0235). Written as the optional "comments", after "flows". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CCommentBox> Comments;

    /** The "graph_type" text as read when GraphType is Unknown (card 0445); ToJson writes it in place of "Unknown" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnknownGraphType;

    /**
     * Fields of the graph's JSON object this build does not know, by name, each value as condensed JSON text, in the
     * order read. ToJson writes them back after the known fields (card 0445).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> UnknownFields;

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

    /** Version information (N2CJsonContract::Version) */
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

    /**
     * Top-level fields this build does not know (a later 2.x document), by name, each value as condensed JSON text, in
     * the order read. ToJson writes them back after the known fields (card 0445).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> UnknownFields;

    FN2CBlueprint()
    {
        // Version is initialized to N2CJsonContract::Version by the FN2CVersion constructor
    }

    /** Validates the Blueprint structure and its enums */
    bool IsValid() const;
};
