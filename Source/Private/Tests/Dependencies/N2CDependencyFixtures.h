// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"

/**
 * The checked-in dependency fixtures (card 0318), in the test-only content plugin Tests/NodeToCodeTestContent under
 * /NodeToCodeTestContent/Dependencies:
 * - E_N2CDep (user enum) and S_N2CDep (user struct with an E_N2CDep member);
 * - BPI_N2CDep (Blueprint interface, function Ping) and BPML_N2CDep (macro library, macro DepMacro);
 * - BP_N2CDep_A (Actor): implements BPI_N2CDep, has variables Data (S_N2CDep) and TargetB (BP_N2CDep_B), and its event
 *   graph calls BP_N2CDep_B's DoB on TargetB and runs DepMacro;
 * - BP_N2CDep_B and BP_N2CDep_C (Actors) reference each other: B has variable OtherC (BP_N2CDep_C) and function DoB;
 *   C has variable OtherB (BP_N2CDep_B) and casts to BP_N2CDep_B in its event graph;
 * - BP_N2CDep_Child, whose parent class is BP_N2CDep_A.
 *
 * Generated in C++ on UE 5.6 only (a package saved by a later engine does not load in 5.6):
 *     $env:N2C_GENERATE_DEPENDENCY_FIXTURES = '1'; .\build.ps1 -UEVersion 5.6 -Test -TestFilter NodeToCode.Dependencies
 * (or -N2CGenerateDependencyFixtures on the editor's command line), then remove the variable and commit the .uasset files.
 */
namespace N2CTest::DependencyFixtures
{
	inline constexpr const TCHAR* FolderPath = TEXT("/NodeToCodeTestContent/Dependencies");
	inline constexpr const TCHAR* EnumName = TEXT("E_N2CDep");
	inline constexpr const TCHAR* StructName = TEXT("S_N2CDep");
	inline constexpr const TCHAR* InterfaceName = TEXT("BPI_N2CDep");
	inline constexpr const TCHAR* MacroLibraryName = TEXT("BPML_N2CDep");
	inline constexpr const TCHAR* AName = TEXT("BP_N2CDep_A");
	inline constexpr const TCHAR* BName = TEXT("BP_N2CDep_B");
	inline constexpr const TCHAR* CName = TEXT("BP_N2CDep_C");
	inline constexpr const TCHAR* ChildName = TEXT("BP_N2CDep_Child");

	/** /NodeToCodeTestContent/Dependencies/<Name>.<Name> */
	FString ObjectPath(const TCHAR* AssetName);

	/** N2C_GENERATE_DEPENDENCY_FIXTURES=1 in the environment, or -N2CGenerateDependencyFixtures on the command line */
	bool ShouldGenerate();

	/**
	 * Builds, compiles and saves every fixture over the files in the plugin's Content/Dependencies. False with OutErrors
	 * on any failure (not UE 5.6, plugin not mounted, a package already loaded, a compile error, a failed save).
	 */
	bool GenerateAll(TArray<FString>& OutErrors, TArray<FString>& OutNotes);
}

#endif // WITH_DEV_AUTOMATION_TESTS
