// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Tools/N2CMcpFunctionGuidUtils.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Core/N2CEditorIntegration.h"
#include "Utils/N2CLogger.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintEditor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node_Composite.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CGraphTargetPrivate
{
	FString Serialize(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	FN2CGraphTarget Fail(const FString& Error)
	{
		FN2CGraphTarget Target;
		Target.Error = Error;
		return Target;
	}

	/** "Name (kind, GUID)" for every graph of the Blueprint, for GRAPH_NOT_FOUND messages */
	FString ListGraphs(const UBlueprint* Blueprint)
	{
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		TArray<FString> Names;
		for (const UEdGraph* Graph : Graphs)
		{
			if (Graph)
			{
				Names.Add(FString::Printf(TEXT("%s (%s, %s)"), *Graph->GetName(), *FN2CGraphTarget::GraphKind(Graph),
					*Graph->GraphGuid.ToString()));
			}
		}
		return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("none"));
	}

	UBlueprint* FocusedBlueprint()
	{
		const TSharedPtr<FBlueprintEditor> Editor = FN2CEditorIntegration::Get().GetActiveBlueprintEditor();
		return Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	}
}

FN2CGraphTargetArgs FN2CGraphTargetArgs::FromJson(const TSharedPtr<FJsonObject>& Arguments)
{
	FN2CGraphTargetArgs Args;
	if (Arguments.IsValid())
	{
		Arguments->TryGetStringField(TEXT("blueprintPath"), Args.BlueprintPath);
		Arguments->TryGetStringField(TEXT("graphName"), Args.GraphName);
		Arguments->TryGetStringField(TEXT("graphGuid"), Args.GraphGuid);
	}
	Args.BlueprintPath.TrimStartAndEndInline();
	Args.GraphName.TrimStartAndEndInline();
	Args.GraphGuid.TrimStartAndEndInline();
	return Args;
}

FString FN2CGraphTarget::ErrorCode() const
{
	FString Code;
	return Error.Split(TEXT(":"), &Code, nullptr) ? Code : Error;
}

UBlueprint* FN2CGraphTarget::LoadBlueprint(const FString& InBlueprintPath, FString& OutError)
{
	OutError.Empty();
	FString BlueprintPath = InBlueprintPath.TrimStartAndEnd();
	if (BlueprintPath.IsEmpty())
	{
		UBlueprint* Focused = N2CGraphTargetPrivate::FocusedBlueprint();
		if (!Focused)
		{
			OutError = TEXT("NO_FOCUSED_GRAPH: no blueprintPath was given and no Blueprint editor is focused. Pass blueprintPath.");
		}
		return Focused;
	}

	// '/Game/BP_Door' names the package; the Blueprint is the object of the same name inside it
	TArray<FString> Candidates;
	Candidates.Add(BlueprintPath);
	if (!BlueprintPath.Contains(TEXT(".")))
	{
		Candidates.Add(BlueprintPath + TEXT(".") + FPackageName::GetShortName(BlueprintPath));
	}

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	for (const FString& Candidate : Candidates)
	{
		// Loaded already (also a Blueprint that was never saved, which the asset registry does not list)
		if (UBlueprint* Loaded = FindObject<UBlueprint>(nullptr, *Candidate))
		{
			return Loaded;
		}
		const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(Candidate));
		if (AssetData.IsValid())
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset()))
			{
				return Blueprint;
			}
		}
	}
	for (const FString& Candidate : Candidates)
	{
		// On disk but not (yet) in the asset registry
		if (FPackageName::IsValidObjectPath(Candidate))
		{
			if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Candidate, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Blueprint;
			}
		}
	}

	OutError = FString::Printf(TEXT("ASSET_NOT_FOUND: no Blueprint at '%s'. Use the asset path, e.g. /Game/Blueprints/BP_Door.BP_Door"),
		*BlueprintPath);
	return nullptr;
}

TArray<UEdGraph*> FN2CGraphTarget::FindGraphsByName(const UBlueprint* Blueprint, const FString& GraphName)
{
	TArray<UEdGraph*> Found;
	if (!Blueprint || GraphName.IsEmpty())
	{
		return Found;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			Found.AddUnique(Graph);
		}
	}
	return Found;
}

TArray<UEdGraph*> FN2CGraphTarget::FindGraphsByGuid(const UBlueprint* Blueprint, const FGuid& GraphGuid)
{
	TArray<UEdGraph*> Found;
	if (!Blueprint || !GraphGuid.IsValid())
	{
		return Found;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GraphGuid == GraphGuid)
		{
			Found.AddUnique(Graph);
		}
	}
	return Found;
}

FString FN2CGraphTarget::GraphKind(const UEdGraph* Graph)
{
	if (!Graph)
	{
		return TEXT("graph");
	}
	const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (Blueprint)
	{
		if (Blueprint->UbergraphPages.Contains(Graph))
		{
			return TEXT("event");
		}
		if (Blueprint->FunctionGraphs.Contains(Graph))
		{
			return TEXT("function");
		}
		if (Blueprint->MacroGraphs.Contains(Graph))
		{
			return TEXT("macro");
		}
		if (Blueprint->DelegateSignatureGraphs.Contains(Graph))
		{
			return TEXT("delegate");
		}
		for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
		{
			if (Interface.Graphs.Contains(Graph))
			{
				return TEXT("interface");
			}
		}
	}
	if (Cast<UK2Node_Composite>(Graph->GetOuter()))
	{
		return TEXT("collapsed");
	}
	return TEXT("graph");
}

FString FN2CGraphTarget::ResolvedByToString(EN2CGraphResolvedBy ResolvedBy)
{
	switch (ResolvedBy)
	{
	case EN2CGraphResolvedBy::Focus:
		return TEXT("focus");
	case EN2CGraphResolvedBy::EventGraph:
		return TEXT("eventGraph");
	default:
		return TEXT("explicit");
	}
}

FN2CGraphTarget FN2CGraphTarget::Resolve(const TSharedPtr<FJsonObject>& Arguments, bool bForWrite)
{
	return Resolve(FN2CGraphTargetArgs::FromJson(Arguments), bForWrite);
}

FN2CGraphTarget FN2CGraphTarget::Resolve(const FN2CGraphTargetArgs& Args, bool bForWrite)
{
	using namespace N2CGraphTargetPrivate;
	check(IsInGameThread());

	FN2CGraphTarget Target;

	if (Args.IsEmpty())
	{
		// No target named: the focused graph
		Target.Graph = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
		if (!Target.Graph)
		{
			return Fail(TEXT("NO_FOCUSED_GRAPH: no graph is focused in a Blueprint editor. Pass blueprintPath with graphName or graphGuid, or open the graph first."));
		}
		Target.Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Target.Graph);
		if (!Target.Blueprint)
		{
			return Fail(FString::Printf(TEXT("NO_FOCUSED_GRAPH: the focused graph %s belongs to no Blueprint"), *Target.Graph->GetName()));
		}
		Target.ResolvedBy = EN2CGraphResolvedBy::Focus;
	}
	else
	{
		// The Blueprint: blueprintPath, else (a graph argument alone) the focused Blueprint
		FString LoadError;
		Target.Blueprint = LoadBlueprint(Args.BlueprintPath, LoadError);
		if (!Target.Blueprint)
		{
			if (Args.BlueprintPath.IsEmpty())
			{
				LoadError = FString::Printf(TEXT("NO_FOCUSED_GRAPH: %s was given without blueprintPath and no Blueprint editor is focused. Pass blueprintPath."),
					!Args.GraphGuid.IsEmpty() ? TEXT("graphGuid") : TEXT("graphName"));
			}
			return Fail(LoadError);
		}

		if (!Args.GraphGuid.IsEmpty())
		{
			FGuid Guid;
			if (!FGuid::Parse(Args.GraphGuid, Guid) || !Guid.IsValid())
			{
				return Fail(FString::Printf(TEXT("GRAPH_NOT_FOUND: graphGuid '%s' is not a GUID. Graphs of %s: %s"), *Args.GraphGuid,
					*Target.Blueprint->GetPathName(), *ListGraphs(Target.Blueprint)));
			}
			const TArray<UEdGraph*> Found = FindGraphsByGuid(Target.Blueprint, Guid);
			if (Found.Num() > 1)
			{
				TArray<FString> Names;
				for (const UEdGraph* Graph : Found)
				{
					Names.Add(FString::Printf(TEXT("%s (%s)"), *Graph->GetName(), *GraphKind(Graph)));
				}
				return Fail(FString::Printf(TEXT("AMBIGUOUS_GRAPH_NAME: %d graphs of %s have the GraphGuid %s (%s). Pass graphName instead."),
					Found.Num(), *Target.Blueprint->GetPathName(), *Args.GraphGuid, *FString::Join(Names, TEXT(", "))));
			}
			if (Found.Num() == 1)
			{
				Target.Graph = Found[0];
			}
			else
			{
				// A function's ID from before card 0152 (the MD5 of path and name), accepted for one release
				bool bLegacy = false;
				UEdGraph* Function = FN2CMcpFunctionGuidUtils::FindFunctionByGuid(Target.Blueprint, Guid, &bLegacy);
				if (!Function)
				{
					return Fail(FString::Printf(TEXT("GRAPH_NOT_FOUND: no graph with the GUID %s in %s. Graphs: %s"), *Args.GraphGuid,
						*Target.Blueprint->GetPathName(), *ListGraphs(Target.Blueprint)));
				}
				Target.Graph = Function;
				Target.bLegacyId = bLegacy;
				Target.LegacyId = bLegacy ? Args.GraphGuid : FString();
			}
			Target.ResolvedBy = EN2CGraphResolvedBy::Explicit;
		}
		else if (!Args.GraphName.IsEmpty())
		{
			const TArray<UEdGraph*> Found = FindGraphsByName(Target.Blueprint, Args.GraphName);
			if (Found.Num() == 0)
			{
				return Fail(FString::Printf(TEXT("GRAPH_NOT_FOUND: no graph '%s' in %s. Graphs: %s"), *Args.GraphName,
					*Target.Blueprint->GetPathName(), *ListGraphs(Target.Blueprint)));
			}
			if (Found.Num() > 1)
			{
				TArray<FString> Candidates;
				for (const UEdGraph* Graph : Found)
				{
					Candidates.Add(FString::Printf(TEXT("%s (%s, graphGuid %s)"), *Graph->GetName(), *GraphKind(Graph),
						*Graph->GraphGuid.ToString()));
				}
				return Fail(FString::Printf(TEXT("AMBIGUOUS_GRAPH_NAME: %d graphs of %s are named '%s': %s. Pass graphGuid instead."),
					Found.Num(), *Target.Blueprint->GetPathName(), *Args.GraphName, *FString::Join(Candidates, TEXT("; "))));
			}
			Target.Graph = Found[0];
			Target.ResolvedBy = EN2CGraphResolvedBy::Explicit;
		}
		else
		{
			// blueprintPath alone: its focused graph when one of its graphs is focused, else its event graph
			UEdGraph* Focused = FN2CEditorIntegration::Get().GetFocusedGraphFromActiveEditor();
			if (Focused && FBlueprintEditorUtils::FindBlueprintForGraph(Focused) == Target.Blueprint)
			{
				Target.Graph = Focused;
				Target.ResolvedBy = EN2CGraphResolvedBy::Focus;
			}
			else
			{
				Target.Graph = FBlueprintEditorUtils::FindEventGraph(Target.Blueprint);
				Target.ResolvedBy = EN2CGraphResolvedBy::EventGraph;
				if (!Target.Graph)
				{
					return Fail(FString::Printf(TEXT("GRAPH_NOT_FOUND: %s has no event graph and none of its graphs is focused. Pass graphName or graphGuid. Graphs: %s"),
						*Target.Blueprint->GetPathName(), *ListGraphs(Target.Blueprint)));
				}
			}
		}
	}

	if (bForWrite && FBlueprintEditorUtils::IsGraphReadOnly(Target.Graph))
	{
		return Fail(FString::Printf(TEXT("READ_ONLY_GRAPH: %s (%s) in %s is read-only in the Blueprint editor (interface and delegate signature graphs, math expressions, graphs that are not editable); it cannot be edited"),
			*Target.Graph->GetName(), *GraphKind(Target.Graph), *Target.Blueprint->GetPathName()));
	}

	if (Target.bLegacyId)
	{
		FN2CLogger::Get().LogWarning(LegacyIdWarning(Target.LegacyId, Target.Graph));
	}
	return Target;
}

FString FN2CGraphTarget::LegacyIdWarning(const FString& OldId, const UEdGraph* Graph)
{
	return FString::Printf(TEXT("'%s' is an old function ID (a hash of the Blueprint path and function name that changes on rename). It is deprecated and accepted only until NodeToCode %s; use this function's stable ID %s (its GraphGuid) instead."),
		*OldId, FN2CMcpFunctionGuidUtils::LegacyIdRemovalVersion(), Graph ? *FN2CMcpFunctionGuidUtils::GetFunctionId(Graph).ToString() : TEXT("?"));
}

TSharedRef<FJsonObject> FN2CGraphTarget::ToJson() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	if (Blueprint)
	{
		Json->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Json->SetStringField(TEXT("blueprintName"), Blueprint->GetName());
	}
	if (Graph)
	{
		Json->SetStringField(TEXT("graphName"), Graph->GetName());
		Json->SetStringField(TEXT("graphGuid"), Graph->GraphGuid.ToString());
		Json->SetStringField(TEXT("graphKind"), GraphKind(Graph));
	}
	Json->SetStringField(TEXT("resolvedBy"), ResolvedByToString(ResolvedBy));
	if (bLegacyId)
	{
		Json->SetStringField(TEXT("deprecatedId"), LegacyId);
		Json->SetStringField(TEXT("warning"), LegacyIdWarning(LegacyId, Graph));
	}
	return Json;
}

void FN2CGraphTarget::AttachTo(FMcpToolCallResult& Result) const
{
	using namespace N2CGraphTargetPrivate;
	if (!Blueprint || !Graph)
	{
		return;
	}
	const TSharedRef<FJsonObject> TargetJson = ToJson();

	for (const TSharedPtr<FJsonObject>& Item : Result.Content)
	{
		FString Type;
		FString Text;
		if (!Item.IsValid() || !Item->TryGetStringField(TEXT("type"), Type) || Type != TEXT("text")
			|| !Item->TryGetStringField(TEXT("text"), Text))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			if (!Parsed->HasField(TEXT("target")))
			{
				Parsed->SetObjectField(TEXT("target"), TargetJson);
				Item->SetStringField(TEXT("text"), Serialize(Parsed.ToSharedRef()));
			}
			return;
		}
		break;
	}

	// An error text or other non-JSON result: the target as its own text item
	const TSharedRef<FJsonObject> Wrapper = MakeShared<FJsonObject>();
	Wrapper->SetObjectField(TEXT("target"), TargetJson);
	const TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
	TextContent->SetStringField(TEXT("type"), TEXT("text"));
	TextContent->SetStringField(TEXT("text"), Serialize(Wrapper));
	Result.Content.Add(TextContent);
}

FMcpToolCallResult FN2CGraphTarget::Finish(FMcpToolCallResult Result) const
{
	AttachTo(Result);
	return Result;
}

void FN2CGraphTarget::AddSchemaProperties(FMcpToolDefinition& Definition, bool bGraphArguments)
{
	if (!Definition.InputSchema.IsValid())
	{
		Definition.InputSchema = MakeShared<FJsonObject>();
		Definition.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
	}
	const TSharedPtr<FJsonObject>* ExistingProperties = nullptr;
	TSharedPtr<FJsonObject> Properties;
	if (Definition.InputSchema->TryGetObjectField(TEXT("properties"), ExistingProperties) && ExistingProperties->IsValid())
	{
		Properties = *ExistingProperties;
	}
	else
	{
		Properties = MakeShared<FJsonObject>();
		Definition.InputSchema->SetObjectField(TEXT("properties"), Properties);
	}

	auto AddString = [&Properties](const TCHAR* Name, const TCHAR* Description)
	{
		if (!Properties->HasField(Name))
		{
			const TSharedPtr<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("type"), TEXT("string"));
			Property->SetStringField(TEXT("description"), Description);
			Properties->SetObjectField(Name, Property);
		}
	};

	AddString(TEXT("blueprintPath"), bGraphArguments
		? TEXT("Optional. Asset path of the Blueprint, e.g. /Game/Blueprints/BP_Door.BP_Door. With no target argument the focused graph is used; with blueprintPath alone, its focused graph or else its event graph. The result's 'target' says which graph was used.")
		: TEXT("Optional. Asset path of the Blueprint, e.g. /Game/Blueprints/BP_Door.BP_Door; without it, the focused Blueprint."));
	if (bGraphArguments)
	{
		AddString(TEXT("graphName"), TEXT("Optional. Name of the graph (a function, macro, event graph or collapsed graph) in blueprintPath, or in the focused Blueprint without blueprintPath. AMBIGUOUS_GRAPH_NAME when two graphs share it: pass graphGuid then."));
		AddString(TEXT("graphGuid"), TEXT("Optional. The graph's GUID (target.graphGuid of an earlier result; a function's functionGuid/guid is its graph GUID). Stable across renames and moves. Wins over graphName."));
	}
}
