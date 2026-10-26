// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

/**
 * MCP tool that lists a Blueprint's function graphs, interface function graphs, custom events and override events.
 * Can list functions from the focused Blueprint or a specific Blueprint by path.
 */
class FN2CMcpListBlueprintFunctionsTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	/**
	 * Resolves the target Blueprint from a path or the focused editor.
	 * @param BlueprintPath Optional path to a specific Blueprint
	 * @return The resolved Blueprint or nullptr if not found
	 */
	// class UBlueprint* ResolveTargetBlueprint(const FString& BlueprintPath) const; // Removed

	/**
	 * Collects information about all functions in a Blueprint.
	 * @param Blueprint The Blueprint to analyze
	 * @return JSON object containing function information
	 */
	TSharedPtr<FJsonObject> CollectFunctionInformation(const class UBlueprint* Blueprint) const;

	/**
	 * Collects information about a single function graph.
	 * @param FunctionGraph The function graph to analyze
	 * @return JSON object containing function details
	 */
	TSharedPtr<FJsonObject> CollectFunctionDetails(const class UEdGraph* FunctionGraph) const;

	/**
	 * Collects information about one event node: a custom event, an override or interface event, or a bound event.
	 * @param EventNode The event node
	 * @return JSON object with the event's details
	 */
	TSharedPtr<FJsonObject> CollectEventDetails(const class UK2Node_Event* EventNode) const;

	/**
	 * Extracts parameter information from function entry and result nodes.
	 * @param EntryNode The function entry node
	 * @param ResultNode The function result node (may be nullptr)
	 * @param OutInputParams Array to fill with input parameter information
	 * @param OutOutputParams Array to fill with output parameter information
	 */
	void ExtractFunctionParameters(
		const class UK2Node_FunctionEntry* EntryNode,
		const class UK2Node_FunctionResult* ResultNode,
		TArray<TSharedPtr<FJsonValue>>& OutInputParams,
		TArray<TSharedPtr<FJsonValue>>& OutOutputParams) const;

	/**
	 * Converts an Unreal pin type to a JSON representation.
	 * @param PinType The pin type to convert
	 * @return JSON object representing the pin type
	 */
	TSharedPtr<FJsonObject> ConvertPinTypeToJson(const struct FEdGraphPinType& PinType) const;

	/**
	 * Gets function flags and metadata.
	 * @param EntryNode The function entry node
	 * @return JSON object containing function flags and metadata
	 */
	TSharedPtr<FJsonObject> GetFunctionFlags(const class UK2Node_FunctionEntry* EntryNode) const;

	/**
	 * Gets the function GUID if available.
	 * @param FunctionGraph The function graph
	 * @return The function's GUID or an empty GUID
	 */
	FGuid GetFunctionGuid(const class UEdGraph* FunctionGraph) const;
};
