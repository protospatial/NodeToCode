// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Utils/N2CFileUtils.h"
#include "Utils/N2CPathUtils.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	const TCHAR* const StateFileName = TEXT("BlueprintGraphState.json");
}

UN2CGraphStateManager* UN2CGraphStateManager::Instance = nullptr;

UN2CGraphStateManager& UN2CGraphStateManager::Get()
{
	if (!Instance)
	{
		Instance = NewObject<UN2CGraphStateManager>(GetTransientPackage(), UN2CGraphStateManager::StaticClass(), NAME_None, RF_Standalone);
		Instance->AddToRoot(); // Prevent garbage collection
		Instance->Initialize();
	}
	return *Instance;
}

UN2CGraphStateManager::UN2CGraphStateManager()
	: bIsDirty(false)
	, bMigrationAttempted(false)
{
}

void UN2CGraphStateManager::Initialize()
{
	FN2CLogger::Get().Log(TEXT("Initializing Graph State Manager"), EN2CLogSeverity::Info);

	if (LoadStateInternal() != ELoadResult::Missing)
	{
		// Loaded, or set aside / unreadable: either way the legacy tags must not replace it
		return;
	}

	// A state file existed before (a backup or a set-aside corrupt copy is next to it), so the legacy tags are older
	// than what the user had: do not rebuild state from them
	const FString StateFilePath = GetStateFilePath();
	TArray<FString> CorruptCopies;
	IFileManager::Get().FindFiles(CorruptCopies, *(StateFilePath + TEXT(".corrupt-*")), true, false);
	if (CorruptCopies.Num() > 0 || FPaths::FileExists(FN2CFileUtils::BackupPathFor(StateFilePath)))
	{
		FN2CLogger::Get().Log(FString::Printf(
			TEXT("No graph state file, but earlier copies of it are in %s; legacy tags are not migrated over them"),
			*GetStateDirectory()), EN2CLogSeverity::Warning);
		return;
	}

	MigrateFromLegacyTags();
}

void UN2CGraphStateManager::Shutdown()
{
	FN2CLogger::Get().Log(TEXT("Shutting down Graph State Manager"), EN2CLogSeverity::Info);

	if (bIsDirty)
	{
		SaveState();
	}

	if (Instance == this)
	{
		RemoveFromRoot();
		Instance = nullptr;
	}
}

void UN2CGraphStateManager::ShutdownInstance()
{
	if (Instance)
	{
		Instance->Shutdown();
	}
}

UN2CGraphStateManager* UN2CGraphStateManager::CreateIsolated(const FString& StateDirectory)
{
	UN2CGraphStateManager* Manager = NewObject<UN2CGraphStateManager>(GetTransientPackage(),
		UN2CGraphStateManager::StaticClass(), NAME_None, RF_Transient);
	Manager->StateDirectoryOverride = FPaths::ConvertRelativePathToFull(StateDirectory);
	Manager->bNotifyUser = false;
	Manager->Initialize();
	return Manager;
}

// ============================================================================
// Graph State Queries
// ============================================================================

FN2CGraphState* UN2CGraphStateManager::FindGraphState(const FGuid& GraphGuid)
{
	for (FN2CGraphState& State : StateFile.Graphs)
	{
		if (State.GraphGuid == GraphGuid)
		{
			return &State;
		}
	}
	return nullptr;
}

const FN2CGraphState* UN2CGraphStateManager::FindGraphState(const FGuid& GraphGuid) const
{
	for (const FN2CGraphState& State : StateFile.Graphs)
	{
		if (State.GraphGuid == GraphGuid)
		{
			return &State;
		}
	}
	return nullptr;
}

TArray<FN2CGraphState> UN2CGraphStateManager::GetGraphsWithTag(const FString& Tag, const FString& Category) const
{
	TArray<FN2CGraphState> Result;

	for (const FN2CGraphState& State : StateFile.Graphs)
	{
		if (State.HasTag(Tag, Category))
		{
			Result.Add(State);
		}
	}

	return Result;
}

TArray<FN2CGraphState> UN2CGraphStateManager::GetGraphsInCategory(const FString& Category) const
{
	TArray<FN2CGraphState> Result;

	for (const FN2CGraphState& State : StateFile.Graphs)
	{
		if (State.GetTagsInCategory(Category).Num() > 0)
		{
			Result.Add(State);
		}
	}

	return Result;
}

TArray<FN2CGraphState> UN2CGraphStateManager::GetGraphsWithTranslation() const
{
	TArray<FN2CGraphState> Result;

	for (const FN2CGraphState& State : StateFile.Graphs)
	{
		if (State.HasTranslation())
		{
			Result.Add(State);
		}
	}

	return Result;
}

TArray<FString> UN2CGraphStateManager::GetAllTagNames() const
{
	TSet<FString> UniqueNames;

	for (const FN2CGraphState& State : StateFile.Graphs)
	{
		for (const FN2CTagEntry& Tag : State.Tags)
		{
			UniqueNames.Add(Tag.Tag);
		}
	}

	return UniqueNames.Array();
}

TArray<FString> UN2CGraphStateManager::GetAllCategories() const
{
	TSet<FString> UniqueCategories;

	for (const FN2CGraphState& State : StateFile.Graphs)
	{
		for (const FN2CTagEntry& Tag : State.Tags)
		{
			UniqueCategories.Add(Tag.Category);
		}
	}

	return UniqueCategories.Array();
}

// ============================================================================
// Tag Operations
// ============================================================================

bool UN2CGraphStateManager::AddTag(const FGuid& GraphGuid, const FString& GraphName,
	const FSoftObjectPath& Blueprint, const FString& Tag,
	const FString& InCategory, const FString& Description)
{
	// A tag is always stored under a real category: an empty one means "Default", never "any category"
	const FString Category = FN2CTagEntry::NormalizeCategory(InCategory);

	// Get or create the graph state
	FN2CGraphState& State = GetOrCreateGraphState(GraphGuid, GraphName, Blueprint);

	// Check if this tag already exists
	for (const FN2CTagEntry& ExistingTag : State.Tags)
	{
		if (ExistingTag.MatchesTagExactly(Tag, Category))
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("Tag '%s' in category '%s' already exists for graph %s"),
				*Tag, *Category, *GraphGuid.ToString()),
				EN2CLogSeverity::Warning);
			if (bIsDirty)
			{
				// GetOrCreateGraphState may have updated the graph name or Blueprint path
				SaveState();
			}
			return true; // Desired state achieved
		}
	}

	// Add the new tag
	FN2CTagEntry NewTag;
	NewTag.Tag = Tag;
	NewTag.Category = Category;
	NewTag.Description = Description;
	NewTag.Timestamp = FDateTime::UtcNow();

	State.Tags.Add(NewTag);
	MarkDirty();

	FN2CLogger::Get().Log(FString::Printf(TEXT("Added tag '%s' in category '%s' to graph %s"),
		*Tag, *Category, *GraphGuid.ToString()),
		EN2CLogSeverity::Info);

	// Fire delegates
	BroadcastTagAdded(State);
	BroadcastStateChanged(GraphGuid);

	// Auto-save
	SaveState();

	return true;
}

bool UN2CGraphStateManager::AddTag(const FN2CTaggedBlueprintGraph& TaggedGraph)
{
	return AddTag(TaggedGraph.GraphGuid, TaggedGraph.GraphName,
		TaggedGraph.OwningBlueprint, TaggedGraph.Tag,
		TaggedGraph.Category, TaggedGraph.Description);
}

bool UN2CGraphStateManager::RemoveTag(const FGuid& GraphGuid, const FString& Tag, const FString& Category)
{
	FN2CGraphState* State = FindGraphState(GraphGuid);
	if (!State)
	{
		return false;
	}

	bool bRemoved = false;
	for (int32 i = State->Tags.Num() - 1; i >= 0; --i)
	{
		if (State->Tags[i].MatchesTag(Tag, Category))
		{
			State->Tags.RemoveAt(i);
			bRemoved = true;
			break;
		}
	}

	if (bRemoved)
	{
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Removed tag '%s' in category '%s' from graph %s"),
			*Tag, *Category, *GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		// Fire delegates
		BroadcastTagRemoved(GraphGuid, Tag);
		BroadcastStateChanged(GraphGuid);

		// Clean up empty graph states (no tags, no translation, no export)
		if (State->Tags.Num() == 0 && !State->HasTranslation() && !State->HasJsonExport())
		{
			RemoveGraphState(GraphGuid);
		}

		SaveState();
	}

	return bRemoved;
}

int32 UN2CGraphStateManager::RemoveTagByName(const FGuid& GraphGuid, const FString& Tag)
{
	FN2CGraphState* State = FindGraphState(GraphGuid);
	if (!State)
	{
		return 0;
	}

	int32 RemovedCount = 0;
	for (int32 i = State->Tags.Num() - 1; i >= 0; --i)
	{
		if (State->Tags[i].Tag.Equals(Tag, ESearchCase::IgnoreCase))
		{
			State->Tags.RemoveAt(i);
			RemovedCount++;
		}
	}

	if (RemovedCount > 0)
	{
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Removed %d instances of tag '%s' from graph %s"),
			RemovedCount, *Tag, *GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		BroadcastTagRemoved(GraphGuid, Tag);
		BroadcastStateChanged(GraphGuid);

		// Clean up empty graph states
		if (State->Tags.Num() == 0 && !State->HasTranslation() && !State->HasJsonExport())
		{
			RemoveGraphState(GraphGuid);
		}

		SaveState();
	}

	return RemovedCount;
}

int32 UN2CGraphStateManager::RemoveAllTagsFromGraph(FGuid GraphGuid)
{
	FN2CGraphState* State = FindGraphState(GraphGuid);
	if (!State)
	{
		return 0;
	}

	int32 RemovedCount = State->Tags.Num();

	// Fire delegate for each removed tag
	for (const FN2CTagEntry& Tag : State->Tags)
	{
		BroadcastTagRemoved(GraphGuid, Tag.Tag);
	}

	State->Tags.Empty();

	if (RemovedCount > 0)
	{
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Removed %d tags from graph %s"),
			RemovedCount, *GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		BroadcastStateChanged(GraphGuid);

		// Clean up empty graph states
		if (!State->HasTranslation() && !State->HasJsonExport())
		{
			RemoveGraphState(GraphGuid);
		}

		SaveState();
	}

	return RemovedCount;
}

int32 UN2CGraphStateManager::ClearAllTags()
{
	FN2CGraphStateBulkUpdate Bulk(*this);

	int32 RemovedCount = 0;
	for (FN2CGraphState& State : StateFile.Graphs)
	{
		RemovedCount += State.Tags.Num();
		State.Tags.Empty();
	}
	if (RemovedCount == 0)
	{
		return 0;
	}

	const int32 GraphsRemoved = StateFile.Graphs.RemoveAll([](const FN2CGraphState& State)
	{
		return State.Tags.Num() == 0 && !State.HasTranslation() && !State.HasJsonExport();
	});
	MarkDirty();

	FN2CLogger::Get().Log(FString::Printf(TEXT("Cleared %d tags (%d graphs had nothing else and were removed)"),
		RemovedCount, GraphsRemoved), EN2CLogSeverity::Info);

	// Saved once when Bulk ends, which also broadcasts OnGraphStatesReloaded once
	SaveState();
	return RemovedCount;
}

bool UN2CGraphStateManager::GraphHasTag(const FGuid& GraphGuid, const FString& Tag, const FString& Category) const
{
	const FN2CGraphState* State = FindGraphState(GraphGuid);
	return State && State->HasTag(Tag, Category);
}

TArray<FN2CTagEntry> UN2CGraphStateManager::GetTagsForGraph(const FGuid& GraphGuid) const
{
	const FN2CGraphState* State = FindGraphState(GraphGuid);
	if (State)
	{
		return State->Tags;
	}
	return TArray<FN2CTagEntry>();
}

TArray<FN2CTaggedBlueprintGraph> UN2CGraphStateManager::GetAllTagsLegacy() const
{
	TArray<FN2CTaggedBlueprintGraph> Result;

	for (const FN2CGraphState& State : StateFile.Graphs)
	{
		for (const FN2CTagEntry& TagEntry : State.Tags)
		{
			FN2CTaggedBlueprintGraph LegacyTag;
			LegacyTag.Tag = TagEntry.Tag;
			LegacyTag.Category = TagEntry.Category;
			LegacyTag.Description = TagEntry.Description;
			LegacyTag.GraphGuid = State.GraphGuid;
			LegacyTag.GraphName = State.GraphName;
			LegacyTag.OwningBlueprint = State.OwningBlueprint;
			LegacyTag.Timestamp = TagEntry.Timestamp;
			Result.Add(LegacyTag);
		}
	}

	return Result;
}

// ============================================================================
// Translation State Operations
// ============================================================================

void UN2CGraphStateManager::SetTranslationState(const FGuid& GraphGuid, const FN2CTranslationState& State)
{
	FN2CGraphState* GraphState = FindGraphState(GraphGuid);
	if (GraphState)
	{
		GraphState->Translation = State;
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Updated translation state for graph %s"),
			*GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		BroadcastTranslationUpdated(GraphGuid);
		BroadcastStateChanged(GraphGuid);

		SaveState();
	}
}

void UN2CGraphStateManager::SetTranslationState(const FGuid& GraphGuid, const FString& GraphName,
	const FSoftObjectPath& Blueprint, const FString& OutputPath,
	const FString& Provider, const FString& Model, const FString& Language,
	const FN2CTranslationSummary& Summary)
{
	FN2CGraphState& State = GetOrCreateGraphState(GraphGuid, GraphName, Blueprint);

	State.Translation.bExists = true;
	State.Translation.OutputPath = OutputPath;
	State.Translation.Timestamp = FDateTime::UtcNow();
	State.Translation.Provider = Provider;
	State.Translation.Model = Model;
	State.Translation.Language = Language;
	State.Translation.Summary = Summary;

	MarkDirty();

	FN2CLogger::Get().Log(FString::Printf(TEXT("Set translation state for graph %s: %s"),
		*GraphGuid.ToString(), *OutputPath),
		EN2CLogSeverity::Info);

	BroadcastTranslationUpdated(GraphGuid);
	BroadcastStateChanged(GraphGuid);

	SaveState();
}

void UN2CGraphStateManager::RecordSavedTranslation(const FGuid& GraphGuid, const FString& GraphName,
	const FSoftObjectPath& Blueprint, const FString& GraphDirectory,
	const FString& Provider, const FString& Model, EN2CCodeLanguage Language,
	const FN2CTranslationSummary& Summary)
{
	SetTranslationState(GraphGuid, GraphName, Blueprint, FN2CPathUtils::MakeTranslationRecordPath(GraphDirectory),
		Provider, Model, FN2CPathUtils::GetLanguageId(Language), Summary);
}

void UN2CGraphStateManager::ClearTranslationState(const FGuid& GraphGuid)
{
	FN2CGraphState* State = FindGraphState(GraphGuid);
	if (State && State->Translation.bExists)
	{
		State->Translation = FN2CTranslationState();
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Cleared translation state for graph %s"),
			*GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		BroadcastTranslationUpdated(GraphGuid);
		BroadcastStateChanged(GraphGuid);

		// Clean up empty graph states
		if (State->Tags.Num() == 0 && !State->HasJsonExport())
		{
			RemoveGraphState(GraphGuid);
		}

		SaveState();
	}
}

bool UN2CGraphStateManager::HasTranslation(const FGuid& GraphGuid) const
{
	const FN2CGraphState* State = FindGraphState(GraphGuid);
	return State && State->HasTranslation();
}

bool UN2CGraphStateManager::LoadTranslation(const FGuid& GraphGuid, FN2CGraphTranslation& OutTranslation) const
{
	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
	return LoadTranslation(GraphGuid, OutTranslation, Language);
}

bool UN2CGraphStateManager::LoadTranslation(const FGuid& GraphGuid, FN2CGraphTranslation& OutTranslation, EN2CCodeLanguage& OutLanguage) const
{
	OutLanguage = EN2CCodeLanguage::Cpp;
	const FN2CGraphState* State = FindGraphState(GraphGuid);
	if (!State || !State->HasTranslation())
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("No translation found for graph %s"),
			*GraphGuid.ToString()),
			EN2CLogSeverity::Warning);
		return false;
	}

	// Absolute, or relative to the project folder
	const FString FullPath = FN2CPathUtils::ResolveTranslationRecordPath(State->Translation.OutputPath);

	// The recorded language picks the extension; an unknown id reads as C++ (what older builds did)
	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
	if (!FN2CPathUtils::ParseLanguageId(State->Translation.Language, Language) && !State->Translation.Language.IsEmpty())
	{
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Unknown translation language '%s' for graph %s, loading it as C++"),
			*State->Translation.Language, *State->GraphName));
	}
	const TArray<FString> Extensions = FN2CPathUtils::GetTranslationFileExtensions(Language);

	// Same stem the save path used (identical to the raw name for names without path characters)
	FString Stem = FN2CPathUtils::GetTranslationFileStem(State->GraphName);
	FString ImplementationPath;
	for (const FString& Extension : Extensions)
	{
		const FString Candidate = FPaths::Combine(FullPath, Stem + Extension);
		if (FPaths::FileExists(Candidate))
		{
			ImplementationPath = Candidate;
			break;
		}
	}
	if (ImplementationPath.IsEmpty())
	{
		// The single translate names the file after the graph name in the LLM's response: take the folder's only
		// implementation file of this language, if there is exactly one
		for (const FString& Extension : Extensions)
		{
			TArray<FString> Found;
			IFileManager::Get().FindFiles(Found, *FPaths::Combine(FullPath, TEXT("*") + Extension), true, false);
			Found.RemoveAll([](const FString& Name) { return Name.EndsWith(TEXT("_Notes.txt"), ESearchCase::IgnoreCase); });
			if (Found.Num() == 1)
			{
				ImplementationPath = FPaths::Combine(FullPath, Found[0]);
				Stem = FPaths::GetBaseFilename(Found[0]);
				break;
			}
		}
	}
	if (ImplementationPath.IsEmpty())
	{
		ImplementationPath = FPaths::Combine(FullPath, Stem + Extensions[0]);
	}
	const FString DeclarationPath = FPaths::Combine(FullPath, Stem + TEXT(".h"));
	const FString NotesPath = FPaths::Combine(FullPath, Stem + TEXT("_Notes.txt"));

	// Load declaration (header) if it exists
	FString Declaration;
	if (FPaths::FileExists(DeclarationPath))
	{
		FFileHelper::LoadFileToString(Declaration, *DeclarationPath);
	}

	// Load implementation
	FString Implementation;
	if (!FFileHelper::LoadFileToString(Implementation, *ImplementationPath))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to load translation implementation from: %s"),
			*ImplementationPath));
		return false;
	}

	// Load notes if they exist
	FString Notes;
	if (FPaths::FileExists(NotesPath))
	{
		FFileHelper::LoadFileToString(Notes, *NotesPath);
	}

	// Populate the output structure
	OutTranslation.GraphName = State->GraphName;
	OutTranslation.GraphType = TEXT("Function"); // Default, could be stored in state if needed
	OutTranslation.GraphClass = TEXT(""); // Could be extracted from declaration
	OutTranslation.Code.GraphDeclaration = Declaration;
	OutTranslation.Code.GraphImplementation = Implementation;
	OutTranslation.Code.ImplementationNotes = Notes;
	OutLanguage = Language;

	FN2CLogger::Get().Log(FString::Printf(TEXT("Loaded translation for graph %s from %s"),
		*State->GraphName, *FullPath),
		EN2CLogSeverity::Info);

	return true;
}

bool UN2CGraphStateManager::LoadN2CJson(const FGuid& GraphGuid, FString& OutJsonContent) const
{
	const FN2CGraphState* State = FindGraphState(GraphGuid);
	if (!State || !State->HasTranslation())
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("No translation found for graph %s when loading JSON"),
			*GraphGuid.ToString()),
			EN2CLogSeverity::Warning);
		return false;
	}

	// Absolute, or relative to the project folder
	const FString FullPath = FN2CPathUtils::ResolveTranslationRecordPath(State->Translation.OutputPath);

	// Try multiple JSON filename patterns:
	// 1. Batch translation format: {GraphName}.json
	// 2. Individual translation format: N2C_BP_{FolderName}.json
	TArray<FString> PossibleJsonPaths;
	PossibleJsonPaths.Add(FPaths::Combine(FullPath, FN2CPathUtils::GetTranslationFileStem(State->GraphName) + TEXT(".json")));
	PossibleJsonPaths.Add(FPaths::Combine(FullPath, TEXT("N2C_BP_") + FPaths::GetBaseFilename(FullPath) + TEXT(".json")));

	FString FoundJsonPath;
	for (const FString& JsonPath : PossibleJsonPaths)
	{
		if (FPaths::FileExists(JsonPath))
		{
			FoundJsonPath = JsonPath;
			break;
		}
	}

	if (FoundJsonPath.IsEmpty())
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("JSON file not found for graph %s. Tried paths: %s, %s"),
			*State->GraphName, *PossibleJsonPaths[0], *PossibleJsonPaths[1]),
			EN2CLogSeverity::Warning);
		return false;
	}

	if (!FFileHelper::LoadFileToString(OutJsonContent, *FoundJsonPath))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to load JSON from: %s"),
			*FoundJsonPath));
		return false;
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Loaded N2C JSON for graph %s from %s"),
		*State->GraphName, *FoundJsonPath),
		EN2CLogSeverity::Info);

	return true;
}

// ============================================================================
// JSON Export State Operations
// ============================================================================

void UN2CGraphStateManager::SetJsonExportState(const FGuid& GraphGuid, const FN2CJsonExportState& State)
{
	FN2CGraphState* GraphState = FindGraphState(GraphGuid);
	if (GraphState)
	{
		GraphState->JsonExport = State;
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Updated JSON export state for graph %s"),
			*GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		BroadcastStateChanged(GraphGuid);

		SaveState();
	}
}

void UN2CGraphStateManager::SetJsonExportState(const FGuid& GraphGuid, const FString& GraphName,
	const FSoftObjectPath& Blueprint, const FString& OutputPath, bool bMinified)
{
	FN2CGraphState& State = GetOrCreateGraphState(GraphGuid, GraphName, Blueprint);

	State.JsonExport.bExists = true;
	State.JsonExport.OutputPath = OutputPath;
	State.JsonExport.Timestamp = FDateTime::UtcNow();
	State.JsonExport.bMinified = bMinified;

	MarkDirty();

	FN2CLogger::Get().Log(FString::Printf(TEXT("Set JSON export state for graph %s: %s"),
		*GraphGuid.ToString(), *OutputPath),
		EN2CLogSeverity::Info);

	BroadcastStateChanged(GraphGuid);

	SaveState();
}

void UN2CGraphStateManager::ClearJsonExportState(const FGuid& GraphGuid)
{
	FN2CGraphState* State = FindGraphState(GraphGuid);
	if (State && State->JsonExport.bExists)
	{
		State->JsonExport = FN2CJsonExportState();
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Cleared JSON export state for graph %s"),
			*GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		BroadcastStateChanged(GraphGuid);

		// Clean up empty graph states
		if (State->Tags.Num() == 0 && !State->HasTranslation())
		{
			RemoveGraphState(GraphGuid);
		}

		SaveState();
	}
}

bool UN2CGraphStateManager::HasJsonExport(const FGuid& GraphGuid) const
{
	const FN2CGraphState* State = FindGraphState(GraphGuid);
	return State && State->HasJsonExport();
}

// ============================================================================
// Graph State Lifecycle
// ============================================================================

FN2CGraphState& UN2CGraphStateManager::GetOrCreateGraphState(const FGuid& GraphGuid,
	const FString& GraphName, const FSoftObjectPath& Blueprint)
{
	// Try to find existing state
	FN2CGraphState* ExistingState = FindGraphState(GraphGuid);
	if (ExistingState)
	{
		// Update graph name and blueprint path if they changed
		if (ExistingState->GraphName != GraphName || ExistingState->OwningBlueprint != Blueprint)
		{
			ExistingState->GraphName = GraphName;
			ExistingState->OwningBlueprint = Blueprint;
			MarkDirty();
		}
		return *ExistingState;
	}

	// Create new state
	FN2CGraphState NewState;
	NewState.GraphGuid = GraphGuid;
	NewState.GraphName = GraphName;
	NewState.OwningBlueprint = Blueprint;

	int32 Index = StateFile.Graphs.Add(NewState);
	MarkDirty();

	FN2CLogger::Get().Log(FString::Printf(TEXT("Created graph state for %s (%s)"),
		*GraphName, *GraphGuid.ToString()),
		EN2CLogSeverity::Info);

	return StateFile.Graphs[Index];
}

bool UN2CGraphStateManager::RemoveGraphState(FGuid GraphGuid)
{
	for (int32 i = StateFile.Graphs.Num() - 1; i >= 0; --i)
	{
		if (StateFile.Graphs[i].GraphGuid == GraphGuid)
		{
			FString GraphName = StateFile.Graphs[i].GraphName;
			StateFile.Graphs.RemoveAt(i);
			MarkDirty();

			FN2CLogger::Get().Log(FString::Printf(TEXT("Removed graph state for %s (%s)"),
				*GraphName, *GraphGuid.ToString()),
				EN2CLogSeverity::Info);

			return true;
		}
	}
	return false;
}

void UN2CGraphStateManager::ClearAllState()
{
	int32 Count = StateFile.Graphs.Num();
	StateFile.Graphs.Empty();
	MarkDirty();

	FN2CLogger::Get().Log(FString::Printf(TEXT("Cleared all %d graph states"), Count),
		EN2CLogSeverity::Info);

	if (bSavingBlocked)
	{
		bSavingBlocked = false;
		FN2CLogger::Get().Log(TEXT("Graph state was reset, so saving is on again"), EN2CLogSeverity::Info);
	}

	if (IsInBulkUpdate())
	{
		bBulkChanged = true;
	}
	else
	{
		OnGraphStatesReloaded.Broadcast();
	}
	SaveState();
}

// ============================================================================
// Bulk updates and broadcasts
// ============================================================================

void UN2CGraphStateManager::BeginBulkUpdate()
{
	++BulkUpdateDepth;
}

void UN2CGraphStateManager::EndBulkUpdate()
{
	if (!ensureMsgf(BulkUpdateDepth > 0, TEXT("EndBulkUpdate without BeginBulkUpdate")))
	{
		return;
	}
	if (--BulkUpdateDepth > 0)
	{
		return;
	}

	const bool bChanged = bBulkChanged;
	const bool bSaveRequested = bBulkSavePending;
	bBulkChanged = false;
	bBulkSavePending = false;

	if (bSaveRequested || (bChanged && bIsDirty))
	{
		SaveState();
	}
	if (bChanged)
	{
		OnGraphStatesReloaded.Broadcast();
	}
}

void UN2CGraphStateManager::BroadcastTagAdded(const FN2CGraphState& State)
{
	if (IsInBulkUpdate())
	{
		bBulkChanged = true;
		return;
	}
	OnGraphTagAdded.Broadcast(State);
}

void UN2CGraphStateManager::BroadcastTagRemoved(const FGuid& GraphGuid, const FString& Tag)
{
	if (IsInBulkUpdate())
	{
		bBulkChanged = true;
		return;
	}
	OnGraphTagRemoved.Broadcast(GraphGuid, Tag);
}

void UN2CGraphStateManager::BroadcastStateChanged(const FGuid& GraphGuid)
{
	if (IsInBulkUpdate())
	{
		bBulkChanged = true;
		return;
	}
	OnGraphStateChanged.Broadcast(GraphGuid);
}

void UN2CGraphStateManager::BroadcastTranslationUpdated(const FGuid& GraphGuid)
{
	if (IsInBulkUpdate())
	{
		bBulkChanged = true;
		return;
	}
	OnGraphTranslationUpdated.Broadcast(GraphGuid);
}

// ============================================================================
// Persistence
// ============================================================================

FString UN2CGraphStateManager::GetStateDirectory() const
{
	if (!StateDirectoryOverride.IsEmpty())
	{
		return StateDirectoryOverride;
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode")));
}

FString UN2CGraphStateManager::GetStateFilePath() const
{
	const FString StateDir = GetStateDirectory();

	// Ensure directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*StateDir);

	return FPaths::Combine(StateDir, StateFileName);
}

FString UN2CGraphStateManager::GetLegacyTagsFilePath() const
{
	return FPaths::Combine(GetStateDirectory(), TEXT("Tags"), TEXT("BlueprintTags.json"));
}

void UN2CGraphStateManager::MarkDirty()
{
	bIsDirty = true;
	if (IsInBulkUpdate())
	{
		bBulkChanged = true;
	}
}

bool UN2CGraphStateManager::SaveState()
{
	if (IsInBulkUpdate())
	{
		// Written once when the outermost bulk update ends
		bBulkSavePending = true;
		return true;
	}

	const FString FilePath = GetStateFilePath();

	if (bSavingBlocked)
	{
		FN2CLogger::Get().LogWarning(FString::Printf(
			TEXT("Graph state was not saved to %s: the previous file could not be read%s. Reload the tags (restart the editor, or Load Tags) or reset them to turn saving back on."),
			*FilePath,
			QuarantinedFilePath.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" and was kept as %s"), *QuarantinedFilePath)));
		return false;
	}

	StateFile.LastSaved = FDateTime::UtcNow();
	FString JsonString = StateFile.ToJsonString(true);

	if (!FN2CFileUtils::SaveStringAtomically(JsonString, FilePath))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save graph state to file: %s"), *FilePath));
		return false;
	}

	bIsDirty = false;
	++SaveCount;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Saved %d graph states to %s"),
		StateFile.Graphs.Num(), *FilePath),
		EN2CLogSeverity::Info);

	return true;
}

bool UN2CGraphStateManager::LoadState()
{
	const ELoadResult Result = LoadStateInternal();

	if ((Result == ELoadResult::Loaded || Result == ELoadResult::Missing) && bSavingBlocked)
	{
		bSavingBlocked = false;
		FN2CLogger::Get().Log(TEXT("Graph state reloaded, so saving is on again"), EN2CLogSeverity::Info);
	}

	if (Result == ELoadResult::Loaded)
	{
		if (IsInBulkUpdate())
		{
			bBulkChanged = true;
		}
		else
		{
			OnGraphStatesReloaded.Broadcast();
		}
	}

	return Result == ELoadResult::Loaded;
}

UN2CGraphStateManager::ELoadResult UN2CGraphStateManager::LoadStateInternal()
{
	const FString FilePath = GetStateFilePath();

	// A save that stopped after the temporary file was complete: finish it
	FN2CFileUtils::FinishInterruptedWrite(FilePath, [](const FString& Content)
	{
		FN2CGraphStateFile Probe;
		return FN2CGraphStateFile::FromJsonString(Content, Probe);
	});

	// Check if file exists
	if (!FPaths::FileExists(FilePath))
	{
		FN2CLogger::Get().Log(TEXT("No graph state file found"), EN2CLogSeverity::Info);
		return ELoadResult::Missing;
	}

	// Load file content
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		// Locked or unreadable: keep it as it is and do not write over it
		bSavingBlocked = true;
		QuarantinedFilePath.Reset();
		FN2CLogger::Get().LogError(FString::Printf(
			TEXT("Failed to read graph state from %s; tag changes are not saved until it can be read and the tags are reloaded"),
			*FilePath));
		return ELoadResult::Unreadable;
	}

	// Parse into a copy so a bad file never replaces the state in memory
	FN2CGraphStateFile Loaded;
	if (!FN2CGraphStateFile::FromJsonString(JsonString, Loaded))
	{
		FString CorruptPath;
		const bool bMoved = FN2CFileUtils::QuarantineCorruptFile(FilePath, CorruptPath);
		QuarantinedFilePath = CorruptPath;
		bSavingBlocked = true;
		ReportCorruptFile(FilePath, bMoved);
		return ELoadResult::Corrupt;
	}

	StateFile = MoveTemp(Loaded);
	bIsDirty = false;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Loaded %d graph states from %s"),
		StateFile.Graphs.Num(), *FilePath),
		EN2CLogSeverity::Info);

	return ELoadResult::Loaded;
}

void UN2CGraphStateManager::ReportCorruptFile(const FString& FilePath, bool bMoved)
{
	const FString BackupPath = FN2CFileUtils::BackupPathFor(FilePath);
	const FString BackupHint = FPaths::FileExists(BackupPath)
		? FString::Printf(TEXT(" The previous save is in %s."), *BackupPath)
		: FString();

	const FString Message = bMoved
		? FString::Printf(
			TEXT("NodeToCode could not read its tag and translation history file, so it was kept as %s and nothing was overwritten. Tag changes are not saved until you reload the tags (restart the editor, or Load Tags) or reset them. To recover, repair that file (or use the backup) and rename it to %s.%s"),
			*QuarantinedFilePath, StateFileName, *BackupHint)
		: FString::Printf(
			TEXT("NodeToCode could not read its tag and translation history file %s and could not set it aside. Tag changes are not saved until it is repaired or removed and the tags are reloaded.%s"),
			*FilePath, *BackupHint);

	FN2CLogger::Get().LogError(Message);

	if (bNotifyUser && IsInGameThread() && FSlateApplication::IsInitialized())
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 20.0f;
		Info.bFireAndForget = true;
		Info.bUseLargeFont = false;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
}

void UN2CGraphStateManager::MigrateFromLegacyTags()
{
	if (bMigrationAttempted)
	{
		return;
	}
	bMigrationAttempted = true;

	FString LegacyPath = GetLegacyTagsFilePath();

	// Check if legacy file exists
	if (!FPaths::FileExists(LegacyPath))
	{
		FN2CLogger::Get().Log(TEXT("No legacy tags file found, starting fresh"), EN2CLogSeverity::Info);
		return;
	}

	FN2CLogger::Get().Log(TEXT("Migrating from legacy BlueprintTags.json"), EN2CLogSeverity::Info);

	// Load legacy file
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *LegacyPath))
	{
		FN2CLogger::Get().LogError(TEXT("Failed to load legacy tags file for migration"));
		return;
	}

	// Parse legacy JSON
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("Failed to parse legacy tags JSON"));
		return;
	}

	{
		// One save and one broadcast for the whole migration instead of one per tag
		FN2CGraphStateBulkUpdate Bulk(*this);

		// Convert tags to new format
		const TArray<TSharedPtr<FJsonValue>>* TagsArray;
		if (RootObject->TryGetArrayField(TEXT("tags"), TagsArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *TagsArray)
			{
				TSharedPtr<FJsonObject> TagObject = Value->AsObject();
				if (TagObject.IsValid())
				{
					FN2CTaggedBlueprintGraph LegacyTag = FN2CTaggedBlueprintGraph::FromJson(TagObject);

					// Add to new format (this will merge into existing graph states)
					AddTag(LegacyTag);
				}
			}
		}

		FN2CLogger::Get().Log(FString::Printf(TEXT("Migration complete: %d graph states created"),
			StateFile.Graphs.Num()),
			EN2CLogSeverity::Info);

		// Save the migrated state (written when Bulk ends)
		SaveState();
	}

	if (bIsDirty || bSavingBlocked)
	{
		FN2CLogger::Get().LogError(FString::Printf(
			TEXT("The migrated tags could not be saved, so the legacy tags file %s was left in place"), *LegacyPath));
		return;
	}

	// Rename the legacy file so it is never migrated again over newer state
	const FString MigratedPath = LegacyPath + TEXT(".migrated");
	if (IFileManager::Get().Move(*MigratedPath, *LegacyPath, /*Replace*/ true, /*EvenIfReadOnly*/ true,
		/*Attributes*/ false, /*bDoNotRetryOrError*/ true))
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Renamed the migrated legacy tags file to %s"), *MigratedPath),
			EN2CLogSeverity::Info);
	}
	else
	{
		FN2CLogger::Get().LogWarning(FString::Printf(
			TEXT("Could not rename the migrated legacy tags file %s; delete it so it is not migrated again"), *LegacyPath));
	}
}
