// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "LLM/Verify/N2CCppCompileDriver.h"
#include "LLM/Verify/N2CCppScratchModuleVerifier.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

namespace N2CCppCompileDriverSpec
{
	/** Sets n2c.Verify.CppCompile for one case and puts it back */
	struct FScopedCompileSwitch
	{
		IConsoleVariable* Variable = nullptr;
		bool bOld = false;
		explicit FScopedCompileSwitch(bool bOn)
		{
			Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("n2c.Verify.CppCompile"));
			if (Variable)
			{
				bOld = Variable->GetBool();
				Variable->Set(bOn, ECVF_SetByCode);
			}
		}
		~FScopedCompileSwitch()
		{
			if (Variable)
			{
				Variable->Set(bOld, ECVF_SetByCode);
			}
		}
	};

	/** The specs' scratch project: kept between runs, so only the first run builds it (about a minute and a half) */
	FString ScratchRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("NodeToCode/CompileScratchSpec"));
	}

	bool SamePath(const FString& A, const FString& B)
	{
		FString KeyA = FPaths::ConvertRelativePathToFull(A);
		FString KeyB = FPaths::ConvertRelativePathToFull(B);
		FPaths::NormalizeFilename(KeyA);
		FPaths::NormalizeFilename(KeyB);
		return KeyA.Equals(KeyB, ESearchCase::IgnoreCase);
	}
}

/**
 * Card 0315: FN2CCppCompileDriver compiles generated C++ and returns the compiler's messages, and
 * FN2CCppScratchModuleVerifier compiles a C++ translation with it. The UnrealBuildTool -SingleFile backend runs headless
 * on a scratch project of its own (Intermediate/NodeToCode/CompileScratchSpec of the host); the Live Coding backend
 * needs an interactive editor with Live Coding on, so here only its refusal is checked (gate card 8 covers it).
 */
BEGIN_DEFINE_SPEC(FN2CCppCompileDriverSpec, "NodeToCode.Translation.CppCompile", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CCppCompileDriverSpec)

void FN2CCppCompileDriverSpec::Define()
{
	using namespace N2CCppCompileDriverSpec;

	Describe(TEXT("ParseDiagnostics"), [this]()
	{
		It(TEXT("reads MSVC, clang and linker messages with their file, line, column, code and severity"), [this]()
		{
			const FString Output = FString(
				TEXT("[1/1] Compile [x64] Broken.cpp\n")
				TEXT("C:\\Scratch\\Source\\M\\Broken.cpp(4,14): error C2440: 'initializing': cannot convert from 'const wchar_t [5]' to 'int32'\n")
				TEXT("C:\\Scratch\\Source\\M\\Broken.cpp(4,14): note: There is no context in which this conversion is possible\n")
				TEXT("C:\\Scratch\\Source\\M\\Broken.cpp(4,14): error C2440: 'initializing': cannot convert from 'const wchar_t [5]' to 'int32'\n")
				TEXT("C:\\Scratch\\Source\\M\\Thing.h(12): warning C4996: 'Old': deprecated\n")
				TEXT("/scratch/Source/M/Thing.cpp:7:3: error: use of undeclared identifier 'Foo'\n")
				TEXT("/scratch/Source/M/Thing.cpp:9:1: warning: unused variable 'X' [-Wunused-variable]\n")
				TEXT("Module.M.cpp.obj : error LNK2019: unresolved external symbol \"void Foo(void)\"\n")
				TEXT("LogLiveCoding: Display: C:\\Project\\Source\\Game\\Actor.cpp(30): error C2065: 'Bar': undeclared identifier\n")
				TEXT("Result: Failed (OtherCompilationError)\n"));
			const TArray<FN2CCompileDiagnostic> Found = FN2CCppCompileDriver::ParseDiagnostics(Output);
			if (!TestEqual(TEXT("six messages: the note and the repeated error left out"), Found.Num(), 6))
			{
				return;
			}
			TestEqual(TEXT("MSVC file"), Found[0].File, FString(TEXT("C:\\Scratch\\Source\\M\\Broken.cpp")));
			TestEqual(TEXT("MSVC line"), Found[0].Line, 4);
			TestEqual(TEXT("MSVC column"), Found[0].Column, 14);
			TestEqual(TEXT("MSVC code"), Found[0].Code, FString(TEXT("C2440")));
			TestTrue(TEXT("MSVC error"), Found[0].Severity == EN2CDiagnosticSeverity::Error);
			TestTrue(TEXT("MSVC message"), Found[0].Message.StartsWith(TEXT("'initializing'")));
			TestTrue(TEXT("a warning without a column"), Found[1].Severity == EN2CDiagnosticSeverity::Warning && Found[1].Line == 12 && Found[1].Column == 0);
			TestEqual(TEXT("clang file"), Found[2].File, FString(TEXT("/scratch/Source/M/Thing.cpp")));
			TestTrue(TEXT("clang line and column"), Found[2].Line == 7 && Found[2].Column == 3);
			TestEqual(TEXT("clang warning flag as code"), Found[3].Code, FString(TEXT("-Wunused-variable")));
			TestEqual(TEXT("clang message without the flag"), Found[3].Message, FString(TEXT("unused variable 'X'")));
			TestEqual(TEXT("linker code"), Found[4].Code, FString(TEXT("LNK2019")));
			TestEqual(TEXT("a Live Coding log line loses its prefix"), Found[5].File, FString(TEXT("C:\\Project\\Source\\Game\\Actor.cpp")));
		});
	});

	Describe(TEXT("Compile"), [this]()
	{
		It(TEXT("does nothing while n2c.Verify.CppCompile is off"), [this]()
		{
			FScopedCompileSwitch Off(false);
			FN2CCppCompileRequest Request;
			Request.Files.Add(TEXT("Never.cpp"), TEXT("int Never() { return 0; }\n"));
			Request.ScratchRoot = ScratchRoot() / TEXT("Off");
			const FN2CCppCompileResult Result = FN2CCppCompileDriver::Compile(Request);
			TestTrue(TEXT("nothing ran"), Result.Backend == EN2CCppCompileBackend::None && !Result.bSuccess);
			TestTrue(TEXT("COMPILE_DISABLED"), Result.Error.StartsWith(TEXT("COMPILE_DISABLED")));
			TestFalse(TEXT("no file written"), FPaths::DirectoryExists(Request.ScratchRoot));
		});

		It(TEXT("refuses Live Coding when this session has it off (the headless test editor runs with -NoLiveCoding)"), [this]()
		{
			FScopedCompileSwitch On(true);
			const FN2CCppCompileResult Result = FN2CCppCompileDriver::Compile(FN2CCppCompileRequest());
			TestTrue(TEXT("the Live Coding backend"), Result.Backend == EN2CCppCompileBackend::LiveCoding);
			TestTrue(TEXT("refused: ") + Result.Error, !Result.bSuccess && Result.Error.StartsWith(TEXT("LIVE_CODING_UNAVAILABLE")));
		});

		It(TEXT("refuses a file path that leaves the scratch module"), [this]()
		{
			FScopedCompileSwitch On(true);
			FN2CCppCompileRequest Request;
			Request.ScratchRoot = ScratchRoot();
			Request.Files.Add(TEXT("../../Escape.cpp"), TEXT("int X;\n"));
			const FN2CCppCompileResult Result = FN2CCppCompileDriver::Compile(Request);
			TestTrue(TEXT("INVALID_ARGUMENT: ") + Result.Error, Result.Error.StartsWith(TEXT("INVALID_ARGUMENT")));
			TestTrue(TEXT("nothing written"), Result.WrittenFiles.IsEmpty());
		});

		It(TEXT("compiles with UnrealBuildTool -SingleFile: a known error comes back with its file and line, valid files succeed"), [this]()
		{
			FScopedCompileSwitch On(true);
			FN2CCppCompileRequest Broken;
			Broken.ScratchRoot = ScratchRoot();
			Broken.Files.Add(TEXT("Private/N2CBroken.cpp"), TEXT("#include \"CoreMinimal.h\"\n\nint32 N2CBroken()\n{\n\tint32 Value = TEXT(\"text\");\n\treturn Value;\n}\n"));
			const FN2CCppCompileResult Failed = FN2CCppCompileDriver::Compile(Broken);
			TestTrue(TEXT("the UnrealBuildTool backend"), Failed.Backend == EN2CCppCompileBackend::UbtSingleFile);
			TestFalse(TEXT("it fails"), Failed.bSuccess);
			const FString BrokenPath = FN2CCppCompileDriver::ScratchRootFor(Broken) / TEXT("Source/N2CEvalScratch/Private/N2CBroken.cpp");
			const FN2CCompileDiagnostic* Error = Failed.Diagnostics.FindByPredicate([](const FN2CCompileDiagnostic& D) { return D.Severity == EN2CDiagnosticSeverity::Error; });
			if (TestNotNull(TEXT("an error diagnostic: ") + Failed.Error + TEXT(" ") + Failed.Output.Right(1500), Error))
			{
				TestTrue(TEXT("in the broken file: ") + Error->File, SamePath(Error->File, BrokenPath));
				TestEqual(TEXT("on line 5"), Error->Line, 5);
				TestFalse(TEXT("with a message"), Error->Message.IsEmpty());
#if PLATFORM_WINDOWS
				TestEqual(TEXT("MSVC's code"), Error->Code, FString(TEXT("C2440")));
#endif
			}

			// A new UCLASS header compiles too: -SingleFile runs UnrealHeaderTool first
			FN2CCppCompileRequest Valid;
			Valid.ScratchRoot = ScratchRoot();
			Valid.Files.Add(TEXT("Public/N2CThing.h"), TEXT("#pragma once\n#include \"CoreMinimal.h\"\n#include \"UObject/Object.h\"\n#include \"N2CThing.generated.h\"\n\n")
				TEXT("UCLASS()\nclass UN2CThing : public UObject\n{\n\tGENERATED_BODY()\npublic:\n\tUFUNCTION(BlueprintCallable, Category = \"N2C\")\n\tint32 Twice(int32 X) const;\n};\n"));
			Valid.Files.Add(TEXT("Private/N2CThing.cpp"), TEXT("#include \"N2CThing.h\"\n\nint32 UN2CThing::Twice(int32 X) const\n{\n\treturn X * 2;\n}\n"));
			const FN2CCppCompileResult Succeeded = FN2CCppCompileDriver::Compile(Valid);
			TestTrue(TEXT("valid files with a UCLASS succeed: ") + Succeeded.Error + TEXT(" ") + Succeeded.Output.Right(1500), Succeeded.bSuccess);
			TestEqual(TEXT("no error"), Succeeded.ErrorCount(), 0);
			TestFalse(TEXT("the broken file of the first request is gone"), FPaths::FileExists(BrokenPath));
		});
	});

	Describe(TEXT("FN2CCppScratchModuleVerifier"), [this]()
	{
		It(TEXT("compiles a C++ translation as a class and reports its compile errors in the implementation"), [this]()
		{
			FScopedCompileSwitch On(true);
			FN2CTranslationResponse Translation;
			FN2CGraphTranslation& Graph = Translation.Graphs.AddDefaulted_GetRef();
			Graph.GraphName = TEXT("Fire");
			Graph.GraphType = TEXT("Function");
			Graph.Code.GraphDeclaration = TEXT("UFUNCTION(BlueprintCallable, Category = \"Verify\")\nint32 Fire();");
			Graph.Code.GraphImplementation = TEXT("int32 ABP_Verify_C::Fire()\n{\n\treturn 2;\n}");
			FN2CBlueprint Capture;
			Capture.Metadata.Name = TEXT("BP_Verify");
			Capture.Metadata.ParentClass = TEXT("Actor");

			FN2CCppScratchModuleVerifier Verifier;
			Verifier.ScratchRoot = ScratchRoot();
			TestFalse(TEXT("C++ only"), Verifier.SupportsLanguage(EN2CCodeLanguage::Python));
			const FN2CVerifyReport Clean = Verifier.Verify(Translation, Capture, EN2CCodeLanguage::Cpp);
			TestTrue(TEXT("it ran: ") + Verifier.LastResult.Error + TEXT(" ") + Verifier.LastResult.Output.Right(1500), Clean.bRan);
			TestFalse(TEXT("the valid translation compiles"), Clean.HasErrors());

			Graph.Code.GraphImplementation = TEXT("int32 ABP_Verify_C::Fire()\n{\n\treturn UndefinedThing;\n}");
			const FN2CVerifyReport Broken = Verifier.Verify(Translation, Capture, EN2CCodeLanguage::Cpp);
			TestTrue(TEXT("it ran"), Broken.bRan);
			const FN2CVerifyDiagnostic* Error = Broken.Diagnostics.FindByPredicate([](const FN2CVerifyDiagnostic& D) { return D.Severity == EN2CDiagnosticSeverity::Error; });
			if (TestNotNull(TEXT("a compile error: ") + Verifier.LastResult.Output.Right(1500), Error))
			{
				TestEqual(TEXT("COMPILE_ERROR"), Error->Code, FString(N2CVerify::CompileError));
				TestEqual(TEXT("in the implementation"), Error->File, FString(TEXT("implementation")));
				TestTrue(TEXT("naming the identifier: ") + Error->Message, Error->Message.Contains(TEXT("UndefinedThing")));
				TestTrue(TEXT("with a line"), Error->Line > 0);
			}
		});

		It(TEXT("says it did not run while the compile is off"), [this]()
		{
			FScopedCompileSwitch Off(false);
			FN2CCppScratchModuleVerifier Verifier;
			Verifier.ScratchRoot = ScratchRoot();
			const FN2CVerifyReport Report = Verifier.Verify(FN2CTranslationResponse(), FN2CBlueprint(), EN2CCodeLanguage::Cpp);
			TestFalse(TEXT("not run"), Report.bRan);
			TestTrue(TEXT("one VERIFIER_UNAVAILABLE warning"), Report.Diagnostics.Num() == 1 && Report.Diagnostics[0].Code == N2CVerify::VerifierUnavailable);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
