// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "N2CBlueprintToolset.generated.h"

USTRUCT(BlueprintType)
struct FN2CBlueprintExportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
	FString BlueprintJson;

	UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
	FString Error;
};

/** Exposes read-only NodeToCode Blueprint inspection tools to Unreal MCP clients. */
UCLASS(BlueprintType, Hidden)
class UN2CBlueprintToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Export a Blueprint's graphs, variables, components, and related structure as NodeToCode JSON.
	 * @param BlueprintAssetPath Blueprint object path, for example /Game/Blueprints/BP_Character.BP_Character.
	 * @return Success state and the serialized Blueprint JSON, or an error describing why export failed.
	 */
	UFUNCTION(Category = "Node to Code", meta = (AICallable))
	static FN2CBlueprintExportResult ExportBlueprintJson(const FString& BlueprintAssetPath);
};
