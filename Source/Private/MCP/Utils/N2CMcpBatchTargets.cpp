// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpBatchTargets.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "Core/N2CGraphStateManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace N2CMcpBatchTargetsPrivate
{
	bool HasK2Node(const UEdGraph& Graph)
	{
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			if (Cast<UK2Node>(Node))
			{
				return true;
			}
		}
		return false;
	}

	FN2CBatchGraph MakeGraph(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		FN2CBatchGraph Entry;
		Entry.Blueprint = Blueprint;
		Entry.Graph = Graph;
		Entry.BlueprintPath = Blueprint->GetPathName();
		Entry.GraphName = Graph->GetName();
		Entry.GraphGuid = Graph->GraphGuid.ToString();
		Entry.Kind = FN2CGraphTarget::GraphKind(Graph);
		return Entry;
	}

	FN2CBatchTargetIssue MakeIssue(const FString& BlueprintPath, const FString& GraphName, const FString& GraphGuid, const FString& Error)
	{
		FN2CBatchTargetIssue Issue;
		Issue.BlueprintPath = BlueprintPath;
		Issue.GraphName = GraphName;
		Issue.GraphGuid = GraphGuid;
		Issue.Error = Error;
		return Issue;
	}

	/** The object paths a user's path may mean: as given, and '/Game/BP' as '/Game/BP.BP' */
	TArray<FString> Candidates(const FString& Path)
	{
		TArray<FString> Out;
		Out.Add(Path);
		if (!Path.Contains(TEXT(".")) && FPackageName::IsValidLongPackageName(Path))
		{
			Out.Add(Path + TEXT(".") + FPackageName::GetShortName(Path));
		}
		return Out;
	}

	/** One requested selection item, before resolution */
	struct FRequested
	{
		FString BlueprintPath;
		FString GraphName;
		FString GraphGuid;
		FString Tag;
		FString Category;
	};
}

TSharedRef<FJsonObject> FN2CBatchGraph::ToJson() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Json->SetStringField(TEXT("graphName"), GraphName);
	Json->SetStringField(TEXT("graphGuid"), GraphGuid);
	Json->SetStringField(TEXT("kind"), Kind);
	if (!Tag.IsEmpty())
	{
		Json->SetStringField(TEXT("tag"), Tag);
		Json->SetStringField(TEXT("category"), Category);
	}
	return Json;
}

TSharedRef<FJsonObject> FN2CBatchTargetIssue::ToJson() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	if (!GraphName.IsEmpty())
	{
		Json->SetStringField(TEXT("graphName"), GraphName);
	}
	if (!GraphGuid.IsEmpty())
	{
		Json->SetStringField(TEXT("graphGuid"), GraphGuid);
	}
	Json->SetStringField(TEXT("code"), N2CMcpBatchTargets::ErrorCode(Error));
	Json->SetStringField(TEXT("error"), Error);
	return Json;
}

TArray<TSharedPtr<FJsonValue>> FN2CBatchTargets::FailuresJson() const
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FN2CBatchTargetIssue& Issue : Failures)
	{
		Out.Add(MakeShared<FJsonValueObject>(Issue.ToJson()));
	}
	return Out;
}

TArray<TSharedPtr<FJsonValue>> FN2CBatchTargets::SkippedJson() const
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FN2CBatchTargetIssue& Issue : Skipped)
	{
		Out.Add(MakeShared<FJsonValueObject>(Issue.ToJson()));
	}
	return Out;
}

FString N2CMcpBatchTargets::ErrorCode(const FString& Error)
{
	FString Code;
	return Error.Split(TEXT(":"), &Code, nullptr) ? Code : Error;
}

UBlueprint* N2CMcpBatchTargets::LoadBlueprint(const FString& InPath, FString& OutError)
{
	using namespace N2CMcpBatchTargetsPrivate;
	OutError.Reset();
	const FString Path = InPath.TrimStartAndEnd();
	if (Path.IsEmpty())
	{
		OutError = TEXT("INVALID_ARGUMENT: blueprintPath is required, the asset path of a Blueprint such as /Game/Blueprints/BP_Door");
		return nullptr;
	}

	FString LoadError;
	if (UBlueprint* Blueprint = FN2CGraphTarget::LoadBlueprint(Path, LoadError))
	{
		return Blueprint;
	}

	// Something else at that path: say what it is instead of "not found" (materials get their own capture, card 0469)
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	for (const FString& Candidate : Candidates(Path))
	{
		// Only object paths: a package path finds the package itself (a missing asset's too, once a load was tried)
		if (!Candidate.Contains(TEXT(".")))
		{
			continue;
		}
		FString ClassName;
		const UObject* Loaded = FindObject<UObject>(nullptr, *Candidate);
		if (Loaded && !Loaded->IsA<UPackage>())
		{
			ClassName = Loaded->GetClass()->GetName();
		}
		else
		{
			const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(Candidate));
			if (AssetData.IsValid())
			{
				ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
			}
		}
		if (!ClassName.IsEmpty() && ClassName != TEXT("Package"))
		{
			OutError = FString::Printf(
				TEXT("UNSUPPORTED_ASSET_TYPE: '%s' is a %s, not a Blueprint; the batch tools read Blueprint graphs only"),
				*Candidate, *ClassName);
			return nullptr;
		}
	}

	OutError = LoadError;
	return nullptr;
}

void N2CMcpBatchTargets::AddSchema(const TSharedPtr<FJsonObject>& Properties, int32 MaxGraphs)
{
	auto StringProperty = [](const TCHAR* Description)
	{
		const TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), TEXT("string"));
		Property->SetStringField(TEXT("description"), Description);
		return Property;
	};

	const TSharedPtr<FJsonObject> TargetProperties = MakeShared<FJsonObject>();
	TargetProperties->SetObjectField(TEXT("blueprintPath"), StringProperty(
		TEXT("Required. Asset path of the Blueprint, e.g. /Game/Blueprints/BP_Door or /Game/Blueprints/BP_Door.BP_Door.")));
	TargetProperties->SetObjectField(TEXT("graphName"), StringProperty(
		TEXT("Optional. One graph by name. With neither graphName nor graphGuid, every graph of the Blueprint that has nodes.")));
	TargetProperties->SetObjectField(TEXT("graphGuid"), StringProperty(
		TEXT("Optional. One graph by its GraphGuid from list-blueprint-graphs; wins over graphName.")));

	const TSharedPtr<FJsonObject> TargetSchema = MakeShared<FJsonObject>();
	TargetSchema->SetStringField(TEXT("type"), TEXT("object"));
	TargetSchema->SetObjectField(TEXT("properties"), TargetProperties);
	TargetSchema->SetArrayField(TEXT("required"), { MakeShared<FJsonValueString>(TEXT("blueprintPath")) });
	TargetSchema->SetBoolField(TEXT("additionalProperties"), false);

	const TSharedPtr<FJsonObject> Targets = MakeShared<FJsonObject>();
	Targets->SetStringField(TEXT("type"), TEXT("array"));
	Targets->SetObjectField(TEXT("items"), TargetSchema);
	Targets->SetNumberField(TEXT("minItems"), 1);
	Targets->SetStringField(TEXT("description"), *FString::Printf(
		TEXT("The graphs to work on: [{blueprintPath, graphName?, graphGuid?}]. Give targets, or tag and/or category, not both. ")
		TEXT("At most %d graphs per call. A path that is not a Blueprint (a Material, a Data Asset) fails with UNSUPPORTED_ASSET_TYPE."),
		MaxGraphs));
	Properties->SetObjectField(TEXT("targets"), Targets);

	Properties->SetObjectField(TEXT("tag"), StringProperty(
		TEXT("Instead of targets: every graph with this tag (tag-blueprint-graph, list-blueprint-tags), in category when given.")));
	Properties->SetObjectField(TEXT("category"), StringProperty(
		TEXT("Instead of targets: every graph tagged in this category (with tag: that tag in this category).")));
}

FN2CBatchTargets N2CMcpBatchTargets::Resolve(const TSharedPtr<FJsonObject>& Arguments, int32 MaxGraphs, const TCHAR* ToolName)
{
	using namespace N2CMcpBatchTargetsPrivate;
	check(IsInGameThread());

	FN2CBatchTargets Result;
	const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();

	FString Tag;
	FString Category;
	Args->TryGetStringField(TEXT("tag"), Tag);
	Args->TryGetStringField(TEXT("category"), Category);
	Tag.TrimStartAndEndInline();
	Category.TrimStartAndEndInline();
	const bool bHasTargets = Args->HasField(TEXT("targets"));
	const bool bHasTagFilter = !Tag.IsEmpty() || !Category.IsEmpty();

	if (bHasTargets && bHasTagFilter)
	{
		Result.Error = FString::Printf(TEXT("INVALID_ARGUMENT: %s takes targets or a tag/category filter, not both"), ToolName);
		return Result;
	}
	if (!bHasTargets && !bHasTagFilter)
	{
		Result.Error = FString::Printf(
			TEXT("INVALID_ARGUMENT: %s needs targets ([{blueprintPath, graphName?, graphGuid?}]) or a tag and/or category"), ToolName);
		return Result;
	}

	// What was asked for, before anything is loaded
	TArray<FRequested> Requested;
	if (bHasTargets)
	{
		const TArray<TSharedPtr<FJsonValue>>* TargetValues = nullptr;
		if (!Args->TryGetArrayField(TEXT("targets"), TargetValues) || TargetValues->Num() == 0)
		{
			Result.Error = TEXT("INVALID_ARGUMENT: targets must be a non-empty array of {blueprintPath, graphName?, graphGuid?}");
			return Result;
		}
		for (int32 Index = 0; Index < TargetValues->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* TargetObject = nullptr;
			if (!(*TargetValues)[Index].IsValid() || !(*TargetValues)[Index]->TryGetObject(TargetObject))
			{
				Result.Error = FString::Printf(TEXT("INVALID_ARGUMENT: targets[%d] is not an object {blueprintPath, graphName?, graphGuid?}"), Index);
				return Result;
			}
			const FN2CGraphTargetArgs TargetArgs = FN2CGraphTargetArgs::FromJson(*TargetObject);
			if (TargetArgs.BlueprintPath.IsEmpty())
			{
				Result.Error = FString::Printf(TEXT("INVALID_ARGUMENT: targets[%d] has no blueprintPath"), Index);
				return Result;
			}
			FRequested& Entry = Requested.AddDefaulted_GetRef();
			Entry.BlueprintPath = TargetArgs.BlueprintPath;
			Entry.GraphName = TargetArgs.GraphName;
			Entry.GraphGuid = TargetArgs.GraphGuid;
		}
	}
	else
	{
		const TArray<FN2CTaggedBlueprintGraph> Tagged = !Tag.IsEmpty()
			? UN2CGraphStateManager::Get().FindTaggedGraphs(Tag, Category)
			: UN2CGraphStateManager::Get().FindTaggedGraphsInCategory(Category);
		if (Tagged.Num() == 0)
		{
			Result.Error = !Tag.IsEmpty()
				? FString::Printf(TEXT("NO_TAGGED_GRAPHS: no graph has the tag '%s'%s (list-blueprint-tags lists the tags)"), *Tag,
					Category.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in category '%s'"), *Category))
				: FString::Printf(TEXT("NO_TAGGED_GRAPHS: no graph is tagged in category '%s' (list-blueprint-tags lists the tags)"), *Category);
			return Result;
		}
		for (const FN2CTaggedBlueprintGraph& TaggedGraph : Tagged)
		{
			FRequested& Entry = Requested.AddDefaulted_GetRef();
			Entry.BlueprintPath = TaggedGraph.OwningBlueprint.ToString();
			Entry.GraphName = TaggedGraph.GraphName;
			Entry.GraphGuid = TaggedGraph.GraphGuid.ToString();
			Entry.Tag = TaggedGraph.Tag;
			Entry.Category = TaggedGraph.Category;
		}
	}

	// Resolve each, every graph once (a graph two targets or two tags name is worked on once)
	TSet<const UEdGraph*> Seen;
	auto Add = [&Result, &Seen](FN2CBatchGraph&& Entry)
	{
		if (!Seen.Contains(Entry.Graph))
		{
			Seen.Add(Entry.Graph);
			Result.Graphs.Add(MoveTemp(Entry));
		}
	};
	for (const FRequested& Entry : Requested)
	{
		FString LoadError;
		UBlueprint* Blueprint = LoadBlueprint(Entry.BlueprintPath, LoadError);
		if (!Blueprint)
		{
			Result.Failures.Add(MakeIssue(Entry.BlueprintPath, Entry.GraphName, Entry.GraphGuid, LoadError));
			continue;
		}

		if (!Entry.GraphName.IsEmpty() || !Entry.GraphGuid.IsEmpty())
		{
			FN2CGraphTargetArgs TargetArgs;
			TargetArgs.BlueprintPath = Blueprint->GetPathName();
			TargetArgs.GraphName = Entry.GraphGuid.IsEmpty() ? Entry.GraphName : FString();
			TargetArgs.GraphGuid = Entry.GraphGuid;
			const FN2CGraphTarget Target = FN2CGraphTarget::Resolve(TargetArgs, /*bForWrite*/ false);
			if (!Target.IsValid())
			{
				Result.Failures.Add(MakeIssue(Blueprint->GetPathName(), Entry.GraphName, Entry.GraphGuid, Target.Error));
				continue;
			}
			if (!HasK2Node(*Target.Graph))
			{
				Result.Failures.Add(MakeIssue(Blueprint->GetPathName(), Target.Graph->GetName(), Target.Graph->GraphGuid.ToString(),
					FString::Printf(TEXT("EMPTY_GRAPH: graph %s has no nodes"), *Target.Graph->GetName())));
				continue;
			}
			FN2CBatchGraph Graph = MakeGraph(Blueprint, Target.Graph);
			Graph.Tag = Entry.Tag;
			Graph.Category = Entry.Category;
			Add(MoveTemp(Graph));
			continue;
		}

		// The whole Blueprint: each graph with nodes
		int32 Added = 0;
		for (UEdGraph* Graph : N2CMcpBlueprintExport::GraphsToCapture(*Blueprint))
		{
			if (!HasK2Node(*Graph))
			{
				Result.Skipped.Add(MakeIssue(Blueprint->GetPathName(), Graph->GetName(), Graph->GraphGuid.ToString(),
					FString::Printf(TEXT("EMPTY_GRAPH: graph %s has no nodes"), *Graph->GetName())));
				continue;
			}
			Add(MakeGraph(Blueprint, Graph));
			++Added;
		}
		if (Added == 0)
		{
			Result.Failures.Add(MakeIssue(Blueprint->GetPathName(), FString(), FString(),
				FString::Printf(TEXT("EMPTY_GRAPH: no graph of %s has a node"), *Blueprint->GetPathName())));
		}
	}

	if (Result.Graphs.Num() > MaxGraphs)
	{
		Result.Error = FString::Printf(TEXT("TOO_MANY_GRAPHS: the selection names %d graphs; %s takes at most %d per call. Split the targets."),
			Result.Graphs.Num(), ToolName, MaxGraphs);
		Result.Graphs.Reset();
	}
	return Result;
}
