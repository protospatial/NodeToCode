// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSearchContentBrowserTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpContentBrowserUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimSequence.h"
#include "Particles/ParticleSystem.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Utils/N2CLogger.h"

REGISTER_MCP_TOOL(FN2CMcpSearchContentBrowserTool)

namespace N2CSearchContentBrowser
{
	/** One assetType value and the class whose assets, subclasses included, it matches */
	struct FAssetTypeClass
	{
		const TCHAR* Name;
		UClass* (*Class)();
	};

	/**
	 * assetType's values (card 0285). Each is matched with bRecursiveClasses, so Sound finds USoundWave and USoundCue
	 * (USoundBase is abstract), Blueprint finds Widget and Animation Blueprints, DataAsset finds every data asset class.
	 */
	const FAssetTypeClass AssetTypes[] = {
		{ TEXT("Blueprint"), [] { return UBlueprint::StaticClass(); } },
		{ TEXT("Material"), [] { return UMaterialInterface::StaticClass(); } },
		{ TEXT("Texture"), [] { return UTexture::StaticClass(); } },
		{ TEXT("StaticMesh"), [] { return UStaticMesh::StaticClass(); } },
		{ TEXT("SkeletalMesh"), [] { return USkeletalMesh::StaticClass(); } },
		{ TEXT("Sound"), [] { return USoundBase::StaticClass(); } },
		{ TEXT("Animation"), [] { return UAnimSequence::StaticClass(); } },
		{ TEXT("ParticleSystem"), [] { return UParticleSystem::StaticClass(); } },
		{ TEXT("DataAsset"), [] { return UDataAsset::StaticClass(); } },
		{ TEXT("DataTable"), [] { return UDataTable::StaticClass(); } },
	};

	/** "All, Blueprint, Material, ...": every value assetType accepts */
	FString AssetTypeList()
	{
		FString List = TEXT("All");
		for (const FAssetTypeClass& Type : AssetTypes)
		{
			List += FString(TEXT(", ")) + Type.Name;
		}
		return List;
	}

	/** The class of the asset, when it is loaded (native classes always are) */
	UClass* ClassOf(const FAssetData& AssetData)
	{
		return FindObject<UClass>(AssetData.AssetClassPath);
	}

	/** A search result: its relevance and the asset */
	struct FScoredAsset
	{
		float Score = 0.0f;
		FAssetData Asset;
	};

	/** A ranks before B: higher score, then package name and asset name, so equal scores come back in a stable order */
	bool RanksBefore(float ScoreA, const FAssetData& A, float ScoreB, const FAssetData& B)
	{
		if (ScoreA != ScoreB)
		{
			return ScoreA > ScoreB;
		}
		if (A.PackageName != B.PackageName)
		{
			return A.PackageName.LexicalLess(B.PackageName);
		}
		return A.AssetName.LexicalLess(B.AssetName);
	}

	bool RanksBefore(const FScoredAsset& A, const FScoredAsset& B)
	{
		return RanksBefore(A.Score, A.Asset, B.Score, B.Asset);
	}
}

FMcpToolDefinition FN2CMcpSearchContentBrowserTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("search-content-browser"),
		TEXT("Search for assets by name or type under /Game, the content of the project's plugins (includePluginContent), ")
		TEXT("and /Engine and engine plugins' content when includeEngineContent is true. Returns the best matches first."),
		N2CMcpToolsets::ContentBrowser
	);

	// Build input schema
	TSharedPtr<FJsonObject> Schema = MakeShareable(new FJsonObject);
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);

	// query property (optional)
	TSharedPtr<FJsonObject> QueryProp = MakeShareable(new FJsonObject);
	QueryProp->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetStringField(TEXT("description"), TEXT("Search query matched against asset names and package paths (case-insensitive, partial match). Leave empty to list all assets of the specified type."));
	Properties->SetObjectField(TEXT("query"), QueryProp);

	// assetType property (optional)
	TSharedPtr<FJsonObject> AssetTypeProp = MakeShareable(new FJsonObject);
	AssetTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetTypeProp->SetStringField(TEXT("description"), TEXT("Filter by asset type (case-insensitive): All, Blueprint, Material, Texture, StaticMesh, SkeletalMesh, Sound, Animation, ParticleSystem, DataAsset, DataTable. Each includes its subclasses (Blueprint includes Widget and Animation Blueprints, Sound includes Sound Waves and Sound Cues); any other value is an error."));
	AssetTypeProp->SetStringField(TEXT("default"), TEXT("All"));
	Properties->SetObjectField(TEXT("assetType"), AssetTypeProp);

	// includeEngineContent property
	TSharedPtr<FJsonObject> IncludeEngineProp = MakeShareable(new FJsonObject);
	IncludeEngineProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeEngineProp->SetBoolField(TEXT("default"), false);
	IncludeEngineProp->SetStringField(TEXT("description"), TEXT("Also search /Engine, and with includePluginContent the content of engine plugins"));
	Properties->SetObjectField(TEXT("includeEngineContent"), IncludeEngineProp);

	// includePluginContent property
	TSharedPtr<FJsonObject> IncludePluginProp = MakeShareable(new FJsonObject);
	IncludePluginProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludePluginProp->SetBoolField(TEXT("default"), true);
	IncludePluginProp->SetStringField(TEXT("description"), TEXT("Also search the content of enabled plugins, mounted at /<PluginName>: the project's plugins, and engine plugins too when includeEngineContent is true"));
	Properties->SetObjectField(TEXT("includePluginContent"), IncludePluginProp);

	// maxResults property
	TSharedPtr<FJsonObject> MaxResultsProp = MakeShareable(new FJsonObject);
	MaxResultsProp->SetStringField(TEXT("type"), TEXT("number"));
	MaxResultsProp->SetNumberField(TEXT("default"), 50);
	MaxResultsProp->SetStringField(TEXT("description"), TEXT("Maximum number of results to return; values outside 1-200 are clamped. searchInfo.totalFound counts every match and searchInfo.truncated says whether some were left out"));
	Properties->SetObjectField(TEXT("maxResults"), MaxResultsProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	// No required fields - all are optional
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	Schema->SetArrayField(TEXT("required"), RequiredArray);

	Definition.InputSchema = Schema;

	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	return Definition;
}

FMcpToolCallResult FN2CMcpSearchContentBrowserTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FString SearchQuery;
		Arguments->TryGetStringField(TEXT("query"), SearchQuery);

		FString RequestedAssetType = TEXT("All");
		Arguments->TryGetStringField(TEXT("assetType"), RequestedAssetType);

		bool bIncludeEngineContent = false;
		Arguments->TryGetBoolField(TEXT("includeEngineContent"), bIncludeEngineContent);

		bool bIncludePluginContent = true;
		Arguments->TryGetBoolField(TEXT("includePluginContent"), bIncludePluginContent);

		int32 MaxResults = 50;
		double MaxResultsDouble;
		if (Arguments->TryGetNumberField(TEXT("maxResults"), MaxResultsDouble))
		{
			MaxResults = FMath::Clamp(static_cast<int32>(MaxResultsDouble), 1, 200);
		}

		// The type in its listed spelling, or an error naming the valid ones (card 0285: an unknown type used to
		// apply no filter and return everything)
		FString AssetType;
		if (!ResolveAssetType(RequestedAssetType, AssetType))
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("Unknown assetType '%s'; use one of %s"),
				*RequestedAssetType, *N2CSearchContentBrowser::AssetTypeList()));
		}

		// Search for assets: the best MaxResults, ranked, and how many matched in all
		const TArray<FString> SearchRoots = GetSearchRoots(bIncludeEngineContent, bIncludePluginContent);
		TArray<FAssetData> FoundAssets;
		TArray<float> Scores;
		int32 TotalFound = 0;
		if (!SearchAssets(SearchQuery, AssetType, SearchRoots, MaxResults, FoundAssets, Scores, TotalFound))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to search assets"));
		}

		// Build response
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);

		// Search metadata
		TSharedPtr<FJsonObject> SearchInfo = MakeShareable(new FJsonObject);
		SearchInfo->SetStringField(TEXT("query"), SearchQuery.IsEmpty() ? TEXT("*") : SearchQuery);
		SearchInfo->SetStringField(TEXT("assetType"), AssetType);
		SearchInfo->SetBoolField(TEXT("includeEngineContent"), bIncludeEngineContent);
		SearchInfo->SetBoolField(TEXT("includePluginContent"), bIncludePluginContent);
		SearchInfo->SetNumberField(TEXT("totalFound"), TotalFound);
		SearchInfo->SetNumberField(TEXT("resultsReturned"), FoundAssets.Num());
		SearchInfo->SetBoolField(TEXT("truncated"), TotalFound > FoundAssets.Num());
		Result->SetObjectField(TEXT("searchInfo"), SearchInfo);

		// Asset results
		TArray<TSharedPtr<FJsonValue>> AssetsArray;
		for (int32 Index = 0; Index < FoundAssets.Num(); ++Index)
		{
			AssetsArray.Add(MakeShareable(new FJsonValueObject(ConvertAssetToJson(FoundAssets[Index], Scores[Index]))));
		}
		Result->SetArrayField(TEXT("assets"), AssetsArray);

		// Add usage tips
		TArray<TSharedPtr<FJsonValue>> TipsArray;
		if (TotalFound > FoundAssets.Num())
		{
			TipsArray.Add(MakeShareable(new FJsonValueString(FString::Printf(
				TEXT("%d of %d matches returned: narrow the query or assetType, or raise maxResults (up to 200)"), FoundAssets.Num(), TotalFound))));
		}
		TipsArray.Add(MakeShareable(new FJsonValueString(TEXT("Use 'open-blueprint-asset' to open found Blueprint assets"))));
		TipsArray.Add(MakeShareable(new FJsonValueString(TEXT("Use 'move-asset' to reorganize found assets"))));
		TipsArray.Add(MakeShareable(new FJsonValueString(TEXT("Use 'read-content-browser-path' to explore specific folders"))));
		Result->SetArrayField(TEXT("tips"), TipsArray);

		// Convert to string
		FString OutputString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

		return FMcpToolCallResult::CreateTextResult(OutputString);
	});
}

bool FN2CMcpSearchContentBrowserTool::ResolveAssetType(const FString& Requested, FString& OutAssetType)
{
	if (Requested.IsEmpty() || Requested.Equals(TEXT("All"), ESearchCase::IgnoreCase))
	{
		OutAssetType = TEXT("All");
		return true;
	}
	for (const N2CSearchContentBrowser::FAssetTypeClass& Type : N2CSearchContentBrowser::AssetTypes)
	{
		if (Requested.Equals(Type.Name, ESearchCase::IgnoreCase))
		{
			OutAssetType = Type.Name;
			return true;
		}
	}
	return false;
}

TArray<FString> FN2CMcpSearchContentBrowserTool::GetSearchRoots(bool bIncludeEngineContent, bool bIncludePluginContent)
{
	TArray<FString> Roots;
	Roots.Add(TEXT("/Game"));
	if (bIncludeEngineContent)
	{
		Roots.Add(TEXT("/Engine"));
	}
	if (bIncludePluginContent)
	{
		// Each enabled plugin with content is mounted at its own root (/<PluginName>), which is not under /Game or
		// /Engine, so it has to be listed (card 0285). Engine plugins (engine, Fab, enterprise) only with engine
		// content, as UE 5.8's AssetTools.find_assets does with get_plugin_content_paths.
		for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
		{
			if (Plugin->GetLoadedFrom() != EPluginLoadedFrom::Project && !bIncludeEngineContent)
			{
				continue;
			}
			FString Mount = Plugin->GetMountedAssetPath();
			if (Mount.IsEmpty() || !FPackageName::MountPointExists(Mount))
			{
				continue;
			}
			Mount.RemoveFromEnd(TEXT("/"));
			Roots.AddUnique(Mount);
		}
	}
	return Roots;
}

bool FN2CMcpSearchContentBrowserTool::SearchAssets(const FString& SearchQuery, const FString& AssetType, const TArray<FString>& SearchRoots,
	int32 MaxResults, TArray<FAssetData>& OutAssets, TArray<float>& OutScores, int32& OutTotalFound)
{
	using namespace N2CSearchContentBrowser;

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	for (const FString& Root : SearchRoots)
	{
		Filter.PackagePaths.Add(FName(*Root));
	}

	// The type's class and every subclass (card 0285: an exact match found nothing for the abstract USoundBase and
	// UDataAsset, and no Widget or Animation Blueprint for Blueprint)
	if (AssetType != TEXT("All"))
	{
		for (const FAssetTypeClass& Type : AssetTypes)
		{
			if (AssetType == Type.Name)
			{
				Filter.ClassPaths.Add(Type.Class()->GetClassPathName());
			}
		}
		Filter.bRecursiveClasses = true;
	}

	// Score as the registry enumerates, keeping only the best MaxResults in a heap whose top is the worst kept
	// (imp-mcp-tools-contentbrowser-fs-5): nothing is copied or sorted for the assets that do not make the cut
	auto WorseFirst = [](const FScoredAsset& A, const FScoredAsset& B) { return RanksBefore(B, A); };
	TArray<FScoredAsset> Best;
	Best.Reserve(MaxResults + 1);
	OutTotalFound = 0;
	const bool bValidFilter = AssetRegistry.EnumerateAssets(Filter, [&](const FAssetData& Asset)
	{
		// Per-actor packages of World Partition levels, which the content browser hides
		if (FN2CMcpContentBrowserUtils::IsExternalPackagePath(Asset.PackagePath.ToString()))
		{
			return true;
		}
		const float Score = ScoreAssetMatch(Asset, SearchQuery);
		if (Score <= 0.0f)
		{
			return true;
		}
		++OutTotalFound;
		// The asset is copied only when it is kept
		if (Best.Num() < MaxResults)
		{
			Best.HeapPush(FScoredAsset{ Score, Asset }, WorseFirst);
		}
		else if (RanksBefore(Score, Asset, Best.HeapTop().Score, Best.HeapTop().Asset))
		{
			Best.HeapPopDiscard(WorseFirst, EAllowShrinking::No);
			Best.HeapPush(FScoredAsset{ Score, Asset }, WorseFirst);
		}
		return true;
	});
	if (!bValidFilter)
	{
		return false;
	}

	Best.Sort([](const FScoredAsset& A, const FScoredAsset& B) { return RanksBefore(A, B); });
	OutAssets.Reset(Best.Num());
	OutScores.Reset(Best.Num());
	for (FScoredAsset& Entry : Best)
	{
		OutScores.Add(Entry.Score);
		OutAssets.Add(MoveTemp(Entry.Asset));
	}
	return true;
}

float FN2CMcpSearchContentBrowserTool::ScoreAssetMatch(const FAssetData& AssetData, const FString& SearchQuery)
{
	if (SearchQuery.IsEmpty())
	{
		return 1.0f;
	}

	const FString AssetName = AssetData.AssetName.ToString();

	// Exact match, starts with, contains (FString compares here ignore case)
	if (AssetName.Equals(SearchQuery, ESearchCase::IgnoreCase))
	{
		return 1.0f;
	}
	if (AssetName.StartsWith(SearchQuery, ESearchCase::IgnoreCase))
	{
		return 0.8f;
	}
	if (AssetName.Contains(SearchQuery, ESearchCase::IgnoreCase))
	{
		return 0.6f;
	}
	// The full package path
	if (AssetData.PackageName.ToString().Contains(SearchQuery, ESearchCase::IgnoreCase))
	{
		return 0.4f;
	}
	return 0.0f;
}

TSharedPtr<FJsonObject> FN2CMcpSearchContentBrowserTool::ConvertAssetToJson(const FAssetData& AssetData, float Score)
{
	TSharedPtr<FJsonObject> AssetJson = MakeShareable(new FJsonObject);

	// Basic info
	AssetJson->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
	AssetJson->SetStringField(TEXT("path"), AssetData.PackageName.ToString());
	AssetJson->SetStringField(TEXT("type"), GetAssetDisplayType(AssetData));
	AssetJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
	AssetJson->SetNumberField(TEXT("relevanceScore"), Score);

	// Additional metadata
	TSharedPtr<FJsonObject> MetadataJson = MakeShareable(new FJsonObject);

	// Add folder path
	MetadataJson->SetStringField(TEXT("folder"), AssetData.PackagePath.ToString());

	// Add object path for move operations
	MetadataJson->SetStringField(TEXT("objectPath"), AssetData.GetObjectPathString());

	// Any Blueprint, Widget and Animation Blueprints included
	const UClass* AssetClass = N2CSearchContentBrowser::ClassOf(AssetData);
	if (AssetClass && AssetClass->IsChildOf(UBlueprint::StaticClass()))
	{
		// Try to get parent class
		FString ParentClassName;
		if (AssetData.GetTagValue(TEXT("ParentClass"), ParentClassName))
		{
			MetadataJson->SetStringField(TEXT("parentClass"), ParentClassName);
		}

		// Try to get Blueprint type
		FString BlueprintType;
		if (AssetData.GetTagValue(TEXT("BlueprintType"), BlueprintType))
		{
			MetadataJson->SetStringField(TEXT("blueprintType"), BlueprintType);
		}
	}

	AssetJson->SetObjectField(TEXT("metadata"), MetadataJson);

	return AssetJson;
}

FString FN2CMcpSearchContentBrowserTool::GetAssetDisplayType(const FAssetData& AssetData)
{
	// The assetType value whose filter finds this asset, so a follow-up search on the type finds it again
	if (const UClass* AssetClass = N2CSearchContentBrowser::ClassOf(AssetData))
	{
		for (const N2CSearchContentBrowser::FAssetTypeClass& Type : N2CSearchContentBrowser::AssetTypes)
		{
			if (AssetClass->IsChildOf(Type.Class()))
			{
				return Type.Name;
			}
		}
	}
	// Otherwise the class's own name, as the class path holds it: UserDefinedStruct, not a U-stripped
	// "serDefinedStruct" (card 0285)
	return AssetData.AssetClassPath.GetAssetName().ToString();
}
