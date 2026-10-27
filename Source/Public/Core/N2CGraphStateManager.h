// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/N2CGraphStateTypes.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Models/N2CTranslation.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "N2CGraphStateManager.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CGraphStateChanged, const FN2CGraphKey& /*Graph*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CGraphTagAdded, const FN2CGraphState& /*GraphState*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnN2CGraphTagRemoved, const FN2CGraphKey& /*Graph*/, const FString& /*Tag*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnN2CGraphTranslationUpdated, const FN2CGraphKey& /*Graph*/);
/** A Blueprint moved or was renamed, and the entries that named its old path now name the new one */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnN2CBlueprintPathChanged, const FSoftObjectPath& /*OldPath*/, const FSoftObjectPath& /*NewPath*/);
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
 *
 * Identity (file version 3): an entry is keyed by FN2CGraphKey {owning Blueprint, GraphGuid}, because a duplicated
 * Blueprint's graphs keep the original's GUIDs. A version 2 file is migrated on load (each entry keeps its current
 * OwningBlueprint); a file from a newer version is left alone and saving stays off. Every method that takes an
 * FN2CGraphKey also takes a bare FGuid, which finds an entry only while exactly one entry has that GUID.
 *
 * The manager follows the Asset Registry: a renamed or moved Blueprint's entries take its new path, a deleted
 * Blueprint's entries are marked missing (and unmarked if it comes back), and PruneMissing removes them.
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
	 * Find a graph's state
	 * @param Key The Blueprint and graph GUID; a GUID-only key matches only an entry whose GUID no other entry has
	 * @return Pointer to the graph state, or nullptr if not found (or the GUID-only key is ambiguous)
	 */
	FN2CGraphState* FindGraphState(const FN2CGraphKey& Key);
	const FN2CGraphState* FindGraphState(const FN2CGraphKey& Key) const;

	/** The keys of every entry with this graph GUID (more than one when Blueprints were duplicated) */
	TArray<FN2CGraphKey> FindKeysForGuid(const FGuid& GraphGuid) const;

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
	 * @param Category The tag category; an empty or blank category is stored as "Default"
	 * @param Description Optional description
	 * @return True if the tag was added, or this exact tag and category was already on the graph
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
	bool RemoveTag(const FN2CGraphKey& Key, const FString& Tag, const FString& Category);

	/**
	 * Remove all instances of a tag from a graph (ignoring category)
	 * @param GraphGuid The GUID of the graph
	 * @param Tag The tag name
	 * @return Number of tags removed
	 */
	int32 RemoveTagByName(const FN2CGraphKey& Key, const FString& Tag);

	/**
	 * Remove all tags from a graph (does not remove translation/export state)
	 * @param GraphGuid The GUID of the graph
	 * @return Number of tags removed
	 */
	int32 RemoveAllTagsFromGraph(const FN2CGraphKey& Key);

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
	bool GraphHasTag(const FN2CGraphKey& Key, const FString& Tag, const FString& Category = TEXT("")) const;

	/**
	 * Get all tags for a specific graph
	 * @param GraphGuid The GUID of the graph
	 * @return Array of tag entries
	 */
	TArray<FN2CTagEntry> GetTagsForGraph(const FN2CGraphKey& Key) const;

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
	void SetTranslationState(const FN2CGraphKey& Key, const FN2CTranslationState& State);

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
	void ClearTranslationState(const FN2CGraphKey& Key);

	/**
	 * Check if a graph has a translation
	 * @param GraphGuid The GUID of the graph
	 * @return True if translation exists
	 */
	bool HasTranslation(const FN2CGraphKey& Key) const;

	/**
	 * Load a translation from disk: <OutputPath>/<Graph><ext> for the recorded language, with the older records
	 * still readable (an enum-qualified or CPP language id, Pseudocode saved as .txt). When that file is missing and
	 * the folder holds exactly one implementation file of the language (the LLM named the graph differently), that one.
	 * @param GraphGuid The GUID of the graph
	 * @param OutTranslation The loaded translation data
	 * @return True if loaded successfully
	 */
	bool LoadTranslation(const FN2CGraphKey& Key, FN2CGraphTranslation& OutTranslation) const;

	/**
	 * LoadTranslation, also giving the recorded language (FN2CPathUtils::ParseLanguageId on the stored id; C++ when
	 * the id is empty or unknown), so the translation viewer can label and highlight the implementation file
	 */
	bool LoadTranslation(const FN2CGraphKey& Key, FN2CGraphTranslation& OutTranslation, EN2CCodeLanguage& OutLanguage) const;

	/**
	 * Load the N2C JSON representation of a graph from disk: the JSON a batch saved in the recorded graph folder, or
	 * the Blueprint capture a single translation saved in the translation root above it (N2C_BP_<root>.json)
	 * @param Key The graph (Blueprint and GUID)
	 * @param OutJsonContent The loaded JSON content as a string
	 * @return True if loaded successfully
	 */
	bool LoadN2CJson(const FN2CGraphKey& Key, FString& OutJsonContent) const;

	// ========================================================================
	// JSON Export State Operations
	// ========================================================================

	/**
	 * Set/update the JSON export state for a graph
	 * @param GraphGuid The GUID of the graph
	 * @param State The export state to set
	 */
	void SetJsonExportState(const FN2CGraphKey& Key, const FN2CJsonExportState& State);

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
	void ClearJsonExportState(const FN2CGraphKey& Key);

	/**
	 * Check if a graph has been exported to JSON
	 * @param GraphGuid The GUID of the graph
	 * @return True if export exists
	 */
	bool HasJsonExport(const FN2CGraphKey& Key) const;

	// ========================================================================
	// Graph State Lifecycle
	// ========================================================================

	/**
	 * Ensure a graph state exists for {Blueprint, GraphGuid}, creating it if needed. An existing entry's graph name is
	 * refreshed; its Blueprint is never rewritten, so tagging a duplicated Blueprint's graph makes a new entry instead
	 * of taking the original's. The one exception is an entry with this GUID that is marked missing (its Blueprint was
	 * deleted, or renamed while the editor was not watching): it is adopted, so its tags follow the Blueprint. With an
	 * empty Blueprint, the GUID-only lookup is used.
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
	bool RemoveGraphState(const FN2CGraphKey& Key);

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
	// Blueprint rename, move and delete
	// ========================================================================

	/**
	 * Entries that name OldPath (a renamed or moved Blueprint) take NewPath, and are no longer missing. Called for the
	 * Asset Registry's OnAssetRenamed. An entry already at NewPath with the same GUID is merged into (tags added,
	 * translation and export kept unless it has none).
	 * @return Number of entries moved
	 */
	int32 HandleBlueprintRenamed(const FSoftObjectPath& OldPath, const FSoftObjectPath& NewPath);

	/** Entries that name Path are marked missing (OnAssetRemoved, a deleted Blueprint). @return Number marked */
	int32 HandleBlueprintRemoved(const FSoftObjectPath& Path);

	/** Entries that name Path are no longer missing (OnAssetAdded: the Blueprint came back). @return Number unmarked */
	int32 HandleBlueprintAdded(const FSoftObjectPath& Path);

	/**
	 * Marks missing every entry whose Blueprint is neither loaded nor known to the Asset Registry, and unmarks the
	 * others. @return Number of entries now missing
	 */
	int32 RefreshMissing();

	/** RefreshMissing, then removes the entries marked missing with their tags and translation records. @return Number removed */
	int32 PruneMissing();

	/** Entries marked missing */
	int32 GetMissingCount() const;

	/** Whether the manager listens to the Asset Registry's rename, remove and add events */
	bool IsFollowingAssetRegistry() const { return AssetRenamedHandle.IsValid(); }

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

	/**
	 * Fired after HandleBlueprintRenamed moved entries to a Blueprint's new path. Anything else that stores Blueprint
	 * paths next to graph state (a translation archive) follows the rename here.
	 */
	FOnN2CBlueprintPathChanged OnBlueprintPathChanged;

	//~ UObject
	virtual void BeginDestroy() override;

private:
	/** Constructor */
	UN2CGraphStateManager();

	enum class ELoadResult : uint8 { Loaded, Missing, Unreadable, Corrupt, TooNew };

	/** The index of the entry the key names (INDEX_NONE if none, or the GUID-only key is ambiguous) */
	int32 FindGraphStateIndex(const FN2CGraphKey& Key) const;

	/** Brings a parsed file from an older version up to FN2CGraphStateFile::CurrentMajorVersion. @return True if it changed */
	static bool MigrateStateFile(FN2CGraphStateFile& File);

	/** Subscribes to the Asset Registry's rename, remove and add events (once) */
	void FollowAssetRegistry();

	/** Removes the Asset Registry subscriptions */
	void StopFollowingAssetRegistry();

	/** Removes the entry at Index if it holds no tags, translation or export. @return True if removed */
	bool RemoveIfEmpty(int32 Index);

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
	void BroadcastTagRemoved(const FN2CGraphKey& Key, const FString& Tag);
	void BroadcastStateChanged(const FN2CGraphKey& Key);
	void BroadcastTranslationUpdated(const FN2CGraphKey& Key);

	/** Logs and (unless isolated) shows a notification that the state file was set aside; Reason says what was wrong */
	void ReportCorruptFile(const FString& FilePath, bool bMoved, const FString& Reason);

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

	/** Asset Registry subscriptions (FollowAssetRegistry) */
	FDelegateHandle AssetRenamedHandle;
	FDelegateHandle AssetRemovedHandle;
	FDelegateHandle AssetAddedHandle;
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
