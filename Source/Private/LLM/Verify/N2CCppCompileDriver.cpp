// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/Verify/N2CCppCompileDriver.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Utils/N2CLogger.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

namespace N2CCppCompileDriverPrivate
{
	TAutoConsoleVariable<bool> CVarCppCompile(
		TEXT("n2c.Verify.CppCompile"),
		false,
		TEXT("NodeToCode: allow the C++ compile check (card 0315). It starts UnrealBuildTool on a scratch project, or ")
		TEXT("compiles the running editor's modules with Live Coding. Off by default."),
		ECVF_Default);

	/** The scratch project's name; its editor target is <Name>Editor */
	const TCHAR* const ProjectName = TEXT("N2CCompileScratch");

	bool IsIdentifier(const FString& Text)
	{
		if (Text.IsEmpty() || FChar::IsDigit(Text[0]))
		{
			return false;
		}
		for (const TCHAR Char : Text)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	/** A relative path of segments of letters, digits, '_', '-' and '.', with no ".." and no drive or root */
	bool IsSafeRelativePath(const FString& Path)
	{
		if (Path.IsEmpty() || Path.StartsWith(TEXT("/")) || Path.StartsWith(TEXT("\\")) || Path.Contains(TEXT(":")))
		{
			return false;
		}
		TArray<FString> Segments;
		Path.Replace(TEXT("\\"), TEXT("/")).ParseIntoArray(Segments, TEXT("/"));
		for (const FString& Segment : Segments)
		{
			if (Segment.IsEmpty() || Segment == TEXT(".") || Segment == TEXT(".."))
			{
				return false;
			}
			for (const TCHAR Char : Segment)
			{
				if (!FChar::IsAlnum(Char) && Char != TEXT('_') && Char != TEXT('-') && Char != TEXT('.'))
				{
					return false;
				}
			}
		}
		return true;
	}

	/** "LogLiveCoding: Display: C:\x.cpp(4): error ..." and "[2026.11.03-...][ 0]Log..." lose their log prefix */
	FString StripLogPrefix(const FString& Line)
	{
		FString Text = Line;
		while (Text.StartsWith(TEXT("[")))
		{
			int32 Close = INDEX_NONE;
			if (!Text.FindChar(TEXT(']'), Close))
			{
				break;
			}
			Text.RightChopInline(Close + 1);
		}
		static const FRegexPattern Prefix(TEXT("^Log[A-Za-z0-9_]+:\\s*(?:(?:Display|Warning|Error|Log|Verbose|VeryVerbose):\\s*)?"));
		FRegexMatcher Matcher(Prefix, Text);
		if (Matcher.FindNext())
		{
			Text.RightChopInline(Matcher.GetMatchEnding());
		}
		return Text.TrimStartAndEnd();
	}

	EN2CDiagnosticSeverity SeverityOf(const FString& Kind)
	{
		return Kind.Contains(TEXT("warning")) ? EN2CDiagnosticSeverity::Warning : EN2CDiagnosticSeverity::Error;
	}

	/** Runs a command, collecting its output, until it ends or TimeoutSeconds pass (then the tree is killed) */
	bool RunProcess(const FString& Executable, const FString& Arguments, double TimeoutSeconds, int32& OutExitCode, FString& OutOutput, FString& OutError)
	{
		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
		{
			OutError = TEXT("PROCESS_FAILED: could not create a pipe for UnrealBuildTool's output");
			return false;
		}
		FProcHandle Process = FPlatformProcess::CreateProc(*Executable, *Arguments, /*bLaunchDetached*/ false, /*bLaunchHidden*/ true,
			/*bLaunchReallyHidden*/ true, nullptr, 0, nullptr, WritePipe, nullptr);
		if (!Process.IsValid())
		{
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
			OutError = FString::Printf(TEXT("PROCESS_FAILED: could not start %s"), *Executable);
			return false;
		}
		const double Start = FPlatformTime::Seconds();
		bool bTimedOut = false;
		while (FPlatformProcess::IsProcRunning(Process))
		{
			OutOutput += FPlatformProcess::ReadPipe(ReadPipe);
			if (FPlatformTime::Seconds() - Start > TimeoutSeconds)
			{
				FPlatformProcess::TerminateProc(Process, /*KillTree*/ true);
				bTimedOut = true;
				break;
			}
			FPlatformProcess::Sleep(0.05f);
		}
		OutOutput += FPlatformProcess::ReadPipe(ReadPipe);
		OutExitCode = -1;
		FPlatformProcess::GetProcReturnCode(Process, &OutExitCode);
		FPlatformProcess::CloseProc(Process);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		if (bTimedOut)
		{
			OutError = FString::Printf(TEXT("TIMEOUT: UnrealBuildTool did not finish in %.0f seconds"), TimeoutSeconds);
			return false;
		}
		return true;
	}

	bool WriteFile(const FString& Path, const FString& Content, FN2CCppCompileResult& Result)
	{
		if (!FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			Result.Error = FString::Printf(TEXT("WRITE_FAILED: could not write %s"), *Path);
			return false;
		}
		Result.WrittenFiles.Add(Path);
		return true;
	}

	FN2CCppCompileResult CompileWithUbt(const FN2CCppCompileRequest& Request)
	{
		FN2CCppCompileResult Result;
		Result.Backend = EN2CCppCompileBackend::UbtSingleFile;
		if (!IsIdentifier(Request.ModuleName) || Request.ModuleName == ProjectName)
		{
			Result.Error = FString::Printf(TEXT("INVALID_ARGUMENT: module name '%s' is not an identifier (or is the scratch project's name)"), *Request.ModuleName);
			return Result;
		}
		for (const FString& Dependency : Request.Dependencies)
		{
			if (!IsIdentifier(Dependency))
			{
				Result.Error = FString::Printf(TEXT("INVALID_ARGUMENT: dependency '%s' is not a module name"), *Dependency);
				return Result;
			}
		}
		for (const TPair<FString, FString>& File : Request.Files)
		{
			if (!IsSafeRelativePath(File.Key))
			{
				Result.Error = FString::Printf(TEXT("INVALID_ARGUMENT: '%s' is not a relative path inside the module"), *File.Key);
				return Result;
			}
		}

		const FString Root = FN2CCppCompileDriver::ScratchRootFor(Request);
		const FString UProject = Root / FString(ProjectName) + TEXT(".uproject");
		// Never the running project: its editor holds Live Coding, and its modules are the user's
		if (FPaths::IsSamePath(UProject, FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()))
			|| FPaths::IsUnderDirectory(Root, FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir())))
		{
			Result.Error = TEXT("INVALID_ARGUMENT: the scratch project cannot be the running project or lie in its Source folder");
			return Result;
		}

		// A fresh module folder each time, so files of an earlier request are not compiled again
		const FString ModuleDir = Root / TEXT("Source") / Request.ModuleName;
		IFileManager::Get().DeleteDirectory(*ModuleDir, /*RequireExists*/ false, /*Tree*/ true);
		const FString TargetName = FString(ProjectName) + TEXT("Editor");
		const FString Dependencies = FString::JoinBy(Request.Dependencies, TEXT(", "), [](const FString& Name) { return FString::Printf(TEXT("\"%s\""), *Name); });
		if (!WriteFile(UProject, FString::Printf(TEXT("{\n\t\"FileVersion\": 3,\n\t\"Modules\": [ { \"Name\": \"%s\", \"Type\": \"Editor\", \"LoadingPhase\": \"Default\" } ]\n}\n"),
				*Request.ModuleName), Result)
			|| !WriteFile(Root / TEXT("Source") / TargetName + TEXT(".Target.cs"), FString::Printf(
				TEXT("using UnrealBuildTool;\n\npublic class %sTarget : TargetRules\n{\n\tpublic %sTarget(TargetInfo Target) : base(Target)\n\t{\n")
				TEXT("\t\tType = TargetType.Editor;\n\t\tDefaultBuildSettings = BuildSettingsVersion.Latest;\n")
				TEXT("\t\tIncludeOrderVersion = EngineIncludeOrderVersion.Latest;\n\t\tExtraModuleNames.Add(\"%s\");\n\t}\n}\n"),
				*TargetName, *TargetName, *Request.ModuleName), Result)
			|| !WriteFile(ModuleDir / Request.ModuleName + TEXT(".Build.cs"), FString::Printf(
				TEXT("using UnrealBuildTool;\n\npublic class %s : ModuleRules\n{\n\tpublic %s(ReadOnlyTargetRules Target) : base(Target)\n\t{\n")
				TEXT("\t\tPCHUsage = PCHUsageMode.NoPCHs;\n\t\tbUseUnity = false;\n\t\tPublicDependencyModuleNames.AddRange(new string[] { %s });\n\t}\n}\n"),
				*Request.ModuleName, *Request.ModuleName, *Dependencies), Result))
		{
			return Result;
		}

		TArray<FString> Sources;
		TArray<FString> Headers;
		for (const TPair<FString, FString>& File : Request.Files)
		{
			const FString Path = ModuleDir / File.Key.Replace(TEXT("\\"), TEXT("/"));
			if (!WriteFile(Path, File.Value, Result))
			{
				return Result;
			}
			(Path.EndsWith(TEXT(".cpp")) ? Sources : Headers).Add(Path);
		}
		// A module needs its implementation; with no .cpp given, one that includes every header compiles them
		const FString ModuleCpp = ModuleDir / TEXT("N2CScratchModule.gen.cpp");
		FString ModuleCode = FString::Printf(TEXT("#include \"Modules/ModuleManager.h\"\nIMPLEMENT_MODULE(FDefaultModuleImpl, %s)\n"), *Request.ModuleName);
		if (Sources.IsEmpty())
		{
			for (const FString& Header : Headers)
			{
				if (Header.EndsWith(TEXT(".h")))
				{
					FString Relative = Header;
					FPaths::MakePathRelativeTo(Relative, *(ModuleDir + TEXT("/")));
					ModuleCode += FString::Printf(TEXT("#include \"%s\"\n"), *Relative);
				}
			}
		}
		if (!WriteFile(ModuleCpp, ModuleCode, Result))
		{
			return Result;
		}
		if (Sources.IsEmpty())
		{
			Sources.Add(ModuleCpp);
		}

#if PLATFORM_WINDOWS
		const FString Executable = TEXT("cmd.exe");
		const FString Script = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/Build.bat"));
		const TCHAR* const Platform = TEXT("Win64");
#elif PLATFORM_MAC
		const FString Executable = TEXT("/bin/sh");
		const FString Script = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/Mac/Build.sh"));
		const TCHAR* const Platform = TEXT("Mac");
#else
		const FString Executable = TEXT("/bin/sh");
		const FString Script = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/Linux/Build.sh"));
		const TCHAR* const Platform = TEXT("Linux");
#endif
		// -NoMutex: this scratch target never meets another UnrealBuildTool's; -Log keeps the shared per-user log out of it
		FString UbtArguments = FString::Printf(TEXT("%s %s Development \"-Project=%s\" -NoMutex -NoHotReload \"-Log=%s\""),
			*TargetName, Platform, *UProject, *(Root / TEXT("UnrealBuildTool.log")));
		for (const FString& Source : Sources)
		{
			UbtArguments += FString::Printf(TEXT(" \"-SingleFile=%s\""), *Source);
		}
#if PLATFORM_WINDOWS
		const FString Arguments = FString::Printf(TEXT("/c \"\"%s\" %s\""), *Script, *UbtArguments);
#else
		const FString Arguments = FString::Printf(TEXT("\"%s\" %s"), *Script, *UbtArguments);
#endif

		int32 ExitCode = -1;
		// A build another UnrealBuildTool holds a shared per-user file of (an IOException, not a compile error) is retried once
		for (int32 Attempt = 0; Attempt < 2; ++Attempt)
		{
			Result.Output.Reset();
			if (!RunProcess(Executable, Arguments, Request.TimeoutSeconds, ExitCode, Result.Output, Result.Error))
			{
				return Result;
			}
			if (ExitCode == 0 || !Result.Output.Contains(TEXT("being used by another process")))
			{
				break;
			}
			FPlatformProcess::Sleep(5.0f);
		}
		Result.Diagnostics = FN2CCppCompileDriver::ParseDiagnostics(Result.Output);
		Result.bSuccess = ExitCode == 0 && Result.ErrorCount() == 0;
		if (!Result.bSuccess && Result.ErrorCount() == 0)
		{
			Result.Error = FString::Printf(TEXT("BUILD_FAILED: UnrealBuildTool exited with %d without a compiler error (see the output)"), ExitCode);
		}
		return Result;
	}

#if PLATFORM_WINDOWS
	/** Collects the log lines of Live Coding categories while a compile runs */
	class FLiveCodingLogCapture : public FOutputDevice
	{
	public:
		FString Lines;
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category.ToString().Contains(TEXT("LiveCoding")))
			{
				Lines += Message;
				Lines += TEXT("\n");
			}
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
	};
#endif

	FN2CCppCompileResult CompileWithLiveCoding()
	{
		FN2CCppCompileResult Result;
		Result.Backend = EN2CCppCompileBackend::LiveCoding;
#if PLATFORM_WINDOWS
		if (!IsInGameThread())
		{
			Result.Error = TEXT("NOT_RUNNABLE_HERE: a Live Coding compile runs on the game thread");
			return Result;
		}
		ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
		if (!LiveCoding || !LiveCoding->IsEnabledForSession())
		{
			Result.Error = TEXT("LIVE_CODING_UNAVAILABLE: Live Coding is not enabled in this editor session (a commandlet or -NoLiveCoding); pass the files to compile them on a scratch project instead");
			return Result;
		}
		FLiveCodingLogCapture Capture;
		GLog->AddOutputDevice(&Capture);
		ELiveCodingCompileResult CompileResult = ELiveCodingCompileResult::NotStarted;
		LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &CompileResult);
		GLog->RemoveOutputDevice(&Capture);
		Result.Output = Capture.Lines;
		Result.Diagnostics = FN2CCppCompileDriver::ParseDiagnostics(Result.Output);
		Result.bSuccess = (CompileResult == ELiveCodingCompileResult::Success || CompileResult == ELiveCodingCompileResult::NoChanges)
			&& Result.ErrorCount() == 0;
		if (!Result.bSuccess && Result.ErrorCount() == 0)
		{
			Result.Error = FString::Printf(TEXT("LIVE_CODING_FAILED: the compile ended with result %d"), static_cast<int32>(CompileResult));
		}
#else
		Result.Error = TEXT("LIVE_CODING_UNAVAILABLE: Live Coding exists on Windows only");
#endif
		return Result;
	}
}

int32 FN2CCppCompileResult::ErrorCount() const
{
	return Diagnostics.FilterByPredicate([](const FN2CCompileDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EN2CDiagnosticSeverity::Error;
	}).Num();
}

bool FN2CCppCompileDriver::IsEnabled()
{
	return N2CCppCompileDriverPrivate::CVarCppCompile.GetValueOnAnyThread();
}

FString FN2CCppCompileDriver::ScratchRootFor(const FN2CCppCompileRequest& Request)
{
	const FString Root = Request.ScratchRoot.IsEmpty()
		? FPaths::ProjectIntermediateDir() / TEXT("NodeToCode/CompileScratch")
		: Request.ScratchRoot;
	FString Full = FPaths::ConvertRelativePathToFull(Root);
	FPaths::NormalizeDirectoryName(Full);
	return Full;
}

TArray<FN2CCompileDiagnostic> FN2CCppCompileDriver::ParseDiagnostics(const FString& Output)
{
	using namespace N2CCppCompileDriverPrivate;
	// MSVC: file(line[,col]): error C2440: message; the linker: file : error LNK2019: message; clang: file:line:col: error: message [-Wflag]
	static const FRegexPattern Msvc(TEXT("^(.+?)\\((\\d+)(?:,(\\d+))?\\)\\s*:\\s*(fatal error|error|warning)\\s*([A-Z]+\\d+)?\\s*:\\s*(.*)$"));
	static const FRegexPattern Linker(TEXT("^(.+?)\\s*:\\s*(fatal error|error|warning)\\s+(LNK\\d+)\\s*:\\s*(.*)$"));
	static const FRegexPattern Clang(TEXT("^(.+?):(\\d+):(\\d+):\\s*(fatal error|error|warning):\\s*(.*?)(?:\\s*\\[(-W[^\\]]+)\\])?$"));

	TArray<FN2CCompileDiagnostic> Diagnostics;
	TSet<FString> Seen;
	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines);
	for (const FString& RawLine : Lines)
	{
		const FString Line = StripLogPrefix(RawLine);
		FN2CCompileDiagnostic Diagnostic;
		FRegexMatcher MsvcMatch(Msvc, Line);
		FRegexMatcher ClangMatch(Clang, Line);
		FRegexMatcher LinkerMatch(Linker, Line);
		if (MsvcMatch.FindNext())
		{
			Diagnostic.File = MsvcMatch.GetCaptureGroup(1).TrimStartAndEnd();
			Diagnostic.Line = FCString::Atoi(*MsvcMatch.GetCaptureGroup(2));
			Diagnostic.Column = FCString::Atoi(*MsvcMatch.GetCaptureGroup(3));
			Diagnostic.Severity = SeverityOf(MsvcMatch.GetCaptureGroup(4));
			Diagnostic.Code = MsvcMatch.GetCaptureGroup(5);
			Diagnostic.Message = MsvcMatch.GetCaptureGroup(6).TrimStartAndEnd();
		}
		else if (ClangMatch.FindNext())
		{
			Diagnostic.File = ClangMatch.GetCaptureGroup(1).TrimStartAndEnd();
			Diagnostic.Line = FCString::Atoi(*ClangMatch.GetCaptureGroup(2));
			Diagnostic.Column = FCString::Atoi(*ClangMatch.GetCaptureGroup(3));
			Diagnostic.Severity = SeverityOf(ClangMatch.GetCaptureGroup(4));
			Diagnostic.Message = ClangMatch.GetCaptureGroup(5).TrimStartAndEnd();
			Diagnostic.Code = ClangMatch.GetCaptureGroup(6);
		}
		else if (LinkerMatch.FindNext())
		{
			Diagnostic.File = LinkerMatch.GetCaptureGroup(1).TrimStartAndEnd();
			Diagnostic.Severity = SeverityOf(LinkerMatch.GetCaptureGroup(2));
			Diagnostic.Code = LinkerMatch.GetCaptureGroup(3);
			Diagnostic.Message = LinkerMatch.GetCaptureGroup(4).TrimStartAndEnd();
		}
		else
		{
			continue;
		}
		// UnrealBuildTool can print a message twice (the action's output and the summary)
		const FString Key = FString::Printf(TEXT("%s|%d|%d|%s|%s"), *Diagnostic.File, Diagnostic.Line, Diagnostic.Column, *Diagnostic.Code, *Diagnostic.Message);
		if (!Seen.Contains(Key))
		{
			Seen.Add(Key);
			Diagnostics.Add(MoveTemp(Diagnostic));
		}
	}
	return Diagnostics;
}

FN2CCppCompileResult FN2CCppCompileDriver::Compile(const FN2CCppCompileRequest& Request)
{
	using namespace N2CCppCompileDriverPrivate;
	const double Start = FPlatformTime::Seconds();
	FN2CCppCompileResult Result;
	if (!IsEnabled())
	{
		Result.Error = TEXT("COMPILE_DISABLED: the C++ compile check is off; set the console variable n2c.Verify.CppCompile to 1 to allow it");
		return Result;
	}
	Result = Request.Files.IsEmpty() ? CompileWithLiveCoding() : CompileWithUbt(Request);
	Result.Seconds = FPlatformTime::Seconds() - Start;
	FN2CLogger::Get().Log(FString::Printf(TEXT("C++ compile (%s): %s, %d error(s), %.1f s%s%s"),
		Result.Backend == EN2CCppCompileBackend::LiveCoding ? TEXT("Live Coding") : TEXT("UnrealBuildTool -SingleFile"),
		Result.bSuccess ? TEXT("succeeded") : TEXT("failed"), Result.ErrorCount(), Result.Seconds,
		Result.Error.IsEmpty() ? TEXT("") : TEXT(": "), *Result.Error), EN2CLogSeverity::Info);
	return Result;
}
