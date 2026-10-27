// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"

// Forward declarations
class FN2CMcpArgumentParser;
class FProperty;
class UBlueprint;
class UEdGraph;
class UK2Node_FunctionEntry;
struct FBPVariableDescription;

/**
 * Shared utilities for MCP variable creation tools
 * Provides common functionality for both member variables and local variables
 */
class FN2CMcpVariableUtils
{
public:
	/**
	 * Validates that a key type is valid for use in Blueprint TMap containers
	 * @param KeyType The type identifier to validate
	 * @param OutError Error message if validation fails
	 * @return true if the key type is valid, false otherwise
	 */
	static bool ValidateMapKeyType(const FString& KeyType, FString& OutError);

	/**
	 * Validates container type and key type combination
	 * @param ContainerType The container type (none, array, set, map)
	 * @param KeyType The key type (only used for maps)
	 * @param OutError Error message if validation fails
	 * @return true if the combination is valid, false otherwise
	 */
	static bool ValidateContainerTypeParameters(const FString& ContainerType, const FString& MapKeyTypeIdentifier, FString& OutError);

	/**
	 * Adds container type properties to the input schema
	 * @param Properties The properties JSON object to add to
	 */
	static void AddContainerTypeSchemaProperties(TSharedPtr<FJsonObject> Properties);

	/**
	 * Parses container type parameters from arguments
	 * @param ArgParser The argument parser
	 * @param OutContainerType The parsed container type
	 * @param OutMapKeyTypeIdentifier The parsed map key type identifier (only relevant for maps)
	 * @param OutError Why containerType was refused: not one of ContainerTypes (any case), or not a string
	 * @return false when containerType is refused (card 0194); OutContainerType is then unset
	 */
	static bool ParseContainerTypeArguments(const FN2CMcpArgumentParser& ArgParser, FString& OutContainerType, FString& OutMapKeyTypeIdentifier,
		FString& OutError);

	/** The containerType values the variable tools take: none, array, set, map */
	static const TArray<FString>& ContainerTypes();

	/**
	 * Adds container type information to a success result
	 * @param Result The result JSON object
	 * @param ContainerType The container type used
	 * @param bIsLocalVariable Whether this is a local variable (affects the note message)
	 */
	static void AddContainerInfoToResult(TSharedPtr<FJsonObject> Result, const FString& ContainerType, bool bIsLocalVariable = false);

	/**
	 * Builds type info JSON object from a pin type
	 * @param PinType The pin type to describe
	 * @return JSON object with type information
	 */
	static TSharedPtr<FJsonObject> BuildTypeInfo(const FEdGraphPinType& PinType);

	/**
	 * The function graph Graph belongs to: Graph itself, or for a collapsed graph inside a function the function
	 * (FBlueprintEditorUtils::GetTopLevelGraph). Null when that graph is not a K2 function graph. Its name is the
	 * scope of the function's local variables (card 0155).
	 */
	static UEdGraph* GetFunctionGraph(UEdGraph* Graph);

	/** The entry node of GetFunctionGraph(Graph), or null outside a function (one shared lookup, card 0155) */
	static UK2Node_FunctionEntry* FindFunctionEntryNode(UEdGraph* Graph);

	/** A local variable of the function by name (FName comparison, so case-insensitive), or null */
	static FBPVariableDescription* FindLocalVariable(UK2Node_FunctionEntry* FunctionEntry, const FString& VariableName);

	/**
	 * A member variable as the Blueprint's graphs see it, through the skeleton class: its own variables, inherited
	 * ones (a parent Blueprint or a C++ base, such as RootComponent) and component variables. It must be visible to
	 * Blueprints (UEdGraphSchema_K2::CanUserKismetAccessVariable, not a delegate) and not private to another class;
	 * with bForSet it must also be writable (a BlueprintReadOnly property is refused, as the editor refuses its Set
	 * node). Null with OutError otherwise (card 0155).
	 */
	static FProperty* FindMemberVariableProperty(UBlueprint* Blueprint, const FString& VariableName, bool bForSet, FString& OutError);

private:
	// Private constructor to prevent instantiation
	FN2CMcpVariableUtils() = delete;
};
