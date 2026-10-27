// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UBlueprint;
class UEdGraph;
class UObject;

/**
 * Reads and writes Blueprint variable defaults the way the editor's Details panel does (card 0283).
 *
 * A member variable's default lives on the generated class's default object (CDO): a full compile copies the
 * variable description's DefaultValue there and then empties the description, so the description is not where the
 * value is. SetMemberDefault parses the value against the compiled FProperty, writes the CDO inside one undo
 * transaction (Modify, PreEditChange, PostEditChangeProperty) and carries the change to every instance that still had
 * the old default, as the property editor does; nothing is compiled unless the class is not up to date with the
 * description. A local variable's default lives in the function entry's description and is compiled into the
 * function; PrepareLocalDefault parses it against the skeleton function's FProperty and returns the text the editor
 * stores.
 */
class FN2CMcpVariableDefaults
{
public:
	/** The member variable's property on the Blueprint's generated class, or null when the class does not have it */
	static FProperty* FindMemberProperty(const UBlueprint* Blueprint, const FName VariableName);

	/**
	 * The member variable's default as exported text (FBlueprintEditorUtils::PropertyValueToString on the CDO), e.g.
	 * "75.000000" or "(X=1.000000,Y=2.000000,Z=3.000000)". False when the generated class has no such property.
	 */
	static bool ReadMemberDefault(const UBlueprint* Blueprint, const FName VariableName, FString& OutValue);

	/**
	 * Parses Value as the property's type, the way FBlueprintEditorUtils::PropertyValueFromString reads a variable
	 * default (an enum entry also by display name or index; a boolean only as true/false/yes/no/1/0), and returns the
	 * value as the editor stores it (PropertyValueToString). An empty Value is the type's default value (zero, empty,
	 * a struct's defaults). False with OutError (the type and what it takes) when the text does not parse.
	 * Touches nothing.
	 */
	static bool ParseValue(const FProperty* Property, const FString& Value, UObject* Owner, FString& OutCanonical, FString& OutError);

	struct FMemberResult
	{
		/** The CDO's value before the call, as exported text */
		FString OldValue;
		/** The CDO's value after the call, read back as exported text */
		FString NewValue;
		/** Instances (placed actors, child class defaults) that had the old default and now have the new one */
		int32 InstancesUpdated = 0;
		/** The Blueprint was compiled first because its class was not up to date with the variable description */
		bool bCompiledFirst = false;
	};

	/**
	 * Sets a member variable's default on the CDO in one undo transaction, as described above. An empty Value resets
	 * it to the type's default value (the parent class has no such property to inherit from). Compiles first only when
	 * the generated class lacks the property, the description still holds a default no compile has applied, or the
	 * Blueprint is dirty; that compile runs before the transaction. False with OutError, and nothing changed, when the
	 * variable is not one of the Blueprint's own or the value does not parse.
	 */
	static bool SetMemberDefault(UBlueprint* Blueprint, const FName VariableName, const FString& Value, FMemberResult& OutResult, FString& OutError);

	/**
	 * Parses a local variable's default against the property of the skeleton class's function (the scope graph's
	 * function), as ParseValue does, with the function entry node as owner. Compiles the Blueprint first when the
	 * skeleton function does not have the local yet. False with OutError when the value does not parse.
	 */
	static bool PrepareLocalDefault(UBlueprint* Blueprint, const UEdGraph* FunctionGraph, UObject* EntryNode, const FName VariableName,
		const FString& Value, FString& OutCanonical, FString& OutError);
};
