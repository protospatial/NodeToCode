// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

class UN2CGraphStateManager;

/** What FN2CDependencyActions::TagDependencies did */
struct NODETOCODE_API FN2CDependencyTagResult
{
	/** Assets the roots depend on, directly or through each other (the roots themselves not counted) */
	int32 Dependencies = 0;
	/** Dependency Blueprints, interfaces and macro libraries that got the tag on at least one graph */
	int32 TaggedBlueprints = 0;
	/** Graphs that got the tag (or already had it) */
	int32 TaggedGraphs = 0;
	/** Dependencies with no graph to tag: user structs and enums, and Blueprints with no translatable graph */
	TArray<FString> Untagged;
	/** The index's warnings (a root that names nothing, an asset that did not load) */
	TArray<FString> Warnings;
};

/**
 * The tag manager's dependency actions (card 0319), on FN2CDependencyIndex (card 0318): tag everything the checked
 * graphs' Blueprints depend on, and open UE's Reference Viewer on those Blueprints.
 */
struct NODETOCODE_API FN2CDependencyActions
{
	/**
	 * Applies Tag (in Category) to every translatable graph (FN2CGraphUtils::CanTranslateGraph: event graphs,
	 * functions, macros, interface functions) of every Blueprint, interface and macro library that RootBlueprintPaths
	 * depend on, transitively, following every edge kind of the index. The roots themselves are not tagged. Structs and
	 * enums have no graph and are listed in Untagged.
	 * @param RootBlueprintPaths Object or package paths of the Blueprints to start from (FN2CTagInfo::BlueprintPath)
	 * @param StateManager       The store to tag in (UN2CGraphStateManager::Get() in the editor; an isolated one in tests)
	 */
	static FN2CDependencyTagResult TagDependencies(const TArray<FString>& RootBlueprintPaths, const FString& Tag,
		const FString& Category, UN2CGraphStateManager& StateManager);

	/** The package names of the given object or package paths, without duplicates, in their order */
	static TArray<FName> ToPackageNames(const TArray<FString>& AssetPaths);

	/**
	 * Opens UE's Reference Viewer on the given assets (FEditorDelegates::OnOpenReferenceViewer, answered by the Asset
	 * Manager Editor plugin). False when no path names a package or nothing listens (that plugin disabled).
	 */
	static bool OpenReferenceViewer(const TArray<FString>& AssetPaths);
};
