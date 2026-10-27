// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/N2CTranslationArchive.h"

#include "Core/N2CSerializer.h"
#include "Core/N2CTargetLanguage.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/N2CBlueprint.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"

namespace N2CTranslationArchivePrivate
{
	/** The run folder BeginRun created last (game thread) */
	FString LatestRunFolder;

	/** <Prefix>_<yyyyMMdd-HHmmss-fff>_<8 hex>, made unique under Base and created; empty on failure */
	FString MakeUniqueFolder(const FString& Base, const FString& Prefix, FString& OutId)
	{
		const FString Stem = FN2CPathUtils::GetTranslationFileStem(Prefix).IsEmpty()
			? FString(TEXT("Translation")) : FN2CPathUtils::GetTranslationFileStem(Prefix);
		IFileManager& Files = IFileManager::Get();
		for (int32 Attempt = 0; Attempt < 16; ++Attempt)
		{
			OutId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower();
			const FString Name = FString::Printf(TEXT("%s_%s_%s"), *Stem, *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-%s")), *OutId);
			const FString Candidate = FPaths::Combine(Base, Name);
			if (!FN2CPathUtils::IsUnderDirectoryResolved(Candidate, Base))
			{
				return FString();
			}
			if (Files.DirectoryExists(*Candidate))
			{
				continue;
			}
			if (Files.MakeDirectory(*Candidate, /*Tree*/ true))
			{
				return Candidate;
			}
		}
		return FString();
	}

	/** Path relative to Root with forward slashes, for the manifest; the path itself when it is not under Root */
	FString RelativeTo(const FString& Path, const FString& Root)
	{
		if (Path.IsEmpty())
		{
			return FString();
		}
		FString Relative = Path;
		FPaths::MakePathRelativeTo(Relative, *(Root / TEXT("")));
		Relative.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Relative;
	}

	FString ToJsonText(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	/** The parsed response as the N2C_Translation_*.json file holds it: graphs and usage */
	FString ResponseJson(const FN2CTranslationResponse& Response)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Graphs;
		for (const FN2CGraphTranslation& Graph : Response.Graphs)
		{
			const TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
			GraphObject->SetStringField(TEXT("graph_name"), Graph.GraphName);
			GraphObject->SetStringField(TEXT("graph_type"), Graph.GraphType);
			GraphObject->SetStringField(TEXT("graph_class"), Graph.GraphClass);
			const TSharedRef<FJsonObject> Code = MakeShared<FJsonObject>();
			Code->SetStringField(TEXT("graphDeclaration"), Graph.Code.GraphDeclaration);
			Code->SetStringField(TEXT("graphImplementation"), Graph.Code.GraphImplementation);
			Code->SetStringField(TEXT("implementationNotes"), Graph.Code.ImplementationNotes);
			GraphObject->SetObjectField(TEXT("code"), Code);
			Graphs.Add(MakeShared<FJsonValueObject>(GraphObject));
		}
		Root->SetArrayField(TEXT("graphs"), Graphs);
		if (Response.Usage.HasAny())
		{
			Root->SetObjectField(TEXT("usage"), Response.Usage.ToJson());
		}
		return ToJsonText(Root);
	}
}

FN2CTranslationArchive::FN2CTranslationArchive(const FN2CTranslationRunInfo& InInfo, const FString& InFolder, const FString& InRunId)
	: Info(InInfo)
	, Folder(InFolder)
	, RunId(InRunId)
	, Created(FDateTime::UtcNow().ToIso8601())
{
}

TSharedPtr<FN2CTranslationArchive> FN2CTranslationArchive::BeginRun(const FN2CTranslationRunInfo& Info, const FString& BaseDirectory)
{
	const FString Base = BaseDirectory.IsEmpty() ? FN2CPathUtils::GetTranslationsBasePath()
		: FPaths::ConvertRelativePathToFull(BaseDirectory);
	FString Id;
	const FString RunFolder = N2CTranslationArchivePrivate::MakeUniqueFolder(Base,
		Info.Name.IsEmpty() ? FString(TEXT("UnknownBlueprint")) : Info.Name, Id);
	if (RunFolder.IsEmpty())
	{
		FN2CLogger::Get().LogError(FString::Printf(TEXT("Could not create a translation folder under %s"), *Base), TEXT("TranslationArchive"));
		return nullptr;
	}

	const TSharedPtr<FN2CTranslationArchive> Run = MakeShareable(new FN2CTranslationArchive(Info, RunFolder, Id));
	N2CTranslationArchivePrivate::LatestRunFolder = RunFolder;
	Run->WriteManifest();
	return Run;
}

FString FN2CTranslationArchive::CreateUniqueFolder(const FString& Prefix, const FString& BaseDirectory)
{
	const FString Base = BaseDirectory.IsEmpty() ? FN2CPathUtils::GetTranslationsBasePath()
		: FPaths::ConvertRelativePathToFull(BaseDirectory);
	FString Id;
	return N2CTranslationArchivePrivate::MakeUniqueFolder(Base, Prefix, Id);
}

bool FN2CTranslationArchive::SaveTextAtomic(const FString& Path, const FString& Text)
{
	IFileManager& Files = IFileManager::Get();
	const FString Directory = FPaths::GetPath(Path);
	if (!Directory.IsEmpty() && !Files.DirectoryExists(*Directory) && !Files.MakeDirectory(*Directory, /*Tree*/ true))
	{
		return false;
	}

	// Beside the target, so the move is a rename on the same volume
	const FString Temporary = FString::Printf(TEXT("%s.%s.tmp"), *Path, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!FFileHelper::SaveStringToFile(Text, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		Files.Delete(*Temporary, false, true, true);
		return false;
	}
	if (!Files.Move(*Path, *Temporary, /*bReplace*/ true, /*bEvenIfReadOnly*/ true))
	{
		Files.Delete(*Temporary, false, true, true);
		return false;
	}
	return true;
}

FString FN2CTranslationArchive::GetLatestRunFolder()
{
	return N2CTranslationArchivePrivate::LatestRunFolder;
}

FString FN2CTranslationArchive::GetEntryRole(const FString& GraphType)
{
	const FString Type = GraphType.TrimStartAndEnd();
	if (Type.Equals(TEXT("Struct"), ESearchCase::IgnoreCase))
	{
		return TEXT("struct");
	}
	if (Type.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
	{
		return TEXT("enum");
	}
	return TEXT("nested");
}

FString FN2CTranslationArchive::KindToString(EN2CTranslationRunKind Kind)
{
	switch (Kind)
	{
	case EN2CTranslationRunKind::Batch:
		return TEXT("batch");
	case EN2CTranslationRunKind::Mcp:
		return TEXT("mcp");
	default:
		return TEXT("single");
	}
}

FString FN2CTranslationArchive::ClaimFolderName(const FString& Parent, const FString& Stem)
{
	const FString Base = Stem.IsEmpty() ? FString(TEXT("Graph")) : Stem;
	IFileManager& Files = IFileManager::Get();
	FString Name = Base;
	for (int32 Suffix = 2; ; ++Suffix)
	{
		const FString Key = (Parent + TEXT("|") + Name).ToLower(); // folder names must differ by more than case
		if (!ClaimedFolders.Contains(Key) && !Files.DirectoryExists(*FPaths::Combine(Parent, Name)))
		{
			ClaimedFolders.Add(Key);
			return Name;
		}
		Name = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
	}
}

bool FN2CTranslationArchive::WriteResponse(const FN2CTranslationResponse& Response, const FN2CArchiveSource& Source,
	const FString& ItemFolder, FN2CArchivedResponse& OutResult)
{
	using namespace N2CTranslationArchivePrivate;
	OutResult = FN2CArchivedResponse();

	// The item's folder: the run folder for a single or MCP translation, a folder of its own for a batch item
	FString Container = Folder;
	if (!ItemFolder.IsEmpty())
	{
		const FString ItemStem = FN2CPathUtils::GetTranslationFileStem(ItemFolder);
		Container = FPaths::Combine(Folder, ClaimFolderName(Folder, ItemStem));
		if (!FN2CPathUtils::IsUnderDirectoryResolved(Container, Folder) || !IFileManager::Get().MakeDirectory(*Container, true))
		{
			FN2CLogger::Get().LogError(FString::Printf(TEXT("Could not create the item folder %s"), *Container), TEXT("TranslationArchive"));
			return false;
		}
	}
	OutResult.Folder = Container;

	const FN2CTargetLanguage& Language = N2C::GetLanguage(Info.Language);
	const FString ImplementationExtension = Language.ImplementationExtension;
	const bool bDeclarationExtension = Language.bHasDeclarationFile && *Language.DeclarationExtension;

	// The root entry: the graph the caller asked for, else the first graph that is not a struct or an enum
	int32 RootIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Response.Graphs.Num() && !Source.GraphName.IsEmpty(); ++Index)
	{
		if (Response.Graphs[Index].GraphName.Equals(Source.GraphName, ESearchCase::IgnoreCase))
		{
			RootIndex = Index;
			break;
		}
	}
	for (int32 Index = 0; Index < Response.Graphs.Num() && RootIndex == INDEX_NONE; ++Index)
	{
		if (GetEntryRole(Response.Graphs[Index].GraphType) == TEXT("nested"))
		{
			RootIndex = Index;
		}
	}
	if (RootIndex == INDEX_NONE && Response.Graphs.Num() > 0)
	{
		RootIndex = 0;
	}

	bool bAnyWritten = false;
	for (int32 Index = 0; Index < Response.Graphs.Num(); ++Index)
	{
		const FN2CGraphTranslation& Graph = Response.Graphs[Index];
		FN2CArchivedEntry Entry;
		Entry.GraphName = Graph.GraphName;
		Entry.GraphType = Graph.GraphType;
		Entry.ResponseIndex = Index;
		Entry.Role = Index == RootIndex ? FString(TEXT("root")) : GetEntryRole(Graph.GraphType);

		// The name comes from the LLM response: a safe stem, and a folder of its own even when two entries share it
		FString Stem = FN2CPathUtils::GetTranslationFileStem(Graph.GraphName);
		if (Stem.IsEmpty())
		{
			Stem = FString::Printf(TEXT("Graph%d"), Index + 1);
		}
		Entry.Folder = FPaths::Combine(Container, ClaimFolderName(Container, Stem));
		if (!FN2CPathUtils::IsUnderDirectoryResolved(Entry.Folder, Container) || !IFileManager::Get().MakeDirectory(*Entry.Folder, true))
		{
			FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Skipping graph '%s': could not create its folder"), *Graph.GraphName),
				TEXT("TranslationArchive"));
			continue;
		}

		// The declaration for every language: its own extension (C++), the implementation's name when the entry has
		// no implementation (a struct or enum definition), else <Stem>_Declaration beside the implementation
		const bool bHasDeclaration = !Graph.Code.GraphDeclaration.IsEmpty();
		const bool bHasImplementation = !Graph.Code.GraphImplementation.IsEmpty();
		if (bHasDeclaration)
		{
			const FString Name = bDeclarationExtension ? Stem + Language.DeclarationExtension
				: bHasImplementation ? Stem + TEXT("_Declaration") + ImplementationExtension
				: Stem + ImplementationExtension;
			const FString Path = FPaths::Combine(Entry.Folder, Name);
			if (SaveTextAtomic(Path, Graph.Code.GraphDeclaration))
			{
				Entry.DeclarationFile = Path;
			}
			else
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save %s"), *Path), TEXT("TranslationArchive"));
			}
		}
		if (bHasImplementation)
		{
			const FString Path = FPaths::Combine(Entry.Folder, Stem + ImplementationExtension);
			if (SaveTextAtomic(Path, Graph.Code.GraphImplementation))
			{
				Entry.ImplementationFile = Path;
			}
			else
			{
				FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Failed to save %s"), *Path), TEXT("TranslationArchive"));
			}
		}
		if (!Graph.Code.ImplementationNotes.IsEmpty())
		{
			const FString Path = FPaths::Combine(Entry.Folder, Stem + TEXT("_Notes.txt"));
			if (SaveTextAtomic(Path, Graph.Code.ImplementationNotes))
			{
				Entry.NotesFile = Path;
			}
		}

		bAnyWritten = true;
		if (Index == RootIndex)
		{
			OutResult.RootIndex = OutResult.Entries.Num();
		}
		OutResult.Entries.Add(MoveTemp(Entry));
	}

	// What was sent, beside the translation
	if (ItemFolder.IsEmpty())
	{
		// A single or MCP run: N2C_BP_<run>.json at the run's root (UN2CGraphStateManager::LoadN2CJson reads it)
		const FString RunName = FPaths::GetCleanFilename(Folder);
		const FString Pretty = Source.Capture ? FN2CSerializer::ToJson(*Source.Capture, EN2CJsonStyle::Pretty) : Source.InputJson;
		if (!Pretty.IsEmpty())
		{
			SaveTextAtomic(FPaths::Combine(Folder, FString::Printf(TEXT("N2C_BP_%s.json"), *RunName)), Pretty);
		}
		if (Source.Capture)
		{
			SaveTextAtomic(FPaths::Combine(Folder, FString::Printf(TEXT("N2C_BP_Minified_%s.json"), *RunName)),
				FN2CSerializer::ToJson(*Source.Capture, EN2CJsonStyle::Condensed));
		}
		SaveTextAtomic(FPaths::Combine(Folder, FString::Printf(TEXT("N2C_Translation_%s.json"), *RunName)), ResponseJson(Response));
	}
	else if (const FN2CArchivedEntry* Root = OutResult.GetRoot())
	{
		// A batch item: <graph>.json in the root entry's folder, named after the graph the item asked for
		const FString JsonStem = FN2CPathUtils::GetTranslationFileStem(Source.GraphName.IsEmpty() ? Root->GraphName : Source.GraphName);
		if (!Source.InputJson.IsEmpty() && !JsonStem.IsEmpty())
		{
			SaveTextAtomic(FPaths::Combine(Root->Folder, JsonStem + TEXT(".json")), Source.InputJson);
		}
	}

	FWrittenItem& Written = Items.AddDefaulted_GetRef();
	Written.Source = Source;
	Written.Source.Capture = nullptr; // the caller's capture does not outlive this call
	Written.Result = OutResult;
	Written.Usage = Response.Usage;
	Written.RequestId = Response.Error.RequestId;
	Written.Timestamp = FDateTime::UtcNow().ToIso8601();
	WriteManifest();

	if (bAnyWritten)
	{
		FN2CLogger::Get().Log(FString::Printf(TEXT("Translation saved to: %s"), *Container), EN2CLogSeverity::Info);
	}
	return bAnyWritten;
}

bool FN2CTranslationArchive::WriteRunFile(const FString& RelativePath, const FString& Text) const
{
	const FString Path = FPaths::Combine(Folder, RelativePath);
	return FN2CPathUtils::IsUnderDirectoryResolved(Path, Folder) && SaveTextAtomic(Path, Text);
}

bool FN2CTranslationArchive::WriteManifest() const
{
	using namespace N2CTranslationArchivePrivate;
	const TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetNumberField(TEXT("schemaVersion"), ManifestSchemaVersion);
	Manifest->SetStringField(TEXT("runId"), RunId);
	Manifest->SetStringField(TEXT("kind"), KindToString(Info.Kind));
	Manifest->SetStringField(TEXT("name"), Info.Name);
	Manifest->SetStringField(TEXT("provider"), Info.Provider);
	Manifest->SetStringField(TEXT("model"), Info.Model);
	Manifest->SetStringField(TEXT("language"), N2C::GetLanguage(Info.Language).Id);
	Manifest->SetStringField(TEXT("timestamp"), Created);

	FN2CTranslationUsage Total;
	TArray<TSharedPtr<FJsonValue>> RequestIds;
	TArray<TSharedPtr<FJsonValue>> ItemsJson;
	TArray<TSharedPtr<FJsonValue>> EntriesJson;
	for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
	{
		const FWrittenItem& Item = Items[ItemIndex];
		Total += Item.Usage;
		if (!Item.RequestId.IsEmpty())
		{
			RequestIds.Add(MakeShared<FJsonValueString>(Item.RequestId));
		}

		const TSharedRef<FJsonObject> ItemJson = MakeShared<FJsonObject>();
		ItemJson->SetStringField(TEXT("graphName"), Item.Source.GraphName);
		ItemJson->SetStringField(TEXT("blueprintPath"), Item.Source.BlueprintPath);
		ItemJson->SetStringField(TEXT("graphGuid"), Item.Source.GraphGuid);
		ItemJson->SetStringField(TEXT("folder"), RelativeTo(Item.Result.Folder, Folder));
		ItemJson->SetStringField(TEXT("timestamp"), Item.Timestamp);
		ItemJson->SetObjectField(TEXT("usage"), Item.Usage.ToJson());
		// The parsed response carries no stop reason yet, and a request id only with an error
		ItemJson->SetStringField(TEXT("stopReason"), FString());
		ItemJson->SetStringField(TEXT("requestId"), Item.RequestId);
		ItemsJson.Add(MakeShared<FJsonValueObject>(ItemJson));

		for (int32 EntryIndex = 0; EntryIndex < Item.Result.Entries.Num(); ++EntryIndex)
		{
			const FN2CArchivedEntry& Entry = Item.Result.Entries[EntryIndex];
			const TSharedRef<FJsonObject> EntryJson = MakeShared<FJsonObject>();
			EntryJson->SetNumberField(TEXT("item"), ItemIndex);
			EntryJson->SetStringField(TEXT("graphName"), Entry.GraphName);
			EntryJson->SetStringField(TEXT("graphType"), Entry.GraphType);
			EntryJson->SetStringField(TEXT("role"), Entry.Role);
			EntryJson->SetStringField(TEXT("folder"), RelativeTo(Entry.Folder, Folder));
			const TSharedRef<FJsonObject> FilesJson = MakeShared<FJsonObject>();
			FilesJson->SetStringField(TEXT("declaration"), RelativeTo(Entry.DeclarationFile, Folder));
			FilesJson->SetStringField(TEXT("implementation"), RelativeTo(Entry.ImplementationFile, Folder));
			FilesJson->SetStringField(TEXT("notes"), RelativeTo(Entry.NotesFile, Folder));
			EntryJson->SetObjectField(TEXT("files"), FilesJson);
			EntriesJson.Add(MakeShared<FJsonValueObject>(EntryJson));
		}
	}
	Manifest->SetObjectField(TEXT("usage"), Total.ToJson());
	Manifest->SetStringField(TEXT("stopReason"), FString());
	Manifest->SetArrayField(TEXT("requestIds"), RequestIds);
	Manifest->SetArrayField(TEXT("items"), ItemsJson);
	Manifest->SetArrayField(TEXT("entries"), EntriesJson);

	return SaveTextAtomic(FPaths::Combine(Folder, TEXT("manifest.json")), ToJsonText(Manifest));
}
