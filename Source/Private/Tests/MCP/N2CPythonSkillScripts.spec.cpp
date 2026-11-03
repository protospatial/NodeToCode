// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PythonScriptTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

/**
 * Card 0306: the scripts of the asset-iterator, asset-validator and project-folder-scaffold Agent Skills
 * (Agents/claude-code/nodetocode/skills/<skill>/scripts), loaded with nodetocode.skill_script, on real assets.
 * Fixtures/Python/n2c_skill_scripts.py makes Blueprints, Widget Blueprints and a material instance in a scratch folder
 * under /Game (and a sibling folder named like it), checks the review findings fixed in the port (python-bridge-31 to
 * -34, -39) and deletes both folders; this spec runs it and reports each check.
 */
BEGIN_DEFINE_SPEC(FN2CPythonSkillScriptsSpec, "NodeToCode.Python.SkillScripts", N2C_TEST_FLAGS)

	static bool PythonAvailable()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		return Python && Python->IsPythonAvailable();
	}

	static FString PythonString(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		return FString::Printf(TEXT("__import__('base64').b64decode('%s').decode('utf-8')"),
			*FBase64::Encode(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()));
	}

END_DEFINE_SPEC(FN2CPythonSkillScriptsSpec)

void FN2CPythonSkillScriptsSpec::Define()
{
	It(TEXT("runs each skill's script on real assets with the review findings fixed"), [this]()
	{
		if (!TestTrue(TEXT("Python is available"), PythonAvailable()))
		{
			return;
		}
		const FString Script = N2CTest::FixturesDir(TEXT("Python")) / TEXT("n2c_skill_scripts.py");
		if (!TestTrue(FString::Printf(TEXT("the fixture %s exists"), *Script), FPaths::FileExists(Script)))
		{
			return;
		}
		N2CTest::FScratchDir Out(TEXT("N2CSkillScripts"));
		const FString ReportPath = Out.Path / TEXT("report.json");
		const TSharedPtr<FJsonObject> Context = MakeShared<FJsonObject>();
		Context->SetStringField(TEXT("out"), ReportPath);
		Context->SetStringField(TEXT("folder"), FString::Printf(TEXT("/Game/__N2CTest__Skills_%s"), *FDateTime::UtcNow().ToString(TEXT("%H%M%S"))));
		FString ContextText;
		FJsonSerializer::Serialize(Context.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ContextText));

		FPythonCommandEx Command;
		Command.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		Command.Command = FString::Printf(TEXT("exec(compile(open(%s, encoding='utf-8').read(), 'n2c_skill_scripts.py', 'exec'), ")
			TEXT("{'__name__': 'n2c_skill_scripts', 'N2C_CTX': %s})"), *PythonString(Script), *PythonString(ContextText));
		const bool bRan = IPythonScriptPlugin::Get()->ExecPythonCommandEx(Command);
		if (!TestTrue(FString::Printf(TEXT("the fixture runs (%s)"), *Command.CommandResult.Left(1500)), bRan))
		{
			return;
		}
		FString ReportText;
		if (!TestTrue(TEXT("the fixture writes its report"), FFileHelper::LoadFileToString(ReportText, *ReportPath)))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Report = N2CTest::ParseJsonObject(ReportText);
		if (!TestTrue(TEXT("the report is JSON"), Report.IsValid()))
		{
			return;
		}
		const FString Problem = Report->GetStringField(TEXT("problem"));
		TestTrue(FString::Printf(TEXT("the fixture runs to its end: %s"), *Problem), Problem.IsEmpty());
		const TArray<TSharedPtr<FJsonValue>>* Checks = nullptr;
		if (!TestTrue(TEXT("the report lists checks"), Report->TryGetArrayField(TEXT("checks"), Checks) && Checks->Num() >= 14))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Checks)
		{
			const TSharedPtr<FJsonObject> Check = Value->AsObject();
			TestTrue(FString::Printf(TEXT("%s %s"), *Check->GetStringField(TEXT("check")), *Check->GetStringField(TEXT("detail"))),
				Check->GetBoolField(TEXT("ok")));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
