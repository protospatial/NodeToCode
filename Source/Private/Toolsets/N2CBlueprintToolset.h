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
    int32 NativePinCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    int32 NativeLinkCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString LinkIdentity;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    TArray<FString> DirectLinks;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    TArray<FString> ConnectionErrors;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString Error;
};

USTRUCT(BlueprintType)
struct FN2CBlueprintMemberRemovalResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bModifiedInMemory = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bExternalReferencesReviewed = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bDefaultValueEmpty = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString AssetObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString VariableName;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    int32 GraphReferenceCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString Error;
};

USTRUCT(BlueprintType)
struct FN2CBlueprintFunctionCallMapping
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString SourceFunctionGraphName;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString TargetClassPath;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString TargetFunctionName;
};

USTRUCT(BlueprintType)
struct FN2CBlueprintMutationResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bModifiedInMemory = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bSaved = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    bool bAlreadyImplemented = false;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString AssetObjectPath;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    int32 ChangeCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    TArray<FString> Changes;
    UPROPERTY(BlueprintReadOnly, Category = "Node to Code")
    FString Error;
};

/** Exposes NodeToCode Blueprint inspection and reviewed graph mutations to Unreal MCP clients. */
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
	UFUNCTION(BlueprintCallable, Category = "Node to Code", meta = (AICallable))
	static FN2CBlueprintExportResult ExportBlueprintJson(const FString& BlueprintAssetPath);

    /** Read-only graph oracle with native counts and endpoint sets for UEdGraph and RigVM. */
    UFUNCTION(BlueprintCallable, Category = "Node to Code", meta = (AICallable))
    static FN2CBlueprintGraphTextResult ExportBlueprintGraphText(
        const FString& BlueprintAssetPath, const FString& GraphName, const FString& GraphPath = FString());

    /**
     * Removes one local Blueprint variable only when its default data is empty, its external callers
     * have been reviewed, and no local graph node references it. This changes the in-memory Blueprint;
     * compile and explicitly save the asset after reviewing the result.
     * @param bExternalReferencesReviewed True only after checking package referencers for variable use.
     * @param bDefaultDataConfirmedEmpty True only after confirming the variable has no serialized data.
     */
    UFUNCTION(Category = "Node to Code", meta = (AICallable))
    static FN2CBlueprintMemberRemovalResult RemoveUnusedBlueprintMemberVariable(
        const FString& BlueprintAssetPath, const FString& VariableName,
        bool bExternalReferencesReviewed, bool bDefaultDataConfirmedEmpty);

    /** Rebinds a reviewed local Boolean to an inherited native Boolean property and migrates its CDO default. */
    UFUNCTION(Category = "Node to Code", meta = (AICallable))
    static FN2CBlueprintMutationResult MigrateBlueprintBooleanMemberToNativeParent(
        const FString& BlueprintAssetPath, const FString& VariableName,
        const FString& NativePropertyName,
        bool bExternalReferencesReviewed);

    /**
     * Rebind reviewed calls to native Blueprint-callable functions, preserving logical links with pure casts when a
     * native output is a base type and the existing Blueprint caller expects a compatible child type.
     */
    UFUNCTION(Category = "Node to Code", meta = (AICallable))
    static FN2CBlueprintMutationResult RebindBlueprintFunctionCalls(
        const FString& BlueprintAssetPath, const FString& SourceFunctionLibraryPath,
        const TArray<FN2CBlueprintFunctionCallMapping>& Mappings,
        bool bExternalReferencesReviewed);

    /** Removes reviewed function graphs only when no surviving local graph calls them, then compiles and saves. */
    UFUNCTION(Category = "Node to Code", meta = (AICallable))
    static FN2CBlueprintMutationResult RemoveBlueprintFunctionGraphs(
        const FString& BlueprintAssetPath, const TArray<FString>& FunctionGraphNames,
        bool bExternalReferencesReviewed);

    /** Guard one reviewed Blueprint event path so local-only controller work skips remote server copies. */
    UFUNCTION(Category = "Node to Code", meta = (AICallable))
    static FN2CBlueprintMutationResult GuardBlueprintEventWithLocalController(
        const FString& BlueprintAssetPath, const FString& GraphName,
        const FString& EventFunctionName, bool bExternalReferencesReviewed);

    static FN2CBlueprintMemberRemovalResult RemoveUnusedBlueprintMemberVariableFromBlueprint(
        UBlueprint* Blueprint, const FName VariableName,
        bool bExternalReferencesReviewed, bool bDefaultDataConfirmedEmpty);

};
