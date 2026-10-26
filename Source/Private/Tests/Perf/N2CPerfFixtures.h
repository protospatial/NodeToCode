// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"

class UBlueprint;

/**
 * The checked-in performance fixtures (card 0359, review later-7 and gap-6): three Actor Blueprints of about 100, 1000
 * and 5000 K2 nodes, plus the user struct and enum they use, in the test-only content plugin
 * Tests/NodeToCodeTestContent (mounted at /NodeToCodeTestContent; build.ps1 junctions it into each host's Plugins
 * folder, and RunUAT BuildPlugin never packages it). Capture time, N2C JSON size and tokens per node are compared
 * across runs and engines on these graphs.
 *
 * The generator builds them in code with N2CTest::FTestBlueprint from a fixed seed per fixture, so a regeneration gives
 * the same graph structure (node GUIDs differ). It runs only on UE 5.6, the floor: a package saved by 5.8 does not load
 * in 5.6, and all three hosts load this one checkout. Regenerate with
 *     $env:N2C_GENERATE_PERF_FIXTURES = '1'; .\build.ps1 -UEVersion 5.6 -Test -TestFilter NodeToCode.Perf
 * (or -N2CGeneratePerfFixtures on the editor's command line), then remove the variable and commit the .uasset files.
 */
namespace N2CTest::PerfFixtures
{
	/** One fixture's shape */
	struct FFixture
	{
		/** Asset name, BP_N2CPerf_<size> */
		const TCHAR* Name;
		/** K2 nodes over all of its graphs; the generator lands on it exactly, the spec allows 5% */
		int32 TargetNodes;
		/** User function graphs, the call chain's included */
		int32 Functions;
		/** User macro graphs */
		int32 Macros;
		/** Collapsed graphs in the event graph */
		int32 CollapsedGraphs;
		/** Functions F_Chain_0 .. F_Chain_<n>, each calling the next, so Max Translation Depth 0..n can be measured; 0: none */
		int32 ChainDepth;
	};

	/** Where the fixtures live: the content plugin's Perf folder */
	inline constexpr const TCHAR* PluginName = TEXT("NodeToCodeTestContent");
	inline constexpr const TCHAR* FolderPath = TEXT("/NodeToCodeTestContent/Perf");
	inline constexpr const TCHAR* StructName = TEXT("S_N2CPerf");
	inline constexpr const TCHAR* EnumName = TEXT("E_N2CPerf");
	inline constexpr const TCHAR* ChainPrefix = TEXT("F_Chain_");

	/** BP_N2CPerf_100 (one event graph), _1k (10 functions, 2 macros), _5k (30 functions, 5 macros, a collapsed graph, a
	 * call chain 5 deep, an event graph of about 2000 nodes) */
	TConstArrayView<FFixture> All();

	/** /NodeToCodeTestContent/Perf/<Name>.<Name> */
	FString ObjectPath(const TCHAR* AssetName);

	/** K2 nodes in every graph of the Blueprint (UBlueprint::GetAllGraphs: collapsed graphs included) */
	int32 CountK2Nodes(const UBlueprint& Blueprint);

	/** N2C_GENERATE_PERF_FIXTURES=1 in the environment, or -N2CGeneratePerfFixtures on the command line */
	bool ShouldGenerate();

	/**
	 * Builds the struct, the enum and the three Blueprints, compiles them and saves them over the files in the
	 * plugin's Content/Perf. False with OutErrors on any failure (not UE 5.6, plugin not mounted, a package already
	 * loaded, a refused link, a compile error, a failed save); OutNotes gets one line per saved asset.
	 */
	bool GenerateAll(TArray<FString>& OutErrors, TArray<FString>& OutNotes);
}

#endif // WITH_DEV_AUTOMATION_TESTS
