// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "Utils/Validators/N2CBlueprintValidator.h"
#include "Utils/Processors/N2CNodeProcessor.h"
#include "Utils/Processors/N2CNodeProcessorFactory.h"

/**
 * @class FN2CNodeTranslator
 * @brief Converts collected Blueprint nodes into N2CStruct format
 *
 * Takes raw Blueprint nodes collected from the editor and translates them
 * into the structured FN2CBlueprint format for further processing.
 */
class FN2CNodeTranslator
{
public:
    /** Get the singleton instance */
    static FN2CNodeTranslator& Get();

    /**
     * @brief Generate N2CStruct from collected nodes
     * @param CollectedNodes Array of nodes from the Blueprint Editor
     * @return True if translation succeeded
     */
    bool GenerateN2CStruct(const TArray<UK2Node*>& CollectedNodes);

    /**
     * @brief Get the generated Blueprint structure
     * @return The translated Blueprint structure
     */
    const FN2CBlueprint& GetN2CBlueprint() const { return N2CBlueprint; }

private:
    /** Constructor */
    FN2CNodeTranslator() = default;

    /** The Blueprint structure being built */
    FN2CBlueprint N2CBlueprint;

    /** Current graph being processed */
    FN2CGraph* CurrentGraph;

    /** Maps node GUIDs to simplified IDs */
    TMap<FGuid, FString> NodeIDMap;

    /** Maps pin GUIDs to simplified IDs */
    TMap<FGuid, FString> PinIDMap;

    /** Tracking sets to prevent duplicate processing */
    TSet<FString> ProcessedStructPaths;
    TSet<FString> ProcessedEnumPaths;

    /** Struct to track graph processing information */
    struct FGraphProcessInfo
    {
        UEdGraph* Graph;
        int32 ParentDepth;

        FGraphProcessInfo(UEdGraph* InGraph, int32 InParentDepth)
            : Graph(InGraph)
            , ParentDepth(InParentDepth)
        {}
    };

    /** Queue of graphs to process with their parent depths */
    TArray<FGraphProcessInfo> AdditionalGraphsToProcess;

    /** Current processing depth */
    int32 CurrentDepth = 0;
    
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

    /** Generate a simplified node ID */
    FString GenerateNodeID();

    /** Generate a simplified pin ID scoped to the containing node */
    FString GeneratePinID(int32 PinCount);

    /** Convert a UK2Node to FN2CNodeDefinition */
    bool ProcessNode(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

    /** Determine node type */
    void DetermineNodeType(UK2Node* Node, EN2CNodeType& OutType);

    /** Process K2Node-specific data */
    void DetermineNodeSpecificProperties(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);
    
    /** Convert UE pin type to N2C pin type */
    EN2CPinType DeterminePinType(const UEdGraphPin* Pin) const;

    /** Follow pin connections through knot nodes to find the first non-knot target */
    UEdGraphPin* TraceConnectionThroughKnots(UEdGraphPin* StartPin) const;

    /** Initialize basic node processing and validation */
    bool InitializeNodeProcessing(UK2Node* Node, FN2CNodeDefinition& OutNodeDef);

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
