// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CCapture.h"
#include "Models/N2CBlueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "Utils/Validators/N2CBlueprintValidator.h"
#include "Utils/Processors/N2CNodeProcessor.h"
#include "Utils/Processors/N2CNodeProcessorFactory.h"

/**
 * @class FN2CCaptureContext
 * @brief The state of one capture (private to the capture module; use N2C::CaptureNodes, CaptureGraph, DescribeNode)
 *
 * Turns collected Blueprint nodes into an FN2CBlueprint. One context serves one call and is then thrown away, so the
 * Blueprint, ID maps, processed type sets and nested-graph queue are never shared between captures.
 */
class FN2CCaptureContext
{
public:
    explicit FN2CCaptureContext(const FN2CCaptureOptions& InOptions);

    FN2CCaptureContext(const FN2CCaptureContext&) = delete;
    FN2CCaptureContext& operator=(const FN2CCaptureContext&) = delete;

    /**
     * Captures CollectedNodes as the main graph, then the nested graphs they use within the depth limit
     * @return True if a graph was captured (false when CollectedNodes is empty)
     */
    bool CaptureNodes(TConstArrayView<UK2Node*> CollectedNodes);

    /** Describes one node into OutNodeDef; its flows go to a scratch graph that is discarded */
    bool DescribeNode(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

    /** Moves the result out; the context is spent afterwards */
    FN2CCapture TakeCapture();

#if WITH_DEV_AUTOMATION_TESTS
    /**
     * Test hook: runs Hook once, right after the next main-graph node any context captures, then clears it. Lets a spec
     * start a second capture in the middle of the first (NodeToCode.Capture.Golden "Isolation").
     */
    static void SetNodeCapturedHookForTests(TFunction<void()> Hook);
#endif

private:
    /** What this capture includes, with Depth resolved from the settings when it was INDEX_NONE */
    FN2CCaptureOptions Options;

    /** The Blueprint structure being built */
    FN2CBlueprint N2CBlueprint;

    /** Index in N2CBlueprint.Graphs of the graph being written, INDEX_NONE between graphs */
    int32 CurrentGraphIndex = INDEX_NONE;

    /** The graph being written (CurrentGraphIndex must be set) */
    FN2CGraph& CurrentGraph() { return N2CBlueprint.Graphs[CurrentGraphIndex]; }

    /**
     * Node and pin IDs, keyed by the objects themselves: a pin's PinId is only unique on its node (a paste keeps it)
     * and a node GUID says nothing about which graph a node was captured in
     */
    TMap<const UEdGraphNode*, FString> NodeIDs;
    TMap<const UEdGraphPin*, FString> PinIDs;

    /** The numbers behind the next N<n> and P<n> (separate counters, never a map's size) */
    int32 NextNodeNumber = 1;
    int32 NextPinNumber = 1;

    /** What the capture result reports for each ID (FN2CCapture::NodeGuids, Pins, GraphGuids) */
    TMap<FString, FGuid> NodeGuids;
    TMap<FString, FN2CCapturedPin> CapturedPins;
    TArray<FGuid> GraphGuids;

    /** Graphs captured or being captured (the captured graph from the start): each graph is captured at most once */
    TSet<const UEdGraph*> VisitedGraphs;

    /** The Blueprint the captured graph belongs to; a nested graph of any other Blueprint names its owner */
    TWeakObjectPtr<UBlueprint> CapturedBlueprint;

    /** Tracking sets to prevent duplicate processing */
    TSet<FString> ProcessedStructPaths;
    TSet<FString> ProcessedEnumPaths;

    /** Struct to track graph processing information */
    struct FGraphProcessInfo
    {
        /** Weak: a queued graph may be deleted (or its Blueprint unloaded) before it is processed */
        TWeakObjectPtr<UEdGraph> Graph;
        /** The graph's own depth: 1 for a graph the captured graph uses directly, 2 for one that graph uses, ... */
        int32 Depth;

        FGraphProcessInfo(UEdGraph* InGraph, int32 InDepth)
            : Graph(InGraph)
            , Depth(InDepth)
        {}
    };

    /** Queue of graphs to process with their depths (processed shallowest first) */
    TArray<FGraphProcessInfo> AdditionalGraphsToProcess;

    /** Depth of the graph being processed (0 for the captured graph); a graph it uses is at CurrentDepth + 1 */
    int32 CurrentDepth = 0;

    /** Starts writing a new graph at the end of N2CBlueprint.Graphs (Source, when given, is marked visited) */
    void BeginGraph(const FString& Name, EN2CGraphType GraphType, const UEdGraph* Source = nullptr);

    /** Gives Node and its visible pins their IDs (the first pass over a graph; knots get none). False for a knot or null. */
    bool AssignIDs(UK2Node* Node);

    /** Fallback method for processing node properties when no processor is available */
    void FallbackProcessNodeProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

    /** Process a single graph */
    bool ProcessGraph(UEdGraph* Graph, EN2CGraphType GraphType);

    /** Validate all flow references after processing */
    bool ValidateFlowReferences(FN2CGraph& Graph);

    /** Determine graph type from UEdGraph */
    EN2CGraphType DetermineGraphType(UEdGraph* Graph) const;

    /** Add a graph to be processed */
    void AddGraphToProcess(UEdGraph* Graph);

    /** Convert a UK2Node to FN2CNodeDefinition (its IDs must have been assigned) */
    bool ProcessNode(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

    /** Determine node type */
    void DetermineNodeType(UK2Node* Node, EN2CNodeType& OutType);

    /** Process K2Node-specific data */
    void DetermineNodeSpecificProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);
    
    /** Convert UE pin type to N2C pin type */
    EN2CPinType DeterminePinType(const UEdGraphPin* Pin) const;

    /** Every non-knot pin an output pin (exec or data) reaches, through knots and every branch of their fan-out, in link order */
    TArray<UEdGraphPin*> TraceTargetsThroughKnots(UEdGraphPin* OutputPin) const;

    /** Process node type and core properties */
    void ProcessNodeTypeAndProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

    /** Process all pins on the node */
    void ProcessNodePins(UK2Node* Node, FN2CNodeDefinition& OutNodeDef, TArray<UEdGraphPin*>& OutExecInputs, TArray<UEdGraphPin*>& OutExecOutputs);

    /** Process execution and data flows for the node */
    void ProcessNodeFlows(UK2Node* Node, const TArray<UEdGraphPin*>& ExecInputs, const TArray<UEdGraphPin*>& ExecOutputs);

    /** Check if a struct is Blueprint-defined */
    bool IsBlueprintStruct(UScriptStruct* Struct) const;

    /** Check if an enum is Blueprint-defined */
    bool IsBlueprintEnum(UEnum* Enum) const;

    /** Process a Blueprint struct into FN2CStruct */
    FN2CStruct ProcessBlueprintStruct(UScriptStruct* Struct);

    /** Process a Blueprint enum into FN2CEnum */
    FN2CEnum ProcessBlueprintEnum(UEnum* Enum);

    /** Process a struct member */
    FN2CStructMember ProcessStructMember(FProperty* Property);

    /** Convert FProperty type to N2C struct member type */
    EN2CStructMemberType ConvertPropertyToStructMemberType(FProperty* Property) const;

    /** Clean up property names by removing numeric suffixes and GUIDs */
    FString CleanPropertyName(const FString& RawName) const;

    /** Process any struct or enum types used in a node */
    void ProcessRelatedTypes(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

    /** Remove SKEL_ prefix and _C suffix from class names */
    FString GetCleanClassName(const FString& InName);

    /** Log detailed debug information about the node */
    void LogNodeDetails(const FN2CNodeDefinition& NodeDef);
};
