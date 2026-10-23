// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * The secret store has one owner per process, writes through a temporary file with a backup, never writes over a
 * file it cannot parse, and moves a project's secrets.json to the per-user file without losing a key.
 * Every case works in its own temporary folder, never in the real per-user file.
 */
BEGIN_DEFINE_SPEC(FN2CSecretStoreSpec, "NodeToCode.Secrets.Store",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	FString BaseDir;
	FString UserFile;
	FString LegacyFile;

	TUniquePtr<FN2CSecretStore> MakeStore(bool bWithLegacy = false) const
	{
		// No environment fallback, so a key in the test machine's environment cannot stand in for a stored one
		return MakeUnique<FN2CSecretStore>(MakeUnique<FN2CSecretFileBackend>(UserFile, bWithLegacy ? LegacyFile : FString()),
			[](const FString&) { return FString(); });
	}

	static TSharedPtr<FJsonObject> ParseFile(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Text, *Path))
		{
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			FJsonSerializer::Deserialize(Reader, Json);
		}
		return Json;
	}

	static FString FieldOf(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		if (Json.IsValid())
		{
			Json->TryGetStringField(Field, Value);
		}
		return Value;
	}

	TArray<FString> CorruptCopies() const
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(FPaths::GetPath(UserFile) / TEXT("secrets.json.corrupt-*")), true, false);
		return Found;
	}

	/** Collects every log line written while it is registered */
	struct FLogCapture : public FOutputDevice
	{
		TArray<FString> Lines;
		FLogCapture() { GLog->AddOutputDevice(this); }
		virtual ~FLogCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override { Lines.Add(V); }
		bool AnyContains(const FString& Text) const
		{
			GLog->Flush();
			return Lines.ContainsByPredicate([&Text](const FString& Line) { return Line.Contains(Text); });
		}
	};

END_DEFINE_SPEC(FN2CSecretStoreSpec)

void FN2CSecretStoreSpec::Define()
{
	BeforeEach([this]()
	{
		BaseDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("N2CSecretStore_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		UserFile = BaseDir / TEXT("User") / TEXT("NodeToCode") / TEXT("secrets.json");
		LegacyFile = BaseDir / TEXT("Project") / TEXT("Saved") / TEXT("NodeToCode") / TEXT("User") / TEXT("secrets.json");
	});

	AfterEach([this]()
	{
		IFileManager::Get().DeleteDirectory(*BaseDir, false, true);
	});

	It(TEXT("keeps the per-user file outside the project and refuses it to agents"), [this]()
	{
		const FString Path = FN2CSecretStore::GetUserSecretsFilePath();
		TestFalse(TEXT("the per-user file is not under the project"), FPaths::IsUnderDirectory(Path, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())));
		TestTrue(TEXT("it is under the user settings folder"), FPaths::IsUnderDirectory(Path, FPaths::ConvertRelativePathToFull(FPlatformProcess::UserSettingsDir())));
		TestTrue(TEXT("the file tools refuse it"), FN2CPathUtils::IsDeniedForAgentAccess(Path));
		TestTrue(TEXT("and its backup"), FN2CPathUtils::IsDeniedForAgentAccess(Path + TEXT(".bak")));
		TestTrue(TEXT("and a leftover project file"), FN2CPathUtils::IsDeniedForAgentAccess(FN2CSecretStore::GetLegacyProjectSecretsFilePath()));
		TestTrue(TEXT("and a set-aside corrupt file anywhere"), FN2CPathUtils::IsDeniedForAgentAccess(FPaths::ProjectDir() / TEXT("Foo/secrets.json.corrupt-20261023-120000")));
		TestEqual(TEXT("one store per process"), &FN2CSecretStore::Get(), &FN2CSecretStore::Get());
	});

	It(TEXT("keeps both of two edits to different keys"), [this]()
	{
		// Two owners loaded before either write, as two editors on one machine are
		TUniquePtr<FN2CSecretStore> First = MakeStore();
		TUniquePtr<FN2CSecretStore> Second = MakeStore();
		TestTrue(TEXT("first edit saved"), First->SetApiKey(EN2CLLMProvider::OpenAI, TEXT("k-openai")));
		TestTrue(TEXT("second edit saved"), Second->SetApiKey(EN2CLLMProvider::Gemini, TEXT("k-gemini")));

		const TSharedPtr<FJsonObject> Json = ParseFile(UserFile);
		TestEqual(TEXT("first key on disk"), FieldOf(Json, TEXT("OpenAI_API_Key")), FString(TEXT("k-openai")));
		TestEqual(TEXT("second key on disk"), FieldOf(Json, TEXT("Gemini_API_Key")), FString(TEXT("k-gemini")));
		int32 Version = 0;
		TestTrue(TEXT("the file has a Version"), Json.IsValid() && Json->TryGetNumberField(TEXT("Version"), Version));
		TestEqual(TEXT("Version"), Version, FN2CSecretFileBackend::FileVersion);
		TestEqual(TEXT("the second owner sees the first key after its write"), Second->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("k-openai")));

		TUniquePtr<FN2CSecretStore> Restarted = MakeStore();
		TestEqual(TEXT("first key after a restart"), Restarted->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("k-openai")));
		TestEqual(TEXT("second key after a restart"), Restarted->GetApiKey(EN2CLLMProvider::Gemini), FString(TEXT("k-gemini")));
		TestFalse(TEXT("no temporary file is left"), FPaths::FileExists(UserFile + TEXT(".tmp")));
	});

	It(TEXT("keeps unknown fields on a save and the previous file as secrets.json.bak"), [this]()
	{
		const FString Saved = TEXT("{\"Ollama_API_Key\":\"k-ollama\",\"Future\":{\"Nested\":true},\"OpenAI_API_Key\":\"k-old\"}");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserFile), true);
		if (!TestTrue(TEXT("test secrets written"), FFileHelper::SaveStringToFile(Saved, *UserFile))) { return; }

		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		TestEqual(TEXT("an unknown provider's key is readable by id"), Store->GetApiKey(TEXT("Ollama")), FString(TEXT("k-ollama")));
		TestTrue(TEXT("save"), Store->SetApiKey(EN2CLLMProvider::OpenAI, TEXT("k-new")));

		const TSharedPtr<FJsonObject> Json = ParseFile(UserFile);
		TestEqual(TEXT("edited key saved"), FieldOf(Json, TEXT("OpenAI_API_Key")), FString(TEXT("k-new")));
		TestEqual(TEXT("unknown key survives"), FieldOf(Json, TEXT("Ollama_API_Key")), FString(TEXT("k-ollama")));
		TestTrue(TEXT("unknown object survives"), Json.IsValid() && Json->HasField(TEXT("Future")));
		TestEqual(TEXT("the backup is the previous file"), FieldOf(ParseFile(UserFile + TEXT(".bak")), TEXT("OpenAI_API_Key")), FString(TEXT("k-old")));
	});

	It(TEXT("sets a truncated file aside and never writes over it"), [this]()
	{
		const FString Truncated = TEXT("{\"OpenAI_API_Key\":\"k-openai\",\"Anthropic_API_K");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserFile), true);
		if (!TestTrue(TEXT("test secrets written"), FFileHelper::SaveStringToFile(Truncated, *UserFile))) { return; }

		AddExpectedError(TEXT("does not parse"), EAutomationExpectedErrorFlags::Contains, 1);
		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		TestTrue(TEXT("keys start empty"), Store->GetApiKey(EN2CLLMProvider::OpenAI).IsEmpty());

		TArray<FString> Corrupt = CorruptCopies();
		if (!TestEqual(TEXT("one corrupt copy kept"), Corrupt.Num(), 1)) { return; }
		const FString CorruptPath = FPaths::GetPath(UserFile) / Corrupt[0];

		TestTrue(TEXT("a new key still saves"), Store->SetApiKey(EN2CLLMProvider::Gemini, TEXT("k-gemini")));
		FString Kept;
		FFileHelper::LoadFileToString(Kept, *CorruptPath);
		TestEqual(TEXT("the truncated file is unchanged"), Kept, Truncated);
		TestEqual(TEXT("still one corrupt copy"), CorruptCopies().Num(), 1);
		TestEqual(TEXT("the new file holds the new key"), FieldOf(ParseFile(UserFile), TEXT("Gemini_API_Key")), FString(TEXT("k-gemini")));
	});

	It(TEXT("finishes a save that stopped before the temporary file replaced the file"), [this]()
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserFile), true);
		FFileHelper::SaveStringToFile(TEXT("{\"Version\":1,\"DeepSeek_API_Key\":\"k-deepseek\"}"), *(UserFile + TEXT(".tmp")));
		AddExpectedMessage(TEXT("Finished an interrupted save"), EAutomationExpectedMessageFlags::Contains, 1);
		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		TestEqual(TEXT("key recovered"), Store->GetApiKey(EN2CLLMProvider::DeepSeek), FString(TEXT("k-deepseek")));
		TestTrue(TEXT("file in place"), FPaths::FileExists(UserFile));
	});

	It(TEXT("moves a project secrets.json into the per-user file and deletes it"), [this]()
	{
		const FString Project = TEXT("{\"OpenAI_API_Key\":\"k-project-openai\",\"Anthropic_API_Key\":\"k-project-anthropic\",\"DeepSeek_API_Key\":\"\",")
			TEXT("\"Custom_Field\":\"kept\",\"OAuth\":{\"Anthropic\":{\"AccessToken\":\"a\",\"RefreshToken\":\"r\"}}}");
		const FString User = TEXT("{\"Version\":1,\"OpenAI_API_Key\":\"\",\"Anthropic_API_Key\":\"k-user-anthropic\",\"DeepSeek_API_Key\":\"k-user-deepseek\"}");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(LegacyFile), true);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(UserFile), true);
		if (!TestTrue(TEXT("project file written"), FFileHelper::SaveStringToFile(Project, *LegacyFile))) { return; }
		if (!TestTrue(TEXT("user file written"), FFileHelper::SaveStringToFile(User, *UserFile))) { return; }

		AddExpectedMessage(TEXT("sign-in were removed in NodeToCode 2.0"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("already had a different key"), EAutomationExpectedMessageFlags::Contains, 1);
		FLogCapture Log;
		TUniquePtr<FN2CSecretStore> Store = MakeStore(/*bWithLegacy*/ true);

		TestEqual(TEXT("an empty per-user key is filled from the project"), Store->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("k-project-openai")));
		TestEqual(TEXT("a per-user key wins a conflict"), Store->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("k-user-anthropic")));
		TestEqual(TEXT("an empty project key changes nothing"), Store->GetApiKey(EN2CLLMProvider::DeepSeek), FString(TEXT("k-user-deepseek")));
		TestFalse(TEXT("the project file is deleted"), FPaths::FileExists(LegacyFile));
		TestFalse(TEXT("its empty folder is removed"), IFileManager::Get().DirectoryExists(*FPaths::GetPath(LegacyFile)));

		const TSharedPtr<FJsonObject> Json = ParseFile(UserFile);
		TestEqual(TEXT("an unknown project field moves too"), FieldOf(Json, TEXT("Custom_Field")), FString(TEXT("kept")));
		TestFalse(TEXT("retired sign-in tokens do not move"), Json.IsValid() && Json->HasField(TEXT("OAuth")));
		const TSharedPtr<FJsonObject>* Conflicts = nullptr;
		const TSharedPtr<FJsonObject>* FromProject = nullptr;
		if (TestTrue(TEXT("the losing project key is kept outside the project"), Json.IsValid()
			&& Json->TryGetObjectField(TEXT("MigrationConflicts"), Conflicts)
			&& (*Conflicts)->TryGetObjectField(FPaths::ConvertRelativePathToFull(LegacyFile), FromProject)))
		{
			TestEqual(TEXT("the project's Anthropic key"), FieldOf(*FromProject, TEXT("Anthropic_API_Key")), FString(TEXT("k-project-anthropic")));
		}

		TestTrue(TEXT("the conflict names the provider"), Log.AnyContains(TEXT("Anthropic already had a different key")));
		for (const TCHAR* Key : { TEXT("k-project-anthropic"), TEXT("k-user-anthropic"), TEXT("k-project-openai"), TEXT("k-user-deepseek") })
		{
			TestFalse(FString::Printf(TEXT("no log line holds %s"), Key), Log.AnyContains(Key));
		}

		// The next start finds no project file and changes nothing
		TUniquePtr<FN2CSecretStore> Restarted = MakeStore(/*bWithLegacy*/ true);
		TestEqual(TEXT("keys after a restart"), Restarted->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("k-project-openai")));
	});

	It(TEXT("leaves a project file it cannot parse in place"), [this]()
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(LegacyFile), true);
		FFileHelper::SaveStringToFile(TEXT("{\"OpenAI_API_Key\":"), *LegacyFile);
		AddExpectedError(TEXT("could not be read, so its keys were not moved"), EAutomationExpectedErrorFlags::Contains, 1);
		TUniquePtr<FN2CSecretStore> Store = MakeStore(/*bWithLegacy*/ true);
		TestTrue(TEXT("the unreadable project file is not deleted"), FPaths::FileExists(LegacyFile));
		TestFalse(TEXT("no per-user file is made from it"), FPaths::FileExists(UserFile));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
