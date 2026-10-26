// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "Engine/Blueprint.h"

/**
 * MCP tool for creating new Blueprint classes with specified parent classes.
 * Integrates with search-blueprint-classes tool results to provide a complete
 * Blueprint creation workflow.
 *
 * Every argument is checked before anything is created; the asset is then made by IAssetTools::CreateAsset with a
 * UBlueprintFactory (so the parent class picks the Blueprint class: a UserWidget parent gives a Widget Blueprint),
 * compiled and saved. Creation is not an undo step, as in the Content Browser: undoing it would destroy the Blueprint
 * in memory and leave its saved package on disk.
 */
class FN2CMcpCreateBlueprintClassTool : public FN2CMcpToolBase
{
public:
	virtual FMcpToolDefinition GetDefinition() const override;
	virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

private:
	/**
	 * Validates the parent class for Blueprint creation
	 * @param ClassPath The path to the parent class (e.g., "/Script/Engine.Actor")
	 * @param OutClass The resolved UClass if found
	 * @param OutErrorMessage Error message if validation fails
	 * @return True if the class is valid for Blueprint creation
	 */
	bool ValidateParentClass(const FString& ClassPath, UClass*& OutClass, FString& OutErrorMessage) const;

	/**
	 * Resolves the blueprintType argument (auto, normal, const or interface, any case) against the parent class.
	 * Refuses an unknown value, the interface type with a parent other than UInterface, and a normal or const type
	 * with an interface parent.
	 */
	bool ResolveBlueprintType(const FString& TypeText, const UClass* ParentClass, EBlueprintType& OutType,
		FString& OutErrorMessage) const;

	/**
	 * Checks the asset name the way the editor checks a Blueprint name (FKismetNameValidator) and an asset name
	 * (object name characters, long package name characters, a name the file system can save)
	 */
	bool ValidateBlueprintName(const FString& AssetName, FString& OutErrorMessage) const;

	/**
	 * Validates the folder and works out the package and asset names. Creates nothing.
	 * @param AssetPath The content folder for the Blueprint (it may already exist)
	 * @param AssetName The validated asset name
	 * @param bUniqueIfExists When an asset of that name exists: true picks a free name, false fails
	 * @param OutFolder The normalized folder
	 * @param OutPackageName The resulting package name
	 * @param OutAssetName The resulting asset name (AssetName, or a free name when bUniqueIfExists)
	 * @param OutErrorMessage Error message if validation fails
	 * @return True if the asset can be created there
	 */
	bool ValidateAndPrepareAssetPath(const FString& AssetPath, const FString& AssetName, bool bUniqueIfExists,
		FString& OutFolder, FString& OutPackageName, FString& OutAssetName, FString& OutErrorMessage) const;

	/**
	 * Creates the Blueprint asset through IAssetTools::CreateAsset and UBlueprintFactory, outside any transaction
	 * @return The created Blueprint, or nullptr on failure
	 */
	class UBlueprint* CreateBlueprintAsset(UClass* ParentClass, const FString& Folder, const FString& AssetName,
		EBlueprintType BlueprintType, const FString* Description, FString& OutErrorMessage) const;

	/**
	 * Opens the Blueprint in the editor
	 * @param Blueprint The Blueprint to open
	 * @param bOpenInFullEditor Whether to open in full Blueprint editor or not
	 */
	void OpenBlueprintInEditor(class UBlueprint* Blueprint, bool bOpenInFullEditor) const;

	/**
	 * Builds the JSON result for a created Blueprint
	 * @param Blueprint The created Blueprint
	 * @param PackageName The package name
	 * @param AssetName The asset name
	 * @param bSaved Whether its package was saved
	 * @return JSON object with creation details
	 */
	TSharedPtr<FJsonObject> BuildResult(const class UBlueprint* Blueprint, const FString& PackageName,
		const FString& AssetName, bool bSaved) const;
};
