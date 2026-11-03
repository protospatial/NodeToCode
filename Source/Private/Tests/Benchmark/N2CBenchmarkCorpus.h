// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;

/**
 * The benchmark corpus (card 0364, epic 0017): small to medium Blueprints, each covering one shape a translation must
 * get right (components with BeginPlay and Tick, functions with parameters and returns, pure functions, local variables,
 * loops, Branch and Switch on an enum, latent Delay, a timeline, an event dispatcher, an interface, a cast, Make and
 * Break of a user struct, a macro, a collapsed graph, an Enhanced Input event, a Widget Blueprint, a component
 * Blueprint, a child Blueprint), plus the user enum, struct and interface they use. One corpus serves the token
 * benchmark, the format benchmark and the compile-gated eval, so their results are comparable.
 *
 * The assets live in the test-only content plugin (Tests/NodeToCodeTestContent/Content/Benchmark, mounted at
 * /NodeToCodeTestContent/Benchmark). For each Blueprint, Benchmarks/corpus/shapes/<name>.json is its expected shape:
 * the C++ class name and parent, the interfaces, the components, the member variables with their types, the functions
 * with their parameters, the events, the dispatchers and, per graph, the functions and macros it calls.
 * Benchmarks/corpus/manifest.json lists every corpus graph (these Blueprints and card 0359's perf fixtures) with its
 * source, asset path, graphs, engines and shape file; Lyra and Cropout are not installed here and are listed as
 * external sources only (their content is under the Unreal Engine EULA and is never committed).
 *
 * The generator builds everything in code and saves it on UE 5.6 only (a 5.8 package does not load in 5.6), then
 * writes the shape files and the manifest from the saved Blueprints. Regenerate with
 *     $env:N2C_GENERATE_BENCHMARK_CORPUS = '1'; .\build.ps1 -UEVersion 5.6 -Test -TestFilter NodeToCode.Benchmark
 * (or -N2CGenerateBenchmarkCorpus on the editor's command line), then remove the variable and commit the .uasset
 * files, the shapes and the manifest. NodeToCode.Benchmark.Corpus checks them on every engine.
 */
namespace N2CTest::BenchmarkCorpus
{
	inline constexpr const TCHAR* FolderPath = TEXT("/NodeToCodeTestContent/Benchmark");
	inline constexpr const TCHAR* EnumName = TEXT("E_N2CBench_State");
	inline constexpr const TCHAR* StructName = TEXT("S_N2CBench_Stats");
	inline constexpr const TCHAR* InterfaceName = TEXT("BPI_N2CBench_Interact");

	/** One corpus Blueprint: its asset name and what it covers */
	struct FFixture
	{
		const TCHAR* Name;
		const TCHAR* Covers;
	};

	/** Every corpus Blueprint, in generation order */
	TConstArrayView<FFixture> All();

	/** <FolderPath>/<Name>.<Name> */
	FString ObjectPath(const TCHAR* AssetName);

	/** N2C_GENERATE_BENCHMARK_CORPUS=1 in the environment, or -N2CGenerateBenchmarkCorpus on the command line */
	bool ShouldGenerate();

	/**
	 * The checkout's root folder (the folder that holds Benchmarks/): the NodeToCode plugin's Source folder is a
	 * junction to the checkout's Source in a build.ps1 host, so the root is found through it; N2C_REPO_ROOT overrides.
	 * Empty when it cannot be found.
	 */
	FString RepoRoot();

	/** <RepoRoot>/Benchmarks/corpus */
	FString CorpusDir();

	/**
	 * The shape of a Blueprint as Benchmarks/corpus/shapes/<name>.json records it (see the namespace comment): every
	 * list sorted, so the JSON is stable
	 */
	TSharedRef<FJsonObject> ComputeShape(UBlueprint& Blueprint);

	/** The graphs of a Blueprint the manifest lists: its event graphs, functions, macros and collapsed graphs, sorted */
	TArray<FString> GraphNames(UBlueprint& Blueprint);

	/** JSON text with sorted keys and two-space indentation, as the shape files and the manifest are written */
	FString StableJson(const TSharedRef<FJsonObject>& Object);

	/**
	 * Builds the enum, struct, interface and every corpus Blueprint, compiles and saves them, then writes the shape
	 * files and the manifest. False with OutErrors on any failure (not UE 5.6, plugin not mounted, a package already
	 * loaded, a refused link, a compile error, a failed save or write); OutNotes gets one line per written file.
	 */
	bool GenerateAll(TArray<FString>& OutErrors, TArray<FString>& OutNotes);

	/** The manifest as GenerateAll writes it, from the saved corpus and perf fixtures */
	TSharedRef<FJsonObject> BuildManifest(TArray<FString>& OutErrors);
}

#endif // WITH_DEV_AUTOMATION_TESTS
