// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

struct FAssetData;

/**
 * MCP tool for searching assets under /Game, the project's plugins' content and, on request, /Engine and engine plugins'
 * content (card 0285)
 */
class FN2CMcpSearchContentBrowserTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	/**
	 * The package paths searched: /Game; /Engine with bIncludeEngineContent; with bIncludePluginContent the mount of
	 * every enabled plugin with content loaded from the project, and of engine plugins too with bIncludeEngineContent
	 */
	static TArray<FString> GetSearchRoots(bool bIncludeEngineContent, bool bIncludePluginContent);

private:
	/** Requested (any case, empty for All) as the assetType it names, in the listed spelling; false for an unknown type */
	static bool ResolveAssetType(const FString& Requested, FString& OutAssetType);

	/**
	 * Searches SearchRoots (recursively) for assets of AssetType, its subclasses included, matching SearchQuery.
	 * Per-actor __External* packages are skipped.
	 * @param MaxResults How many to return: the best ones, highest score first, ties by package name
	 * @param OutAssets The assets returned, ranked
	 * @param OutScores The relevance score of each of OutAssets
	 * @param OutTotalFound Every match, also those not returned
	 * @return False if the asset registry refused the filter
	 */
	static bool SearchAssets(const FString& SearchQuery, const FString& AssetType, const TArray<FString>& SearchRoots,
		int32 MaxResults, TArray<FAssetData>& OutAssets, TArray<float>& OutScores, int32& OutTotalFound);

	/**
	 * Scores an asset based on how well it matches the search query
	 * @return 1 for an exact name, 0.8 a name prefix, 0.6 part of the name, 0.4 part of the package path, 0 no match
	 */
	static float ScoreAssetMatch(const FAssetData& AssetData, const FString& SearchQuery);

	/** A search result: name, path, type, class, relevanceScore and metadata */
	static TSharedPtr<FJsonObject> ConvertAssetToJson(const FAssetData& AssetData, float Score);

	/** The assetType whose filter finds the asset (Blueprint for a Widget Blueprint), else its class name as the class path holds it */
	static FString GetAssetDisplayType(const FAssetData& AssetData);
};
