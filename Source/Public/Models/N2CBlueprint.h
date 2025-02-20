// Copyright ProtoSpatial. All Rights Reserved.

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
    Animation      UMETA(DisplayName = "Animation")
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

    FN2CGraph()
        : GraphType(EN2CGraphType::EventGraph)
    {
    }

    /** Validates the graph structure */
    bool IsValid() const
    {
        if (Name.IsEmpty())
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: Empty graph name")));
            return false;
        }

        // Check nodes array
        if (Nodes.Num() == 0)
        {
            FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: No nodes in graph %s"), *Name));
            return false;
        }

        // Build set of node IDs for validation
        TSet<FString> NodeIds;
        for (const FN2CNodeDefinition& Node : Nodes)
        {
            if (!Node.IsValid())
            {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: Invalid node %s in graph %s"), *Node.ID, *Name));
                return false;
            }

            // Check for duplicate node IDs
            if (NodeIds.Contains(Node.ID))
            {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: Duplicate node ID %s in graph %s"), *Node.ID, *Name));
                return false;
            }
            NodeIds.Add(Node.ID);
        }

        // Log all node IDs for debugging
        FString NodeIdList = TEXT("Valid Node IDs in graph ") + Name + TEXT(": ");
        for (const FString& Id : NodeIds)
        {
            NodeIdList += Id + TEXT(", ");
        }
        FN2CLogger::Get().Log(NodeIdList, EN2CLogSeverity::Debug);

        // Validate flows
        // Check execution flows
        for (const FString& ExecFlow : Flows.Execution)
        {
            TArray<FString> FlowNodes;
            ExecFlow.ParseIntoArray(FlowNodes, TEXT("->"));
            
            // Each flow must have at least 2 nodes
            if (FlowNodes.Num() < 2)
            {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: Invalid execution flow %s (needs at least 2 nodes) in graph %s"), *ExecFlow, *Name));
                return false;
            }

            // Verify all referenced nodes exist
            for (const FString& NodeId : FlowNodes)
            {
                if (!NodeIds.Contains(NodeId))
                {
                    FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: Execution flow %s references non-existent node %s in graph %s"), *ExecFlow, *NodeId, *Name));
                    return false;
                }
            }
        }

        // Check data flows
        for (const auto& DataFlow : Flows.Data)
        {
            // Validate source pin format (N#.P#)
            TArray<FString> SourceParts;
            DataFlow.Key.ParseIntoArray(SourceParts, TEXT("."));
            if (SourceParts.Num() != 2 || !NodeIds.Contains(SourceParts[0]))
            {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: Invalid source pin format %s in graph %s"), *DataFlow.Key, *Name));
                return false;
            }

            // Validate target pin format (N#.P#)
            TArray<FString> TargetParts;
            DataFlow.Value.ParseIntoArray(TargetParts, TEXT("."));
            if (TargetParts.Num() != 2 || !NodeIds.Contains(TargetParts[0]))
            {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Graph validation failed: Invalid target pin format %s in graph %s"), *DataFlow.Value, *Name));
                return false;
            }
        }

        // Log successful validation
        FN2CLogger::Get().Log(FString::Printf(TEXT("Graph %s validation successful: %d nodes, %d execution flows, %d data flows"), 
            *Name, Nodes.Num(), Flows.Execution.Num(), Flows.Data.Num()), EN2CLogSeverity::Debug);

        return true;
    }
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

    FN2CBlueprint()
    {
        // Version is automatically initialized to "1.0.0" by FN2CVersion constructor
    }

    /** Validates the Blueprint structure and its enums */
    bool IsValid() const
    {
        // Check version
        if (Version.Value.IsEmpty() || Version.Value != TEXT("1.0.0"))
        {
            FN2CLogger::Get().LogError(TEXT("Invalid or missing version"));
            return false;
        }

        // Check metadata
        if (Metadata.Name.IsEmpty())
        {
            FN2CLogger::Get().LogError(TEXT("Missing Blueprint name"));
            return false;
        }

        if (Metadata.BlueprintClass.IsEmpty())
        {
            FN2CLogger::Get().LogError(TEXT("Missing Blueprint class"));
            return false;
        }

        // Check graphs array
        if (Graphs.Num() == 0)
        {
            FN2CLogger::Get().LogError(TEXT("No graphs found"));
            return false;
        }

        // Check that at least one graph has nodes
        bool bHasNodes = false;
        for (const FN2CGraph& Graph : Graphs)
        {
            if (Graph.Nodes.Num() > 0)
            {
                bHasNodes = true;
                break;
            }
        }

        if (!bHasNodes)
        {
            FN2CLogger::Get().LogError(TEXT("No nodes found in any graph"));
            return false;
        }

        // Validate each graph
        for (const FN2CGraph& Graph : Graphs)
        {
            if (!Graph.IsValid())
            {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("Invalid graph: %s"), *Graph.Name));
                return false;
            }
        }

        return true;
    }
};
