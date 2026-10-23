// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CGraphStateManager.h"
#include "Utils/N2CLogger.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Utils/N2CPathUtils.h"

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

	// Try to load existing state
	if (!LoadState())
	{
		// If no state file exists, try migration from legacy tags
		MigrateFromLegacyTags();
	}
}

void UN2CGraphStateManager::Shutdown()
{
	FN2CLogger::Get().Log(TEXT("Shutting down Graph State Manager"), EN2CLogSeverity::Info);

	if (bIsDirty)
	{
		SaveState();
	}

	if (Instance)
	{
		Instance->RemoveFromRoot();
		Instance = nullptr;
	}
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
	const FString& Category, const FString& Description)
{
	// Get or create the graph state
	FN2CGraphState& State = GetOrCreateGraphState(GraphGuid, GraphName, Blueprint);

	// Check if this tag already exists
	for (const FN2CTagEntry& ExistingTag : State.Tags)
	{
		if (ExistingTag.MatchesTag(Tag, Category))
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("Tag '%s' in category '%s' already exists for graph %s"),
				*Tag, *Category, *GraphGuid.ToString()),
				EN2CLogSeverity::Warning);
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
	OnGraphTagAdded.Broadcast(State);
	OnGraphStateChanged.Broadcast(GraphGuid);

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
		OnGraphTagRemoved.Broadcast(GraphGuid, Tag);
		OnGraphStateChanged.Broadcast(GraphGuid);

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

		OnGraphTagRemoved.Broadcast(GraphGuid, Tag);
		OnGraphStateChanged.Broadcast(GraphGuid);

		// Clean up empty graph states
		if (State->Tags.Num() == 0 && !State->HasTranslation() && !State->HasJsonExport())
		{
			RemoveGraphState(GraphGuid);
		}

		SaveState();
	}

	return RemovedCount;
}

int32 UN2CGraphStateManager::RemoveAllTagsFromGraph(const FGuid& GraphGuid)
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
		OnGraphTagRemoved.Broadcast(GraphGuid, Tag.Tag);
	}

	State->Tags.Empty();

	if (RemovedCount > 0)
	{
		MarkDirty();

		FN2CLogger::Get().Log(FString::Printf(TEXT("Removed %d tags from graph %s"),
			RemovedCount, *GraphGuid.ToString()),
			EN2CLogSeverity::Info);

		OnGraphStateChanged.Broadcast(GraphGuid);

		// Clean up empty graph states
		if (!State->HasTranslation() && !State->HasJsonExport())
		{
			RemoveGraphState(GraphGuid);
		}

		SaveState();
	}

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

		OnGraphTranslationUpdated.Broadcast(GraphGuid);
		OnGraphStateChanged.Broadcast(GraphGuid);

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

	OnGraphTranslationUpdated.Broadcast(GraphGuid);
	OnGraphStateChanged.Broadcast(GraphGuid);

	SaveState();
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

		OnGraphTranslationUpdated.Broadcast(GraphGuid);
		OnGraphStateChanged.Broadcast(GraphGuid);

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
	const FN2CGraphState* State = FindGraphState(GraphGuid);
	if (!State || !State->HasTranslation())
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("No translation found for graph %s"),
			*GraphGuid.ToString()),
			EN2CLogSeverity::Warning);
		return false;
	}

	// Build full path from relative path
	FString FullPath = FPaths::Combine(FPaths::ProjectDir(), State->Translation.OutputPath);

	// Determine file extension based on language
	FString Extension = TEXT(".cpp");
	FString Language = State->Translation.Language;
	if (Language.Equals(TEXT("Python"), ESearchCase::IgnoreCase))
	{
		Extension = TEXT(".py");
	}
	else if (Language.Equals(TEXT("JavaScript"), ESearchCase::IgnoreCase))
	{
		Extension = TEXT(".js");
	}
	else if (Language.Equals(TEXT("CSharp"), ESearchCase::IgnoreCase))
	{
		Extension = TEXT(".cs");
	}
	else if (Language.Equals(TEXT("Swift"), ESearchCase::IgnoreCase))
	{
		Extension = TEXT(".swift");
	}
	else if (Language.Equals(TEXT("Pseudocode"), ESearchCase::IgnoreCase))
	{
		Extension = TEXT(".txt");
	}

	// Build file paths
	// Same stem the save path used (identical to the raw name for names without path characters)
	const FString Stem = FN2CPathUtils::GetTranslationFileStem(State->GraphName);
	FString DeclarationPath = FPaths::Combine(FullPath, Stem + TEXT(".h"));
	FString ImplementationPath = FPaths::Combine(FullPath, Stem + Extension);
	FString NotesPath = FPaths::Combine(FullPath, Stem + TEXT("_Notes.txt"));

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

	// Build full path from relative path
	FString FullPath = FPaths::Combine(FPaths::ProjectDir(), State->Translation.OutputPath);

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

		OnGraphStateChanged.Broadcast(GraphGuid);

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

	OnGraphStateChanged.Broadcast(GraphGuid);

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

		OnGraphStateChanged.Broadcast(GraphGuid);

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

bool UN2CGraphStateManager::RemoveGraphState(const FGuid& GraphGuid)
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
}

// ============================================================================
// Persistence
// ============================================================================

FString UN2CGraphStateManager::GetStateFilePath() const
{
	FString ProjectSavedDir = FPaths::ProjectSavedDir();
	FString StateDir = FPaths::Combine(ProjectSavedDir, TEXT("NodeToCode"));

	// Ensure directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*StateDir);

	return FPaths::Combine(StateDir, TEXT("BlueprintGraphState.json"));
}

FString UN2CGraphStateManager::GetLegacyTagsFilePath() const
{
	FString ProjectSavedDir = FPaths::ProjectSavedDir();
	return FPaths::Combine(ProjectSavedDir, TEXT("NodeToCode"), TEXT("Tags"), TEXT("BlueprintTags.json"));
}

void UN2CGraphStateManager::MarkDirty()
{
	bIsDirty = true;
}

bool UN2CGraphStateManager::SaveState()
{
	FString FilePath = GetStateFilePath();

	StateFile.LastSaved = FDateTime::UtcNow();
	FString JsonString = StateFile.ToJsonString(true);

	if (!FFileHelper::SaveStringToFile(JsonString, *FilePath))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to save graph state to file: %s"), *FilePath));
		return false;
	}

	bIsDirty = false;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Saved %d graph states to %s"),
		StateFile.Graphs.Num(), *FilePath),
		EN2CLogSeverity::Info);

	return true;
}

bool UN2CGraphStateManager::LoadState()
{
	FString FilePath = GetStateFilePath();

	// Check if file exists
	if (!FPaths::FileExists(FilePath))
	{
		FN2CLogger::Get().Log(TEXT("No graph state file found"), EN2CLogSeverity::Info);
		return false;
	}

	// Load file content
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Failed to load graph state from file: %s"), *FilePath));
		return false;
	}

	// Parse JSON
	if (!FN2CGraphStateFile::FromJsonString(JsonString, StateFile))
	{
		FN2CLogger::Get().LogError(TEXT("Failed to parse graph state JSON"));
		return false;
	}

	bIsDirty = false;
	FN2CLogger::Get().Log(FString::Printf(TEXT("Loaded %d graph states from %s"),
		StateFile.Graphs.Num(), *FilePath),
		EN2CLogSeverity::Info);

	return true;
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

	// Create backup of legacy file
	FString BackupPath = LegacyPath + TEXT(".backup");
	if (IFileManager::Get().Copy(*BackupPath, *LegacyPath) == COPY_OK)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Created backup of legacy tags at: %s"), *BackupPath),
			EN2CLogSeverity::Info);
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Migration complete: %d graph states created"),
		StateFile.Graphs.Num()),
		EN2CLogSeverity::Info);

	// Save the migrated state
	SaveState();
}
