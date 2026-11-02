// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpPlaceCppTranslationTool.h"

#include "N2CCppPlacement.h"
#include "Core/N2CSerializer.h"
#include "LLM/N2CTranslationArchive.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

REGISTER_MCP_TOOL(FN2CMcpPlaceCppTranslationTool)

namespace N2CPlaceCppPrivate
{
	TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : Values)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}

	TSharedRef<FJsonObject> FileJson(const FN2CPlacedFile& File, bool bWithContent)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("path"), File.Path);
		Object->SetBoolField(TEXT("exists"), File.bExists);
		if (bWithContent)
		{
			Object->SetStringField(TEXT("content"), File.Content);
		}
		return Object;
	}
}

FMcpToolDefinition FN2CMcpPlaceCppTranslationTool::GetDefinition() const
{
	FMcpToolDefinition Definition(
		TEXT("place-cpp-translation"),
		TEXT("Turns a saved C++ translation (a run folder from translate-focused-blueprint or the NodeToCode window) into a class in one of ")
		TEXT("the project's C++ modules: a UCLASS header (parent class: the translated Blueprint's native parent, or parent_class) with the ")
		TEXT("graph declarations in the class body, a .cpp with the implementations, the #includes resolved from the classes the Blueprint ")
		TEXT("calls (their IncludePath metadata), and the modules the module's Build.cs needs. dry_run (default true) returns the files, their ")
		TEXT("target paths and the modules and writes nothing. dry_run false writes the two files into the module's Source folder (Public/ and ")
		TEXT("Private/ when it has them) and adds the missing modules to its Build.cs, keeping each replaced file as <file>.bak. Only modules of ")
		TEXT("the project or of a plugin in the project can be targets, and nothing is written outside the module's Source folder. The code is ")
		TEXT("LLM output: review it before you compile."),
		N2CMcpToolsets::Translation);

	TMap<FString, FString> Properties = {
		{ TEXT("module"), TEXT("string") },
		{ TEXT("class_name"), TEXT("string") },
		{ TEXT("parent_class"), TEXT("string") },
		{ TEXT("run_folder"), TEXT("string") },
		{ TEXT("subfolder"), TEXT("string") },
		{ TEXT("dry_run"), TEXT("boolean") },
	};
	TMap<FString, FString> Descriptions = {
		{ TEXT("module"), TEXT("The target module: a C++ module of the project or of a plugin in the project") },
		{ TEXT("class_name"), TEXT("The new class's name, with or without its prefix (MyActor or AMyActor); also the file names") },
		{ TEXT("parent_class"), TEXT("The native parent class (Actor, ACharacter, /Script/Engine.Pawn). Default: the translated Blueprint's first native parent") },
		{ TEXT("run_folder"), TEXT("A C++ translation run folder in the translation output directory (absolute, or relative to it). Default: the latest run of this editor session") },
		{ TEXT("subfolder"), TEXT("A folder below Public/ and Private/ (or the module root), letters, digits, _ and / only. Default: none") },
		{ TEXT("dry_run"), TEXT("true (default): return the plan and write nothing. false: write the files and the Build.cs") },
	};
	Definition.InputSchema = BuildInputSchema(Properties, { TEXT("module"), TEXT("class_name") }, Descriptions);
	if (TSharedPtr<FJsonObject> DryRun = Definition.InputSchema->GetObjectField(TEXT("properties"))->GetObjectField(TEXT("dry_run")))
	{
		DryRun->SetBoolField(TEXT("default"), true);
	}

	{
		using namespace N2CMcpOutputSchema;
		Definition.OutputSchema = Object({
			{ TEXT("dryRun"), Boolean() },
			{ TEXT("runFolder"), String(TEXT("The translation run that was placed")) },
			{ TEXT("module"), String() },
			{ TEXT("moduleSourceDir"), String(TEXT("The module's Source folder; every file written lies inside it")) },
			{ TEXT("className"), String(TEXT("The class as written in C++ (AMyActor)")) },
			{ TEXT("parentClass"), String(TEXT("The parent class in C++ (AActor)")) },
			{ TEXT("header"), Object({ { TEXT("path"), String() }, { TEXT("exists"), Boolean() }, { TEXT("content"), String() } }) },
			{ TEXT("source"), Object({ { TEXT("path"), String() }, { TEXT("exists"), Boolean() }, { TEXT("content"), String() } }) },
			{ TEXT("includes"), Array(String(), TEXT("#include paths the .cpp gets")) },
			{ TEXT("requiredModules"), Array(String(), TEXT("Modules the code needs")) },
			{ TEXT("missingModules"), Array(String(), TEXT("Required modules the Build.cs does not list")) },
			{ TEXT("buildCs"), Object({ { TEXT("path"), String() }, { TEXT("changed"), Boolean() }, { TEXT("problem"), String() } }) },
			{ TEXT("warnings"), Array(String()) },
			{ TEXT("written"), Array(String(), TEXT("Files written (dry_run false)")) },
			{ TEXT("backups"), Array(String(), TEXT("Backups of the files replaced (dry_run false)")) },
		}, { TEXT("dryRun"), TEXT("module"), TEXT("className"), TEXT("header"), TEXT("source"), TEXT("requiredModules") });
	}

	// Writes source files when dry_run is false (and may replace them: backups are kept); stays in the project
	SetAnnotations(Definition, /*bReadOnly*/ false, /*bDestructive*/ true, /*bIdempotent*/ false, /*bOpenWorld*/ false);
	SetStrictArguments(Definition);
	return Definition;
}

bool FN2CMcpPlaceCppTranslationTool::ResolveRunFolder(const FString& RunFolder, FString& OutFolder, FString& OutError)
{
	const FString Base = FPaths::ConvertRelativePathToFull(FN2CPathUtils::GetTranslationsBasePath());
	FString Folder = RunFolder.TrimStartAndEnd();
	if (Folder.IsEmpty())
	{
		Folder = FN2CTranslationArchive::GetLatestRunFolder();
		if (Folder.IsEmpty())
		{
			OutError = TEXT("No translation was made in this editor session; pass run_folder (a run folder in the translation output directory)");
			return false;
		}
	}
	else if (FPaths::IsRelative(Folder))
	{
		Folder = FPaths::Combine(Base, Folder);
	}
	FString Normalized;
	if (!FN2CPathUtils::ValidatePathWithinBounds(Base, FPaths::ConvertRelativePathToFull(Folder), Normalized) || FN2CPathUtils::IsDeniedForAgentAccess(Normalized))
	{
		OutError = FString::Printf(TEXT("run_folder must be a folder in the translation output directory %s"), *Base);
		return false;
	}
	if (!FPaths::DirectoryExists(Normalized))
	{
		OutError = FString::Printf(TEXT("run_folder %s does not exist"), *Normalized);
		return false;
	}
	OutFolder = Normalized;
	return true;
}

FMcpToolCallResult FN2CMcpPlaceCppTranslationTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	return ExecuteOnGameThread([Arguments]() -> FMcpToolCallResult
	{
		using namespace N2CPlaceCppPrivate;
		FN2CMcpArgumentParser Parser(Arguments);
		FString Error;
		FN2CCppPlacementRequest Request;
		if (!Parser.TryGetRequiredString(TEXT("module"), Request.ModuleName, Error)
			|| !Parser.TryGetRequiredString(TEXT("class_name"), Request.ClassName, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}
		Request.ParentClass = Parser.GetOptionalString(TEXT("parent_class"));
		Request.Subfolder = Parser.GetOptionalString(TEXT("subfolder"));
		const bool bDryRun = Parser.GetOptionalBool(TEXT("dry_run"), true);

		FString Folder;
		if (!ResolveRunFolder(Parser.GetOptionalString(TEXT("run_folder")), Folder, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}
		const FString RunName = FPaths::GetCleanFilename(Folder);

		// The run must be a single or MCP run in C++ (manifest.json)
		FString ManifestText;
		TSharedPtr<FJsonObject> Manifest;
		if (FFileHelper::LoadFileToString(ManifestText, *FPaths::Combine(Folder, TEXT("manifest.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest) && Manifest.IsValid())
		{
			FString Language, Kind;
			Manifest->TryGetStringField(TEXT("language"), Language);
			Manifest->TryGetStringField(TEXT("kind"), Kind);
			if (!Language.IsEmpty() && !Language.Equals(TEXT("cpp"), ESearchCase::IgnoreCase))
			{
				return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("The run %s is a %s translation; only C++ translations can be placed"), *RunName, *Language));
			}
			if (Kind == TEXT("batch"))
			{
				return FMcpToolCallResult::CreateErrorResult(TEXT("A batch run holds several Blueprints; translate the one to place with translate-focused-blueprint and place that run"));
			}
		}

		// The translation (graphs) and the N2C JSON that was sent (member_parent of each node, the Blueprint's name)
		FString TranslationText;
		TSharedPtr<FJsonObject> TranslationJson;
		if (!FFileHelper::LoadFileToString(TranslationText, *FPaths::Combine(Folder, FString::Printf(TEXT("N2C_Translation_%s.json"), *RunName)))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TranslationText), TranslationJson) || !TranslationJson.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("%s has no readable N2C_Translation_%s.json"), *Folder, *RunName));
		}
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (TranslationJson->TryGetArrayField(TEXT("graphs"), Graphs))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Graphs)
			{
				const TSharedPtr<FJsonObject> GraphJson = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!GraphJson.IsValid())
				{
					continue;
				}
				FN2CGraphTranslation& Graph = Request.Translation.Graphs.AddDefaulted_GetRef();
				GraphJson->TryGetStringField(TEXT("graph_name"), Graph.GraphName);
				GraphJson->TryGetStringField(TEXT("graph_type"), Graph.GraphType);
				GraphJson->TryGetStringField(TEXT("graph_class"), Graph.GraphClass);
				const TSharedPtr<FJsonObject>* Code = nullptr;
				if (GraphJson->TryGetObjectField(TEXT("code"), Code))
				{
					(*Code)->TryGetStringField(TEXT("graphDeclaration"), Graph.Code.GraphDeclaration);
					(*Code)->TryGetStringField(TEXT("graphImplementation"), Graph.Code.GraphImplementation);
					(*Code)->TryGetStringField(TEXT("implementationNotes"), Graph.Code.ImplementationNotes);
				}
			}
		}
		FString BlueprintText;
		if (FFileHelper::LoadFileToString(BlueprintText, *FPaths::Combine(Folder, FString::Printf(TEXT("N2C_BP_%s.json"), *RunName))))
		{
			FN2CBlueprint Blueprint;
			if (FN2CSerializer::FromJson(BlueprintText, Blueprint))
			{
				Request.Blueprint = MoveTemp(Blueprint);
			}
		}

		FN2CCppPlacementPlan Plan;
		if (!FN2CCppPlacement::BuildPlan(Request, Plan, Error))
		{
			return FMcpToolCallResult::CreateErrorResult(Error);
		}

		TArray<FString> Written, Backups;
		if (!bDryRun)
		{
			if (!FN2CCppPlacement::Apply(Plan, Written, Backups, Error))
			{
				return FMcpToolCallResult::CreateErrorResult(Error);
			}
			FN2CLogger::Get().Log(FString::Printf(TEXT("place-cpp-translation wrote %s into module %s (%d file(s), %d backup(s))"),
				*Plan.ClassName, *Plan.ModuleName, Written.Num(), Backups.Num()), EN2CLogSeverity::Info, TEXT("MCP"));
		}

		const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		Result->SetStringField(TEXT("runFolder"), Folder);
		Result->SetStringField(TEXT("module"), Plan.ModuleName);
		Result->SetStringField(TEXT("moduleSourceDir"), Plan.ModuleSourceDir);
		Result->SetStringField(TEXT("className"), Plan.ClassName);
		Result->SetStringField(TEXT("parentClass"), Plan.ParentClassName);
		Result->SetObjectField(TEXT("header"), FileJson(Plan.Header, bDryRun));
		Result->SetObjectField(TEXT("source"), FileJson(Plan.Source, bDryRun));
		Result->SetArrayField(TEXT("includes"), Strings(Plan.Includes));
		Result->SetArrayField(TEXT("requiredModules"), Strings(Plan.RequiredModules));
		Result->SetArrayField(TEXT("missingModules"), Strings(Plan.MissingModules));
		const TSharedRef<FJsonObject> BuildCs = MakeShared<FJsonObject>();
		BuildCs->SetStringField(TEXT("path"), Plan.BuildCsPath);
		BuildCs->SetBoolField(TEXT("changed"), !Plan.BuildCsContent.IsEmpty());
		BuildCs->SetStringField(TEXT("problem"), Plan.BuildCsProblem);
		Result->SetObjectField(TEXT("buildCs"), BuildCs);
		Result->SetArrayField(TEXT("warnings"), Strings(Plan.Warnings));
		Result->SetArrayField(TEXT("written"), Strings(Written));
		Result->SetArrayField(TEXT("backups"), Strings(Backups));
		return FMcpToolCallResult::CreateJsonResult(Result, /*bIndented*/ true);
	});
}
