// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpDependencies.h"
#include "Core/N2CDependencyIndex.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CMcpDependenciesPrivate
{
#if WITH_DEV_AUTOMATION_TESTS
	FString& ExportDirectoryOverride()
	{
		static FString Directory;
		return Directory;
	}
#endif

	TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> Paths(const FN2CDependencyIndex& Index, const TArray<int32>& Nodes)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(Nodes.Num());
		for (const int32 Node : Nodes)
		{
			if (Index.Nodes.IsValidIndex(Node))
			{
				Out.Add(MakeShared<FJsonValueString>(Index.Nodes[Node].Path));
			}
		}
		return Out;
	}

	FString Condensed(const TSharedRef<FJsonObject>& Json)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Json, Writer);
		return Text;
	}

	FString Pretty(const TSharedRef<FJsonObject>& Json)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Json, Writer);
		return Text;
	}

	/** Writes Text to Directory/FileName through a temporary file, so a reader never sees half a document */
	bool WriteExport(const FString& Text, const FString& FileName, FString& OutPath, int64& OutBytes, FString& OutError)
	{
		const FString Directory = N2CMcpDependencies::ExportDirectory();
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			OutError = FString::Printf(TEXT("EXPORT_FAILED: could not create %s"), *Directory);
			return false;
		}
		const FString Path = FPaths::ConvertRelativePathToFull(Directory / FileName);
		if (!FN2CPathUtils::IsUnderDirectoryResolved(Path, Directory))
		{
			OutError = FString::Printf(TEXT("EXPORT_FAILED: %s is not inside %s"), *Path, *Directory);
			return false;
		}
		const FString TempPath = Path + FString::Printf(TEXT(".%s.tmp"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!FFileHelper::SaveStringToFile(Text, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("EXPORT_FAILED: could not write %s"), *TempPath);
			return false;
		}
		if (!IFileManager::Get().Move(*Path, *TempPath, /*bReplace*/ true))
		{
			IFileManager::Get().Delete(*TempPath);
			OutError = FString::Printf(TEXT("EXPORT_FAILED: could not move the export to %s"), *Path);
			return false;
		}
		OutPath = Path;
		OutBytes = IFileManager::Get().FileSize(*Path);
		return true;
	}
}

TSharedRef<FJsonObject> N2CMcpDependencies::ToJson(const FN2CDependencyIndex& Index, const TArray<FString>& Roots)
{
	using namespace N2CMcpDependenciesPrivate;

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetArrayField(TEXT("roots"), Strings(Roots));
	Json->SetNumberField(TEXT("nodeCount"), Index.Nodes.Num());
	Json->SetNumberField(TEXT("edgeCount"), Index.Edges.Num());

	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Reserve(Index.Nodes.Num());
	for (const FN2CDependencyNode& Node : Index.Nodes)
	{
		const TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("path"), Node.Path);
		NodeJson->SetStringField(TEXT("name"), Node.Name);
		NodeJson->SetStringField(TEXT("kind"), FN2CDependencyIndex::NodeKindToString(Node.Kind));
		NodeJson->SetBoolField(TEXT("root"), Node.bRoot);
		Nodes.Add(MakeShared<FJsonValueObject>(NodeJson));
	}
	Json->SetArrayField(TEXT("nodes"), Nodes);

	TArray<TSharedPtr<FJsonValue>> Edges;
	Edges.Reserve(Index.Edges.Num());
	for (const FN2CDependencyEdge& Edge : Index.Edges)
	{
		if (!Index.Nodes.IsValidIndex(Edge.From) || !Index.Nodes.IsValidIndex(Edge.To))
		{
			continue;
		}
		const TSharedRef<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
		EdgeJson->SetStringField(TEXT("from"), Index.Nodes[Edge.From].Path);
		EdgeJson->SetStringField(TEXT("to"), Index.Nodes[Edge.To].Path);
		EdgeJson->SetStringField(TEXT("kind"), FN2CDependencyIndex::EdgeKindToString(Edge.Kind));
		if (!Edge.Graph.IsEmpty())
		{
			EdgeJson->SetStringField(TEXT("graph"), Edge.Graph);
		}
		if (!Edge.Node.IsEmpty())
		{
			EdgeJson->SetStringField(TEXT("node"), Edge.Node);
		}
		Edges.Add(MakeShared<FJsonValueObject>(EdgeJson));
	}
	Json->SetArrayField(TEXT("edges"), Edges);

	Json->SetArrayField(TEXT("order"), Paths(Index, Index.Order));
	TArray<TSharedPtr<FJsonValue>> Groups;
	for (const TArray<int32>& Group : Index.CycleGroups)
	{
		Groups.Add(MakeShared<FJsonValueArray>(Paths(Index, Group)));
	}
	Json->SetArrayField(TEXT("cycleGroups"), Groups);
	Json->SetArrayField(TEXT("warnings"), Strings(Index.Warnings));
	return Json;
}

TSharedPtr<FJsonObject> N2CMcpDependencies::Run(const FRequest& Request, FString& OutError)
{
	using namespace N2CMcpDependenciesPrivate;

	TArray<FString> Roots;
	for (const FString& Path : Request.Paths)
	{
		const FString Trimmed = Path.TrimStartAndEnd();
		if (!Trimmed.StartsWith(TEXT("/")))
		{
			OutError = FString::Printf(TEXT("INVALID_ARGUMENT: '%s' is not an asset path or content folder (it starts with /, e.g. /Game/Blueprints or /Game/Blueprints/BP_Door)"), *Path);
			return nullptr;
		}
		Roots.AddUnique(Trimmed);
	}
	if (Roots.Num() == 0)
	{
		OutError = TEXT("INVALID_ARGUMENT: paths is required: asset paths or content folders such as /Game/Blueprints");
		return nullptr;
	}

	FN2CDependencyIndexOptions Options;
	Options.bLoadAssets = Request.bIncludeCallGraph;
	Options.bIncludeReferencedUserAssets = Request.bIncludeReferenced;
	const FN2CDependencyIndex Index = FN2CDependencyIndex::Build(Roots, Options);
	if (Index.Nodes.Num() == 0)
	{
		OutError = FString::Printf(TEXT("NOT_FOUND: no Blueprint, Blueprint interface, macro library, user struct or user enum under %s.%s%s"),
			*FString::Join(Roots, TEXT(", ")), Index.Warnings.Num() > 0 ? TEXT(" ") : TEXT(""), *FString::Join(Index.Warnings, TEXT(" ")));
		return nullptr;
	}

	const TSharedRef<FJsonObject> Json = ToJson(Index, Roots);
	const FString Text = Condensed(Json);
	const bool bTooLarge = Request.MaxInlineTokens != MAX_int32 && N2CMcpBlueprintExport::EstimateTokens(Text) > Request.MaxInlineTokens;
	if (Request.bToFile || bTooLarge)
	{
		FString SavedTo;
		int64 Bytes = 0;
		if (!WriteExport(Pretty(Json), ExportFileName(Request), SavedTo, Bytes, OutError))
		{
			return nullptr;
		}
		Json->SetStringField(TEXT("savedTo"), SavedTo);
		Json->SetNumberField(TEXT("bytes"), static_cast<double>(Bytes));
	}
	if (bTooLarge)
	{
		// The counts, warnings and the file stay; the lists are in the file
		for (const TCHAR* Field : { TEXT("nodes"), TEXT("edges"), TEXT("order"), TEXT("cycleGroups") })
		{
			Json->RemoveField(Field);
		}
		Json->SetNumberField(TEXT("cycleGroupCount"), Index.CycleGroups.Num());
		Json->SetStringField(TEXT("reason"), FString::Printf(
			TEXT("The index would be about %d tokens, over the %d inline limit: read savedTo (read-file) for nodes, edges, order and cycleGroups."),
			N2CMcpBlueprintExport::EstimateTokens(Text), Request.MaxInlineTokens));
	}
	Json->SetBoolField(TEXT("inline"), !bTooLarge);
	return Json;
}

FString N2CMcpDependencies::ExportDirectory()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!N2CMcpDependenciesPrivate::ExportDirectoryOverride().IsEmpty())
	{
		return N2CMcpDependenciesPrivate::ExportDirectoryOverride();
	}
#endif
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Dependencies"));
}

FString N2CMcpDependencies::ExportFileName(const FRequest& Request)
{
	TArray<FString> Roots;
	for (const FString& Path : Request.Paths)
	{
		Roots.AddUnique(Path.TrimStartAndEnd());
	}
	FString Key = FString::Join(Roots, TEXT("|"));
	Key += FString::Printf(TEXT("|%d|%d"), Request.bIncludeCallGraph ? 1 : 0, Request.bIncludeReferenced ? 1 : 0);

	// The first root's last path element without its object name: /Game/BP_Door.BP_Door -> BP_Door, /Game/AI -> AI
	FString Stem = Roots.Num() > 0 ? FPaths::GetBaseFilename(Roots[0]) : FString();
	Stem = FN2CPathUtils::MakeSafeFileStem(Stem);
	return FString::Printf(TEXT("%s__%08x.json"), Stem.IsEmpty() ? TEXT("Dependencies") : *Stem, FCrc::StrCrc32(*Key));
}

#if WITH_DEV_AUTOMATION_TESTS
void N2CMcpDependencies::SetExportDirectoryForTests(const FString& Directory)
{
	N2CMcpDependenciesPrivate::ExportDirectoryOverride() = Directory;
}
#endif
