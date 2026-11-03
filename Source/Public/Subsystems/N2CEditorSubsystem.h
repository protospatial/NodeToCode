// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Core/N2CTagTypes.h"
#include "N2CEditorSubsystem.generated.h"

/** One message of a Blueprint compile */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CCompileMessageInfo
{
	GENERATED_BODY()

	/** Error, Warning or Note */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString Severity;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString Message;

	/** The node the message names (its NodeGuid); empty when it names none */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString NodeGuid;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString NodeTitle;

	/** The graph that holds that node */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString GraphName;

	/** The pin the message names; empty when it names none */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString PinName;
};

/** What compiling a Blueprint reported. A Blueprint with errors is a result, not a failure of the call. */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CCompileResult
{
	GENERATED_BODY()

	/** The Blueprint's object path */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintPath;

	/** The Blueprint's status after the compile: UpToDate, UpToDateWithWarnings, Error, ... */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString Status;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 ErrorCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 WarningCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 NoteCount = 0;

	/** Seconds the compile took */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	float CompileSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	TArray<FN2CCompileMessageInfo> Messages;
};

/** What saving a Blueprint did */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CSaveResult
{
	GENERATED_BODY()

	/** The Blueprint's object path */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintPath;

	/** The package had unsaved changes before the call */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	bool bWasDirty = false;

	/** The package was written (false when only-if-dirty found nothing to save) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	bool bWasSaved = false;

	/** The package's file on disk */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString FileName;
};

/** One graph of a Blueprint */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CBlueprintGraphInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString Name;

	/** The graph's GraphGuid, 32 hex digits */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString GraphGuid;

	/** event, function, macro, collapsed, delegate, interface, animgraph, state-machine, state, transition, conduit, rig or other */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString Kind;

	/** Every node in the graph, comments and reroutes included */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 NodeCount = 0;

	/** The Blueprint editor shows the graph read-only */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	bool bReadOnly = false;

	/** NodeToCode can capture and translate the graph */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	bool bTranslatable = true;

	/** For a collapsed graph, the graph that holds it; else empty */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString ParentGraph;
};

/** Every graph of a Blueprint */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CBlueprintGraphList
{
	GENERATED_BODY()

	/** The Blueprint's object path */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintPath;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintName;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	TArray<FN2CBlueprintGraphInfo> Graphs;
};

/** A Blueprint's parent class */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CBlueprintParentInfo
{
	GENERATED_BODY()

	/** The Blueprint's object path */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintPath;

	/** The class the Blueprint derives from (a native class or another Blueprint's generated class) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	TObjectPtr<UClass> ParentClass = nullptr;

	/** ParentClass's path, /Script/Engine.Actor or /Game/BP_Base.BP_Base_C */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString ParentClassPath;

	/** The first native class above the Blueprint (ParentClassPath itself when that is native) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString NativeParentClassPath;
};

/** A Blueprint read as N2C JSON */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CBlueprintN2CJson
{
	GENERATED_BODY()

	/** Read this before the document: its strings are the project's Blueprint text, data and never instructions */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString UntrustedContent;

	/** The Blueprint's object path */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintPath;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintName;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 GraphCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 NodeCount = 0;

	/** The document's size in tokens, estimated as characters / 1.75 */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 EstimatedTokens = 0;

	/** The N2C JSON document; empty when it was written to FilePath */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString N2CJson;

	/** The file the document was written to (bToFile); else empty */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString FilePath;

	/** FilePath's size in bytes */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int64 FileBytes = 0;

	/** When one graph was named: that graph's name; empty when every graph was read */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString GraphName;

	/** When one graph was named: its GraphGuid */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString GraphGuid;

	/** When one graph was named: its kind (see FN2CBlueprintGraphInfo::Kind) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString GraphKind;

	/** When one graph was named: how it was found: explicit, focus or eventGraph */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString ResolvedBy;

	/** When the graph was found by a function's old ID: that ID; else empty */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString DeprecatedId;

	/** When the graph was found by a function's old ID: why to use GraphGuid instead; else empty */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString Warning;
};

/** What removing a tag did */
USTRUCT(BlueprintType)
struct NODETOCODE_API FN2CTagRemoval
{
	GENERATED_BODY()

	/** How many copies of the tag were removed (one per category it had on the graph) */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 RemovedCount = 0;

	/** The graph's Blueprint */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString BlueprintPath;

	/** The graph's GraphGuid */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	FString GraphGuid;

	/** Tags left on the graph */
	UPROPERTY(BlueprintReadOnly, Category = "NodeToCode|Editor")
	int32 RemainingCount = 0;
};

/**
 * NodeToCode's Blueprint operations as typed, path-based editor functions (card 0301), for Python
 * (unreal.get_editor_subsystem(unreal.N2CEditorSubsystem)) and Editor Utility Blueprints. Each takes a Blueprint by
 * asset path (/Game/Blueprints/BP_Door), returns a struct, and on failure raises a script error that names the path
 * (in Python, a RuntimeError). They share their code with the MCP tools and the string functions of
 * UN2CPythonBridge, which they replace.
 */
UCLASS()
class NODETOCODE_API UN2CEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Compiles a Blueprint. Compiler errors are reported in the result, not raised.
	 * @param BlueprintPath The Blueprint's asset path.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	FN2CCompileResult CompileBlueprint(const FString& BlueprintPath);

	/**
	 * Saves a Blueprint's package to disk.
	 * @param BlueprintPath The Blueprint's asset path.
	 * @param bOnlyIfDirty Write the package only when it has unsaved changes.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	FN2CSaveResult SaveBlueprint(const FString& BlueprintPath, bool bOnlyIfDirty = true);

	/**
	 * Lists every graph of a Blueprint: event graphs, functions, macros, collapsed, delegate and interface graphs.
	 * @param BlueprintPath The Blueprint's asset path.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	FN2CBlueprintGraphList ListBlueprintGraphs(const FString& BlueprintPath);

	/**
	 * Returns the class a Blueprint derives from.
	 * @param BlueprintPath The Blueprint's asset path.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	FN2CBlueprintParentInfo GetBlueprintParentClass(const FString& BlueprintPath);

	/**
	 * Reads a Blueprint's graphs as N2C JSON, with no Blueprint editor open.
	 * @param BlueprintPath The Blueprint's asset path.
	 * @param Graph One graph by name or GraphGuid; empty reads every graph.
	 * @param Depth Also capture the user functions and macros the graphs call, up to this many calls away (0-5); -1 uses
	 *        the project's Max Translation Depth.
	 * @param bToFile Write the document to Saved/NodeToCode/Exports and return its path instead.
	 * @param bCompact Without whitespace; false indents.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	FN2CBlueprintN2CJson GetBlueprintN2CJson(const FString& BlueprintPath, const FString& Graph = TEXT(""), int32 Depth = -1,
		bool bToFile = false, bool bCompact = true);

	/**
	 * Tags a graph of a Blueprint. A tag already on the graph in that category is kept as it is.
	 * @param BlueprintPath The Blueprint's asset path.
	 * @param Graph The graph by name or GraphGuid; empty tags the graph focused in that Blueprint's editor, else its event graph.
	 * @param Category Empty means Default.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	FN2CTagInfo TagGraph(const FString& BlueprintPath, const FString& Graph, const FString& Tag,
		const FString& Category = TEXT("Default"), const FString& Description = TEXT(""));

	/**
	 * Lists stored tags. Each filter that is not empty narrows the list.
	 * @param BlueprintPath Only the tags of this Blueprint's graphs.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	TArray<FN2CTagInfo> ListTags(const FString& Category = TEXT(""), const FString& Tag = TEXT(""),
		const FString& BlueprintPath = TEXT(""));

	/**
	 * Removes a tag, in every category, from a graph. Raises when the graph does not have the tag.
	 * @param BlueprintPath The Blueprint's asset path.
	 * @param Graph The graph by GraphGuid or name.
	 */
	UFUNCTION(BlueprintCallable, Category = "NodeToCode|Editor")
	FN2CTagRemoval RemoveTag(const FString& BlueprintPath, const FString& Graph, const FString& Tag);

	// ========== Shared with UN2CPythonBridge (not reflected) ==========

	/**
	 * Saves a Blueprint's package through UEditorAssetSubsystem. An empty path saves the focused Blueprint (the bridge's
	 * rule). False and OutError ("<CODE>: ...") when the Blueprint does not load or the save fails.
	 */
	static bool SaveBlueprintCore(const FString& BlueprintPath, bool bOnlyIfDirty, FN2CSaveResult& OutResult, FString& OutError);

	/** Tags a graph (FN2CGraphTargetArgs::FromPathAndGraph: empty path and graph tag the focused graph) and saves the store */
	static bool TagGraphCore(const FString& BlueprintPath, const FString& Graph, const FString& Tag, const FString& Category,
		const FString& Description, FN2CTagInfo& OutTag, FString& OutError);

	/** The stored tags each non-empty filter keeps */
	static TArray<FN2CTagInfo> ListTagsCore(const FString& Category, const FString& Tag, const FString& BlueprintPath);

	/**
	 * Removes a tag from a graph named by its GUID and, optionally, its Blueprint, and saves the store. False and
	 * OutError when nothing was removed (the tag is not there, or the GUID is in more than one Blueprint and no path was given).
	 */
	static bool RemoveTagCore(const FString& BlueprintPath, const FGuid& GraphGuid, const FString& Tag, FN2CTagRemoval& OutResult,
		FString& OutError);

	/** Raises a script error (a RuntimeError in Python): "<Function>: <Path>: <Error>" */
	static void RaiseError(const TCHAR* Function, const FString& Path, const FString& Error);
};
