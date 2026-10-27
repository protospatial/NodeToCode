// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

// Forward declarations
class UEdGraphSchema_K2;
struct FAssetData;

/**
 * MCP tool that searches for available variable types in the Blueprint editor
 * Returns type names, identifiers, categories, and descriptions for use with CreateVariable
 */
class FN2CMcpSearchVariableTypesTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

	// Type information structure
	struct FVariableTypeInfo
	{
		FString TypeName;
		FString TypeIdentifier;
		FString Category;
		FString Description;
		FString Icon;
		FString ParentClass;
		bool bIsAbstract;
		/** Comes from the engine or an engine plugin (by module or mount point, not by path prefix) */
		bool bIsEngine;
		/** TypeName and Description in lower case, for matching */
		FString LowerName;
		FString LowerDescription;

		FVariableTypeInfo() : bIsAbstract(false), bIsEngine(false) {}
	};

private:
	// Type collection methods
	void CollectPrimitiveTypes(TArray<FVariableTypeInfo>& OutTypes) const;
	/** Builds the cached native and asset types if stale; loads no asset */
	void BuildTypeIndex() const;
	/** Appends user structs, enums and Blueprint classes loaded in memory that the index does not have */
	void CollectLoadedUserTypes(TArray<FVariableTypeInfo>& InOutTypes) const;

	/** The top MaxResults matches; OutTotalMatches is the number that matched before the cut */
	TArray<FVariableTypeInfo> FilterTypesBySearchTerm(const TArray<FVariableTypeInfo>& AllTypes,
		const FString& SearchTerm, int32 MaxResults, int32& OutTotalMatches) const;

	// JSON conversion
	TSharedPtr<FJsonObject> BuildJsonResult(const TArray<FVariableTypeInfo>& FilteredTypes, int32 TotalMatches) const;

	// Utility
	FString GetTypeDescription(const UObject* TypeObject) const;
};