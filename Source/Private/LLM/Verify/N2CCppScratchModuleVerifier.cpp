// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/Verify/N2CCppScratchModuleVerifier.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MCP/Tools/Implementations/Translation/N2CCppPlacement.h"

namespace N2CCppScratchModuleVerifierPrivate
{
	FN2CVerifyReport Unavailable(const FString& Name, const FString& Why)
	{
		FN2CVerifyReport Report;
		Report.Verifier = Name;
		FN2CVerifyDiagnostic& Diagnostic = Report.Diagnostics.AddDefaulted_GetRef();
		Diagnostic.Severity = EN2CDiagnosticSeverity::Warning;
		Diagnostic.Code = N2CVerify::VerifierUnavailable;
		Diagnostic.Message = Why;
		return Report;
	}

	/** Letters and digits of the Blueprint's name, so the class name is an identifier ("N2CVerify" when none is left) */
	FString ClassStem(const FN2CBlueprint& Capture)
	{
		FString Stem;
		for (const TCHAR Char : Capture.Metadata.Name)
		{
			if (FChar::IsAlnum(Char))
			{
				Stem.AppendChar(Char);
			}
		}
		return Stem.IsEmpty() || FChar::IsDigit(Stem[0]) ? FString(TEXT("N2CVerify")) : FString(TEXT("N2CVerify")) + Stem;
	}

	FString PathKey(const FString& Path)
	{
		FString Key = FPaths::ConvertRelativePathToFull(Path).Replace(TEXT("\\"), TEXT("/"));
		FPaths::NormalizeFilename(Key);
		return Key.ToLower();
	}
}

FN2CVerifyReport FN2CCppScratchModuleVerifier::Verify(const FN2CTranslationResponse& Translation, const FN2CBlueprint& Capture,
	EN2CCodeLanguage Language) const
{
	using namespace N2CCppScratchModuleVerifierPrivate;
	LastResult = FN2CCppCompileResult();
	if (!SupportsLanguage(Language))
	{
		FN2CVerifyReport Report;
		Report.Verifier = GetName();
		return Report;
	}
	if (!FN2CCppCompileDriver::IsEnabled())
	{
		return Unavailable(GetName(), TEXT("The C++ compile check is off; set the console variable n2c.Verify.CppCompile to 1 to allow it"));
	}

	// The scratch module's folder with a Build.cs, which the placement reads for the modules it already lists
	FN2CCppCompileRequest Compile;
	Compile.ScratchRoot = ScratchRoot;
	const FString ModuleDir = FN2CCppCompileDriver::ScratchRootFor(Compile) / TEXT("Source") / Compile.ModuleName;
	const FString BuildCs = FString::Printf(TEXT("public class %s : ModuleRules\n{\n\tpublic %s(ReadOnlyTargetRules Target) : base(Target)\n\t{\n")
		TEXT("\t\tPublicDependencyModuleNames.AddRange(new string[] { \"Core\", \"CoreUObject\", \"Engine\" });\n\t}\n}\n"),
		*Compile.ModuleName, *Compile.ModuleName);
	if (!FFileHelper::SaveStringToFile(BuildCs, *(ModuleDir / Compile.ModuleName + TEXT(".Build.cs"))))
	{
		return Unavailable(GetName(), FString::Printf(TEXT("Could not write the scratch module in %s"), *ModuleDir));
	}

	// The class place-cpp-translation would write: parent from the capture's parent_class when it names a native class,
	// else the translated Blueprint's native parent, else Actor
	FN2CCppPlacementRequest Placement;
	Placement.Translation = Translation;
	Placement.Blueprint = Capture;
	Placement.ModuleName = Compile.ModuleName;
	Placement.ClassName = ClassStem(Capture);
	Placement.ModuleSourceDirOverride = ModuleDir;
	FString Include, Module;
	if (!Capture.Metadata.ParentClass.IsEmpty() && FN2CCppPlacement::ResolveType(Capture.Metadata.ParentClass, Include, Module))
	{
		Placement.ParentClass = Capture.Metadata.ParentClass;
	}
	FN2CCppPlacementPlan Plan;
	FString Error;
	if (!FN2CCppPlacement::BuildPlan(Placement, Plan, Error))
	{
		Placement.ParentClass = TEXT("Actor");
		if (!FN2CCppPlacement::BuildPlan(Placement, Plan, Error))
		{
			return Unavailable(GetName(), FString::Printf(TEXT("The translation could not be assembled into a class: %s"), *Error));
		}
	}

	// The two files, relative to the module, and every module the code needs
	auto Relative = [&ModuleDir](const FString& Path)
	{
		FString Rel = Path;
		FPaths::MakePathRelativeTo(Rel, *(ModuleDir + TEXT("/")));
		return Rel;
	};
	Compile.Files.Add(Relative(Plan.Header.Path), Plan.Header.Content);
	Compile.Files.Add(Relative(Plan.Source.Path), Plan.Source.Content);
	for (const FString& Required : Plan.RequiredModules)
	{
		Compile.Dependencies.AddUnique(Required);
	}
	LastResult = FN2CCppCompileDriver::Compile(Compile);

	FN2CVerifyReport Report;
	Report.Verifier = GetName();
	if (LastResult.Backend == EN2CCppCompileBackend::None || (!LastResult.bSuccess && LastResult.Diagnostics.IsEmpty()))
	{
		return Unavailable(GetName(), FString::Printf(TEXT("The compile did not run: %s"), *LastResult.Error));
	}
	Report.bRan = true;
	const FString HeaderKey = PathKey(Plan.Header.Path);
	const FString SourceKey = PathKey(Plan.Source.Path);
	for (const FN2CCompileDiagnostic& Compiled : LastResult.Diagnostics)
	{
		FN2CVerifyDiagnostic& Diagnostic = Report.Diagnostics.AddDefaulted_GetRef();
		const FString Key = PathKey(Compiled.File);
		Diagnostic.File = Key == HeaderKey ? TEXT("declaration") : (Key == SourceKey ? TEXT("implementation") : FString());
		Diagnostic.Line = Compiled.Line;
		Diagnostic.Column = Compiled.Column;
		Diagnostic.Severity = Compiled.Severity;
		Diagnostic.Code = Compiled.Severity == EN2CDiagnosticSeverity::Error ? N2CVerify::CompileError : N2CVerify::CompileWarning;
		// The line is the assembled file's, which holds every graph: the file name and the compiler's code say where
		Diagnostic.Message = FString::Printf(TEXT("%s(%d): %s%s%s"), *FPaths::GetCleanFilename(Compiled.File), Compiled.Line,
			*Compiled.Code, Compiled.Code.IsEmpty() ? TEXT("") : TEXT(": "), *Compiled.Message);
	}
	return Report;
}
