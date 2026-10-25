// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CGraphStateTypes.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Models/N2CTranslation.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "N2CGraphStateManager.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CGraphStateChanged, const FGuid& /*GraphGuid*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CGraphTagAdded, const FN2CGraphState& /*GraphState*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnN2CGraphTagRemoved, const FGuid& /*GraphGuid*/, const FString& /*Tag*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CGraphTranslationUpdated, const FGuid& /*GraphGuid*/);
/** Many graphs may have changed at once (a bulk update ended, or the state was reloaded): re-read everything */
DECLARE_MULTICAST_DELEGATE(FOnN2CGraphStatesReloaded);

/**
 * @class UN2CGraphStateManager
 * @brief Singleton manager for unified Blueprint graph state tracking
 *
 * This class provides a single source of truth for tracking:
 * - Tags applied to Blueprint graphs
 * - Translation state (path, provider, model, language, summary)
 * - JSON export state (path, timestamp, minification)
 *
 * It replaces the previous tag-only system with a unified approach that enables
 * the UI to accurately reflect graph state, including whether translations exist.
 *
 * Persistence: Saved/NodeToCode/BlueprintGraphState.json, written atomically (FN2CFileUtils::SaveStringAtomically,
 * keeping BlueprintGraphState.json.bak). A file that does not parse is renamed to
 * BlueprintGraphState.json.corrupt-<UTC timestamp>, the user is notified, and saving stays off until LoadState
 * (reload) or ClearAllState (reset), so the next change cannot overwrite the user's data.
 */
UCLASS()
class NODETOCODE_API UN2CGraphStateManager : public UObject
{
	GENERATED_BODY()

public:
	/** Get the singleton instance */
	static UN2CGraphStateManager& Get();

	/** Initialize the manager (loads persisted state, migrates legacy tags) */
	void Initialize();

	/** Shutdown the manager (saves state if dirty) */
	void Shutdown();

	/** The singleton if Get() has created it, else nullptr (does not create it) */
	static UN2CGraphStateManager* GetIfExists() { return Instance; }

	/** Shuts the singleton down if it exists (module shutdown); does not create it */
	static void ShutdownInstance();

	/**
	 * A manager that is not the singleton and keeps its files in StateDirectory instead of Saved/NodeToCode
	 * (BlueprintGraphState.json there, legacy tags in StateDirectory/Tags/BlueprintTags.json). It is initialized
	 * (loaded or migrated) before it is returned and does not show notifications. For automation tests: hold it in a
	 * TStrongObjectPtr.
	 */
	static UN2CGraphStateManager* CreateIsolated(const FString& StateDirectory);

	// ========================================================================
	// Graph State Queries
	// ========================================================================

	/**
	 * Find graph state by GUID
	 * @param GraphGuid The unique identifier for the graph
	 * @return Pointer to the graph state, or nullptr if not found
	 */
	FN2CGraphState* FindGraphState(const FGuid& GraphGuid);
	const FN2CGraphState* FindGraphState(const FGuid& GraphGuid) const;

	/**
	 * Get all tracked graph states
	 * @return Array of all graph states
	 */
	const TArray<FN2CGraphState>& GetAllGraphStates() const { return StateFile.Graphs; }

	/**
	 * Get all graphs that have a specific tag
	 * @param Tag The tag name to search for
	 * @param Category Optional category filter (empty for all categories)
	 * @return Array of matching graph states
	 */
	TArray<FN2CGraphState> GetGraphsWithTag(const FString& Tag, const FString& Category = TEXT("")) const;

	/**
	 * Get all graphs in a specific category
	 * @param Category The category name
	 * @return Array of graph states with tags in the category
	 */
	TArray<FN2CGraphState> GetGraphsInCategory(const FString& Category) const;

	/**
	 * Get all graphs that have translations
	 * @return Array of graph states with bExists == true
	 */
	TArray<FN2CGraphState> GetGraphsWithTranslation() const;

	/**
	 * Get all unique tag names across all graphs
	 * @return Array of unique tag names
	 */
	TArray<FString> GetAllTagNames() const;

	/**
	 * Get all unique categories across all graphs
	 * @return Array of unique category names
	 */
	TArray<FString> GetAllCategories() const;

	// ========================================================================
	// Tag Operations (preserves existing API for backward compatibility)
	// ========================================================================

	/**
	 * Add a tag to a graph
	 * @param GraphGuid The unique identifier for the graph
	 * @param GraphName The human-readable graph name
	 * @param Blueprint The owning Blueprint asset
	 * @param Tag The tag name
	 * @param Category The tag category
	 * @param Description Optional description
	 * @return True if the tag was added
	 */
	bool AddTag(const FGuid& GraphGuid, const FString& GraphName,
		const FSoftObjectPath& Blueprint, const FString& Tag,
		const FString& Category, const FString& Description = TEXT(""));

	/**
	 * Add a tag using the legacy structure (for backward compatibility)
	 * @param TaggedGraph The tag information
	 * @return True if the tag was added
	 */
	bool AddTag(const FN2CTaggedBlueprintGraph& TaggedGraph);

	/**
	 * Remove a specific tag from a graph
	 * @param GraphGuid The GUID of the graph
	 * @param Tag The tag name
	 * @param Category The tag category
	 * @return True if removed
	 */
	bool RemoveTag(const FGuid& GraphGuid, const FString& Tag, const FString& Category);

	/**
	 * Remove all instances of a tag from a graph (ignoring category)
	 * @param GraphGuid The GUID of the graph
	 * @param Tag The tag name
	 * @return Number of tags removed
	 */
	int32 RemoveTagByName(const FGuid& GraphGuid, const FString& Tag);

	/**
	 * Remove all tags from a graph (does not remove translation/export state)
	 * @param GraphGuid The GUID of the graph
	 * @return Number of tags removed
	 */
	int32 RemoveAllTagsFromGraph(FGuid GraphGuid);

	/**
	 * Remove every tag from every graph (translation and export state are kept; graphs left with nothing are removed).
	 * Broadcasts OnGraphStatesReloaded once and saves once.
	 * @return Number of tags removed
	 */
	int32 ClearAllTags();

	/**
	 * Check if a graph has a specific tag
	 * @param GraphGuid The GUID of the graph
	 * @param Tag The tag name
	 * @param Category The tag category (empty for any category)
	 * @return True if the tag exists
	 */
	bool GraphHasTag(const FGuid& GraphGuid, const FString& Tag, const FString& Category = TEXT("")) const;

	/**
	 * Get all tags for a specific graph
	 * @param GraphGuid The GUID of the graph
	 * @return Array of tag entries
	 */
	TArray<FN2CTagEntry> GetTagsForGraph(const FGuid& GraphGuid) const;

	/**
	 * Get all tags as legacy format (for backward compatibility)
	 * @return Array of all tags in FN2CTaggedBlueprintGraph format
	 */
	TArray<FN2CTaggedBlueprintGraph> GetAllTagsLegacy() const;

	// ========================================================================
	// Translation State Operations
	// ========================================================================

	/**
	 * Set/update the translation state for a graph
	 * @param GraphGuid The GUID of the graph
	 * @param State The translation state to set
	 */
	void SetTranslationState(const FGuid& GraphGuid, const FN2CTranslationState& State);

	/**
	 * Set translation state with individual parameters
	 * @param GraphGuid The GUID of the graph
	 * @param GraphName The graph name (for creating new state if needed)
	 * @param Blueprint The owning Blueprint
	 * @param OutputPath Path to translation output directory
	 * @param Provider LLM provider name
	 * @param Model Model name
	 * @param Language Target language
	 * @param Summary Translation summary metadata
	 */
	void SetTranslationState(const FGuid& GraphGuid, const FString& GraphName,
		const FSoftObjectPath& Blueprint, const FString& OutputPath,
		const FString& Provider, const FString& Model, const FString& Language,
		const FN2CTranslationSummary& Summary);

	/**
	 * Record a translation saved to disk. Every writer (batch, main window, LLM module) records through this, so the
	 * stored language id (FN2CPathUtils::GetLanguageId) and output path (FN2CPathUtils::MakeTranslationRecordPath) are
	 * the ones LoadTranslation reads.
	 * @param GraphDirectory The folder holding <Graph><ext> (absolute, or relative to the working directory)
	 * @param Language The language the implementation file was saved as
	 */
	void RecordSavedTranslation(const FGuid& GraphGuid, const FString& GraphName,
		const FSoftObjectPath& Blueprint, const FString& GraphDirectory,
		const FString& Provider, const FString& Model, EN2CCodeLanguage Language,
		const FN2CTranslationSummary& Summary);

	/**
	 * Clear the translation state for a graph
	 * @param GraphGuid The GUID of the graph
	 */
	void ClearTranslationState(const FGuid& GraphGuid);

	/**
	 * Check if a graph has a translation
	 * @param GraphGuid The GUID of the graph
	 * @return True if translation exists
	 */
	bool HasTranslation(const FGuid& GraphGuid) const;

	/**
	 * Load a translation from disk: <OutputPath>/<Graph><ext> for the recorded language, with the older records
	 * still readable (an enum-qualified or CPP language id, Pseudocode saved as .txt). When that file is missing and
	 * the folder holds exactly one implementation file of the language (the LLM named the graph differently), that one.
	 * @param GraphGuid The GUID of the graph
	 * @param OutTranslation The loaded translation data
	 * @return True if loaded successfully
	 */
	bool LoadTranslation(const FGuid& GraphGuid, FN2CGraphTranslation& OutTranslation) const;

	/**
	 * LoadTranslation, also giving the recorded language (FN2CPathUtils::ParseLanguageId on the stored id; C++ when
	 * the id is empty or unknown), so the translation viewer can label and highlight the implementation file
	 */
	bool LoadTranslation(const FGuid& GraphGuid, FN2CGraphTranslation& OutTranslation, EN2CCodeLanguage& OutLanguage) const;

	/**
	 * Load the N2C JSON representation of a graph from disk
	 * @param GraphGuid The GUID of the graph
	 * @param OutJsonContent The loaded JSON content as a string
	 * @return True if loaded successfully
	 */
	bool LoadN2CJson(const FGuid& GraphGuid, FString& OutJsonContent) const;

	// ========================================================================
	// JSON Export State Operations
	// ========================================================================

	/**
	 * Set/update the JSON export state for a graph
	 * @param GraphGuid The GUID of the graph
	 * @param State The export state to set
	 */
	void SetJsonExportState(const FGuid& GraphGuid, const FN2CJsonExportState& State);

	/**
	 * Set JSON export state with individual parameters
	 * @param GraphGuid The GUID of the graph
	 * @param GraphName The graph name (for creating new state if needed)
	 * @param Blueprint The owning Blueprint
	 * @param OutputPath Path to exported JSON file
	 * @param bMinified Whether the JSON was minified
	 */
	void SetJsonExportState(const FGuid& GraphGuid, const FString& GraphName,
		const FSoftObjectPath& Blueprint, const FString& OutputPath, bool bMinified);

	/**
	 * Clear the JSON export state for a graph
	 * @param GraphGuid The GUID of the graph
	 */
	void ClearJsonExportState(const FGuid& GraphGuid);

	/**
	 * Check if a graph has been exported to JSON
	 * @param GraphGuid The GUID of the graph
	 * @return True if export exists
	 */
	bool HasJsonExport(const FGuid& GraphGuid) const;

	// ========================================================================
	// Graph State Lifecycle
	// ========================================================================

	/**
	 * Ensure a graph state exists, creating it if needed
	 * @param GraphGuid The GUID of the graph
	 * @param GraphName The graph name
	 * @param Blueprint The owning Blueprint
	 * @return Reference to the graph state
	 */
	FN2CGraphState& GetOrCreateGraphState(const FGuid& GraphGuid, const FString& GraphName,
		const FSoftObjectPath& Blueprint);

	/**
	 * Remove a graph state entirely (tags, translation, export state)
	 * @param GraphGuid The GUID of the graph
	 * @return True if removed
	 */
	bool RemoveGraphState(FGuid GraphGuid);

	/**
	 * Clear all state (use with caution). Also turns saving back on after a corrupt file was set aside, and saves.
	 */
	void ClearAllState();

	// ========================================================================
	// Bulk updates
	// ========================================================================

	/**
	 * Start a bulk update: until the matching EndBulkUpdate, saves and per-graph broadcasts are held back. When the
	 * outermost bulk update ends, the state is saved once and OnGraphStatesReloaded is broadcast once if anything
	 * changed. Prefer FN2CGraphStateBulkUpdate.
	 */
	void BeginBulkUpdate();
	void EndBulkUpdate();
	bool IsInBulkUpdate() const { return BulkUpdateDepth > 0; }

	// ========================================================================
	// Persistence
	// ========================================================================

	/** Save state to disk (deferred inside a bulk update; refused while a corrupt file blocks saving) */
	bool SaveState();

	/**
	 * Load state from disk. A file that does not parse is set aside as .corrupt-<timestamp> and saving is turned off;
	 * a later LoadState that finds a good file or no file turns saving back on.
	 * @return True if a state file was read
	 */
	bool LoadState();

	/** Check if there are unsaved changes */
	bool IsDirty() const { return bIsDirty; }

	/** True after a state file failed to parse, until LoadState or ClearAllState */
	bool IsSavingBlocked() const { return bSavingBlocked; }

	/** Where the last unparseable state file was moved (empty if none, or if it could not be moved) */
	const FString& GetQuarantinedFilePath() const { return QuarantinedFilePath; }

	/** How many times this manager has written the state file */
	int32 GetSaveCount() const { return SaveCount; }

	/** Full path of BlueprintGraphState.json (creates its folder) */
	FString GetStateFilePath() const;

	/** Migrate from legacy BlueprintTags.json file */
	void MigrateFromLegacyTags();

	// ========================================================================
	// Delegates
	// ========================================================================

	/** Fired when any graph state changes */
	FOnN2CGraphStateChanged OnGraphStateChanged;

	/** Fired when a tag is added */
	FOnN2CGraphTagAdded OnGraphTagAdded;

	/** Fired when a tag is removed */
	FOnN2CGraphTagRemoved OnGraphTagRemoved;

	/** Fired when translation state is updated */
	FOnN2CGraphTranslationUpdated OnGraphTranslationUpdated;

	/** Fired once after a bulk update that changed something, and after LoadState: listeners should re-read all state */
	FOnN2CGraphStatesReloaded OnGraphStatesReloaded;

private:
	/** Constructor */
	UN2CGraphStateManager();

	enum class ELoadResult : uint8 { Loaded, Missing, Unreadable, Corrupt };

	/** Reads the state file into StateFile; sets the file aside and blocks saving when it does not parse */
	ELoadResult LoadStateInternal();

	/** The folder holding the state file: StateDirectoryOverride, or Saved/NodeToCode */
	FString GetStateDirectory() const;

	/** Get the legacy tags file path */
	FString GetLegacyTagsFilePath() const;

	/** Mark state as modified */
	void MarkDirty();

	/** Per-graph broadcasts, held back during a bulk update */
	void BroadcastTagAdded(const FN2CGraphState& State);
	void BroadcastTagRemoved(const FGuid& GraphGuid, const FString& Tag);
	void BroadcastStateChanged(const FGuid& GraphGuid);
	void BroadcastTranslationUpdated(const FGuid& GraphGuid);

	/** Logs and (unless isolated) shows a notification that the state file was set aside */
	void ReportCorruptFile(const FString& FilePath, bool bMoved);

	/** The singleton instance */
	static UN2CGraphStateManager* Instance;

	/** The state file data */
	UPROPERTY()
	FN2CGraphStateFile StateFile;

	/** Flag indicating if state has been modified since last save */
	bool bIsDirty;

	/** Flag to track if migration has been attempted */
	bool bMigrationAttempted;

	/** Set by CreateIsolated: the folder used instead of Saved/NodeToCode */
	FString StateDirectoryOverride;

	/** False for isolated managers (tests): no Slate notifications */
	bool bNotifyUser = true;

	/** Nesting depth of BeginBulkUpdate */
	int32 BulkUpdateDepth = 0;

	/** Something changed during the current bulk update */
	bool bBulkChanged = false;

	/** A save was requested during the current bulk update */
	bool bBulkSavePending = false;

	/** A state file failed to parse; saving is off until LoadState or ClearAllState */
	bool bSavingBlocked = false;

	/** Where the unparseable file was moved */
	FString QuarantinedFilePath;

	/** Successful writes of the state file */
	int32 SaveCount = 0;
};

/** Holds back saves and per-graph broadcasts on a graph state manager for its lifetime (see BeginBulkUpdate) */
class FN2CGraphStateBulkUpdate
{
public:
	explicit FN2CGraphStateBulkUpdate(UN2CGraphStateManager& InManager)
		: Manager(InManager)
	{
		Manager.BeginBulkUpdate();
	}

	~FN2CGraphStateBulkUpdate()
	{
		Manager.EndBulkUpdate();
	}

	FN2CGraphStateBulkUpdate(const FN2CGraphStateBulkUpdate&) = delete;
	FN2CGraphStateBulkUpdate& operator=(const FN2CGraphStateBulkUpdate&) = delete;

private:
	UN2CGraphStateManager& Manager;
};
