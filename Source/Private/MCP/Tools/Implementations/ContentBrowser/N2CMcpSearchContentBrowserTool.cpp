// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpSearchContentBrowserTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimSequence.h"
#include "Particles/ParticleSystem.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Utils/N2CLogger.h"

REGISTER_MCP_TOOL(FN2CMcpSearchContentBrowserTool)

FMcpToolDefinition FN2CMcpSearchContentBrowserTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("search-content-browser"),
		TEXT("Search for assets under /Game (and /Engine when includeEngineContent is true) by name or type. ")
		TEXT("Plugin content mounted at /<PluginName> is not searched; list it with read-content-browser-path."),
		TEXT("Content Browser")
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
	AssetTypeProp->SetStringField(TEXT("description"), TEXT("Filter by asset type (case-insensitive): All, Blueprint, Material, Texture, StaticMesh, SkeletalMesh, Sound, Animation, ParticleSystem, DataAsset, DataTable. Each matches its exact asset classes only (Blueprint does not include Widget or Animation Blueprints); any other value applies no type filter."));
	AssetTypeProp->SetStringField(TEXT("default"), TEXT("All"));
	Properties->SetObjectField(TEXT("assetType"), AssetTypeProp);

	// includeEngineContent property
	TSharedPtr<FJsonObject> IncludeEngineProp = MakeShareable(new FJsonObject);
	IncludeEngineProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeEngineProp->SetBoolField(TEXT("default"), false);
	IncludeEngineProp->SetStringField(TEXT("description"), TEXT("Include assets from Engine content"));
	Properties->SetObjectField(TEXT("includeEngineContent"), IncludeEngineProp);

	// includePluginContent property
	TSharedPtr<FJsonObject> IncludePluginProp = MakeShareable(new FJsonObject);
	IncludePluginProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludePluginProp->SetBoolField(TEXT("default"), true);
	IncludePluginProp->SetStringField(TEXT("description"), TEXT("Has no effect at present: the search only covers /Game and, with includeEngineContent, /Engine, so plugin content mounted at /<PluginName> is never returned. Echoed in searchInfo."));
	Properties->SetObjectField(TEXT("includePluginContent"), IncludePluginProp);

	// maxResults property
	TSharedPtr<FJsonObject> MaxResultsProp = MakeShareable(new FJsonObject);
	MaxResultsProp->SetStringField(TEXT("type"), TEXT("number"));
	MaxResultsProp->SetNumberField(TEXT("default"), 50);
	MaxResultsProp->SetStringField(TEXT("description"), TEXT("Maximum number of results to return; values outside 1-200 are clamped"));
	Properties->SetObjectField(TEXT("maxResults"), MaxResultsProp);

	Schema->SetObjectField(TEXT("properties"), Properties);
	
	// No required fields - all are optional
	TArray<TSharedPtr<FJsonValue>> RequiredArray;
	Schema->SetArrayField(TEXT("required"), RequiredArray);
	
	Definition.InputSchema = Schema;
	
	return Definition;
}

FMcpToolCallResult FN2CMcpSearchContentBrowserTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
	{
		// Parse arguments
		FString SearchQuery;
		Arguments->TryGetStringField(TEXT("query"), SearchQuery);

		FString AssetType = TEXT("All");
		Arguments->TryGetStringField(TEXT("assetType"), AssetType);

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

		// Search for assets
		TArray<FAssetData> FoundAssets;
		if (!SearchAssets(SearchQuery, AssetType, bIncludeEngineContent, bIncludePluginContent, FoundAssets))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to search assets"));
		}

		// Score and sort results by relevance if we have a search query
		TArray<TPair<FAssetData, float>> ScoredAssets;
		if (!SearchQuery.IsEmpty())
		{
			for (const FAssetData& Asset : FoundAssets)
			{
				float Score = ScoreAssetMatch(Asset, SearchQuery);
				if (Score > 0.0f)
				{
					ScoredAssets.Add(TPair<FAssetData, float>(Asset, Score));
				}
			}

			// Sort by score (highest first)
			ScoredAssets.Sort([](const TPair<FAssetData, float>& A, const TPair<FAssetData, float>& B)
			{
				return A.Value > B.Value;
			});
		}
		else
		{
			// No search query - include all with score 1.0
			for (const FAssetData& Asset : FoundAssets)
			{
				ScoredAssets.Add(TPair<FAssetData, float>(Asset, 1.0f));
			}
		}

		// Build response
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		
		// Search metadata
		TSharedPtr<FJsonObject> SearchInfo = MakeShareable(new FJsonObject);
		SearchInfo->SetStringField(TEXT("query"), SearchQuery.IsEmpty() ? TEXT("*") : SearchQuery);
		SearchInfo->SetStringField(TEXT("assetType"), AssetType);
		SearchInfo->SetBoolField(TEXT("includeEngineContent"), bIncludeEngineContent);
		SearchInfo->SetBoolField(TEXT("includePluginContent"), bIncludePluginContent);
		SearchInfo->SetNumberField(TEXT("totalFound"), ScoredAssets.Num());
		SearchInfo->SetNumberField(TEXT("resultsReturned"), FMath::Min(ScoredAssets.Num(), MaxResults));
		Result->SetObjectField(TEXT("searchInfo"), SearchInfo);

		// Asset results
		TArray<TSharedPtr<FJsonValue>> AssetsArray;
		int32 ResultCount = 0;
		for (const auto& ScoredAsset : ScoredAssets)
		{
			if (ResultCount >= MaxResults)
			{
				break;
			}

			TSharedPtr<FJsonObject> AssetJson = ConvertAssetToJson(ScoredAsset.Key, ScoredAsset.Value);
			AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));
			ResultCount++;
		}
		Result->SetArrayField(TEXT("assets"), AssetsArray);

		// Add usage tips
		TArray<TSharedPtr<FJsonValue>> TipsArray;
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

bool FN2CMcpSearchContentBrowserTool::SearchAssets(const FString& SearchQuery, const FString& AssetType,
	bool bIncludeEngineContent, bool bIncludePluginContent, TArray<FAssetData>& OutAssets) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Build filter
	FARFilter Filter;
	Filter.bRecursivePaths = true;

	// Add paths to search
	Filter.PackagePaths.Add(TEXT("/Game"));
	
	if (bIncludeEngineContent)
	{
		Filter.PackagePaths.Add(TEXT("/Engine"));
	}

	// Note: Plugin content is typically under /Game or custom mount points
	// We'll filter plugin content in post-processing if needed

	// Add class filter if specific type requested
	if (AssetType != TEXT("All"))
	{
		if (AssetType == TEXT("Blueprint"))
		{
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("Material"))
		{
			Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("Texture"))
		{
			Filter.ClassPaths.Add(UTexture::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("StaticMesh"))
		{
			Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("SkeletalMesh"))
		{
			Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("Sound"))
		{
			Filter.ClassPaths.Add(USoundBase::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("Animation"))
		{
			Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("ParticleSystem"))
		{
			Filter.ClassPaths.Add(UParticleSystem::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("DataAsset"))
		{
			Filter.ClassPaths.Add(UDataAsset::StaticClass()->GetClassPathName());
		}
		else if (AssetType == TEXT("DataTable"))
		{
			Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
		}
	}

	// Get assets
	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAssets(Filter, AllAssets);

	// Post-process to filter by path inclusion rules and search query
	OutAssets.Reset();
	for (const FAssetData& Asset : AllAssets)
	{
		FString PackagePath = Asset.PackagePath.ToString();
		
		// Check path inclusion
		if (!ShouldIncludePath(PackagePath, bIncludeEngineContent, bIncludePluginContent))
		{
			continue;
		}

		// If we have a search query, check if asset name matches
		if (!SearchQuery.IsEmpty())
		{
			FString AssetName = Asset.AssetName.ToString();
			if (!AssetName.Contains(SearchQuery, ESearchCase::IgnoreCase))
			{
				// Also check the full path
				FString FullPath = Asset.PackageName.ToString();
				if (!FullPath.Contains(SearchQuery, ESearchCase::IgnoreCase))
				{
					continue;
				}
			}
		}

		OutAssets.Add(Asset);
	}

	return true;
}

void FN2CMcpSearchContentBrowserTool::FilterAssetsByType(const TArray<FAssetData>& Assets, const FString& AssetType,
	TArray<FAssetData>& OutFilteredAssets) const
{
	// This is now handled in SearchAssets with the FARFilter
	OutFilteredAssets = Assets;
}

float FN2CMcpSearchContentBrowserTool::ScoreAssetMatch(const FAssetData& AssetData, const FString& SearchQuery) const
{
	if (SearchQuery.IsEmpty())
	{
		return 1.0f;
	}

	FString AssetName = AssetData.AssetName.ToString();
	FString PackageName = AssetData.PackageName.ToString();
	FString SearchLower = SearchQuery.ToLower();
	FString AssetNameLower = AssetName.ToLower();

	float Score = 0.0f;

	// Exact match
	if (AssetNameLower == SearchLower)
	{
		Score = 1.0f;
	}
	// Starts with
	else if (AssetNameLower.StartsWith(SearchLower))
	{
		Score = 0.8f;
	}
	// Contains
	else if (AssetNameLower.Contains(SearchLower))
	{
		Score = 0.6f;
	}
	// Check full path
	else if (PackageName.ToLower().Contains(SearchLower))
	{
		Score = 0.4f;
	}

	return Score;
}

TSharedPtr<FJsonObject> FN2CMcpSearchContentBrowserTool::ConvertAssetToJson(const FAssetData& AssetData, float Score) const
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
	FString FolderPath = FPaths::GetPath(AssetData.PackageName.ToString());
	MetadataJson->SetStringField(TEXT("folder"), FolderPath);

	// Add object path for move operations
	FString ObjectPath = AssetData.PackageName.ToString() + TEXT(".") + AssetData.AssetName.ToString();
	MetadataJson->SetStringField(TEXT("objectPath"), ObjectPath);

	// Check if it's a Blueprint
	if (AssetData.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
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

FString FN2CMcpSearchContentBrowserTool::GetAssetDisplayType(const FAssetData& AssetData) const
{
	FTopLevelAssetPath ClassPath = AssetData.AssetClassPath;
	
	if (ClassPath == UBlueprint::StaticClass()->GetClassPathName())
	{
		return TEXT("Blueprint");
	}
	else if (ClassPath == UMaterial::StaticClass()->GetClassPathName() ||
		ClassPath == UMaterialInstance::StaticClass()->GetClassPathName() ||
		ClassPath == UMaterialInstanceConstant::StaticClass()->GetClassPathName())
	{
		return TEXT("Material");
	}
	else if (ClassPath == UTexture::StaticClass()->GetClassPathName() ||
		ClassPath == UTexture2D::StaticClass()->GetClassPathName())
	{
		return TEXT("Texture");
	}
	else if (ClassPath == UStaticMesh::StaticClass()->GetClassPathName())
	{
		return TEXT("StaticMesh");
	}
	else if (ClassPath == USkeletalMesh::StaticClass()->GetClassPathName())
	{
		return TEXT("SkeletalMesh");
	}
	else if (ClassPath == USoundBase::StaticClass()->GetClassPathName())
	{
		return TEXT("Sound");
	}
	else if (ClassPath == UAnimSequence::StaticClass()->GetClassPathName())
	{
		return TEXT("Animation");
	}
	else if (ClassPath == UParticleSystem::StaticClass()->GetClassPathName())
	{
		return TEXT("ParticleSystem");
	}
	else if (ClassPath == UDataAsset::StaticClass()->GetClassPathName())
	{
		return TEXT("DataAsset");
	}
	else if (ClassPath == UDataTable::StaticClass()->GetClassPathName())
	{
		return TEXT("DataTable");
	}
	else
	{
		// Return the class name without the U prefix
		FString ClassName = ClassPath.GetAssetName().ToString();
		if (ClassName.StartsWith(TEXT("U")))
		{
			ClassName = ClassName.RightChop(1);
		}
		return ClassName;
	}
}

bool FN2CMcpSearchContentBrowserTool::ShouldIncludePath(const FString& PackagePath, bool bIncludeEngineContent, bool bIncludePluginContent) const
{
	// Always include /Game content
	if (PackagePath.StartsWith(TEXT("/Game")))
	{
		return true;
	}

	// Check engine content
	if (PackagePath.StartsWith(TEXT("/Engine")))
	{
		return bIncludeEngineContent;
	}

	// Plugin content can be under various mount points
	// Common patterns: /PluginName/, /Plugins/PluginName/
	if (bIncludePluginContent)
	{
		// If it's not /Game or /Engine, it's likely plugin content
		return true;
	}

	return false;
}