// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Tests/Dependencies/N2CDependencyFixtures.h"
#include "Core/N2CMigrationPlan.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/Implementations/Translation/N2CCppPlacement.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * Card 0323: FN2CMigrationPlan and create-migration-plan over the dependency fixtures of card 0318
 * (/NodeToCodeTestContent/Dependencies), with the host project's own module as the target. The plan lists the enum,
 * struct, interface and macro library first, the cycle {B, C} as one step, then A and its child; it proposes native
 * types and files (the three assets named N2CDep get distinct names and a name-collision flag), the graphs with
 * token estimates and the risk flags; it starts as a draft, honours the exclude list, and writes plan.json and plan.md
 * under the plans folder and nothing else.
 */
BEGIN_DEFINE_SPEC(FN2CMigrationPlanSpec, "NodeToCode.Migration.Plan", N2C_TEST_FLAGS)

	static FString Path(const TCHAR* AssetName)
	{
		return N2CTest::DependencyFixtures::ObjectPath(AssetName);
	}

	static FString FirstText(const FMcpToolCallResult& Result)
	{
		FString Text;
		if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
		{
			Result.Content[0]->TryGetStringField(TEXT("text"), Text);
		}
		return Text;
	}

	/** Every file under Directory, sorted */
	static TArray<FString> FilesUnder(const FString& Directory)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), /*Files*/ true, /*Directories*/ false);
		Files.Sort();
		return Files;
	}

	/** The host project's module (the hosts' module is named after the project) and its Source folder */
	bool HostModule(FN2CMigrationPlanOptions& Options)
	{
		Options.TargetModule = FApp::GetProjectName();
		FString Error;
		return TestTrue(TEXT("the host project's module is found: ") + Error,
			FN2CCppPlacement::FindModule(Options.TargetModule, Options.ModuleSourceDir, Error));
	}

	/** [step][asset] -> path, and each step's kind */
	static TArray<TArray<FString>> StepPaths(const TSharedPtr<FJsonObject>& Plan, TArray<FString>* OutKinds = nullptr)
	{
		TArray<TArray<FString>> Out;
		for (const TSharedPtr<FJsonValue>& Step : Plan->GetArrayField(TEXT("steps")))
		{
			TArray<FString>& Paths = Out.AddDefaulted_GetRef();
			for (const TSharedPtr<FJsonValue>& Asset : Step->AsObject()->GetArrayField(TEXT("assets")))
			{
				Paths.Add(Asset->AsObject()->GetStringField(TEXT("path")));
			}
			if (OutKinds)
			{
				OutKinds->Add(Step->AsObject()->GetStringField(TEXT("kind")));
			}
		}
		return Out;
	}

	static TSharedPtr<FJsonObject> AssetOf(const TSharedPtr<FJsonObject>& Plan, const FString& AssetPath)
	{
		for (const TSharedPtr<FJsonValue>& Step : Plan->GetArrayField(TEXT("steps")))
		{
			for (const TSharedPtr<FJsonValue>& Asset : Step->AsObject()->GetArrayField(TEXT("assets")))
			{
				if (Asset->AsObject()->GetStringField(TEXT("path")) == AssetPath)
				{
					return Asset->AsObject();
				}
			}
		}
		return nullptr;
	}

	static bool HasRisk(const TSharedPtr<FJsonObject>& Asset, const FString& Flag)
	{
		if (!Asset.IsValid())
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Risk : Asset->GetArrayField(TEXT("risks")))
		{
			if (Risk->AsObject()->GetStringField(TEXT("flag")) == Flag)
			{
				return true;
			}
		}
		return false;
	}

	static FString NativeName(const TSharedPtr<FJsonObject>& Asset)
	{
		const TSharedPtr<FJsonObject>* Native = nullptr;
		return Asset.IsValid() && Asset->TryGetObjectField(TEXT("native"), Native) ? (*Native)->GetStringField(TEXT("name")) : FString();
	}

END_DEFINE_SPEC(FN2CMigrationPlanSpec)

void FN2CMigrationPlanSpec::Define()
{
	using namespace N2CTest::DependencyFixtures;

	It(TEXT("takes the asset name without its prefix as the native stem"), [this]()
	{
		TestEqual(TEXT("BP_"), FN2CMigrationPlan::NativeStem(TEXT("BP_Door")), FString(TEXT("Door")));
		TestEqual(TEXT("BPI_"), FN2CMigrationPlan::NativeStem(TEXT("BPI_Usable")), FString(TEXT("Usable")));
		TestEqual(TEXT("BPML_"), FN2CMigrationPlan::NativeStem(TEXT("BPML_Tools")), FString(TEXT("Tools")));
		TestEqual(TEXT("S_"), FN2CMigrationPlan::NativeStem(TEXT("S_N2CDep")), FString(TEXT("N2CDep")));
		TestEqual(TEXT("no prefix"), FN2CMigrationPlan::NativeStem(TEXT("Door")), FString(TEXT("Door")));
		TestEqual(TEXT("only a prefix"), FN2CMigrationPlan::NativeStem(TEXT("BP_")), FString(TEXT("BP_")));
		TestEqual(TEXT("spaces and dashes dropped"), FN2CMigrationPlan::NativeStem(TEXT("BP_My Door-2")), FString(TEXT("MyDoor2")));
		TestEqual(TEXT("a leading digit"), FN2CMigrationPlan::NativeStem(TEXT("BP_3DCursor")), FString(TEXT("Migrated3DCursor")));
	});

	It(TEXT("orders the types first and the cycle {B, C} as one step, proposes native types, files, graphs and risks"), [this]()
	{
		N2CTest::FScratchDir Plans(TEXT("N2CMigrationPlan"));
		FN2CMigrationPlan::SetPlansDirectoryForTests(Plans.Path);
		ON_SCOPE_EXIT { FN2CMigrationPlan::SetPlansDirectoryForTests(FString()); };

		FN2CMigrationPlanOptions Options;
		if (!HostModule(Options))
		{
			return;
		}
		Options.Roots = { FolderPath };
		const TArray<FString> ModuleFilesBefore = FilesUnder(Options.ModuleSourceDir);

		const FN2CMigrationPlanResult Result = FN2CMigrationPlan::Create(Options);
		if (!TestTrue(TEXT("plan created: ") + Result.Error, Result.IsValid()))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Plan = Result.Plan;

		TestTrue(TEXT("the plan folder is under the plans folder"), FPaths::IsUnderDirectory(Result.PlanDir, Plans.Path));
		TestEqual(TEXT("the plan folder holds plan.json and plan.md"), FilesUnder(Result.PlanDir),
			TArray<FString>({ Result.PlanJsonPath, Result.PlanMdPath }));
		TestEqual(TEXT("nothing in the target module changed"), FilesUnder(Options.ModuleSourceDir), ModuleFilesBefore);
		FString Written;
		const TSharedPtr<FJsonObject> FromDisk = FFileHelper::LoadFileToString(Written, *Result.PlanJsonPath) ? N2CTest::ParseJsonObject(Written) : nullptr;
		TestTrue(TEXT("plan.json parses, with the plan's id"), FromDisk.IsValid() && FromDisk->GetStringField(TEXT("planId")) == Result.PlanId);

		TestEqual(TEXT("schemaVersion"), static_cast<int32>(Plan->GetNumberField(TEXT("schemaVersion"))), FN2CMigrationPlan::SchemaVersion);
		TestEqual(TEXT("a new plan is a draft"), Plan->GetStringField(TEXT("status")), FString(TEXT("draft")));
		TestEqual(TEXT("an empty exclude list to edit"), Plan->GetArrayField(TEXT("exclude")).Num(), 0);
		TestEqual(TEXT("target module"), Plan->GetStringField(TEXT("targetModule")), Options.TargetModule);

		TArray<FString> Kinds;
		const TArray<TArray<FString>> Steps = StepPaths(Plan, &Kinds);
		const TArray<TArray<FString>> Expected = { { Path(EnumName) }, { Path(StructName) }, { Path(InterfaceName) },
			{ Path(MacroLibraryName) }, { Path(BName), Path(CName) }, { Path(AName) }, { Path(ChildName) } };
		TestEqual(TEXT("seven steps"), Steps.Num(), Expected.Num());
		for (int32 Index = 0; Index < FMath::Min(Steps.Num(), Expected.Num()); ++Index)
		{
			TestEqual(FString::Printf(TEXT("step %d"), Index + 1), Steps[Index], Expected[Index]);
		}
		TestEqual(TEXT("step 5 is the cycle group"), Kinds.IsValidIndex(4) ? Kinds[4] : FString(), FString(TEXT("cycle-group")));
		TestEqual(TEXT("step 1 is one asset"), Kinds.IsValidIndex(0) ? Kinds[0] : FString(), FString(TEXT("asset")));
		TestEqual(TEXT("stepCount"), static_cast<int32>(Plan->GetNumberField(TEXT("stepCount"))), Steps.Num());
		TestEqual(TEXT("four Blueprints: B, C, A and the child"), static_cast<int32>(Plan->GetNumberField(TEXT("blueprintCount"))), 4);

		// Native types: the three assets named N2CDep get distinct names, and the later two say why
		const TSharedPtr<FJsonObject> E = AssetOf(Plan, Path(EnumName)), S = AssetOf(Plan, Path(StructName)), I = AssetOf(Plan, Path(InterfaceName));
		TestEqual(TEXT("the enum"), NativeName(E), FString(TEXT("EN2CDep")));
		TestEqual(TEXT("the struct, renamed"), NativeName(S), FString(TEXT("FN2CDep2")));
		TestEqual(TEXT("the interface, renamed"), NativeName(I), FString(TEXT("UN2CDep3")));
		TestFalse(TEXT("the enum keeps its name"), HasRisk(E, TEXT("name-collision")));
		TestTrue(TEXT("the struct says it was renamed"), HasRisk(S, TEXT("name-collision")));
		const TSharedPtr<FJsonObject> M = AssetOf(Plan, Path(MacroLibraryName));
		TestFalse(TEXT("a macro library has no native type"), M.IsValid() && M->HasField(TEXT("native")));
		TestTrue(TEXT("and says so"), HasRisk(M, TEXT("macro-library")));

		const TSharedPtr<FJsonObject> A = AssetOf(Plan, Path(AName));
		if (TestTrue(TEXT("A is planned"), A.IsValid()))
		{
			TestEqual(TEXT("A becomes an Actor class"), NativeName(A), FString(TEXT("AN2CDep_A")));
			TestEqual(TEXT("A's class derives from AActor"), A->GetStringField(TEXT("nativeBase")), FString(TEXT("AActor")));
			const TSharedPtr<FJsonObject> Native = A->GetObjectField(TEXT("native"));
			TestTrue(TEXT("A's header is in the target module"), Native->GetStringField(TEXT("header")).EndsWith(Options.TargetModule + TEXT("/N2CDep_A.h"))
				|| Native->GetStringField(TEXT("header")).EndsWith(TEXT("Public/N2CDep_A.h")));
			TestTrue(TEXT("A has a source file"), Native->GetStringField(TEXT("source")).EndsWith(TEXT("N2CDep_A.cpp")));
			bool bEventGraph = false;
			for (const TSharedPtr<FJsonValue>& Graph : A->GetArrayField(TEXT("graphs")))
			{
				if (Graph->AsObject()->GetStringField(TEXT("name")) == TEXT("EventGraph"))
				{
					bEventGraph = Graph->AsObject()->GetStringField(TEXT("kind")) == TEXT("event")
						&& Graph->AsObject()->GetNumberField(TEXT("estimatedTokens")) > 0;
				}
			}
			TestTrue(TEXT("A's event graph is listed with a token estimate"), bEventGraph);
			TestTrue(TEXT("A's tokens add up to more than 0"), A->GetNumberField(TEXT("estimatedTokens")) > 0);
			TestTrue(TEXT("A runs a macro"), HasRisk(A, TEXT("macro")));
			TestFalse(TEXT("A is in no cycle"), HasRisk(A, TEXT("cycle")));
		}
		TestTrue(TEXT("B is in the cycle"), HasRisk(AssetOf(Plan, Path(BName)), TEXT("cycle")));
		TestTrue(TEXT("C is in the cycle"), HasRisk(AssetOf(Plan, Path(CName)), TEXT("cycle")));
		const TSharedPtr<FJsonObject> Child = AssetOf(Plan, Path(ChildName));
		TestEqual(TEXT("the child's class derives from A's planned class"), Child.IsValid() ? Child->GetStringField(TEXT("nativeBase")) : FString(),
			FString(TEXT("AN2CDep_A")));
		TestTrue(TEXT("riskCounts counts the cycle members"), static_cast<int32>(Plan->GetObjectField(TEXT("riskCounts"))->GetNumberField(TEXT("cycle"))) == 2);

		FString Markdown;
		if (TestTrue(TEXT("plan.md written"), FFileHelper::LoadFileToString(Markdown, *Result.PlanMdPath)))
		{
			TestTrue(TEXT("plan.md says draft"), Markdown.Contains(TEXT("Status: **draft**")));
			TestTrue(TEXT("plan.md says how to approve"), Markdown.Contains(TEXT("\"status\": \"approved\"")));
			TestTrue(TEXT("plan.md marks the cycle step"), Markdown.Contains(TEXT("| 5 (cycle) | ") + Path(BName)));
			TestTrue(TEXT("plan.md lists the child last"), Markdown.Contains(TEXT("| 7 | ") + Path(ChildName)));
		}
	});

	It(TEXT("keeps excluded assets in the order, marked, and warns about an exclude path that names nothing"), [this]()
	{
		N2CTest::FScratchDir Plans(TEXT("N2CMigrationExclude"));
		FN2CMigrationPlan::SetPlansDirectoryForTests(Plans.Path);
		ON_SCOPE_EXIT { FN2CMigrationPlan::SetPlansDirectoryForTests(FString()); };

		FN2CMigrationPlanOptions Options;
		if (!HostModule(Options))
		{
			return;
		}
		Options.Roots = { FolderPath };
		Options.Exclude = { FString::Printf(TEXT("%s/%s"), FolderPath, ChildName), TEXT("/Game/N2CNoSuchAsset") };
		Options.bEstimateTokens = false;
		const FN2CMigrationPlanResult Result = FN2CMigrationPlan::Create(Options);
		if (!TestTrue(TEXT("plan created: ") + Result.Error, Result.IsValid()))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Child = AssetOf(Result.Plan, Path(ChildName));
		TestTrue(TEXT("the child is still listed, excluded"), Child.IsValid() && Child->GetBoolField(TEXT("excluded")));
		const TArray<TSharedPtr<FJsonValue>> Steps = Result.Plan->GetArrayField(TEXT("steps"));
		TestTrue(TEXT("its step is excluded"), Steps.Num() == 7 && Steps.Last()->AsObject()->GetBoolField(TEXT("excluded")));
		TestEqual(TEXT("excludedCount"), static_cast<int32>(Result.Plan->GetNumberField(TEXT("excludedCount"))), 1);
		TArray<FString> Exclude;
		for (const TSharedPtr<FJsonValue>& Value : Result.Plan->GetArrayField(TEXT("exclude")))
		{
			Exclude.Add(Value->AsString());
		}
		TestTrue(TEXT("the exclude list names the child by object path"), Exclude.Contains(Path(ChildName)));
		bool bWarned = false;
		for (const TSharedPtr<FJsonValue>& Warning : Result.Plan->GetArrayField(TEXT("warnings")))
		{
			bWarned |= Warning->AsString().Contains(TEXT("N2CNoSuchAsset"));
		}
		TestTrue(TEXT("a warning names the unknown exclude path"), bWarned);
		const TSharedPtr<FJsonObject> A = AssetOf(Result.Plan, Path(AName));
		TestTrue(TEXT("without estimates, tokens are 0"), A.IsValid() && A->GetNumberField(TEXT("estimatedTokens")) == 0);
	});

	It(TEXT("create-migration-plan writes the plan and returns its summary; refuses bad arguments and an unknown module"), [this]()
	{
		N2CTest::FScratchDir Plans(TEXT("N2CMigrationTool"));
		FN2CMigrationPlan::SetPlansDirectoryForTests(Plans.Path);
		ON_SCOPE_EXIT { FN2CMigrationPlan::SetPlansDirectoryForTests(FString()); };

		auto Args = [](const TArray<FString>& Paths, const FString& Module)
		{
			const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& Value : Paths)
			{
				Values.Add(MakeShared<FJsonValueString>(Value));
			}
			Json->SetArrayField(TEXT("paths"), Values);
			if (!Module.IsEmpty())
			{
				Json->SetStringField(TEXT("targetModule"), Module);
			}
			return Json;
		};
		const TSharedPtr<FJsonObject> Good = Args({ FolderPath }, FApp::GetProjectName());
		Good->SetBoolField(TEXT("estimateTokens"), false);
		const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("create-migration-plan"), Good);
		if (!TestFalse(TEXT("not an error: ") + FirstText(Result), Result.bIsError))
		{
			return;
		}
		TestTrue(TEXT("marked as untrusted"), N2CTest::IsMarkedUntrusted(Result));
		const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(TEXT("create-migration-plan"));
		if (TestTrue(TEXT("structuredContent"), Tool.IsValid() && Result.StructuredContent.IsValid()))
		{
			TArray<FString> Errors;
			TestTrue(TEXT("structuredContent matches the outputSchema"), N2CMcpOutputSchema::Validate(Tool->GetDefinition().OutputSchema,
				MakeShared<FJsonValueObject>(Result.StructuredContent), Errors));
			for (const FString& Error : Errors)
			{
				AddError(Error);
			}
			const FMcpToolDefinition Definition = Tool->GetDefinition();
			TestFalse(TEXT("not read-only (it writes the plan)"), Definition.Annotations.IsReadOnly());
			TestTrue(TEXT("not destructive"), Definition.Annotations.DestructiveHint == TOptional<bool>(false));
		}
		const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(Result);
		if (TestTrue(TEXT("result JSON"), Json.IsValid()))
		{
			TestEqual(TEXT("seven steps"), static_cast<int32>(Json->GetNumberField(TEXT("stepCount"))), 7);
			TestEqual(TEXT("draft"), Json->GetStringField(TEXT("status")), FString(TEXT("draft")));
			TestTrue(TEXT("plan.json exists"), FPaths::FileExists(Json->GetStringField(TEXT("planJson"))));
			TestTrue(TEXT("plan.md exists"), FPaths::FileExists(Json->GetStringField(TEXT("planMd"))));
			const TArray<TSharedPtr<FJsonValue>> Steps = Json->GetArrayField(TEXT("steps"));
			TestTrue(TEXT("step 5 lists B and C"), Steps.Num() == 7 && Steps[4]->AsObject()->GetArrayField(TEXT("assets")).Num() == 2
				&& Steps[4]->AsObject()->GetStringField(TEXT("kind")) == TEXT("cycle-group"));
		}

		auto Fails = [this](const FString& What, const TSharedPtr<FJsonObject>& Arguments, const FString& Code)
		{
			const FMcpToolCallResult Bad = N2CTest::RunTool(TEXT("create-migration-plan"), Arguments);
			TestTrue(What + TEXT(": an error"), Bad.bIsError);
			TestTrue(FString::Printf(TEXT("%s: starts with %s (%s)"), *What, *Code, *FirstText(Bad)), FirstText(Bad).StartsWith(Code));
		};
		Fails(TEXT("no targetModule"), Args({ FolderPath }, FString()), TEXT("INVALID_ARGUMENT"));
		Fails(TEXT("no paths"), Args({}, FApp::GetProjectName()), TEXT("INVALID_ARGUMENT"));
		Fails(TEXT("a path without a leading slash"), Args({ TEXT("Game/Blueprints") }, FApp::GetProjectName()), TEXT("INVALID_ARGUMENT"));
		Fails(TEXT("an unknown module"), Args({ FolderPath }, TEXT("N2CNoSuchModule")), TEXT("MODULE_NOT_FOUND"));
		Fails(TEXT("an engine module"), Args({ FolderPath }, TEXT("Engine")), TEXT("MODULE_NOT_FOUND"));
		Fails(TEXT("a folder with nothing to plan"), Args({ TEXT("/Game/N2CNoSuchMigrationFolder") }, FApp::GetProjectName()), TEXT("NOT_FOUND"));
		{
			const TSharedPtr<FJsonObject> Arguments = Args({ FolderPath }, FApp::GetProjectName());
			Arguments->SetStringField(TEXT("exclude"), Path(AName));
			Fails(TEXT("exclude as a string"), Arguments, TEXT("INVALID_ARGUMENT"));
		}
		{
			const TSharedPtr<FJsonObject> Arguments = Args({ FolderPath }, FApp::GetProjectName());
			Arguments->SetStringField(TEXT("estimateTokens"), TEXT("no"));
			Fails(TEXT("estimateTokens as a string"), Arguments, TEXT("INVALID_ARGUMENT"));
		}
		TestEqual(TEXT("only the one successful call wrote a plan"), FilesUnder(Plans.Path).Num(), 2);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
