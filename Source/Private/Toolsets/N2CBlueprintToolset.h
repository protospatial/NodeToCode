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
	FString AssetObjectPath;

	UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
	FString Error;
	UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
	TArray<FString> Errors;
	UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
	TArray<FString> Warnings;
};

USTRUCT(BlueprintType)
struct FN2CBlueprintGraphTextResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString GraphText;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString GraphName;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString GraphGuid;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString GraphPath;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    int32 NativeNodeCount = 0;
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
     * Export Blueprint graphs, variables, class defaults, SCS components, UMG widget trees,
     * Control Rig hierarchies, and reflected native node data as NodeToCode JSON.
	 * @param BlueprintAssetPath Blueprint object path, for example /Game/Blueprints/BP_Character.BP_Character.
	 * @return Success state and the serialized Blueprint JSON, or an error describing why export failed.
	 */
	UFUNCTION(Category = "Node to Code", meta = (AICallable))
	static FN2CBlueprintExportResult ExportBlueprintJson(const FString& BlueprintAssetPath);

    /** Read-only graph oracle; uses clipboard text for UEdGraph and model data for RigVM. */
    UFUNCTION(Category = "Node to Code", meta = (AICallable))
    static FN2CBlueprintGraphTextResult ExportBlueprintGraphText(
        const FString& BlueprintAssetPath, const FString& GraphName, const FString& GraphPath = FString());
};
