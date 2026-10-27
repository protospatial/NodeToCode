// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Templates/ValueOrError.h"

/**
 * One codec between a type written as text and FEdGraphPinType (card 0347, review arch-arch-core-llm-ui-12), so a type
 * an agent reads anywhere can be written back through any tool. Before it, UN2CStructUtilsLibrary had its own
 * vocabulary (it took "integer", which the MCP tools refused, and refused everything but a few primitives, math
 * structs and /Script classes), and nothing turned a pin type back into text a tool accepts.
 *
 * Terminal types go through FN2CMcpTypeResolver, the one terminal vocabulary: primitives (with the aliases integer,
 * integer64, real, boolean), math structs, short type names with or without a C++ prefix, object paths, and the
 * generic categories with a sub-type. On top of it Parse reads one string that also says the container and the
 * reference kind:
 * - containers: array<T>, set<T>, map<K, V> (also TArray<T>, TSet<T>, TMap<K, V>);
 * - references: class<T> (TSubclassOf<T>), softobject<T> (TSoftObjectPtr<T>), softclass<T> (TSoftClassPtr<T>),
 *   interface<T> (TScriptInterface<T>), object<T> (TObjectPtr<T>); a bare class, a T* or an object path is an object
 *   reference; softobject and softclass alone are soft references to UObject.
 * Format writes the canonical spelling Parse reads back to the same pin type: lower-case primitives, the full path of
 * every struct, enum and class (no lookup by short name), and the wrappers above in lower case.
 *
 * ParseN2C reads the type of an N2C JSON v2 pin or variable (type, sub_type, the container flags, value_type and
 * value_sub_type, as N2CStableNames writes them: Integer, Real + "float", Struct + "Vector", Object + "BP_Enemy").
 *
 * Resolving user structs, enums and Blueprints can load assets: game thread only.
 */
namespace N2C::PinType
{
	/** The pin type a type string names, or the reason it names none (it starts with an error code: INVALID_TYPE, ...) */
	NODETOCODE_API TValueOrError<FEdGraphPinType, FText> Parse(const FString& Text);

	/** The canonical text of a pin type, which Parse reads back to the same type; empty for a pin type no variable can have */
	NODETOCODE_API FString Format(const FEdGraphPinType& PinType);

	/** The pin type of an N2C JSON v2 pin or variable's type fields */
	NODETOCODE_API TValueOrError<FEdGraphPinType, FText> ParseN2C(const FString& Type, const FString& SubType, bool bIsArray,
		bool bIsSet, bool bIsMap, const FString& ValueType = FString(), const FString& ValueSubType = FString());

	/** Whether two pin types are the same variable type: category, sub-category, sub-type object, container and map value */
	NODETOCODE_API bool SameType(const FEdGraphPinType& A, const FEdGraphPinType& B);
}
