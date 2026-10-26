// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "N2CGraphStateTypes.generated.h"

/**
 * Translation summary metadata for quick preview without loading full translation
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTranslationSummary
{
	GENERATED_BODY()

	/** First line of declaration (e.g., "void AMyActor::MyFunction()") */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FString DeclarationPreview;

	/** Number of lines in the implementation */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	int32 ImplementationLines = 0;

	/** Whether implementation notes exist */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	bool bHasNotes = false;

	/** Serialize to JSON */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Deserialize from JSON */
	static FN2CTranslationSummary FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * Translation state metadata (hybrid approach - stores path + summary)
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTranslationState
{
	GENERATED_BODY()

	/** Whether a translation exists for this graph */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	bool bExists = false;

	/** Path to translation output directory (relative to project) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FString OutputPath;

	/** When the translation was created */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FDateTime Timestamp;

	/** LLM provider used (e.g., "Anthropic", "OpenAI") */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FString Provider;

	/** Model used (e.g., "claude-4-sonnet-20250514") */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FString Model;

	/** Target language (e.g., "CPP", "Python") */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FString Language;

	/** Quick preview metadata */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FN2CTranslationSummary Summary;

	/** Serialize to JSON */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Deserialize from JSON */
	static FN2CTranslationState FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * JSON export state metadata
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CJsonExportState
{
	GENERATED_BODY()

	/** Whether a JSON export exists for this graph */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	bool bExists = false;

	/** Path to the exported JSON file (relative to project) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FString OutputPath;

	/** When the export was created */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FDateTime Timestamp;

	/** Whether the JSON was minified */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	bool bMinified = false;

	/** Serialize to JSON */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Deserialize from JSON */
	static FN2CJsonExportState FromJson(const TSharedPtr<FJsonObject>& JsonObject);
};

/**
 * Individual tag entry (mirrors existing tag structure)
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTagEntry
{
	GENERATED_BODY()

	/** Tag name */
	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|GraphState")
	FString Tag;

	/** Category for organization */
	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|GraphState")
	FString Category = TEXT("Default");

	/** Optional description/notes */
	UPROPERTY(BlueprintReadWrite, Category = "NodeToCode|GraphState")
	FString Description;

	/** When the tag was applied (UTC) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FDateTime Timestamp;

	/** Serialize to JSON */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Deserialize from JSON */
	static FN2CTagEntry FromJson(const TSharedPtr<FJsonObject>& JsonObject);

	/** Check if this matches a tag name and category (case-insensitive); an empty InCategory matches any category (query and remove) */
	bool MatchesTag(const FString& InTag, const FString& InCategory) const;

	/** Check if this is exactly this tag name and category (case-insensitive, no wildcard); used for the duplicate check on add */
	bool MatchesTagExactly(const FString& InTag, const FString& InCategory) const;

	/** The category a new tag is stored under: an empty or blank category becomes "Default" */
	static FString NormalizeCategory(const FString& InCategory);
};

/**
 * Per-graph state container - the main unified tracking structure
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CGraphState
{
	GENERATED_BODY()

	/** Unique identifier for the graph */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FGuid GraphGuid;

	/** Human-readable graph name */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FString GraphName;

	/** Reference to the owning Blueprint asset */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FSoftObjectPath OwningBlueprint;

	/** All tags applied to this graph */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	TArray<FN2CTagEntry> Tags;

	/** Latest translation state (if any) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FN2CTranslationState Translation;

	/** Latest JSON export state (if any) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FN2CJsonExportState JsonExport;

	/** Serialize to JSON */
	TSharedPtr<FJsonObject> ToJson() const;

	/** Deserialize from JSON */
	static FN2CGraphState FromJson(const TSharedPtr<FJsonObject>& JsonObject);

	/** Check if this graph has a specific tag */
	bool HasTag(const FString& InTag, const FString& InCategory = TEXT("")) const;

	/** Get all tags in a specific category */
	TArray<FN2CTagEntry> GetTagsInCategory(const FString& InCategory) const;

	/** Check if graph has any tags */
	bool HasAnyTags() const { return Tags.Num() > 0; }

	/** Check if graph has a translation */
	bool HasTranslation() const { return Translation.bExists; }

	/** Check if graph has been exported to JSON */
	bool HasJsonExport() const { return JsonExport.bExists; }
};

/**
 * Root container for the state file
 */
USTRUCT()
struct NODETOCODE_API FN2CGraphStateFile
{
	GENERATED_BODY()

	/** File format version */
	UPROPERTY()
	FString Version = TEXT("2.0");

	/** When the file was last saved */
	UPROPERTY()
	FDateTime LastSaved;

	/** All tracked graph states */
	UPROPERTY()
	TArray<FN2CGraphState> Graphs;

	/** Serialize to JSON string */
	FString ToJsonString(bool bPrettyPrint = true) const;

	/** Deserialize from JSON string */
	static bool FromJsonString(const FString& JsonString, FN2CGraphStateFile& OutFile);
};
