// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

class UBlueprint;
class FBlueprintEditor;

/**
 * get-open-blueprint-editors: every open Blueprint editor (Widget and Animation Blueprint editors included, card 0275)
 * with its graphs, the graph it shows, and its saved and compile state
 */
class FN2CMcpGetOpenBlueprintEditorsTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;

	// Always requires game thread for editor access
	virtual bool RequiresGameThread() const override { return true; }

	/** One editor's entry */
	static TSharedPtr<FJsonObject> CollectEditorInfo(const TSharedPtr<FBlueprintEditor>& Editor, bool bFocused);

	/** The Blueprint's graphs as its editor lists them: [{name, graphGuid, type, isEditable}] */
	static TArray<TSharedPtr<FJsonValue>> CollectBlueprintGraphs(const UBlueprint& Blueprint);
};
