// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Models/N2CBlueprint.h"

/** How a compile ran */
enum class EN2CCppCompileBackend : uint8
{
	/** Nothing ran (the compile is switched off, or the request was refused) */
	None,
	/** UnrealBuildTool -SingleFile, compile only, on a scratch project of its own */
	UbtSingleFile,
	/** The running editor's Live Coding, on the project's own modules */
	LiveCoding,
};

/** One compiler message, parsed from MSVC ("file(line,col): error C2440: ...") or clang ("file:line:col: error: ...") output */
struct NODETOCODE_API FN2CCompileDiagnostic
{
	/** The file as the compiler named it (absolute) */
	FString File;
	/** 1-based; 0 when the message has no position */
	int32 Line = 0;
	int32 Column = 0;
	/** The compiler's code (C2440, LNK2019) or clang's warning flag; empty when it gives none */
	FString Code;
	EN2CDiagnosticSeverity Severity = EN2CDiagnosticSeverity::Error;
	FString Message;
};

/** One compile (card 0315) */
struct NODETOCODE_API FN2CCppCompileRequest
{
	/**
	 * Files of a scratch module, by path relative to its folder (Thing.h, Private/Thing.cpp) and content. Given: they are
	 * written to a scratch project of their own (never the running project) and each .cpp is compiled with
	 * UnrealBuildTool -SingleFile, compile only. Empty: the running editor's own modules are compiled with Live Coding.
	 */
	TMap<FString, FString> Files;
	/** The scratch module's name and the modules it depends on (its Build.cs PublicDependencyModuleNames) */
	FString ModuleName = TEXT("N2CEvalScratch");
	TArray<FString> Dependencies = { TEXT("Core"), TEXT("CoreUObject"), TEXT("Engine") };
	/** The scratch project's folder; empty: <Project>/Intermediate/NodeToCode/CompileScratch */
	FString ScratchRoot;
	/** UnrealBuildTool is stopped after this long (its first run on a new scratch project takes about a minute and a half) */
	double TimeoutSeconds = 600.0;
};

/** What a compile reported */
struct NODETOCODE_API FN2CCppCompileResult
{
	EN2CCppCompileBackend Backend = EN2CCppCompileBackend::None;
	/** It compiled without an error */
	bool bSuccess = false;
	/** Why nothing ran, or why the build failed without a compiler message ("<CODE>: ..."): COMPILE_DISABLED, ... */
	FString Error;
	TArray<FN2CCompileDiagnostic> Diagnostics;
	/** UnrealBuildTool's output, or the Live Coding log lines */
	FString Output;
	/** The files written (UbtSingleFile) */
	TArray<FString> WrittenFiles;
	double Seconds = 0.0;

	int32 ErrorCount() const;
};

/**
 * Compiles generated C++ and returns the compiler's messages as data (card 0315, review rec-agentic-landscape-6), for
 * the compile-repair loop and FN2CCppScratchModuleVerifier.
 *
 * Two backends:
 * - UbtSingleFile (the request has Files): writes a scratch project with one module (its Build.cs, the files) under
 *   ScratchRoot and runs Build.bat <Scratch>Editor -SingleFile=<each .cpp> -NoMutex, which runs UnrealHeaderTool and
 *   compiles without linking, so UCLASS headers work. It never builds the running project or its modules, so the
 *   editor's Live Coding does not stand in its way. Blocks until UnrealBuildTool ends (any thread).
 * - LiveCoding (no Files): ILiveCodingModule::Compile(WaitForCompletion) on the running editor's modules, for code
 *   already placed in a project module (place-cpp-translation); the LiveCoding log lines of the compile are parsed.
 *   Game thread only; refused when Live Coding is not enabled for this session (a commandlet, -NoLiveCoding) or on a
 *   platform without it.
 *
 * Off unless the console variable n2c.Verify.CppCompile is 1 (ini [ConsoleVariables], or the console): a compile
 * starts UnrealBuildTool or patches the running editor.
 */
class NODETOCODE_API FN2CCppCompileDriver
{
public:
	static FN2CCppCompileResult Compile(const FN2CCppCompileRequest& Request);

	/** Whether n2c.Verify.CppCompile is on */
	static bool IsEnabled();

	/** Every MSVC or clang message of Output (linker messages too); other lines are left out */
	static TArray<FN2CCompileDiagnostic> ParseDiagnostics(const FString& Output);

	/** The scratch project folder a request uses */
	static FString ScratchRootFor(const FN2CCppCompileRequest& Request);
};
