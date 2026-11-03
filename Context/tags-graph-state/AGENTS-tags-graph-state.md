# Tags and graph state

Per-graph state NodeToCode keeps for a project: tags, the last translation and the last N2C JSON export of each
Blueprint graph. One owner holds it in memory and on disk; the Tag Manager, the graph overlay, the MCP tag tools, the
Blueprint library and the translation flow all go through it. User view: `wiki/Utilizing-Saved-Translations.md`.

## Owner: `UN2CGraphStateManager`

`Source/Public/Core/N2CGraphStateManager.h`, `Source/Private/Core/N2CGraphStateManager.cpp`. The editor's store
belongs to `UN2CGraphStateSubsystem` (`Source/Public/Subsystems/N2CGraphStateSubsystem.h`, an editor subsystem, card
0346): its `Initialize` creates the manager (outer: the subsystem, not rooted) and loads the state file or migrates
the legacy tags; its `Deinitialize` writes what is pending. `UN2CGraphStateManager::Get()` and `GetIfExists()` return
that store; without an editor (a commandlet) `Get()` makes a stand-alone one, held by a strong pointer and released
by `ShutdownInstance()`, which `Source/Private/NodeToCode.cpp` calls at editor exit (it also flushes the subsystem's
store, since `OnPreExit` can come before `Deinitialize`). `CreateIsolated(StateDirectory)` makes a store for specs;
`UN2CGraphStateSubsystem::SetManagerOverride` puts one in the editor store's place. From Python:
`unreal.get_editor_subsystem(unreal.N2CGraphStateSubsystem)` (`save_now`, `has_unsaved_changes`,
`get_state_file_path`, `get_tag_count`). `UN2CTagSubsystem` initializes after it (`InitializeDependency`).

Writes: every mutator checks it runs on the game thread and calls `RequestSave`. With a save delay
(`SetSaveDelay`; the editor's store uses `UN2CGraphStateSubsystem::EditorSaveDelaySeconds`, 1 s) the requests within
that delay are written once by a core-ticker save; with none (`CreateIsolated`'s default) `RequestSave` saves at
once. `SaveState` writes at once and drops the waiting save; `LoadState` and `Shutdown` write a waiting change first.
Every write is atomic (`FN2CFileUtils::SaveStringAtomically`: a temporary file in the same folder moved over the
old one, which is kept as `.bak`). Spec: `NodeToCode.Core.GraphStateSubsystem`.

Several editors (card 0436): the manager remembers the file as it last read or wrote it (`BaseState`, with the
text's length and CRC). A save that finds a different file merges it in first (`MergeStates`, a three-way merge per
graph: the other writer's state with this manager's changes since `BaseState` on top), keeps the result in memory
and broadcasts `OnGraphStatesReloaded`; `GetMergeCount` counts these. A file it cannot read as the current version
is written over (kept as `.bak`) with a warning. Spec: `NodeToCode.Core.GraphStateMerge`.

| Area | Functions |
|---|---|
| Queries | `FindGraphState`, `FindKeysForGuid`, `GetGraphsWithTag`, `GetAllTagNames`, `GetAllCategories`, `GraphHasTag` |
| Tags | `AddTag`, `RemoveTag`, `RemoveTagByName`, `RemoveAllTagsFromGraph`, `ClearAllTags`, `RemoveTags` and `RestoreTags` (undo; `FN2CTagRemovalRequest`) |
| Translations | `SetTranslationState`, `RecordArchivedTranslation`, `RecordSavedTranslation`, `ClearTranslationState`, `HasTranslation`, `LoadTranslation`, `LoadN2CJson` |
| JSON export | `SetJsonExportState`, `HasJsonExport` |
| Lifecycle | `GetOrCreateGraphState`, `RemoveGraphState`, `BeginBulkUpdate`/`EndBulkUpdate` (RAII: `FN2CGraphStateBulkUpdate`) |
| Persistence | `SaveState`, `RequestSave`, `SetSaveDelay`, `HasPendingSave`, `LoadState`, `IsSavingBlocked`, `GetQuarantinedFilePath`, `GetStateFilePath`, `GetSaveCount` |
| Asset Registry | `HandleBlueprintRenamed`, `HandleBlueprintRemoved`, `HandleBlueprintAdded`, `RefreshMissing`, `PruneMissing`, `IsFollowingAssetRegistry` |

Events: `OnGraphStateChanged`, `OnGraphTagAdded`, `OnGraphTagRemoved`, `OnGraphTranslationUpdated`,
`OnGraphStatesReloaded`, `OnBlueprintPathChanged`.

## Data model

`Source/Public/Core/N2CGraphStateTypes.h`:

- `FN2CGraphKey`: `{FSoftObjectPath Blueprint; FGuid GraphGuid}`, the identity of a graph (`FromGraph`,
  `FromStrings`, `NormalizeBlueprintPath`, `ToString`). Two Blueprints can hold graphs with the same GUID (a
  duplicated asset), so the Blueprint path is part of the key. A bare `FGuid` converts to a GUID-only key, which
  finds an entry only while exactly one entry has that GUID; callers that only have a GUID must handle the
  ambiguous case (`N2CPythonBridge.cpp` refuses it).
- `FN2CGraphState`: GraphGuid, GraphName, OwningBlueprint, `Tags` (`FN2CTagEntry`, category normalized by
  `NormalizeCategory`), `Translation` (`FN2CTranslationState`), `JsonExport` (`FN2CJsonExportState`) and `bMissing`.
- `FN2CGraphStateFile`: the file, `CurrentMajorVersion` 3 (`"3.0"`); `FromJsonString` refuses a file of the wrong
  shape.
- Tag Manager models (statuses, filters, tree and list items, UI events):
  `Source/Public/TagManager/Models/N2CTagManagerTypes.h`.

## On disk

| File | What |
|---|---|
| `Saved/NodeToCode/BlueprintGraphState.json` | The state (`GetStateDirectory()` is `<ProjectSaved>/NodeToCode`) |
| `BlueprintGraphState.json.bak` | The previous file, kept by every save |
| `BlueprintGraphState.json.corrupt-<UTC time>` | A file that did not parse, set aside; saving then stays off until `LoadState` or `ClearAllState` |
| `Saved/NodeToCode/Tags/BlueprintTags.json` | 1.x tags: migrated once by `MigrateFromLegacyTags`, then renamed `.migrated` |

Writes go through `FN2CFileUtils::SaveStringAtomically` (`Source/Public/Utils/N2CFileUtils.h`: `.tmp`, then `.bak`,
then a move; `FinishInterruptedWrite` completes a crash between the two). Loading reports
`Loaded`, `Missing`, `Unreadable`, `Corrupt` or `TooNew`: a version 2 file (GUID-only keys) is migrated to 3; a file
from a newer version is left alone and saving stays off, so an older plugin never overwrites it.

## Facades and callers

- `UN2CGraphStateManager` is the one tag store (card 0345 removed the 1.x `UN2CTagManager` facade). Its tag
  projections are `GetAllTags`, `GetTaggedGraphs`, `FindTaggedGraphs`, `FindTaggedGraphsInCategory` and
  `RemoveTagByName`; `OnGraphTagAdded` carries `(FN2CTagEntry, FN2CGraphRef)`, with `FN2CGraphRef` and `FN2CTagInfo`
  in `Source/Public/Core/N2CTagTypes.h`. `SN2CGraphOverlay` and `SN2CTagManager` subscribe to the store's events.
- `UN2CTagSubsystem` (`Source/Public/Subsystems/N2CTagSubsystem.h`): an editor subsystem with dynamic delegates for
  Blueprint and UMG (`FOnN2CTagAdded`, `FOnN2CTagRemoved`, `FOnN2CTagsLoaded`, `FOnN2CTagsSaved`),
  `GetFocusedGraphInfo` and `RefreshTags`.
- `UN2CTagBlueprintLibrary` (`Source/Public/BlueprintLibraries/N2CTagBlueprintLibrary.h`, `FN2CTagInfo`):
  `TagFocusedBlueprintGraph`, `TagBlueprintGraph`, `RemoveTag`, `GetAllTagNames`, `GetAllCategories`, `GraphHasTag`,
  `SaveTags`, `LoadTags`, `GetTagStatistics`, `HasTranslation`, `LoadTranslation`, `LoadTranslationFromTagInfo`.
- MCP: `tag-blueprint-graph`, `list-blueprint-tags` and `remove-tag-from-graph` in
  `Source/Private/MCP/Tools/Implementations/Blueprint/Organization/` (toolset `BlueprintTag`; its
  [AGENTS.md](../../Source/Private/MCP/Tools/Implementations/Blueprint/Organization/AGENTS.md)), helpers in
  `Source/Private/MCP/Utils/N2CMcpTagUtils.h`.
- Translations: `SN2CMainWindow` and `UN2CBatchTranslationOrchestrator` call `RecordArchivedTranslation` (card 0253),
  which stores, beside the folder, provider, model and language id, the archive run (`runId`, `manifestPath`: the run's
  `manifest.json`), the root entry's exact files (`files.implementation`, `.declaration`, `.notes`) and the request's
  `usage` (input and output tokens); paths are project-relative inside the project, absolute outside it.
  `LoadTranslation` reads the recorded files; a record without them (older builds, `RecordSavedTranslation`) still
  loads by the file-name rules. `SN2CTranslationViewer` reads the saved N2C JSON with `LoadN2CJson`. Every translation
  also lands in a run folder of `FN2CTranslationArchive` (`Source/Public/LLM/N2CTranslationArchive.h`, a manifest per
  run).

## Specs

`NodeToCode.State` (`Source/Private/Tests/Core/N2CGraphState.spec.cpp`), `NodeToCode.Robustness.GraphStatePersistence`
(`N2CGraphStateTests.cpp`), `NodeToCode.Robustness.GraphStateIdentity` and `.GraphStateVersion`
(`N2CGraphStateIdentityTests.cpp`), `NodeToCode.Robustness.TagEvents` (`N2CTagTests.cpp`),
`NodeToCode.Tags.GraphsAndFilters`, `NodeToCode.Tags.LibraryRemoveTag`, `NodeToCode.UI.TagManager`
(`Source/Private/Tests/Core/`), `NodeToCode.MCP.PythonBridgeTags`, `NodeToCode.Translation.Archive`. Use
`CreateIsolated` so a spec never touches the project's state file.
