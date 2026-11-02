// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/** What a node of the dependency index is (card 0318) */
enum class EN2CDependencyNodeKind : uint8
{
	Enum,
	Struct,
	Interface,
	MacroLibrary,
	Blueprint,
};

/** Why one asset depends on another (card 0318). From depends on To: To comes first in the order. */
enum class EN2CDependencyEdgeKind : uint8
{
	/** An AssetRegistry hard package dependency (EDependencyCategory::Package, EDependencyQuery::Hard) */
	HardPackage,
	/** To's generated class is From's parent class (the ParentClassPath tag) */
	Parent,
	/** From implements the Blueprint interface To (the ImplementedInterfaces tag) */
	Interface,
	/** A call node in From calls a function of To (K2Node_CallFunction and subclasses, interface messages included) */
	Calls,
	/** A macro instance node in From uses a macro of To (a macro library) */
	Macro,
	/** A cast node in From casts to To's class */
	Cast,
	/** A member or local variable of From has To's type (class, object reference, struct or enum, containers too) */
	VariableType,
};

/** One asset of the index */
struct NODETOCODE_API FN2CDependencyNode
{
	/** Object path, e.g. /Game/Blueprints/BP_Door.BP_Door */
	FString Path;
	/** Asset name, e.g. BP_Door */
	FString Name;
	EN2CDependencyNodeKind Kind = EN2CDependencyNodeKind::Blueprint;
	/** Named by the roots (false: pulled in because a root depends on it, bIncludeReferencedUserAssets) */
	bool bRoot = false;
};

/** One reason From depends on To; node-level edges name the graph and node they come from */
struct NODETOCODE_API FN2CDependencyEdge
{
	int32 From = INDEX_NONE;
	int32 To = INDEX_NONE;
	EN2CDependencyEdgeKind Kind = EN2CDependencyEdgeKind::HardPackage;
	/** For Calls, Macro, Cast and a local variable's VariableType: the graph of From that holds it */
	FString Graph;
	/** For Calls, Macro and Cast: the node's GUID (32 hex digits); for VariableType: the variable's name */
	FString Node;
};

/** How FN2CDependencyIndex::Build reads the project */
struct NODETOCODE_API FN2CDependencyIndexOptions
{
	/**
	 * Load the Blueprints of the index (game thread) for the edges only their graphs hold: Calls, Macro, Cast and
	 * VariableType. False: the AssetRegistry alone (HardPackage, Parent, Interface), nothing is loaded.
	 */
	bool bLoadAssets = true;
	/**
	 * Also index the project's own Blueprints, structs and enums the roots depend on (hard package dependencies,
	 * followed transitively), so an order over a folder still puts a struct kept elsewhere first. Engine and engine
	 * plugin content is never indexed.
	 */
	bool bIncludeReferencedUserAssets = true;
};

/**
 * A computed dependency index over Blueprints, Blueprint interfaces, macro libraries, user structs and user enums
 * (card 0318, review issue-10, issue-12, rec-agentic-landscape-10). Nothing is stored: it is built from the
 * AssetRegistry first, then from the loaded Blueprints' graphs and variables, and writes no tag data. One index serves
 * an agent tool (card 0319), the batch translation order (card 0320) and a migration plan (card 0323).
 *
 * Order is dependencies first: Kahn's algorithm over the strongly connected components, taking among the ready ones
 * enums, then structs, then interfaces, then macro libraries, then Blueprints, each by path. A component of more than
 * one node is a cycle group; its nodes are ordered together, by kind and path.
 */
struct NODETOCODE_API FN2CDependencyIndex
{
	TArray<FN2CDependencyNode> Nodes;
	/** Each distinct (From, To, Kind, Graph, Node) once; never an edge from a node to itself */
	TArray<FN2CDependencyEdge> Edges;
	/** Every node index once, dependencies first */
	TArray<int32> Order;
	/** Strongly connected components of more than one node, each sorted by path */
	TArray<TArray<int32>> CycleGroups;
	/** Roots that named nothing, assets that did not load, and the like */
	TArray<FString> Warnings;

	/**
	 * Builds the index over Roots: asset paths (/Game/BP_A or /Game/BP_A.BP_A) and content folders (/Game/Folder, with
	 * its subfolders). Game thread.
	 */
	static FN2CDependencyIndex Build(const TArray<FString>& Roots, const FN2CDependencyIndexOptions& Options = FN2CDependencyIndexOptions());

	/** The index of the node with this object path or asset path (/Game/BP_A or /Game/BP_A.BP_A); INDEX_NONE if none */
	int32 FindNode(const FString& Path) const;

	/** Whether an edge From -> To of this kind exists */
	bool HasEdge(int32 From, int32 To, EN2CDependencyEdgeKind Kind) const;

	/** "hard-package", "parent", "interface", "calls", "macro", "cast", "variable-type" */
	static FString EdgeKindToString(EN2CDependencyEdgeKind Kind);
	/** "enum", "struct", "interface", "macro-library", "blueprint" */
	static FString NodeKindToString(EN2CDependencyNodeKind Kind);

	/**
	 * Fills Order and CycleGroups from Nodes and Edges (Build calls it; public so the ordering can be checked on a
	 * graph made in code)
	 */
	void ComputeOrder();
};
