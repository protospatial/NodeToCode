// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/N2CMigrationPlan.h"
#include "Core/N2CDependencyIndex.h"
#include "Core/N2CTagTypes.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Utils/N2CGraphUtils.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_BaseAsyncTask.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Literal.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_Switch.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_VariableSetRef.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

namespace N2CMigrationPlanPrivate
{
#if WITH_DEV_AUTOMATION_TESTS
	FString& PlansDirectoryOverride()
	{
		static FString Directory;
		return Directory;
	}
#endif

	/** Node classes with a direct C++ counterpart (a call, a variable, an event, control flow, a cast, a literal, ...) */
	bool HasNativeCounterpart(const UEdGraphNode* Node)
	{
		static const TArray<UClass*> Classes = {
			UK2Node_CallFunction::StaticClass(), UK2Node_VariableGet::StaticClass(), UK2Node_VariableSet::StaticClass(),
			UK2Node_VariableSetRef::StaticClass(), UK2Node_Event::StaticClass(), UK2Node_FunctionEntry::StaticClass(),
			UK2Node_FunctionResult::StaticClass(), UK2Node_IfThenElse::StaticClass(), UK2Node_ExecutionSequence::StaticClass(),
			UK2Node_DynamicCast::StaticClass(), UK2Node_Self::StaticClass(), UK2Node_Knot::StaticClass(),
			UK2Node_MakeStruct::StaticClass(), UK2Node_BreakStruct::StaticClass(), UK2Node_Select::StaticClass(),
			UK2Node_Switch::StaticClass(), UK2Node_Tunnel::StaticClass(), UK2Node_MakeArray::StaticClass(),
			UK2Node_MakeMap::StaticClass(), UK2Node_MakeSet::StaticClass(), UK2Node_GetArrayItem::StaticClass(),
			UK2Node_Literal::StaticClass(), UK2Node_EnumLiteral::StaticClass(), UK2Node_PromotableOperator::StaticClass(),
		};
		for (UClass* Class : Classes)
		{
			if (Node->IsA(Class))
			{
				return true;
			}
		}
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : Values)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}

	/** Path relative to the project folder with forward slashes, or the absolute path when it is outside it */
	FString ProjectRelative(const FString& AbsolutePath)
	{
		FString Path = AbsolutePath;
		FPaths::NormalizeFilename(Path);
		FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::NormalizeDirectoryName(Project);
		if (FPaths::IsUnderDirectory(Path, Project))
		{
			FPaths::MakePathRelativeTo(Path, *(Project + TEXT("/")));
		}
		return Path;
	}

	void AddRisk(TArray<TSharedPtr<FJsonValue>>& Risks, const TCHAR* Flag, int32 Count, const FString& Detail)
	{
		const TSharedRef<FJsonObject> Risk = MakeShared<FJsonObject>();
		Risk->SetStringField(TEXT("flag"), Flag);
		Risk->SetNumberField(TEXT("count"), Count);
		Risk->SetStringField(TEXT("detail"), Detail);
		Risks.Add(MakeShared<FJsonValueObject>(Risk));
	}

	/** The graphs a Blueprint's migration converts: event graphs, functions and macros of a supported type */
	TArray<TPair<UEdGraph*, FString>> GraphsToMigrate(UBlueprint& Blueprint)
	{
		TArray<TPair<UEdGraph*, FString>> Out;
		auto Add = [&Out](const TArray<TObjectPtr<UEdGraph>>& Graphs, const TCHAR* Kind)
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph && FN2CGraphUtils::ClassifyGraphType(Graph).bSupported)
				{
					Out.Add({ Graph, Kind });
				}
			}
		};
		Add(Blueprint.UbergraphPages, TEXT("event"));
		Add(Blueprint.FunctionGraphs, TEXT("function"));
		Add(Blueprint.MacroGraphs, TEXT("macro"));
		return Out;
	}

	bool WriteFile(const FString& Text, const FString& Path, FString& OutError)
	{
		const FString TempPath = Path + FString::Printf(TEXT(".%s.tmp"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!FFileHelper::SaveStringToFile(Text, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
			|| !IFileManager::Get().Move(*Path, *TempPath, /*bReplace*/ true))
		{
			IFileManager::Get().Delete(*TempPath);
			OutError = FString::Printf(TEXT("PLAN_WRITE_FAILED: could not write %s"), *Path);
			return false;
		}
		return true;
	}

	FString MarkdownCell(const FString& Text)
	{
		return Text.Replace(TEXT("|"), TEXT("\\|")).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
	}
}

FString FN2CMigrationPlan::NativeStem(const FString& AssetName)
{
	static const TCHAR* const Prefixes[] = { TEXT("BPML_"), TEXT("BPFL_"), TEXT("BPI_"), TEXT("ABP_"), TEXT("WBP_"),
		TEXT("BP_"), TEXT("ST_"), TEXT("S_"), TEXT("F_"), TEXT("E_") };
	FString Name = AssetName;
	for (const TCHAR* Prefix : Prefixes)
	{
		if (Name.StartsWith(Prefix, ESearchCase::CaseSensitive) && Name.Len() > FCString::Strlen(Prefix))
		{
			Name.RightChopInline(FCString::Strlen(Prefix));
			break;
		}
	}
	FString Stem;
	for (const TCHAR Char : Name)
	{
		if (FChar::IsAlnum(Char) || Char == TEXT('_'))
		{
			Stem.AppendChar(Char);
		}
	}
	if (Stem.IsEmpty() || FChar::IsDigit(Stem[0]))
	{
		Stem = TEXT("Migrated") + Stem;
	}
	return Stem;
}

TSharedRef<FJsonObject> FN2CMigrationPlan::Build(const FN2CDependencyIndex& Index, const FN2CMigrationPlanOptions& Options,
	const FString& PlanId)
{
	using namespace N2CMigrationPlanPrivate;

	TArray<FString> Warnings = Index.Warnings;

	// Excludes, by node; a path that names no asset of the plan is a warning
	TSet<int32> Excluded;
	TArray<FString> ExcludePaths;
	for (const FString& Path : Options.Exclude)
	{
		const int32 Node = Index.FindNode(Path.TrimStartAndEnd());
		if (Node == INDEX_NONE)
		{
			Warnings.Add(FString::Printf(TEXT("exclude: %s names no asset of this plan"), *Path));
			ExcludePaths.AddUnique(Path.TrimStartAndEnd());
			continue;
		}
		Excluded.Add(Node);
		ExcludePaths.AddUnique(Index.Nodes[Node].Path);
	}

	TMap<int32, int32> GroupOf;
	for (int32 Group = 0; Group < Index.CycleGroups.Num(); ++Group)
	{
		for (const int32 Node : Index.CycleGroups[Group])
		{
			GroupOf.Add(Node, Group);
		}
	}

	// The native type each asset becomes; a name taken twice gets 2, 3, ... and a name-collision flag
	TArray<FString> NativeNames;
	TArray<bool> Renamed;
	NativeNames.SetNum(Index.Nodes.Num());
	Renamed.SetNumZeroed(Index.Nodes.Num());
	TArray<UBlueprint*> Blueprints;
	Blueprints.SetNumZeroed(Index.Nodes.Num());
	TSet<FString> Taken;
	for (const int32 NodeIndex : Index.Order)
	{
		const FN2CDependencyNode& Node = Index.Nodes[NodeIndex];
		FString Prefix;
		switch (Node.Kind)
		{
		case EN2CDependencyNodeKind::Enum: Prefix = TEXT("E"); break;
		case EN2CDependencyNodeKind::Struct: Prefix = TEXT("F"); break;
		case EN2CDependencyNodeKind::Interface: Prefix = TEXT("U"); break;
		case EN2CDependencyNodeKind::MacroLibrary: continue;
		case EN2CDependencyNodeKind::Blueprint:
		{
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Node.Path);
			Blueprints[NodeIndex] = Blueprint;
			Prefix = Blueprint && Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(AActor::StaticClass()) ? TEXT("A") : TEXT("U");
			break;
		}
		}
		const FString Stem = NativeStem(Node.Name);
		FString Name = Prefix + Stem;
		for (int32 Suffix = 2; Taken.Contains(Name.Mid(1)); ++Suffix)
		{
			Name = FString::Printf(TEXT("%s%s%d"), *Prefix, *Stem, Suffix);
			Renamed[NodeIndex] = true;
		}
		// Unreal's prefixes differ (AFoo and UFoo are both Foo to UHT), so collisions are checked without them
		Taken.Add(Name.Mid(1));
		NativeNames[NodeIndex] = Name;
	}

	// Where the files go: Public/ and Private/ when the module has them, else its Source folder
	const FString ModuleDir = Options.ModuleSourceDir;
	const bool bSplit = !ModuleDir.IsEmpty() && IFileManager::Get().DirectoryExists(*(ModuleDir / TEXT("Public")))
		&& IFileManager::Get().DirectoryExists(*(ModuleDir / TEXT("Private")));
	const FString HeaderDir = bSplit ? ModuleDir / TEXT("Public") : ModuleDir;
	const FString SourceDir = bSplit ? ModuleDir / TEXT("Private") : ModuleDir;

	TMap<FString, int32> RiskCounts;
	int32 BlueprintCount = 0;
	int64 PlanTokens = 0;

	auto AssetJson = [&](int32 NodeIndex) -> TSharedRef<FJsonObject>
	{
		const FN2CDependencyNode& Node = Index.Nodes[NodeIndex];
		const TSharedRef<FJsonObject> Asset = MakeShared<FJsonObject>();
		Asset->SetStringField(TEXT("path"), Node.Path);
		Asset->SetStringField(TEXT("name"), Node.Name);
		Asset->SetStringField(TEXT("kind"), FN2CDependencyIndex::NodeKindToString(Node.Kind));
		Asset->SetBoolField(TEXT("root"), Node.bRoot);
		const bool bExcluded = Excluded.Contains(NodeIndex);
		Asset->SetBoolField(TEXT("excluded"), bExcluded);

		TArray<TSharedPtr<FJsonValue>> Risks;
		if (!NativeNames[NodeIndex].IsEmpty())
		{
			const TSharedRef<FJsonObject> Native = MakeShared<FJsonObject>();
			const FString& Name = NativeNames[NodeIndex];
			const FString FileStem = Name.Mid(1);
			Native->SetStringField(TEXT("name"), Name);
			if (Node.Kind == EN2CDependencyNodeKind::Interface)
			{
				Native->SetStringField(TEXT("interfaceName"), TEXT("I") + FileStem);
			}
			Native->SetStringField(TEXT("header"), ModuleDir.IsEmpty() ? FileStem + TEXT(".h") : ProjectRelative(HeaderDir / FileStem + TEXT(".h")));
			// Enums and structs are header-only
			if (Node.Kind == EN2CDependencyNodeKind::Blueprint || Node.Kind == EN2CDependencyNodeKind::Interface)
			{
				Native->SetStringField(TEXT("source"), ModuleDir.IsEmpty() ? FileStem + TEXT(".cpp") : ProjectRelative(SourceDir / FileStem + TEXT(".cpp")));
			}
			Asset->SetObjectField(TEXT("native"), Native);
		}
		else
		{
			AddRisk(Risks, TEXT("macro-library"), 1, TEXT("A macro library has no native type: its macros become code where they are used"));
		}
		if (Renamed[NodeIndex])
		{
			AddRisk(Risks, TEXT("name-collision"), 1, FString::Printf(TEXT("Another asset of the plan has the native name %s; this one is %s"),
				*(NativeStem(Node.Name)), *NativeNames[NodeIndex]));
		}
		if (const int32* Group = GroupOf.Find(NodeIndex))
		{
			TArray<FString> Others;
			for (const int32 Other : Index.CycleGroups[*Group])
			{
				if (Other != NodeIndex)
				{
					Others.Add(Index.Nodes[Other].Name);
				}
			}
			AddRisk(Risks, TEXT("cycle"), Others.Num(), TEXT("Depends on, and is needed by: ") + FString::Join(Others, TEXT(", ")));
		}

		if (UBlueprint* Blueprint = Blueprints[NodeIndex])
		{
			++BlueprintCount;
			UClass* Parent = Blueprint->ParentClass;
			Asset->SetStringField(TEXT("parentClass"), Parent ? Parent->GetPathName() : FString());
			// The generated class derives from the parent's native class, or from the parent Blueprint's planned one
			FString Base;
			if (Parent && Parent->ClassGeneratedBy)
			{
				const int32 ParentNode = Index.FindNode(Parent->ClassGeneratedBy->GetPathName());
				if (ParentNode != INDEX_NONE && !NativeNames[ParentNode].IsEmpty())
				{
					Base = NativeNames[ParentNode];
				}
				else
				{
					Base = Parent->GetName();
					AddRisk(Risks, TEXT("parent-not-in-plan"), 1, FString::Printf(
						TEXT("The parent Blueprint %s is not in this plan: migrate it first, or keep this class Blueprint-only"), *Parent->ClassGeneratedBy->GetPathName()));
				}
			}
			else if (Parent)
			{
				Base = FString(Parent->GetPrefixCPP()) + Parent->GetName();
			}
			Asset->SetStringField(TEXT("nativeBase"), Base);

			TArray<TSharedPtr<FJsonValue>> Graphs;
			int64 Tokens = 0;
			for (const TPair<UEdGraph*, FString>& Graph : GraphsToMigrate(*Blueprint))
			{
				const TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
				GraphJson->SetStringField(TEXT("name"), Graph.Key->GetName());
				GraphJson->SetStringField(TEXT("graphGuid"), Graph.Key->GraphGuid.ToString(EGuidFormats::Digits));
				GraphJson->SetStringField(TEXT("kind"), Graph.Value);
				GraphJson->SetNumberField(TEXT("nodeCount"), Graph.Key->Nodes.Num());
				int32 GraphTokens = 0;
				if (Options.bEstimateTokens && Graph.Key->Nodes.Num() > 0)
				{
					FN2CTagInfo Info;
					Info.BlueprintPath = Blueprint->GetPathName();
					Info.GraphGuid = Graph.Key->GraphGuid.ToString();
					Info.GraphName = Graph.Key->GetName();
					GraphTokens = FN2CTokenEstimationService::Get().GetTokenEstimate(Info);
				}
				GraphJson->SetNumberField(TEXT("estimatedTokens"), GraphTokens);
				Tokens += GraphTokens;
				Graphs.Add(MakeShared<FJsonValueObject>(GraphJson));
			}
			Asset->SetArrayField(TEXT("graphs"), Graphs);
			Asset->SetNumberField(TEXT("estimatedTokens"), static_cast<double>(Tokens));
			if (!bExcluded)
			{
				PlanTokens += Tokens;
			}

			// Risks read from every graph of the Blueprint, nested ones included
			int32 Latent = 0, Timelines = 0, Macros = 0;
			TMap<FString, int32> Others;
			TArray<UEdGraph*> AllGraphs;
			Blueprint->GetAllGraphs(AllGraphs);
			for (const UEdGraph* Graph : AllGraphs)
			{
				if (!Graph || !FN2CGraphUtils::ClassifyGraphType(Graph).bSupported)
				{
					continue;
				}
				for (const UEdGraphNode* GraphNode : Graph->Nodes)
				{
					if (!GraphNode || GraphNode->IsA<UEdGraphNode_Comment>())
					{
						continue;
					}
					if (GraphNode->IsA<UK2Node_BaseAsyncTask>())
					{
						++Latent;
					}
					else if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(GraphNode))
					{
						const UFunction* Function = Call->GetTargetFunction();
						if (Function && Function->HasMetaData(FBlueprintMetadata::MD_Latent))
						{
							++Latent;
						}
					}
					else if (GraphNode->IsA<UK2Node_Timeline>())
					{
						++Timelines;
					}
					else if (GraphNode->IsA<UK2Node_MacroInstance>())
					{
						++Macros;
					}
					else if (!HasNativeCounterpart(GraphNode))
					{
						Others.FindOrAdd(GraphNode->GetClass()->GetName())++;
					}
				}
			}
			if (Latent > 0)
			{
				AddRisk(Risks, TEXT("latent"), Latent, TEXT("Latent or async nodes (Delay, async actions): they need a latent action, timer or delegate in C++"));
			}
			if (Timelines > 0)
			{
				AddRisk(Risks, TEXT("timeline"), Timelines, TEXT("Timelines: they become a UTimelineComponent with curves set up in C++"));
			}
			if (Macros > 0)
			{
				AddRisk(Risks, TEXT("macro"), Macros, TEXT("Macro instances (ForEachLoop and other macros): each is expanded into code"));
			}
			if (Others.Num() > 0)
			{
				Others.KeySort(TLess<FString>());
				TArray<FString> Listed;
				int32 Count = 0;
				for (const TPair<FString, int32>& Pair : Others)
				{
					Listed.Add(FString::Printf(TEXT("%s x%d"), *Pair.Key, Pair.Value));
					Count += Pair.Value;
				}
				AddRisk(Risks, TEXT("blueprint-only-nodes"), Count, TEXT("Node classes without a direct C++ counterpart: ") + FString::Join(Listed, TEXT(", ")));
			}
		}

		for (const TSharedPtr<FJsonValue>& Risk : Risks)
		{
			RiskCounts.FindOrAdd(Risk->AsObject()->GetStringField(TEXT("flag")))++;
		}
		Asset->SetArrayField(TEXT("risks"), Risks);
		return Asset;
	};

	// One step per asset in dependency order; a cycle group is one step, where its first member comes
	TArray<TSharedPtr<FJsonValue>> Steps;
	TSet<int32> Emitted;
	for (const int32 NodeIndex : Index.Order)
	{
		if (Emitted.Contains(NodeIndex))
		{
			continue;
		}
		const int32* Group = GroupOf.Find(NodeIndex);
		const TArray<int32> Members = Group ? Index.CycleGroups[*Group] : TArray<int32>({ NodeIndex });
		TArray<TSharedPtr<FJsonValue>> Assets;
		bool bAllExcluded = true;
		for (const int32 Member : Members)
		{
			Emitted.Add(Member);
			Assets.Add(MakeShared<FJsonValueObject>(AssetJson(Member)));
			bAllExcluded &= Excluded.Contains(Member);
		}
		const TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetNumberField(TEXT("step"), Steps.Num() + 1);
		Step->SetStringField(TEXT("kind"), Group ? TEXT("cycle-group") : TEXT("asset"));
		Step->SetBoolField(TEXT("excluded"), bAllExcluded);
		Step->SetArrayField(TEXT("assets"), Assets);
		Steps.Add(MakeShared<FJsonValueObject>(Step));
	}

	const TSharedRef<FJsonObject> Plan = MakeShared<FJsonObject>();
	Plan->SetNumberField(TEXT("schemaVersion"), SchemaVersion);
	Plan->SetStringField(TEXT("planId"), PlanId);
	Plan->SetStringField(TEXT("status"), TEXT("draft"));
	Plan->SetStringField(TEXT("approval"), TEXT("Review the steps, then set status to \"approved\" in this file; later migration stages run only an approved plan. Add asset paths to exclude to leave them out."));
	Plan->SetStringField(TEXT("createdAt"), FDateTime::UtcNow().ToIso8601());
	Plan->SetArrayField(TEXT("roots"), Strings(Options.Roots));
	Plan->SetStringField(TEXT("targetModule"), Options.TargetModule);
	Plan->SetStringField(TEXT("moduleSourceDir"), ModuleDir.IsEmpty() ? FString() : ProjectRelative(ModuleDir));
	Plan->SetArrayField(TEXT("exclude"), Strings(ExcludePaths));
	const TSharedRef<FJsonObject> OptionsJson = MakeShared<FJsonObject>();
	OptionsJson->SetBoolField(TEXT("includeReferenced"), Options.bIncludeReferenced);
	OptionsJson->SetBoolField(TEXT("estimateTokens"), Options.bEstimateTokens);
	Plan->SetObjectField(TEXT("options"), OptionsJson);
	Plan->SetNumberField(TEXT("stepCount"), Steps.Num());
	Plan->SetNumberField(TEXT("assetCount"), Index.Nodes.Num());
	Plan->SetNumberField(TEXT("blueprintCount"), BlueprintCount);
	Plan->SetNumberField(TEXT("excludedCount"), Excluded.Num());
	Plan->SetNumberField(TEXT("estimatedTokens"), static_cast<double>(PlanTokens));
	const TSharedRef<FJsonObject> Counts = MakeShared<FJsonObject>();
	RiskCounts.KeySort(TLess<FString>());
	for (const TPair<FString, int32>& Pair : RiskCounts)
	{
		Counts->SetNumberField(Pair.Key, Pair.Value);
	}
	Plan->SetObjectField(TEXT("riskCounts"), Counts);
	Plan->SetArrayField(TEXT("steps"), Steps);
	Plan->SetArrayField(TEXT("warnings"), Strings(Warnings));
	return Plan;
}

FString FN2CMigrationPlan::ToMarkdown(const TSharedRef<FJsonObject>& Plan)
{
	using namespace N2CMigrationPlanPrivate;

	auto StringsOf = [](const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsString());
			}
		}
		return Out;
	};

	TStringBuilder<8192> Md;
	Md.Appendf(TEXT("# Migration plan %s\n\n"), *Plan->GetStringField(TEXT("planId")));
	Md.Appendf(TEXT("- Status: **%s**\n"), *Plan->GetStringField(TEXT("status")));
	Md.Appendf(TEXT("- Roots: %s\n"), *FString::Join(StringsOf(Plan, TEXT("roots")), TEXT(", ")));
	Md.Appendf(TEXT("- Target module: %s (%s)\n"), *Plan->GetStringField(TEXT("targetModule")), *Plan->GetStringField(TEXT("moduleSourceDir")));
	Md.Appendf(TEXT("- %d steps, %d assets, %d Blueprints, %d excluded, about %d tokens to translate\n"),
		static_cast<int32>(Plan->GetNumberField(TEXT("stepCount"))), static_cast<int32>(Plan->GetNumberField(TEXT("assetCount"))),
		static_cast<int32>(Plan->GetNumberField(TEXT("blueprintCount"))), static_cast<int32>(Plan->GetNumberField(TEXT("excludedCount"))),
		static_cast<int32>(Plan->GetNumberField(TEXT("estimatedTokens"))));
	const TSharedPtr<FJsonObject>* Counts = nullptr;
	if (Plan->TryGetObjectField(TEXT("riskCounts"), Counts) && (*Counts)->Values.Num() > 0)
	{
		TArray<FString> Listed;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Counts)->Values)
		{
			Listed.Add(FString::Printf(TEXT("%s %d"), *Pair.Key, static_cast<int32>(Pair.Value->AsNumber())));
		}
		Md.Appendf(TEXT("- Assets with risks: %s\n"), *FString::Join(Listed, TEXT(", ")));
	}
	Md.Append(TEXT("\nNothing has changed yet. To approve, set `\"status\": \"approved\"` in plan.json; to leave an asset out, add its path to `\"exclude\"` there. Later migration stages read plan.json, not this file.\n\n"));
	Md.Append(TEXT("| Step | Asset | Kind | Native type | Files | Graphs | Tokens | Risks |\n|---|---|---|---|---|---|---|---|\n"));

	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (Plan->TryGetArrayField(TEXT("steps"), Steps))
	{
		for (const TSharedPtr<FJsonValue>& StepValue : *Steps)
		{
			const TSharedPtr<FJsonObject> Step = StepValue->AsObject();
			const bool bCycle = Step->GetStringField(TEXT("kind")) == TEXT("cycle-group");
			for (const TSharedPtr<FJsonValue>& AssetValue : Step->GetArrayField(TEXT("assets")))
			{
				const TSharedPtr<FJsonObject> Asset = AssetValue->AsObject();
				const TSharedPtr<FJsonObject>* Native = nullptr;
				FString NativeName = TEXT("none"), Files;
				if (Asset->TryGetObjectField(TEXT("native"), Native))
				{
					NativeName = (*Native)->GetStringField(TEXT("name"));
					FString Source;
					Files = (*Native)->GetStringField(TEXT("header"));
					if ((*Native)->TryGetStringField(TEXT("source"), Source))
					{
						Files += TEXT(", ") + Source;
					}
				}
				TArray<FString> Graphs;
				const TArray<TSharedPtr<FJsonValue>>* GraphValues = nullptr;
				if (Asset->TryGetArrayField(TEXT("graphs"), GraphValues))
				{
					for (const TSharedPtr<FJsonValue>& Graph : *GraphValues)
					{
						Graphs.Add(Graph->AsObject()->GetStringField(TEXT("name")));
					}
				}
				TArray<FString> Risks;
				for (const TSharedPtr<FJsonValue>& Risk : Asset->GetArrayField(TEXT("risks")))
				{
					Risks.Add(Risk->AsObject()->GetStringField(TEXT("flag")));
				}
				double Tokens = 0.0;
				Asset->TryGetNumberField(TEXT("estimatedTokens"), Tokens);
				Md.Appendf(TEXT("| %d%s | %s%s | %s | %s | %s | %s | %d | %s |\n"),
					static_cast<int32>(Step->GetNumberField(TEXT("step"))), bCycle ? TEXT(" (cycle)") : TEXT(""),
					*MarkdownCell(Asset->GetStringField(TEXT("path"))), Asset->GetBoolField(TEXT("excluded")) ? TEXT(" (excluded)") : TEXT(""),
					*Asset->GetStringField(TEXT("kind")), *MarkdownCell(NativeName), *MarkdownCell(Files),
					*MarkdownCell(FString::Join(Graphs, TEXT(", "))), static_cast<int32>(Tokens), *FString::Join(Risks, TEXT(", ")));
			}
		}
	}
	const TArray<FString> Warnings = StringsOf(Plan, TEXT("warnings"));
	if (Warnings.Num() > 0)
	{
		Md.Append(TEXT("\n## Warnings\n\n"));
		for (const FString& Warning : Warnings)
		{
			Md.Appendf(TEXT("- %s\n"), *MarkdownCell(Warning));
		}
	}
	return FString(Md.ToString());
}

FN2CMigrationPlanResult FN2CMigrationPlan::Create(const FN2CMigrationPlanOptions& Options)
{
	using namespace N2CMigrationPlanPrivate;

	FN2CMigrationPlanResult Result;
	FN2CDependencyIndexOptions IndexOptions;
	IndexOptions.bLoadAssets = true;
	IndexOptions.bIncludeReferencedUserAssets = Options.bIncludeReferenced;
	const FN2CDependencyIndex Index = FN2CDependencyIndex::Build(Options.Roots, IndexOptions);
	if (Index.Nodes.Num() == 0)
	{
		Result.Error = FString::Printf(TEXT("NOT_FOUND: no Blueprint, Blueprint interface, macro library, user struct or user enum under %s.%s%s"),
			*FString::Join(Options.Roots, TEXT(", ")), Index.Warnings.Num() > 0 ? TEXT(" ") : TEXT(""), *FString::Join(Index.Warnings, TEXT(" ")));
		return Result;
	}

	Result.PlanId = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6).ToLower();
	const FString Root = PlansDirectory();
	Result.PlanDir = FPaths::ConvertRelativePathToFull(Root / Result.PlanId);
	if (!FN2CPathUtils::IsUnderDirectoryResolved(Result.PlanDir, Root) || !IFileManager::Get().MakeDirectory(*Result.PlanDir, true))
	{
		Result.Error = FString::Printf(TEXT("PLAN_WRITE_FAILED: could not create %s"), *Result.PlanDir);
		return Result;
	}

	const TSharedRef<FJsonObject> Plan = Build(Index, Options, Result.PlanId);
	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Plan, Writer);
	Result.PlanJsonPath = Result.PlanDir / TEXT("plan.json");
	Result.PlanMdPath = Result.PlanDir / TEXT("plan.md");
	if (!WriteFile(Json, Result.PlanJsonPath, Result.Error) || !WriteFile(ToMarkdown(Plan), Result.PlanMdPath, Result.Error))
	{
		return Result;
	}
	Result.Plan = Plan;
	return Result;
}

FString FN2CMigrationPlan::PlansDirectory()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!N2CMigrationPlanPrivate::PlansDirectoryOverride().IsEmpty())
	{
		return N2CMigrationPlanPrivate::PlansDirectoryOverride();
	}
#endif
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Migration"));
}

#if WITH_DEV_AUTOMATION_TESTS
void FN2CMigrationPlan::SetPlansDirectoryForTests(const FString& Directory)
{
	N2CMigrationPlanPrivate::PlansDirectoryOverride() = Directory;
}
#endif
