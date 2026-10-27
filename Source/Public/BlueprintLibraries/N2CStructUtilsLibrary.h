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
 * In Python, a function with out parameters returns a tuple: the return value first (when it has one), then the
 * out parameters in declaration order. So create_user_defined_struct returns (struct, success, error),
 * add_struct_variable and remove_default_variable return (success, error), and add_struct_variables_batch returns
 * (count, success, error). scripts/data/struct_creator.py wraps these calls.
 *
 * Example Python usage:
 * @code
 *     import unreal
 *
 *     lib = unreal.N2CStructUtilsLibrary
 *
 *     # Create a new struct (the return value comes first, then the out parameters)
 *     struct, success, error = lib.create_user_defined_struct('/Game/Data', 'F_MyRowStruct')
 *     if not success:
 *         raise RuntimeError(error)
 *
 *     # Add variables to the struct; each call returns (success, error)
 *     for name, var_type in [('MyString', 'FString'), ('MyFloat', 'float'), ('MyInt', 'int32')]:
 *         ok, error = lib.add_struct_variable(struct, name, var_type)
 *         if not ok:
 *             raise RuntimeError(error)
 *
 *     # A new struct starts with one default member (a bool named MemberVar_<n>); remove it once the struct has
 *     # others, then save
 *     lib.remove_default_variable(struct)
 *     lib.save_struct(struct)
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
	 * @param bSuccess Whether the variable was successfully added. False, with nothing added, when the struct already
	 *        has a variable with this name or the new variable could not be given it.
	 * @param ErrorMessage Error message if the operation failed
	 *
	 * Adding and naming the variable is one undo step.
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
	 * Remove the default variable that is created when a struct is first made (a bool named MemberVar_<n>)
	 * @param Struct The UserDefinedStruct to modify
	 * @param bSuccess Whether the default variable was successfully removed. False, with nothing removed, when the
	 *        struct has no such variable (it was already removed, or every member was named by the user) or when it
	 *        is the struct's only member (a struct cannot be empty: add the other variables first).
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
	 * @return Array of the members' names as given to AddStructVariable and shown in the struct editor (e.g. "MyFloat")
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
