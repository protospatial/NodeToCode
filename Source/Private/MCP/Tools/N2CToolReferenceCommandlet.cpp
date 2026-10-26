// Copyright (c) 2026 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CToolReferenceCommandlet.h"
#include "MCP/Tools/N2CMcpToolReference.h"
#include "Models/N2CLogging.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(N2CToolReferenceCommandlet)

UN2CToolReferenceCommandlet::UN2CToolReferenceCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UN2CToolReferenceCommandlet::Main(const FString& Params)
{
	FString OutputDir;
	if (!FParse::Value(*Params, TEXT("Output="), OutputDir) || OutputDir.IsEmpty())
	{
		OutputDir = N2CMcpToolReference::DefaultOutputDir();
	}
	if (OutputDir.IsEmpty())
	{
		UE_LOG(LogNodeToCode, Error, TEXT("N2CToolReference: the NodeToCode plugin was not found and no -Output= was given"));
		return 2;
	}
	OutputDir = FPaths::ConvertRelativePathToFull(OutputDir);
	const bool bCheck = FParse::Param(*Params, TEXT("Check"));

	const TArray<FMcpToolDefinition> Tools = N2CMcpToolReference::CollectTools();
	if (Tools.Num() == 0)
	{
		UE_LOG(LogNodeToCode, Error, TEXT("N2CToolReference: the tool registry is empty"));
		return 2;
	}
	const TArray<FMcpToolDefinition> MetaTools = N2CMcpToolReference::CollectMetaTools();

	const TPair<const TCHAR*, FString> Files[] = {
		{ N2CMcpToolReference::JsonFileName, N2CMcpToolReference::BuildJson(Tools, MetaTools) },
		{ N2CMcpToolReference::MarkdownFileName, N2CMcpToolReference::BuildMarkdown(Tools, MetaTools) } };

	int32 Result = 0;
	for (const TPair<const TCHAR*, FString>& File : Files)
	{
		const FString Path = OutputDir / File.Key;
		if (bCheck)
		{
			FString Existing;
			if (!FFileHelper::LoadFileToString(Existing, *Path))
			{
				UE_LOG(LogNodeToCode, Error, TEXT("N2CToolReference: %s is missing; run Scripts/generate_tool_reference.py"), *Path);
				Result = 1;
			}
			else if (Existing.Replace(TEXT("\r\n"), TEXT("\n")) != File.Value)
			{
				UE_LOG(LogNodeToCode, Error, TEXT("N2CToolReference: %s is stale (the tool registry builds different text); run Scripts/generate_tool_reference.py"), *Path);
				Result = 1;
			}
			else
			{
				UE_LOG(LogNodeToCode, Display, TEXT("N2CToolReference: %s is current"), *Path);
			}
			continue;
		}

		if (!FFileHelper::SaveStringToFile(File.Value, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogNodeToCode, Error, TEXT("N2CToolReference: could not write %s"), *Path);
			return 2;
		}
		UE_LOG(LogNodeToCode, Display, TEXT("N2CToolReference: wrote %s"), *Path);
	}

	UE_LOG(LogNodeToCode, Display, TEXT("N2CToolReference: %d tools, %d tool-search meta-tools, output %s%s"),
		Tools.Num(), MetaTools.Num(), *OutputDir, bCheck ? (Result == 0 ? TEXT(" (check passed)") : TEXT(" (check failed)")) : TEXT(""));
	return Result;
}
