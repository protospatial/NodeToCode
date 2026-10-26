// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpExportN2CJsonBatchTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpBatchTargets.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpExportN2CJsonBatchTool)

namespace N2CMcpExportN2CJsonBatchToolPrivate
{
	TSharedPtr<FJsonObject> Property(const TCHAR* Type, const TCHAR* Description)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("type"), Type);
		Json->SetStringField(TEXT("description"), Description);
		return Json;
	}

	FString ToText(const TSharedRef<FJsonObject>& Json)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Json, Writer);
		return Text;
	}
}

FMcpToolDefinition FN2CMcpExportN2CJsonBatchTool::GetDefinition() const
{
	using namespace N2CMcpExportN2CJsonBatchToolPrivate;

	FMcpToolDefinition Definition(
		TEXT("export-n2cjson-batch"),
		TEXT("Exports many Blueprint graphs as N2C JSON in one call, one file per graph, with no Blueprint editor open and ")
		TEXT("without changing focus. Select the graphs with targets [{blueprintPath, graphName?, graphGuid?}] (a target with ")
		TEXT("only blueprintPath exports every graph of that Blueprint that has nodes) or with a tag and/or category ")
		TEXT("(tag-blueprint-graph). Each file holds the document get-blueprint-n2cjson returns for that graph. The files go ")
		TEXT("to one folder under Saved/NodeToCode/Exports (outDir, or a new Batch_<time> folder); the result lists each ")
		TEXT("file {path, bytes, estimatedTokens, nodeCount} and each target that failed with its code. Read the files in ")
		TEXT("parts with read-file. Assets that are not Blueprints are refused per target (UNSUPPORTED_ASSET_TYPE)."),
		N2CMcpToolsets::BlueprintDiscovery
	);
	Definition.Title = TEXT("Export N2C JSON for many graphs");

	const TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	N2CMcpBatchTargets::AddSchema(Properties, MaxGraphs);

	const TSharedPtr<FJsonObject> Depth = Property(TEXT("integer"), TEXT("Optional, 0 to 5. Each file also captures the functions, macros and collapsed graphs its graph uses up to this many calls away. Default: the project's Max Translation Depth setting."));
	Depth->SetNumberField(TEXT("minimum"), 0);
	Depth->SetNumberField(TEXT("maximum"), 5);
	Properties->SetObjectField(TEXT("depth"), Depth);

	const TSharedPtr<FJsonObject> Compact = Property(TEXT("boolean"), TEXT("Optional, default true. No whitespace; false indents the JSON."));
	Compact->SetBoolField(TEXT("default"), true);
	Properties->SetObjectField(TEXT("compact"), Compact);

	Properties->SetObjectField(TEXT("outDir"), Property(TEXT("string"),
		TEXT("Optional. A folder name under Saved/NodeToCode/Exports for the files (letters, digits, _ - . and up to four parts joined by /). Exporting the same graphs to the same outDir again replaces their files. Default: a new Batch_<date-time> folder.")));

	const TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	Schema->SetBoolField(TEXT("additionalProperties"), false);
	Definition.InputSchema = Schema;

	// Reads Blueprints; the only writes are its own export files under Saved/NodeToCode/Exports, as get-blueprint-n2cjson
	SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

	// Results carry graph names (Blueprint text): marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FString FN2CMcpExportN2CJsonBatchTool::ValidateOutDir(const FString& InOutDir, FString& OutError)
{
	OutError.Reset();
	FString OutDir = InOutDir.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	OutDir.RemoveFromEnd(TEXT("/"));

	TArray<FString> Parts;
	OutDir.ParseIntoArray(Parts, TEXT("/"), /*InCullEmpty*/ false);
	bool bValid = !OutDir.IsEmpty() && Parts.Num() >= 1 && Parts.Num() <= 4 && !OutDir.StartsWith(TEXT("/"));
	for (const FString& Part : Parts)
	{
		bValid &= !Part.IsEmpty() && Part != TEXT(".") && Part != TEXT("..") && Part.Len() <= 64;
		for (const TCHAR Char : Part)
		{
			bValid &= FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('-') || Char == TEXT('.');
		}
	}
	if (!bValid)
	{
		OutError = FString::Printf(TEXT("INVALID_ARGUMENT: outDir '%s' must be a folder name under Saved/NodeToCode/Exports: ")
			TEXT("letters, digits, _ - . in up to four parts joined by /, with no '..'"), *InOutDir);
		return FString();
	}
	return FString::Join(Parts, TEXT("/"));
}

FMcpToolCallResult FN2CMcpExportN2CJsonBatchTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([Arguments]() -> FMcpToolCallResult
	{
		using namespace N2CMcpExportN2CJsonBatchToolPrivate;
		const TSharedPtr<FJsonObject> Args = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
		const double StartTime = FPlatformTime::Seconds();

		int32 Depth = INDEX_NONE;
		if (Args->HasField(TEXT("depth")))
		{
			double Value = 0.0;
			if (!Args->TryGetNumberField(TEXT("depth"), Value) || Value != FMath::RoundToDouble(Value) || Value < 0.0 || Value > 5.0)
			{
				return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: depth must be a whole number from 0 to 5"));
			}
			Depth = static_cast<int32>(Value);
		}
		bool bCompact = true;
		Args->TryGetBoolField(TEXT("compact"), bCompact);

		FString OutDir;
		FString OutDirArg;
		if (Args->TryGetStringField(TEXT("outDir"), OutDirArg) && !OutDirArg.TrimStartAndEnd().IsEmpty())
		{
			FString OutDirError;
			OutDir = ValidateOutDir(OutDirArg, OutDirError);
			if (OutDir.IsEmpty())
			{
				return FMcpToolCallResult::CreateErrorResult(OutDirError);
			}
		}
		else
		{
			OutDir = FString::Printf(TEXT("Batch_%s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d-%H.%M.%S-%s")));
		}

		const FN2CBatchTargets Targets = N2CMcpBatchTargets::Resolve(Args, MaxGraphs, TEXT("export-n2cjson-batch"));
		if (!Targets.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Targets.Error);
		}

		const FString ExportRoot = N2CMcpBlueprintExport::ExportDirectory();
		const FString Directory = FPaths::ConvertRelativePathToFull(ExportRoot / OutDir);
		if (!FN2CPathUtils::IsUnderDirectoryResolved(Directory, ExportRoot))
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("INVALID_ARGUMENT: outDir resolves to %s, outside %s"), *Directory, *ExportRoot));
		}

		TArray<TSharedPtr<FJsonValue>> Files;
		TArray<TSharedPtr<FJsonValue>> Failures = Targets.FailuresJson();
		if (Targets.Graphs.Num() > 0 && !IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true))
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("EXPORT_FAILED: could not create %s"), *Directory));
		}

		int64 TotalBytes = 0;
		for (const FN2CBatchGraph& Entry : Targets.Graphs)
		{
			auto Fail = [&Failures, &Entry](const FString& Error)
			{
				FN2CBatchTargetIssue Issue;
				Issue.BlueprintPath = Entry.BlueprintPath;
				Issue.GraphName = Entry.GraphName;
				Issue.GraphGuid = Entry.GraphGuid;
				Issue.Error = Error;
				Failures.Add(MakeShared<FJsonValueObject>(Issue.ToJson()));
			};

			TValueOrError<FN2CCapture, FString> Capture = N2CMcpBlueprintExport::CaptureGraph(*Entry.Graph, Depth);
			if (Capture.HasError())
			{
				Fail(Capture.GetError());
				continue;
			}
			const FString Json = N2CMcpBlueprintExport::ToResultJson(Capture.GetValue(), bCompact);
			if (Json.IsEmpty())
			{
				Fail(TEXT("CAPTURE_FAILED: the capture could not be serialized"));
				continue;
			}

			FString Path;
			int64 Bytes = 0;
			FString WriteError;
			const FString FileName = OutDir / N2CMcpBlueprintExport::ExportFileName(*Entry.Blueprint, Entry.Graph, Depth, bCompact);
			if (!N2CMcpBlueprintExport::WriteExport(Json, FileName, Path, Bytes, WriteError))
			{
				Fail(WriteError);
				continue;
			}
			TotalBytes += Bytes;

			int32 NodeCount = 0;
			for (const FN2CGraph& CapturedGraph : Capture.GetValue().Blueprint.Graphs)
			{
				NodeCount += CapturedGraph.Nodes.Num();
			}
			const TSharedRef<FJsonObject> File = Entry.ToJson();
			File->SetStringField(TEXT("path"), Path);
			File->SetNumberField(TEXT("bytes"), static_cast<double>(Bytes));
			File->SetNumberField(TEXT("estimatedTokens"), N2CMcpBlueprintExport::EstimateTokens(Json));
			File->SetNumberField(TEXT("graphCount"), Capture.GetValue().Blueprint.Graphs.Num());
			File->SetNumberField(TEXT("nodeCount"), NodeCount);
			Files.Add(MakeShared<FJsonValueObject>(File));
		}

		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("outDir"), Directory);
		Result->SetNumberField(TEXT("fileCount"), Files.Num());
		Result->SetNumberField(TEXT("failureCount"), Failures.Num());
		Result->SetNumberField(TEXT("totalBytes"), static_cast<double>(TotalBytes));
		Result->SetBoolField(TEXT("compact"), bCompact);
		Result->SetArrayField(TEXT("files"), Files);
		Result->SetArrayField(TEXT("failures"), Failures);
		if (Targets.Skipped.Num() > 0)
		{
			Result->SetArrayField(TEXT("skipped"), Targets.SkippedJson());
		}

		FN2CLogger::Get().Log(FString::Printf(TEXT("export-n2cjson-batch: %d files (%lld bytes), %d failures, %d empty graphs skipped, to %s in %.2f s"),
			Files.Num(), TotalBytes, Failures.Num(), Targets.Skipped.Num(), *Directory, FPlatformTime::Seconds() - StartTime), EN2CLogSeverity::Info);

		// Nothing exported: the call failed, with every target's reason in the body
		return FMcpToolCallResult::CreateTextResult(ToText(Result), /*bInIsError*/ Files.Num() == 0);
	}, 300.0f);
}
