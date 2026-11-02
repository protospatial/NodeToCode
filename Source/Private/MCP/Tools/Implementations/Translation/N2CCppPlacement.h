// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"

/** One file the placement writes: where, what, and whether a file is already there (it is backed up first) */
struct FN2CPlacedFile
{
	FString Path;
	FString Content;
	bool bExists = false;
};

/** What placing a C++ translation would produce (card 0314) */
struct FN2CCppPlacementPlan
{
	FString ModuleName;
	/** <module>/Source/<Module> folder (absolute); every file written lies inside it */
	FString ModuleSourceDir;
	/** The class as written in C++, with its prefix (AMyActor) */
	FString ClassName;
	/** The parent class in C++ (AActor) and the include that declares it */
	FString ParentClassName;
	FString ParentInclude;
	FN2CPlacedFile Header;
	FN2CPlacedFile Source;
	/** #include lines the .cpp gets, from the IncludePath metadata of each node's member_parent */
	TArray<FString> Includes;
	/** Modules the code needs (the parent's and every resolved member_parent's) */
	TArray<FString> RequiredModules;
	/** Required modules the target Build.cs does not list yet */
	TArray<FString> MissingModules;
	FString BuildCsPath;
	/** The Build.cs with MissingModules added; empty when nothing changes or the dependency list was not found */
	FString BuildCsContent;
	/** Why the Build.cs cannot be changed automatically (no dependency list), empty otherwise */
	FString BuildCsProblem;
	/** Things the caller should know: unresolved member parents, renamed class qualifiers */
	TArray<FString> Warnings;
};

/** The arguments of one placement */
struct FN2CCppPlacementRequest
{
	/** The translation: its graphs (C++) */
	FN2CTranslationResponse Translation;
	/** The N2C JSON that was translated (member_parent of each node), when known */
	TOptional<FN2CBlueprint> Blueprint;
	/** A project module, or a module of a plugin in the project */
	FString ModuleName;
	/** The new class's name, with or without its prefix (MyActor or AMyActor) */
	FString ClassName;
	/** The native parent class (Actor, AActor, /Script/Engine.Actor); empty: the translated Blueprint's native parent */
	FString ParentClass;
	/** Optional folder below Public/ and Private/ (or the module root): letters, digits, _ and / only */
	FString Subfolder;
	/** Specs: the module's Source folder instead of looking the module up (the module's Build.cs must be in it) */
	FString ModuleSourceDirOverride;
};

/**
 * Turns a C++ translation (per-graph graphDeclaration, graphImplementation) into a class a project module can compile
 * (card 0314): a UCLASS header with the declarations in the class body (struct and enum entries before it), a .cpp with
 * the implementations (class qualifiers the LLM gave that name no native type are renamed to the new class), includes
 * resolved from each member_parent's IncludePath metadata, and the modules the target Build.cs needs. Files are only
 * written inside the chosen module's Source folder, and only for a module of the project or of a plugin in the project;
 * an existing file is backed up as <file>.bak. Game thread only.
 */
class FN2CCppPlacement
{
public:
	/**
	 * The Source folder of ModuleName: a module of the current project (<Project>/Source/<Module>) or of a plugin loaded
	 * from the project (<Plugin>/Source/<Module>), holding <Module>.Build.cs. Engine and marketplace modules are refused.
	 */
	static bool FindModule(const FString& ModuleName, FString& OutSourceDir, FString& OutError);

	/** The include and module of a native class or struct named TypeName (KismetSystemLibrary, UKismetSystemLibrary, /Script/Engine.Actor) */
	static bool ResolveType(const FString& TypeName, FString& OutInclude, FString& OutModule, FString* OutCppName = nullptr);

	/** The first native class of the Blueprint asset named BlueprintName (exactly one such asset), else empty with OutError */
	static FString FindBlueprintNativeParent(const FString& BlueprintName, FString& OutError);

	/** Builds the plan without writing anything */
	static bool BuildPlan(const FN2CCppPlacementRequest& Request, FN2CCppPlacementPlan& OutPlan, FString& OutError);

	/**
	 * BuildCs with Modules added to its first PrivateDependencyModuleNames (else PublicDependencyModuleNames) AddRange
	 * list. False, with OutError, when it has no such list.
	 */
	static bool AddModulesToBuildCs(const FString& BuildCs, const TArray<FString>& Modules, FString& OutBuildCs, FString& OutError);

	/** The module names a Build.cs lists (every quoted identifier in its *DependencyModuleNames statements) */
	static TSet<FString> GetListedModules(const FString& BuildCs);

	/**
	 * Writes the plan: the header, the .cpp and the changed Build.cs, each through a temporary file with the previous file
	 * kept as <file>.bak. Every path must lie inside the module's Source folder. OutWritten lists the files written.
	 */
	static bool Apply(const FN2CCppPlacementPlan& Plan, TArray<FString>& OutWritten, TArray<FString>& OutBackups, FString& OutError);

	/** True when Path (after resolving links and ..) is inside Dir */
	static bool IsInside(const FString& Path, const FString& Dir);
};
