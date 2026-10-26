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
 * Which graph a state entry belongs to: the owning Blueprint asset and the graph's GUID. The GUID alone is not enough,
 * because duplicating a Blueprint in the Content Browser copies UEdGraph::GraphGuid, so a Blueprint and its copies have
 * graphs with the same GUID.
 *
 * A key without a Blueprint (made from a bare FGuid, which converts implicitly so GUID-only callers keep compiling) is
 * a GUID-only lookup: it finds an entry only while exactly one entry has that GUID.
 */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CGraphKey
{
	GENERATED_BODY()

	/** The owning Blueprint's object path (/Game/Folder/BP_Name.BP_Name); null for a GUID-only key */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FSoftObjectPath Blueprint;

	/** The graph's UEdGraph::GraphGuid */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	FGuid GraphGuid;

	FN2CGraphKey() = default;

	/** The Blueprint path is normalised (see NormalizeBlueprintPath) */
	FN2CGraphKey(const FSoftObjectPath& InBlueprint, const FGuid& InGraphGuid);

	/** A GUID-only key (implicit on purpose: existing FGuid callers become GUID-only lookups) */
	FN2CGraphKey(const FGuid& InGraphGuid);

	/** From a Blueprint path and a GUID string as the UI and the Blueprint library carry them; an unparsable GUID gives an invalid key */
	static FN2CGraphKey FromStrings(const FString& BlueprintPath, const FString& GraphGuid);

	/** The key of a live graph: its GUID and the Blueprint that owns it (outer chain) */
	static FN2CGraphKey FromGraph(const class UEdGraph* Graph);

	/**
	 * One spelling per Blueprint: a package path (/Game/BP) gets its asset name (/Game/BP.BP), an export-text path
	 * (Blueprint'/Game/BP.BP') loses its class, and surrounding blanks are dropped
	 */
	static FSoftObjectPath NormalizeBlueprintPath(const FSoftObjectPath& Path);
	static FSoftObjectPath NormalizeBlueprintPath(const FString& Path);

	/** The key names a graph (its GUID is set) */
	bool IsValid() const { return GraphGuid.IsValid(); }

	/** The key names the Blueprint, so it identifies one entry even when copies share the GUID */
	bool HasBlueprint() const { return !Blueprint.IsNull(); }

	/** "<Blueprint>:<GUID>", or the GUID alone for a GUID-only key (for logs and messages) */
	FString ToString() const;

	bool operator==(const FN2CGraphKey& Other) const { return GraphGuid == Other.GraphGuid && Blueprint == Other.Blueprint; }
	bool operator!=(const FN2CGraphKey& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FN2CGraphKey& Key)
	{
		return HashCombine(GetTypeHash(Key.GraphGuid), GetTypeHash(Key.Blueprint));
	}
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

	/**
	 * The owning Blueprint was deleted (Asset Registry removal) and has not come back. The entry is kept, so the tags
	 * are not lost to an accident, until the Tag Manager's Prune Missing removes it
	 */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|GraphState")
	bool bMissing = false;

	/** This entry's identity: {OwningBlueprint, GraphGuid} */
	FN2CGraphKey GetKey() const;

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

	/**
	 * The current file format: 3 keys entries by {owningBlueprint, graphGuid} (2 keyed them by graphGuid alone, so one
	 * entry could stand for a Blueprint and all of its copies)
	 */
	static constexpr int32 CurrentMajorVersion = 3;

	/** File format version, "<major>.<minor>" */
	UPROPERTY()
	FString Version = TEXT("3.0");

	/** The major version Version names: 1 for an empty or unreadable value (the oldest files wrote none) */
	int32 GetMajorVersion() const;

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
