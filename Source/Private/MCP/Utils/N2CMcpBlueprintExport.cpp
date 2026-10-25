// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Core/N2CSerializer.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node.h"
#include "K2Node_Composite.h"
#include "K2Node_MathExpression.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CMcpBlueprintExportPrivate
{
#if WITH_DEV_AUTOMATION_TESTS
	FString& ExportDirectoryOverride()
	{
		static FString Directory;
		return Directory;
	}
#endif

	/** K2 nodes of a graph (what the capture reads; comments and other non-K2 nodes are not captured) */
	int32 CountK2Nodes(const UEdGraph& Graph)
	{
		int32 Count = 0;
		for (const UEdGraphNode* Node : Graph.Nodes)
		{
			Count += Cast<UK2Node>(Node) != nullptr ? 1 : 0;
		}
		return Count;
	}

	bool IsMathExpressionGraph(const UEdGraph* Graph)
	{
		return Graph && Graph->GetOuter() && Graph->GetOuter()->IsA<UK2Node_MathExpression>();
	}

	/** Each node's and pin's "id" becomes "ids": {"short", "guid"} (pins also "name"); the flows keep the short IDs */
	void ExpandIds(const TSharedPtr<FJsonObject>& RootObject, const FN2CCapture& Capture)
	{
		const TArray<TSharedPtr<FJsonValue>>* GraphsArray = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("graphs"), GraphsArray))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
		{
			const TSharedPtr<FJsonObject> GraphObject = GraphValue->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
			if (!GraphObject.IsValid() || !GraphObject->TryGetArrayField(TEXT("nodes"), NodesArray))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
			{
				const TSharedPtr<FJsonObject> NodeObject = NodeValue->AsObject();
				if (!NodeObject.IsValid())
				{
					continue;
				}

				FString ShortNodeID;
				if (NodeObject->TryGetStringField(TEXT("id"), ShortNodeID))
				{
					TSharedPtr<FJsonObject> IdsObject = MakeShared<FJsonObject>();
					IdsObject->SetStringField(TEXT("short"), ShortNodeID);
					if (const FGuid* NodeGuid = Capture.NodeGuids.Find(ShortNodeID))
					{
						IdsObject->SetStringField(TEXT("guid"), NodeGuid->ToString(EGuidFormats::DigitsWithHyphens));
					}
					NodeObject->RemoveField(TEXT("id"));
					NodeObject->SetObjectField(TEXT("ids"), IdsObject);
				}

				for (const TCHAR* PinArrayName : { TEXT("input_pins"), TEXT("output_pins") })
				{
					const TArray<TSharedPtr<FJsonValue>>* PinsArray = nullptr;
					if (!NodeObject->TryGetArrayField(PinArrayName, PinsArray))
					{
						continue;
					}
					for (const TSharedPtr<FJsonValue>& PinValue : *PinsArray)
					{
						const TSharedPtr<FJsonObject> PinObject = PinValue->AsObject();
						FString ShortPinID;
						if (!PinObject.IsValid() || !PinObject->TryGetStringField(TEXT("id"), ShortPinID))
						{
							continue;
						}
						TSharedPtr<FJsonObject> PinIdsObject = MakeShared<FJsonObject>();
						PinIdsObject->SetStringField(TEXT("short"), ShortPinID);
						if (const FN2CCapturedPin* Pin = Capture.Pins.Find(ShortPinID))
						{
							PinIdsObject->SetStringField(TEXT("guid"), Pin->PinGuid.ToString(EGuidFormats::DigitsWithHyphens));
						}
						FString PinName;
						if (PinObject->TryGetStringField(TEXT("name"), PinName))
						{
							PinIdsObject->SetStringField(TEXT("name"), PinName);
						}
						PinObject->RemoveField(TEXT("id"));
						PinObject->SetObjectField(TEXT("ids"), PinIdsObject);
					}
				}
			}
		}
	}

	FN2CBlueprintReadResult Fail(FN2CBlueprintReadResult Result, const FString& Error)
	{
		Result.Error = Error;
		return Result;
	}
}

TSharedRef<FJsonObject> FN2CBlueprintReadResult::FileResultJson() const
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("path"), FilePath);
	Object->SetNumberField(TEXT("bytes"), static_cast<double>(FileBytes));
	Object->SetNumberField(TEXT("estimatedTokens"), EstimatedTokens);
	Object->SetNumberField(TEXT("maxInlineTokens"), N2CMcpBlueprintExport::MaxInlineTokens);
	Object->SetStringField(TEXT("reason"), FileReason);
	Object->SetNumberField(TEXT("graphCount"), GraphCount);
	Object->SetNumberField(TEXT("nodeCount"), NodeCount);
	return Object;
}

FN2CCaptureOptions N2CMcpBlueprintExport::CaptureOptions(int32 Depth)
{
	FN2CCaptureOptions Options;
	Options.Depth = Depth;
	// The default events the editor placed stay visible (marked disabled), so an agent wires them instead of adding a
	// duplicate event (card 0137)
	Options.bIncludeGhostNodes = true;
	return Options;
}

TValueOrError<FN2CCapture, FString> N2CMcpBlueprintExport::CaptureGraph(UEdGraph& Graph, int32 Depth)
{
	using namespace N2CMcpBlueprintExportPrivate;
	if (CountK2Nodes(Graph) == 0)
	{
		return MakeError(FString::Printf(TEXT("EMPTY_GRAPH: graph %s has no nodes"), *Graph.GetName()));
	}
	TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(Graph, CaptureOptions(Depth));
	if (Capture.HasError())
	{
		return MakeError(FString::Printf(TEXT("CAPTURE_FAILED: graph %s: %s"), *Graph.GetName(), *Capture.GetError()));
	}
	if (!Capture.GetValue().Blueprint.IsValid())
	{
		return MakeError(FString::Printf(TEXT("CAPTURE_FAILED: the capture of graph %s did not validate"), *Graph.GetName()));
	}
	return Capture;
}

TArray<UEdGraph*> N2CMcpBlueprintExport::GraphsToCapture(const UBlueprint& Blueprint)
{
	TArray<UEdGraph*> All;
	Blueprint.GetAllGraphs(All);
	TArray<UEdGraph*> Graphs;
	for (UEdGraph* Graph : All)
	{
		if (Graph && !N2CMcpBlueprintExportPrivate::IsMathExpressionGraph(Graph))
		{
			Graphs.AddUnique(Graph);
		}
	}
	return Graphs;
}

TValueOrError<FN2CCapture, FString> N2CMcpBlueprintExport::CaptureAllGraphs(const UBlueprint& Blueprint, int32 Depth)
{
	const TArray<UEdGraph*> Graphs = GraphsToCapture(Blueprint);
	TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraphs(Graphs, CaptureOptions(Depth));
	if (Capture.HasError())
	{
		return MakeError(FString::Printf(TEXT("EMPTY_GRAPH: none of the %d graphs of %s has a node"), Graphs.Num(),
			*Blueprint.GetPathName()));
	}
	if (!Capture.GetValue().Blueprint.IsValid())
	{
		return MakeError(FString::Printf(TEXT("CAPTURE_FAILED: the capture of %s did not validate"), *Blueprint.GetPathName()));
	}
	return Capture;
}

FString N2CMcpBlueprintExport::ToResultJson(const FN2CCapture& Capture, bool bCompact)
{
	// Serialized once, read once, IDs expanded in place, written once
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
		FN2CSerializer::ToJson(Capture, EN2CJsonStyle::Condensed, EN2CJsonGuids::Table));
	if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
	{
		FN2CLogger::Get().LogError(TEXT("N2CMcpBlueprintExport: the serialized capture is not a JSON object"));
		return FString();
	}
	N2CMcpBlueprintExportPrivate::ExpandIds(RootObject, Capture);

	FString OutputString;
	if (bCompact)
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	}
	else
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
		FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	}
	return OutputString;
}

TSharedRef<FJsonObject> N2CMcpBlueprintExport::ListGraphs(const UBlueprint& Blueprint)
{
	TArray<UEdGraph*> All;
	Blueprint.GetAllGraphs(All);

	TArray<TSharedPtr<FJsonValue>> Graphs;
	TSet<const UEdGraph*> Seen;
	for (UEdGraph* Graph : All)
	{
		if (!Graph || Seen.Contains(Graph))
		{
			continue;
		}
		Seen.Add(Graph);
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Graph->GetName());
		Entry->SetStringField(TEXT("graphGuid"), Graph->GraphGuid.ToString());
		Entry->SetStringField(TEXT("kind"), FN2CGraphTarget::GraphKind(Graph));
		Entry->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
		Entry->SetBoolField(TEXT("readOnly"), FBlueprintEditorUtils::IsGraphReadOnly(Graph));
		if (const UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Graph->GetOuter()))
		{
			if (const UEdGraph* Parent = Composite->GetGraph())
			{
				Entry->SetStringField(TEXT("parentGraph"), Parent->GetName());
			}
		}
		Graphs.Add(MakeShared<FJsonValueObject>(Entry));
	}

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("blueprintPath"), Blueprint.GetPathName());
	Json->SetStringField(TEXT("blueprintName"), Blueprint.GetName());
	Json->SetNumberField(TEXT("graphCount"), Graphs.Num());
	Json->SetArrayField(TEXT("graphs"), Graphs);
	return Json;
}

int32 N2CMcpBlueprintExport::EstimateTokens(const FString& Text)
{
	return FMath::CeilToInt(static_cast<double>(Text.Len()) / JsonCharsPerToken);
}

FString N2CMcpBlueprintExport::ExportDirectory()
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!N2CMcpBlueprintExportPrivate::ExportDirectoryOverride().IsEmpty())
	{
		return N2CMcpBlueprintExportPrivate::ExportDirectoryOverride();
	}
#endif
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NodeToCode") / TEXT("Exports"));
}

#if WITH_DEV_AUTOMATION_TESTS
void N2CMcpBlueprintExport::SetExportDirectoryForTests(const FString& Directory)
{
	N2CMcpBlueprintExportPrivate::ExportDirectoryOverride() = Directory;
}
#endif

FString N2CMcpBlueprintExport::ExportFileName(const UBlueprint& Blueprint, const UEdGraph* Graph, int32 Depth, bool bCompact)
{
	const FString Key = FString::Printf(TEXT("%s|%s|%d|%d"), *Blueprint.GetPathName(),
		Graph ? *Graph->GraphGuid.ToString() : TEXT("all"), Depth, bCompact ? 1 : 0);
	const FString BlueprintStem = FN2CPathUtils::MakeSafeFileStem(Blueprint.GetName());
	const FString GraphStem = Graph ? FN2CPathUtils::MakeSafeFileStem(Graph->GetName()) : FString(TEXT("AllGraphs"));
	return FString::Printf(TEXT("%s__%s__%08x.json"), BlueprintStem.IsEmpty() ? TEXT("Blueprint") : *BlueprintStem,
		GraphStem.IsEmpty() ? TEXT("Graph") : *GraphStem, FCrc::StrCrc32(*Key));
}

bool N2CMcpBlueprintExport::WriteExport(const FString& Json, const FString& FileName, FString& OutPath, int64& OutBytes, FString& OutError)
{
	const FString Directory = ExportDirectory();
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

	// A temporary file moved over the target, so a reader never sees half a document
	const FString TempPath = Path + FString::Printf(TEXT(".%s.tmp"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!FFileHelper::SaveStringToFile(Json, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
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

FN2CBlueprintReadResult N2CMcpBlueprintExport::Read(const FN2CBlueprintReadRequest& Request)
{
	using namespace N2CMcpBlueprintExportPrivate;
	check(IsInGameThread());

	FN2CBlueprintReadResult Result;
	const FString BlueprintPath = Request.BlueprintPath.TrimStartAndEnd();
	if (BlueprintPath.IsEmpty())
	{
		return Fail(Result, TEXT("INVALID_ARGUMENT: blueprintPath is required, the asset path of a Blueprint such as /Game/Blueprints/BP_Door"));
	}
	if (Request.Depth != INDEX_NONE && (Request.Depth < 0 || Request.Depth > 5))
	{
		return Fail(Result, FString::Printf(TEXT("INVALID_ARGUMENT: depth must be 0 to 5 (got %d)"), Request.Depth));
	}

	// One graph (by name or GUID), or every graph
	TValueOrError<FN2CCapture, FString> Capture = MakeError(FString());
	UEdGraph* Graph = nullptr;
	if (!Request.GraphName.TrimStartAndEnd().IsEmpty() || !Request.GraphGuid.TrimStartAndEnd().IsEmpty())
	{
		FN2CGraphTargetArgs Args;
		Args.BlueprintPath = BlueprintPath;
		Args.GraphName = Request.GraphName.TrimStartAndEnd();
		Args.GraphGuid = Request.GraphGuid.TrimStartAndEnd();
		Result.Target = FN2CGraphTarget::Resolve(Args, /*bForWrite*/ false);
		if (!Result.Target.IsValid())
		{
			return Fail(Result, Result.Target.Error);
		}
		Result.Blueprint = Result.Target.Blueprint;
		Graph = Result.Target.Graph;
		Capture = CaptureGraph(*Graph, Request.Depth);
	}
	else
	{
		FString LoadError;
		Result.Blueprint = FN2CGraphTarget::LoadBlueprint(BlueprintPath, LoadError);
		if (!Result.Blueprint)
		{
			return Fail(Result, LoadError);
		}
		Capture = CaptureAllGraphs(*Result.Blueprint, Request.Depth);
	}
	if (Capture.HasError())
	{
		return Fail(Result, Capture.GetError());
	}

	const FN2CCapture& Captured = Capture.GetValue();
	Result.GraphCount = Captured.Blueprint.Graphs.Num();
	for (const FN2CGraph& CapturedGraph : Captured.Blueprint.Graphs)
	{
		Result.NodeCount += CapturedGraph.Nodes.Num();
	}
	Result.Json = ToResultJson(Captured, Request.bCompact);
	if (Result.Json.IsEmpty())
	{
		return Fail(Result, TEXT("CAPTURE_FAILED: the capture could not be serialized"));
	}
	Result.EstimatedTokens = EstimateTokens(Result.Json);

	const bool bTooLarge = Request.bSpillLargeToFile && Result.EstimatedTokens > MaxInlineTokens;
	if (Request.bToFile || bTooLarge)
	{
		FString WriteError;
		if (!WriteExport(Result.Json, ExportFileName(*Result.Blueprint, Graph, Request.Depth, Request.bCompact),
			Result.FilePath, Result.FileBytes, WriteError))
		{
			return Fail(Result, WriteError);
		}
		Result.FileReason = Request.bToFile ? TEXT("toFile") : TEXT("size");
	}

	FN2CLogger::Get().Log(FString::Printf(TEXT("Read %s (%s): %d graphs, %d nodes, about %d tokens%s"),
		*Result.Blueprint->GetPathName(), Graph ? *Graph->GetName() : TEXT("every graph"), Result.GraphCount,
		Result.NodeCount, Result.EstimatedTokens, Result.WroteFile() ? *(TEXT(", written to ") + Result.FilePath) : TEXT("")),
		EN2CLogSeverity::Info);
	return Result;
}
