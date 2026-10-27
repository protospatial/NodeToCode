// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "MCP/Tools/N2CMcpToolBase.h"

class UBlueprint;

/**
 * Opens a specified blueprint asset in the Blueprint Editor
 */
class FN2CMcpOpenBlueprintAssetTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * The Blueprint's kind (card 0503): Widget (a Widget Blueprint), Animation (an Anim Blueprint), Component, Actor or
	 * Object for a normal Blueprint, from its asset class and parent class; Const, MacroLibrary, Interface, LevelScript
	 * or FunctionLibrary from its BlueprintType. Unknown for null.
	 */
	static FString GetBlueprintTypeString(const UBlueprint* Blueprint);

	/**
	 * The result of a successful open: success, asset_path, editor_id, focused_graph, asset_type, blueprint_name,
	 * parent_class and graph_counts. event_graphs counts the event graph pages (UbergraphPages), not the transient
	 * EventGraphs list, which is empty outside a compile (card 0503).
	 */
	static TSharedPtr<FJsonObject> BuildSuccessResult(const UBlueprint* Blueprint, const FString& AssetPath, const FString& FocusedGraph);

private:
	/**
	 * Validates that the given asset path is a valid blueprint path
	 * @param AssetPath The path to validate
	 * @param OutErrorMsg Error message if validation fails
	 * @return True if valid blueprint path
	 */
	bool ValidateBlueprintAssetPath(const FString& AssetPath, FString& OutErrorMsg) const;
	
	/**
	 * Focuses on a specific graph within the blueprint editor
	 * @param BlueprintEditor The blueprint editor instance
	 * @param Blueprint The blueprint being edited
	 * @param GraphName The name of the graph to focus on
	 * @return True if the graph was found and focused
	 */
	bool FocusOnGraph(TSharedPtr<class IBlueprintEditor> BlueprintEditor, UBlueprint* Blueprint, const FString& GraphName) const;
};
