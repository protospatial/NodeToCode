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
 * @enum EN2CAssetKind
 * @brief What kind of asset an N2C JSON document describes: "metadata"."asset_kind" (card 0447, decision 0009)
 */
UENUM(BlueprintType)
enum class EN2CAssetKind : uint8
{
    /** A Blueprint (the K2 graphs of a UBlueprint); also what a document without "asset_kind" describes */
    Blueprint UMETA(DisplayName = "Blueprint"),
    /** A material graph: a UMaterial or a UMaterialFunction (decision 0009; graph_type tells them apart) */
    Material UMETA(DisplayName = "Material"),
    /**
     * An "asset_kind" this build does not know (a later 2.x document), read by FN2CSerializer::FromJson and kept with
     * its text in FN2CMetadata::UnknownAssetKind so ToJson writes it back unchanged. Never captured.
     */
    Unknown UMETA(DisplayName = "Unknown"),
};

/**
 * @struct FN2CMaterialMetadata
 * @brief A material's settings (card 0449, N2C JSON "metadata"."material"): written for a captured UMaterial only
 */
USTRUCT(BlueprintType)
struct FN2CMaterialMetadata
{
    GENERATED_BODY()

    /** True when the document has "material" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bSet = false;

    /** EMaterialDomain without its prefix: "Surface", "DeferredDecal", "LightFunction", "Volume", "PostProcess", "UI" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Domain;

    /** EBlendMode without its prefix: "Opaque", "Masked", "Translucent", "Additive", ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString BlendMode;

    /** EMaterialShadingModel values without their prefix, in enum order: "DefaultLit", "Unlit", ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FString> ShadingModels;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bTwoSided = false;

    /** The project renders materials with Substrate (r.Substrate; card 0451). Written as "substrate", only when true */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bSubstrate = false;
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

    /** The Blueprint class this graph belongs to */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString BlueprintClass;

    /**
     * Optional "parent_class" (card 0518, finding F4): the class the Blueprint derives from, named as member_parent
     * names classes (N2CStableNames::ClassName: a native class without its Unreal prefix, e.g. "Actor" or
     * "UserWidget"; a Blueprint class by its Blueprint's name). Empty when not known (1.x and earlier 2.0.0 documents,
     * or a Blueprint whose parent class is missing); ToJson then leaves it out.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ParentClass;

    /**
     * Optional "asset_class" (card 0518, finding F4): the class of the Blueprint asset itself, which tells a Widget,
     * Animation or Editor Utility Blueprint from a plain one ("Blueprint", "WidgetBlueprint", "AnimBlueprint",
     * "EditorUtilityBlueprint", "EditorUtilityWidgetBlueprint"). Empty when not known; ToJson then leaves it out.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString AssetClass;

    /**
     * "asset_kind" (card 0447, decision 0009): Blueprint, written only when it is not (a document without it is a
     * Blueprint, so every Blueprint document is unchanged). blueprint_type and blueprint_class describe Blueprints only:
     * a document of another kind may leave them out, and ToJson then writes blueprint_class only when set and
     * blueprint_type only when it is a kind this build does not know (kept from the document read).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CAssetKind AssetKind = EN2CAssetKind::Blueprint;

    /** The "asset_kind" text as read when AssetKind is Unknown; ToJson writes it in place of "Unknown" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnknownAssetKind;

    /** A material's settings ("material", card 0449); bSet only for a captured UMaterial */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FN2CMaterialMetadata Material;

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

    /**
     * The value's number (UEnum::GetValueByIndex, card 0236), N2C JSON "value"; written when bHasValue (a capture always
     * sets it; a document written before card 0236 has none and keeps none)
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    int64 Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bHasValue = false;

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
 * @struct FN2CVariable
 * @brief A Blueprint member variable or a function's local variable (card 0242, from PR #8 by flashpoint493)
 *
 * The type uses the pin spellings (N2CStableNames::PinType and PinSubType, the same helper pins use): a float variable
 * is "Real" with sub_type "float", a map is typed by its key with its value in ValueType and ValueSubType.
 */
USTRUCT(BlueprintType)
struct FN2CVariable
{
    GENERATED_BODY()

    /** The variable's name as declared (FBPVariableDescription::VarName) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Name;

    /** The variable's type, as a pin of its type is typed; a map's key */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CPinType Type = EN2CPinType::Wildcard;

    /** The "type" text as read when Type is Unknown (a later 2.x document); ToJson writes it in place of "Unknown" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnknownType;

    /** The struct, enum, class or object type, or the real's "float" or "double"; a map's key */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString SubType;

    /** Container flags */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsArray = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsSet = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bIsMap = false;

    /** A map's value type; Unknown with an empty UnknownValueType means none */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    EN2CPinType ValueType = EN2CPinType::Unknown;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString UnknownValueType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString ValueSubType;

    /** The default value as text (the class default object's value for a member variable), if any */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString DefaultValue;

    /** The My Blueprint category, left empty for the default category */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Category;

    /** The variable's tooltip (its description), if any */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString Comment;

    /** Instance Editable: editable on placed instances */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bInstanceEditable = false;

    /** Blueprint Read Only */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bReadOnly = false;

    /** Expose on Spawn */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bExposeOnSpawn = false;

    /** Private: other Blueprints cannot read or write it */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bPrivate = false;

    /** Replicated (or RepNotify) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    bool bReplicated = false;

    /** The RepNotify function's name, when the variable is RepNotify */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    FString RepNotify;

    /** Fields of the variable's JSON object this build does not know, as condensed JSON text, in the order read */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TMap<FString, FString> UnknownFields;

    bool HasValueType() const { return ValueType != EN2CPinType::Unknown || !UnknownValueType.IsEmpty(); }
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
    /** A material's expression graph (card 0449; a document whose asset_kind is Material) */
    Material       UMETA(DisplayName = "Material"),
    /** A material function's expression graph (card 0449) */
    MaterialFunction UMETA(DisplayName = "Material Function"),
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

    /**
     * A function graph's local variables (card 0242), when the capture asked for variables. Written as the optional
     * "local_variables", after "comments".
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CVariable> LocalVariables;

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
     * The Blueprint's member variables (card 0242), when the capture asked for variables. Written as the optional
     * top-level "variables", after "enums".
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code")
    TArray<FN2CVariable> Variables;

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

    /** Validates the Blueprint structure and its enums: true when it has no error (warnings do not fail it, card 0240) */
    bool IsValid() const;
};

/** How bad one validation or capture finding is (card 0240) */
enum class EN2CDiagnosticSeverity : uint8
{
    /** Recoverable: the document is still used (a pin missing its sub_type, a graph left out at the depth limit) */
    Warning,
    /** Structural: the document cannot be used as it is (a flow naming a missing node, a duplicate ID) */
    Error,
};

/**
 * One finding of FN2CBlueprintValidator or of a capture (card 0240, imp-translation-core-8): what agents see in a tool
 * result instead of only a log line. Codes are N2CJsonContract::Diagnostic.
 */
struct FN2CDiagnostic
{
    EN2CDiagnosticSeverity Severity = EN2CDiagnosticSeverity::Warning;
    /** N2CJsonContract::Diagnostic code, for example "DANGLING_FLOW" */
    FString Code;
    /** Where in the document: "graphs[0]", "graphs[0].nodes[2].input_pins[1]", "structs[1]", "graphs[0].flows.data" */
    FString Path;
    /** A sentence for people, naming the graph, node or pin */
    FString Message;

    bool IsError() const { return Severity == EN2CDiagnosticSeverity::Error; }
};
