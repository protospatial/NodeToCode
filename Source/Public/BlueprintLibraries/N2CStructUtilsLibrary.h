// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "StructUtils/UserDefinedStruct.h"
#include "N2CStructUtilsLibrary.generated.h"

/**
 * @class UN2CStructUtilsLibrary
 * @brief Blueprint function library for creating and modifying UserDefinedStruct assets
 *
 * This library exposes struct creation and modification functionality to Blueprints and Python,
 * enabling programmatic creation of DataTable row structures without manual editor interaction.
 *
 * Example Python usage:
 * @code
 *     import unreal
 *
 *     # Create a new struct
 *     success, struct, error = unreal.N2CStructUtilsLibrary.create_user_defined_struct(
 *         '/Game/Data', 'F_MyRowStruct')
 *
 *     # Add variables to the struct
 *     unreal.N2CStructUtilsLibrary.add_struct_variable(struct, 'MyString', 'FString')
 *     unreal.N2CStructUtilsLibrary.add_struct_variable(struct, 'MyFloat', 'float')
 *     unreal.N2CStructUtilsLibrary.add_struct_variable(struct, 'MyInt', 'int32')
 * @endcode
 */
UCLASS()
class NODETOCODE_API UN2CStructUtilsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a new UserDefinedStruct asset
	 * @param PackagePath The content folder path (e.g., '/Game/Data')
	 * @param StructName The name for the struct (e.g., 'F_MyStruct')
	 * @param bSuccess Whether the struct was successfully created
	 * @param ErrorMessage Error message if the operation failed
	 * @return The created UserDefinedStruct, or nullptr on failure
	 *
	 * Note: ExpandBoolAsExecs removed to fix Python binding compatibility issue.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Struct", meta = (DisplayName = "Create User Defined Struct"))
	static UUserDefinedStruct* CreateUserDefinedStruct(
		const FString& PackagePath,
		const FString& StructName,
		bool& bSuccess,
		FString& ErrorMessage
	);

	/**
	 * Add a variable/property to a UserDefinedStruct
	 * @param Struct The UserDefinedStruct to modify
	 * @param VariableName The name for the new variable
	 * @param VariableType The type of the variable. Supported types:
	 *        - Primitives: bool, int32, int64, float, double, byte
	 *        - Strings: FString, FName, FText
	 *        - Vectors: FVector, FVector2D, FRotator, FTransform
	 *        - Colors: FColor, FLinearColor
	 *        - Objects: Use full path like '/Script/Engine.Actor' or class name 'Actor'
	 *        - Structs: Use full path like '/Script/CoreUObject.Vector' or struct name
	 * @param bSuccess Whether the variable was successfully added
	 * @param ErrorMessage Error message if the operation failed
	 *
	 * Note: ExpandBoolAsExecs removed to fix Python binding compatibility issue.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Struct", meta = (DisplayName = "Add Struct Variable"))
	static void AddStructVariable(
		UUserDefinedStruct* Struct,
		const FString& VariableName,
		const FString& VariableType,
		bool& bSuccess,
		FString& ErrorMessage
	);

	/**
	 * Add multiple variables to a UserDefinedStruct in a batch operation
	 * @param Struct The UserDefinedStruct to modify
	 * @param VariableNames Array of variable names
	 * @param VariableTypes Array of variable types (must match VariableNames length)
	 * @param bSuccess Whether all variables were successfully added
	 * @param ErrorMessage Error message if any operation failed
	 * @return Number of variables successfully added
	 *
	 * Note: ExpandBoolAsExecs removed to fix Python binding compatibility issue.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Struct", meta = (DisplayName = "Add Struct Variables Batch"))
	static int32 AddStructVariablesBatch(
		UUserDefinedStruct* Struct,
		const TArray<FString>& VariableNames,
		const TArray<FString>& VariableTypes,
		bool& bSuccess,
		FString& ErrorMessage
	);

	/**
	 * Remove the default variable that is created when a struct is first made
	 * @param Struct The UserDefinedStruct to modify
	 * @param bSuccess Whether the default variable was successfully removed
	 * @param ErrorMessage Error message if the operation failed
	 *
	 * Note: ExpandBoolAsExecs removed to fix Python binding compatibility issue.
	 * The bool out parameter was being misinterpreted when called from Python.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Struct", meta = (DisplayName = "Remove Default Struct Variable"))
	static void RemoveDefaultVariable(
		UUserDefinedStruct* Struct,
		bool& bSuccess,
		FString& ErrorMessage
	);

	/**
	 * Get a list of all variable names in a UserDefinedStruct
	 * @param Struct The UserDefinedStruct to query
	 * @return Array of variable names
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Struct", meta = (DisplayName = "Get Struct Variable Names"))
	static TArray<FString> GetStructVariableNames(UUserDefinedStruct* Struct);

	/**
	 * Check if a UserDefinedStruct exists at the given path
	 * @param AssetPath Full asset path (e.g., '/Game/Data/F_MyStruct')
	 * @return True if the struct exists
	 */
	UFUNCTION(BlueprintPure, Category = "NodeToCode|Struct", meta = (DisplayName = "Struct Exists"))
	static bool StructExists(const FString& AssetPath);

	/**
	 * Load a UserDefinedStruct from an asset path
	 * @param AssetPath Full asset path (e.g., '/Game/Data/F_MyStruct')
	 * @return The loaded struct, or nullptr if not found
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Struct", meta = (DisplayName = "Load User Defined Struct"))
	static UUserDefinedStruct* LoadUserDefinedStruct(const FString& AssetPath);

	/**
	 * Save a UserDefinedStruct asset to disk
	 * @param Struct The struct to save
	 * @return True if saved successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Struct", meta = (DisplayName = "Save Struct"))
	static bool SaveStruct(UUserDefinedStruct* Struct);

private:
	/**
	 * Convert a type string to an FEdGraphPinType
	 * @param TypeString The type name (e.g., 'float', 'FString', 'Actor')
	 * @param OutPinType The resulting pin type
	 * @param OutErrorMessage Error message if conversion failed
	 * @return True if conversion was successful
	 */
	static bool TypeStringToPinType(
		const FString& TypeString,
		FEdGraphPinType& OutPinType,
		FString& OutErrorMessage
	);
};
