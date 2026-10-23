// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Toolsets/N2CBlueprintToolset.h"

#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Engine/Blueprint.h"

FN2CBlueprintExportResult UN2CBlueprintToolset::ExportBlueprintJson(const FString& BlueprintAssetPath)
{
	FN2CBlueprintExportResult Result;
	const FString AssetObjectPath = BlueprintAssetPath.TrimStartAndEnd();
	if (AssetObjectPath.IsEmpty())
	{
		Result.Error = TEXT("BlueprintAssetPath must be a non-empty Blueprint object path.");
		return Result;
	}

	if (!IsInGameThread())
	{
		Result.Error = TEXT("Blueprint export must run on the Unreal game thread.");
		return Result;
	}

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetObjectPath);
	if (!Blueprint)
	{
		Result.Error = FString::Printf(
			TEXT("Could not load Blueprint at '%s'. Use its object path, such as /Game/Blueprints/BP_Character.BP_Character."),
			*AssetObjectPath);
		return Result;
	}

	FN2CNodeTranslator& Translator = FN2CNodeTranslator::Get();
	if (!Translator.GenerateFromBlueprint(Blueprint, true))
	{
		Result.Error = FString::Printf(TEXT("NodeToCode could not translate Blueprint '%s'."), *AssetObjectPath);
		return Result;
	}

	const FN2CBlueprint& BlueprintData = Translator.GetN2CBlueprint();
	if (!BlueprintData.IsValid())
	{
		Result.Error = FString::Printf(TEXT("NodeToCode produced invalid data for Blueprint '%s'."), *AssetObjectPath);
		return Result;
	}

	Result.BlueprintJson = FN2CSerializer::ToJson(BlueprintData);
	if (Result.BlueprintJson.IsEmpty())
	{
		Result.Error = FString::Printf(TEXT("NodeToCode could not serialize Blueprint '%s' to JSON."), *AssetObjectPath);
		return Result;
	}

	Result.bSuccess = true;
	return Result;
}
