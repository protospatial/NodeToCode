// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

// Forward declarations
struct FAssetData;

/**
 * MCP tool that searches for available parent classes for Blueprint creation
 * Similar to the "Pick Parent Class" dialog in the UE editor
 * Returns class names, paths, metadata, and hierarchy information
 */
class FN2CMcpSearchBlueprintClassesTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	// Class information structure
	struct FBlueprintClassInfo
	{
		FString ClassName;
		FString ClassPath;
		FString DisplayName;
		FString Category;
		FString Description;
		FString ParentClass;
		FString Module;
		FString Icon;
		bool bIsAbstract;
		bool bIsDeprecated;
		bool bIsBlueprintable;
		bool bIsNative;
		bool bIsCommonClass;
		int32 RelevanceScore;

		FBlueprintClassInfo() 
			: bIsAbstract(false)
			, bIsDeprecated(false)
			, bIsBlueprintable(true)
			, bIsNative(true)
			, bIsCommonClass(false)
			, RelevanceScore(0)
		{}
	};

	// Collection methods
	void CollectNativeClasses(TArray<FBlueprintClassInfo>& OutClasses, const FString& ClassTypeFilter, 
		bool bIncludeEngineClasses, bool bIncludePluginClasses, bool bIncludeDeprecated, bool bIncludeAbstract) const;
	void CollectBlueprintClasses(TArray<FBlueprintClassInfo>& OutClasses, const FString& ClassTypeFilter,
		bool bIncludeEngineClasses, bool bIncludePluginClasses, bool bIncludeDeprecated, bool bIncludeAbstract) const;
	/** The class is one of the common parent classes (Common*Classes): it is marked commonClass and scores higher */
	bool IsCommonClassPath(const FString& ClassPath) const;

	// Helper methods
	bool PassesClassTypeFilter(const UClass* Class, const FString& ClassTypeFilter) const;
	bool CanCreateBlueprintOfClass(const UClass* Class) const;
	FString GetClassModule(const UClass* Class) const;
	FString GetClassCategory(const UClass* Class) const;
	FString GetClassDescription(const UClass* Class) const;
	FString GetClassDisplayName(const UClass* Class) const;
	FString GetClassIcon(const UClass* Class) const;
	bool IsEngineClass(const FString& ClassPath) const;
	bool IsPluginClass(const FString& ClassPath) const;
	
	// Filtering and scoring
	TArray<FBlueprintClassInfo> FilterAndScoreClasses(const TArray<FBlueprintClassInfo>& AllClasses,
		const FString& SearchTerm, int32 MaxResults, int32& OutTotalMatches) const;
	int32 CalculateRelevanceScore(const FBlueprintClassInfo& ClassInfo, const FString& SearchTerm) const;
	
	// JSON conversion
	TSharedPtr<FJsonObject> BuildJsonResult(const TArray<FBlueprintClassInfo>& FilteredClasses, int32 TotalFound) const;
	TSharedPtr<FJsonObject> ClassInfoToJson(const FBlueprintClassInfo& ClassInfo) const;

	// Common parent classes (similar to UE's default classes)
	static const TArray<FString> CommonActorClasses;
	static const TArray<FString> CommonComponentClasses;
	static const TArray<FString> CommonObjectClasses;
	static const TArray<FString> CommonWidgetClasses;
};