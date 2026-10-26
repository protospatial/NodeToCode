// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Code Editor/Syntax/N2CSyntaxDefinition.h"
#include "Code Editor/Syntax/N2CSyntaxDefinitionFactory.h"
#include "Core/N2CGraphStateManager.h"
#include "Core/N2CTargetLanguage.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "LLM/N2CBatchTranslationOrchestrator.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CSystemPromptManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Models/N2CBatchTranslationTypes.h"
#include "Models/N2CTranslation.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Utils/N2CPathUtils.h"

// The batch summary writer, its output folder and its options (the language it records)
N2C_TEST_PRIVATE_MEMBER(FN2CLanguagesGenerateBatchSummary, void (UN2CBatchTranslationOrchestrator::*)(),
	&UN2CBatchTranslationOrchestrator::GenerateBatchSummary)
N2C_TEST_PRIVATE_MEMBER(FN2CLanguagesBatchOutputPath, FString UN2CBatchTranslationOrchestrator::*,
	&UN2CBatchTranslationOrchestrator::BatchOutputPath)
N2C_TEST_PRIVATE_MEMBER(FN2CLanguagesBatchOptions, FN2CBatchTranslationOptions UN2CBatchTranslationOrchestrator::*,
	&UN2CBatchTranslationOrchestrator::BatchOptions)

/**
 * The target-language table (card 0250, Core/N2CTargetLanguage.h) is the one place that knows each language's id,
 * extensions and prompt key. Checks: every EN2CCodeLanguage value has exactly one row and a code-editor syntax
 * definition (the code editor keeps its own map, so this is what ties the two together); FindLanguage reads every
 * name form a record or a client may hold; every row's prompt loads under its key; the extension helpers, the MCP
 * targets tool and the MCP prompts all report what the table says; the batch summary records the id, not
 * EN2CCodeLanguage::X; and a translation without a declaration file does not pick up a stale .h.
 */
BEGIN_DEFINE_SPEC(FN2CTargetLanguageSpec, "NodeToCode.Languages", N2C_TEST_FLAGS)

	/** Every value of the enum, from reflection (no _MAX), so a value added without a row is found */
	static TArray<EN2CCodeLanguage> EnumValues()
	{
		TArray<EN2CCodeLanguage> Values;
		const UEnum* Enum = StaticEnum<EN2CCodeLanguage>();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			Values.Add(static_cast<EN2CCodeLanguage>(Enum->GetValueByIndex(Index)));
		}
		return Values;
	}

	static FString ShortName(EN2CCodeLanguage Language)
	{
		return StaticEnum<EN2CCodeLanguage>()->GetNameStringByValue(static_cast<int64>(Language));
	}

	static TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Object);
		return Object;
	}

END_DEFINE_SPEC(FN2CTargetLanguageSpec)

void FN2CTargetLanguageSpec::Define()
{
	Describe(TEXT("The table"), [this]()
	{
		It(TEXT("has one row per EN2CCodeLanguage value, in enum order, with its id and extensions"), [this]()
		{
			const TArray<EN2CCodeLanguage> Values = EnumValues();
			const TConstArrayView<FN2CTargetLanguage> Rows = N2C::GetLanguages();
			TestEqual(TEXT("one row per enum value"), Rows.Num(), Values.Num());
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				const EN2CCodeLanguage Language = Values[Index];
				const FString Name = ShortName(Language);
				const FN2CTargetLanguage* Row = N2C::FindLanguageRow(Language);
				if (!TestNotNull(FString::Printf(TEXT("%s has a row"), *Name), Row))
				{
					continue;
				}
				TestTrue(FString::Printf(TEXT("%s is row %d"), *Name, Index), Rows.IsValidIndex(Index) && &Rows[Index] == Row);
				TestEqual(FString::Printf(TEXT("%s id is the enum's short name"), *Name), FString(Row->Id), Name);
				TestTrue(FString::Printf(TEXT("%s has a display name"), *Name), *Row->DisplayName != TEXT('\0'));
				TestTrue(FString::Printf(TEXT("%s implementation extension starts with a dot"), *Name),
					FString(Row->ImplementationExtension).StartsWith(TEXT(".")));
				TestEqual(FString::Printf(TEXT("%s has a declaration extension exactly when it has a declaration file"), *Name),
					*Row->DeclarationExtension != TEXT('\0'), Row->bHasDeclarationFile);
				TestTrue(FString::Printf(TEXT("%s has a prompt key"), *Name), *Row->PromptKey != TEXT('\0'));
				TestTrue(FString::Printf(TEXT("%s has an MCP description and category"), *Name),
					*Row->Description != TEXT('\0') && *Row->Category != TEXT('\0') && *Row->Features != TEXT('\0'));
			}
			TestEqual(TEXT("Pseudocode is .md"), FString(N2C::GetLanguage(EN2CCodeLanguage::Pseudocode).ImplementationExtension), FString(TEXT(".md")));
			TestEqual(TEXT("only C++ has a declaration file"), FString(N2C::GetLanguage(EN2CCodeLanguage::Cpp).DeclarationExtension), FString(TEXT(".h")));
		});

		It(TEXT("has a code-editor syntax definition for every language"), [this]()
		{
			for (const EN2CCodeLanguage Language : EnumValues())
			{
				const TSharedPtr<FN2CSyntaxDefinition> Definition = FN2CSyntaxDefinitionFactory::Get().CreateDefinition(Language);
				if (TestTrue(FString::Printf(TEXT("%s has a syntax definition"), *ShortName(Language)), Definition.IsValid()))
				{
					TestTrue(FString::Printf(TEXT("%s syntax definition is its own"), *ShortName(Language)), Definition->GetLanguage() == Language);
				}
			}
		});
	});

	Describe(TEXT("FindLanguage"), [this]()
	{
		It(TEXT("reads the id in any case, the enum-qualified name, the display name and the prompt key"), [this]()
		{
			for (const FN2CTargetLanguage& Row : N2C::GetLanguages())
			{
				const FString Id = Row.Id;
				for (const FString& Name : {Id, Id.ToLower(), Id.ToUpper(), FString::Printf(TEXT("  %s "), *Id),
					FString::Printf(TEXT("EN2CCodeLanguage::%s"), *Id), UEnum::GetValueAsString(Row.Enum),
					FString(Row.DisplayName), FString(Row.PromptKey)})
				{
					const FN2CTargetLanguage* Found = N2C::FindLanguage(Name);
					TestTrue(FString::Printf(TEXT("'%s' is %s"), *Name, *Id), Found == &Row);
				}
			}
			TestTrue(TEXT("C++ is Cpp"), N2C::FindLanguage(TEXT("C++")) == N2C::FindLanguageRow(EN2CCodeLanguage::Cpp));
			TestTrue(TEXT("c# is CSharp"), N2C::FindLanguage(TEXT("c#")) == N2C::FindLanguageRow(EN2CCodeLanguage::CSharp));
			TestNull(TEXT("an empty name is unknown"), N2C::FindLanguage(TEXT("")));
			TestNull(TEXT("EN2CCodeLanguage:: alone is unknown"), N2C::FindLanguage(TEXT("EN2CCodeLanguage::")));
			TestNull(TEXT("COBOL is unknown"), N2C::FindLanguage(TEXT("COBOL")));
		});
	});

	Describe(TEXT("Prompts"), [this]()
	{
		It(TEXT("load one prompt per language, under <family>_<prompt key>, from Content/Prompting"), [this]()
		{
			const TStrongObjectPtr<UN2CSystemPromptManager> Manager(NewObject<UN2CSystemPromptManager>(GetTransientPackage()));
			Manager->Initialize(FN2CLLMConfig());
			const FString PromptDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("NodeToCode"))->GetContentDir(), TEXT("Prompting"));
			for (const FN2CTargetLanguage& Row : N2C::GetLanguages())
			{
				const FString Key = N2C::GetPromptKey(Row.Enum);
				TestEqual(FString::Printf(TEXT("%s key"), Row.Id), Key, FString::Printf(TEXT("CodeGen_%s"), Row.PromptKey));
				TestEqual(FString::Printf(TEXT("%s: the manager uses the same key"), Row.Id),
					UN2CSystemPromptManager::GetLanguagePromptKey(N2C::BlueprintPromptFamily, Row.Enum), Key);
				TestTrue(FString::Printf(TEXT("%s: %s.md exists"), Row.Id, *Key), FPaths::FileExists(PromptDir / (Key + TEXT(".md"))));
				TestTrue(FString::Printf(TEXT("%s: %s is loaded"), Row.Id, *Key), Manager->HasPrompt(Key));
				TestFalse(FString::Printf(TEXT("%s: the language prompt is not empty"), Row.Id),
					Manager->GetLanguageSpecificPrompt(N2C::BlueprintPromptFamily, Row.Enum).IsEmpty());
			}
		});
	});

	Describe(TEXT("Consumers"), [this]()
	{
		It(TEXT("the extension helpers report the table's extensions"), [this]()
		{
			for (const FN2CTargetLanguage& Row : N2C::GetLanguages())
			{
				TestEqual(FString::Printf(TEXT("%s: FN2CPathUtils extension"), Row.Id), FN2CPathUtils::GetFileExtensionForLanguage(Row.Enum),
					FString(Row.ImplementationExtension));
				TestEqual(FString::Printf(TEXT("%s: UN2CLLMModule extension"), Row.Id), UN2CLLMModule::Get()->GetFileExtensionForLanguage(Row.Enum),
					FString(Row.ImplementationExtension));
				TestEqual(FString::Printf(TEXT("%s: load extensions"), Row.Id), FN2CPathUtils::GetTranslationFileExtensions(Row.Enum),
					Row.GetImplementationExtensions());
				TestEqual(FString::Printf(TEXT("%s: record id"), Row.Id), FN2CPathUtils::GetLanguageId(Row.Enum), FString(Row.Id));
			}
		});

		It(TEXT("get-available-translation-targets lists every row with its id and extensions"), [this]()
		{
			const TSharedPtr<FJsonObject> Json = N2CTest::ResultJson(N2CTest::RunToolUnmarked(TEXT("get-available-translation-targets"),
				MakeShared<FJsonObject>()));
			if (!TestTrue(TEXT("the tool answers"), Json.IsValid()))
			{
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>& Languages = Json->GetArrayField(TEXT("languages"));
			const TConstArrayView<FN2CTargetLanguage> Rows = N2C::GetLanguages();
			TestEqual(TEXT("one entry per row"), Languages.Num(), Rows.Num());
			TestEqual(TEXT("languageCount"), static_cast<int32>(Json->GetNumberField(TEXT("languageCount"))), Rows.Num());
			TestEqual(TEXT("defaultLanguage"), Json->GetStringField(TEXT("defaultLanguage")), FString(TEXT("cpp")));
			for (int32 Index = 0; Index < FMath::Min(Languages.Num(), Rows.Num()); ++Index)
			{
				const FN2CTargetLanguage& Row = Rows[Index];
				const TSharedPtr<FJsonObject> Entry = Languages[Index]->AsObject();
				TestEqual(FString::Printf(TEXT("%s id"), Row.Id), Entry->GetStringField(TEXT("id")), Row.GetMcpId());
				TestEqual(FString::Printf(TEXT("%s displayName"), Row.Id), Entry->GetStringField(TEXT("displayName")), FString(Row.DisplayName));
				TArray<FString> Extensions;
				Entry->TryGetStringArrayField(TEXT("fileExtensions"), Extensions);
				TestEqual(FString::Printf(TEXT("%s fileExtensions"), Row.Id), Extensions, Row.GetFileExtensions());
				TestTrue(FString::Printf(TEXT("%s: the implementation extension is listed"), Row.Id), Extensions.Contains(Row.ImplementationExtension));
				TestEqual(FString::Printf(TEXT("%s category"), Row.Id), Entry->GetStringField(TEXT("category")), FString(Row.Category));
			}
			TestTrue(TEXT("currentLanguage is a listed id"), N2C::FindLanguage(Json->GetStringField(TEXT("currentLanguage"))) != nullptr);
		});

		It(TEXT("a batch summary records the language id, not EN2CCodeLanguage::X"), [this]()
		{
			N2CTest::FScratchDir Output(TEXT("N2CLanguagesBatch"));
			for (const FN2CTargetLanguage& Row : N2C::GetLanguages())
			{
				TStrongObjectPtr<UN2CBatchTranslationOrchestrator> Orchestrator(NewObject<UN2CBatchTranslationOrchestrator>());
				const FString Folder = Output / Row.Id;
				IFileManager::Get().MakeDirectory(*Folder, true);
				Orchestrator.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CLanguagesBatchOutputPath()) = Folder;
				FN2CBatchTranslationOptions& Options = Orchestrator.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CLanguagesBatchOptions());
				Options.bUseRequestConfig = true;
				Options.Language = Row.Enum;
				(Orchestrator.Get()->*N2CTest::GetPrivateMember(N2CTest::FN2CLanguagesGenerateBatchSummary()))();

				FString Text;
				const TSharedPtr<FJsonObject> Summary = FFileHelper::LoadFileToString(Text, *(Folder / TEXT("BatchSummary.json")))
					? ParseObject(Text) : nullptr;
				const TSharedPtr<FJsonObject>* Settings = nullptr;
				if (TestTrue(FString::Printf(TEXT("%s: the summary has settings"), Row.Id),
					Summary.IsValid() && Summary->TryGetObjectField(TEXT("settings"), Settings)))
				{
					TestEqual(FString::Printf(TEXT("%s: summary language"), Row.Id), (*Settings)->GetStringField(TEXT("language")), FString(Row.Id));
				}
			}
		});

		It(TEXT("a translation without a declaration file does not load a .h left in its folder"), [this]()
		{
			N2CTest::FScratchDir StateDir(TEXT("N2CLanguagesState"));
			N2CTest::FScratchDir Output(TEXT("N2CLanguagesLoad"));
			TStrongObjectPtr<UN2CGraphStateManager> Manager(UN2CGraphStateManager::CreateIsolated(StateDir.Path));
			for (const FN2CTargetLanguage& Row : N2C::GetLanguages())
			{
				const FString GraphName = FString::Printf(TEXT("Stale%s"), Row.Id);
				const FString GraphDir = Output / GraphName;
				FFileHelper::SaveStringToFile(FString::Printf(TEXT("code %s"), Row.Id), *(GraphDir / (GraphName + Row.ImplementationExtension)));
				FFileHelper::SaveStringToFile(FString(TEXT("void Stale();")), *(GraphDir / (GraphName + TEXT(".h"))));
				const FGuid Guid = FGuid::NewGuid();
				Manager->SetTranslationState(Guid, GraphName, FSoftObjectPath(TEXT("/Game/N2CTest/BP_N2CLanguages.BP_N2CLanguages")),
					GraphDir, TEXT("Anthropic"), TEXT("claude-haiku-5-5"), Row.Id, FN2CTranslationSummary());

				FN2CGraphTranslation Loaded;
				EN2CCodeLanguage LoadedLanguage = EN2CCodeLanguage::Cpp;
				if (TestTrue(FString::Printf(TEXT("%s loads"), Row.Id), Manager->LoadTranslation(Guid, Loaded, LoadedLanguage)))
				{
					TestTrue(FString::Printf(TEXT("%s: loaded as its language"), Row.Id), LoadedLanguage == Row.Enum);
					TestEqual(FString::Printf(TEXT("%s: implementation"), Row.Id), Loaded.Code.GraphImplementation, FString::Printf(TEXT("code %s"), Row.Id));
					TestEqual(FString::Printf(TEXT("%s: declaration only for a language with one"), Row.Id),
						Loaded.Code.GraphDeclaration.IsEmpty(), !Row.bHasDeclarationFile);
				}
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
