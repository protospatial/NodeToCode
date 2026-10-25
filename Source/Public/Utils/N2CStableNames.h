// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

/**
 * @file N2CStableNames.h
 * @brief The names capture writes into N2C JSON: identifiers that never change with the editor language or
 * preferences, and display hints in source (English) text (card 0138, gap-5, translation-core-28)
 */

#pragma once

#include "CoreMinimal.h"

class UClass;
class UObject;
class UEdGraphNode;
class UEdGraphPin;

namespace N2CStableNames
{
	/**
	 * A class's name for member_parent and type names: a Blueprint class by its Blueprint's name (BP_Enemy, from
	 * BP_Enemy_C or SKEL_BP_Enemy_C), any other class by its own name (Actor, KismetSystemLibrary). Only Blueprint
	 * classes lose a "_C" suffix, so a native class or a member named Temperature_C keeps it.
	 */
	NODETOCODE_API FString ClassName(const UClass* Class);

	/** The name of a pin's type object (PinSubCategoryObject): ClassName for a class, the object's own name otherwise */
	NODETOCODE_API FString TypeObjectName(const UObject* Object);

	/** Text in its source culture (FText::BuildSourceString), so it reads the same whatever the editor language */
	NODETOCODE_API FString SourceText(const FText& Text);

	/** The node's menu title in source text: the node's display hint, never its identity */
	NODETOCODE_API FString NodeTitle(const UEdGraphNode* Node);

	/**
	 * The pin's own display label in source text (the node's pin-name override or the pin's friendly name), empty when
	 * it has none or the label says no more than PinName. Read from the pin, not from UEdGraphPin::GetDisplayName, which
	 * blanks exec pins and spells names differently with the friendly-names preference.
	 */
	NODETOCODE_API FString PinLabel(const UEdGraphPin* Pin);

	/**
	 * What a keyword search of a graph's nodes matches (find-nodes-in-graph, the Python bridge's find_nodes_in_graph):
	 * the node's full title as the editor shows it and in source text, its class and the function or variable it
	 * names, so the names get-focused-blueprint returns match in every editor language
	 */
	NODETOCODE_API FString NodeSearchText(const UEdGraphNode* Node);
}
