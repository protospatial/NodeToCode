// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Tests/Core/N2CTestCredentials.h"
#include "Tests/N2CTestHelpers.h"
#include "Utils/N2CFileUtils.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace N2CCredentialManagerSpec
{
	/** An in-memory backend whose writes can fail or come back changed, for the migration's failure paths */
	class FFakeBackend : public IN2CSecretBackend
	{
	public:
		TMap<FString, FString> Keys;
		/** StoreKey fails for this id */
		FString FailId;
		/** LoadAll returns this id's key changed (a write that did not stick) */
		FString GarbleId;

		virtual FString Describe() const override { return TEXT("fake backend"); }
		virtual FString GetDisplayName() const override { return TEXT("fake backend"); }
		virtual bool LoadAll(TMap<FString, FString>& OutKeys) override
		{
			OutKeys = Keys;
			if (FString* Garbled = OutKeys.Find(GarbleId))
			{
				*Garbled += TEXT("-changed");
			}
			return true;
		}
		virtual bool StoreKey(const FString& ProviderId, const FString& Key, TMap<FString, FString>& OutKeys) override
		{
			if (ProviderId == FailId)
			{
				return false;
			}
			if (Key.IsEmpty())
			{
				Keys.Remove(ProviderId);
			}
			else
			{
				Keys.Add(ProviderId, Key);
			}
			return LoadAll(OutKeys);
		}
	};
}

/**
 * Windows Credential Manager holds the API keys on Windows (card 0161). Every case uses its own target prefix
 * "NodeToCodeTest/<guid>/", never the user's "NodeToCode/" entries, and removes what it wrote. The migration cases
 * work on files in a scratch folder, never the real per-user file.
 */
BEGIN_DEFINE_SPEC(FN2CCredentialManagerSpec, "NodeToCode.Secrets.CredentialManager", N2C_TEST_FLAGS)

	FString Prefix;
	TUniquePtr<N2CTest::FScratchDir> Scratch;
	FString UserFile;
	FString LegacyFile;

	TUniquePtr<FN2CCredentialManagerBackend> MakeVault() const
	{
		return MakeUnique<FN2CCredentialManagerBackend>(Prefix);
	}

	TUniquePtr<FN2CSecretStore> MakeStore() const
	{
		// No environment fallback, so a key in the test machine's environment cannot stand in for a stored one
		return MakeUnique<FN2CSecretStore>(MakeVault(), [](const FString&) { return FString(); });
	}


	void WriteFile(const FString& Path, const FString& Text)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		TestTrue(FString::Printf(TEXT("wrote %s"), *FPaths::GetCleanFilename(Path)), FFileHelper::SaveStringToFile(Text, *Path));
	}

END_DEFINE_SPEC(FN2CCredentialManagerSpec)

void FN2CCredentialManagerSpec::Define()
{
	BeforeEach([this]()
	{
		N2CTest::SweepStaleTestCredentialsOnce();
		Prefix = N2CTest::MakeTestCredentialPrefix();
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CCredentialManager"));
		UserFile = *Scratch / TEXT("User/NodeToCode/secrets.json");
		LegacyFile = *Scratch / TEXT("Project/Saved/NodeToCode/User/secrets.json");
	});

	AfterEach([this]()
	{
		if (FN2CCredentialManagerBackend::IsSupported())
		{
			TestEqual(TEXT("no test credential is left"), N2CTest::RemoveTestCredentials(Prefix), 0);
		}
		Scratch.Reset();
	});

	It(TEXT("is the store on Windows, under NodeToCode/<provider>"), [this]()
	{
		TestEqual(TEXT("supported exactly on Windows"), FN2CCredentialManagerBackend::IsSupported(), PLATFORM_WINDOWS != 0);
		TestEqual(TEXT("default target name"), FN2CCredentialManagerBackend().GetTargetName(TEXT("OpenAI")), FString(TEXT("NodeToCode/OpenAI")));
		TestEqual(TEXT("test target name"), MakeVault()->GetTargetName(TEXT("OpenAI")), Prefix + TEXT("OpenAI"));
#if PLATFORM_WINDOWS
		TestEqual(TEXT("Project Settings names it"), FN2CSecretStore::Get().GetStoreDisplayName(), FString(TEXT("Windows Credential Manager")));
#endif
	});

	It(TEXT("sets, gets, replaces and clears a key, which a restarted store reads back"), [this]()
	{
		if (!FN2CCredentialManagerBackend::IsSupported())
		{
			AddInfo(TEXT("Windows Credential Manager exists only on Windows"));
			return;
		}
		const FString First = TEXT("n2c-test-openai-key-first-0001");
		const FString Second = TEXT("n2c-test-openai-key-second-0002");
		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		TestEqual(TEXT("starts empty"), Store->GetApiKey(EN2CLLMProvider::OpenAI), FString());

		TestTrue(TEXT("set saves"), Store->SetApiKey(EN2CLLMProvider::OpenAI, First));
		TestTrue(TEXT("a second provider saves"), Store->SetApiKey(EN2CLLMProvider::Anthropic, TEXT("n2c-test-anthropic-key-0003")));
		TestEqual(TEXT("get gives it"), Store->GetApiKey(EN2CLLMProvider::OpenAI), First);
		TestEqual(TEXT("its origin is the store"), Store->GetApiKeySource(EN2CLLMProvider::OpenAI), EN2CApiKeySource::Store);
		TestEqual(TEXT("a restarted store reads it"), MakeStore()->GetApiKey(EN2CLLMProvider::OpenAI), First);

		TestTrue(TEXT("replace saves"), Store->SetApiKey(EN2CLLMProvider::OpenAI, Second));
		TestEqual(TEXT("a restarted store reads the new key"), MakeStore()->GetApiKey(EN2CLLMProvider::OpenAI), Second);

		TestTrue(TEXT("clear saves"), Store->SetApiKey(EN2CLLMProvider::OpenAI, FString()));
		TUniquePtr<FN2CSecretStore> Restarted = MakeStore();
		TestEqual(TEXT("cleared after a restart"), Restarted->GetApiKey(EN2CLLMProvider::OpenAI), FString());
		TestEqual(TEXT("the other provider's key is untouched"), Restarted->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("n2c-test-anthropic-key-0003")));
		TMap<FString, FString> Entries;
		MakeVault()->LoadAll(Entries);
		TestFalse(TEXT("the cleared entry is deleted, not left empty"), Entries.Contains(TEXT("OpenAI")));
	});

#if PLATFORM_WINDOWS
	It(TEXT("stores the key as one generic credential for this machine, and reads one cmdkey wrote"), [this]()
	{
		const FString Key = TEXT("n2c-test-gemini-key-0004");
		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		Store->SetApiKey(EN2CLLMProvider::Gemini, Key);

		PCREDENTIALW Credential = nullptr;
		const FString Target = Prefix + TEXT("Gemini");
		if (TestTrue(TEXT("CredReadW finds the target"), ::CredReadW(*Target, CRED_TYPE_GENERIC, 0, &Credential) != 0))
		{
			TestEqual(TEXT("persisted for this user on this machine (does not roam)"), static_cast<uint32>(Credential->Persist), static_cast<uint32>(CRED_PERSIST_LOCAL_MACHINE));
			TestEqual(TEXT("the blob is the key in UTF-8"), static_cast<int32>(Credential->CredentialBlobSize), Key.Len());
			TestEqual(TEXT("user name"), FString(Credential->UserName), FString(TEXT("NodeToCode")));
			::CredFree(Credential);
		}

		// cmdkey /generic:<target> /user:NodeToCode /pass:<key> stores the password as UTF-16LE
		const FString CmdkeyKey = TEXT("n2c-test-deepseek-key-0005");
		FString CmdkeyTarget = Prefix + TEXT("DeepSeek");
		CREDENTIALW Written = {};
		Written.Type = CRED_TYPE_GENERIC;
		Written.TargetName = CmdkeyTarget.GetCharArray().GetData();
		Written.CredentialBlobSize = CmdkeyKey.Len() * sizeof(WIDECHAR);
		Written.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<TCHAR*>(*CmdkeyKey));
		Written.Persist = CRED_PERSIST_LOCAL_MACHINE;
		TestTrue(TEXT("a UTF-16 entry written"), ::CredWriteW(&Written, 0) != 0);
		TestEqual(TEXT("a UTF-16 (cmdkey) entry reads as the key"), MakeStore()->GetApiKey(EN2CLLMProvider::DeepSeek), CmdkeyKey);
	});
#endif

	It(TEXT("moves a per-user and a project secrets.json into Credential Manager, reading back before it deletes them"), [this]()
	{
		if (!FN2CCredentialManagerBackend::IsSupported())
		{
			AddInfo(TEXT("Windows Credential Manager exists only on Windows"));
			return;
		}
		WriteFile(UserFile, TEXT("{\"Version\":1,\"OpenAI_API_Key\":\"n2c-test-user-openai\",\"Anthropic_API_Key\":\"n2c-test-user-anthropic\",\"Gemini_API_Key\":\"\"}"));
		WriteFile(FN2CFileUtils::BackupPathFor(UserFile), TEXT("{\"OpenAI_API_Key\":\"n2c-test-old-openai\"}"));
		WriteFile(LegacyFile, TEXT("{\"DeepSeek_API_Key\":\"n2c-test-project-deepseek\",\"Anthropic_API_Key\":\"n2c-test-project-anthropic\"}"));
		AddExpectedMessage(TEXT("already had a different key"), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(TEXT("keys that differed from the ones already stored were kept"), EAutomationExpectedMessageFlags::Contains, 1);

		TUniquePtr<FN2CCredentialManagerBackend> Vault = MakeVault();
		const FN2CSecretMigrationResult Result = FN2CSecretStore::MigrateFileToBackend(UserFile, LegacyFile, *Vault);
		TestTrue(TEXT("found a file"), Result.bFoundFile);
		TestTrue(TEXT("completed"), Result.bCompleted);
		TestFalse(TEXT("did not fail"), Result.bFailed);
		TestEqual(TEXT("moved the three non-empty keys"), Result.Moved, TArray<FString>{ TEXT("Anthropic"), TEXT("DeepSeek"), TEXT("OpenAI") });
		TestEqual(TEXT("the project's differing Anthropic key is kept beside it"), Result.ConflictIds, TArray<FString>{ TEXT("Anthropic.Conflict.1") });

		TMap<FString, FString> Entries;
		Vault->LoadAll(Entries);
		TestEqual(TEXT("OpenAI"), Entries.FindRef(TEXT("OpenAI")), FString(TEXT("n2c-test-user-openai")));
		TestEqual(TEXT("Anthropic: the per-user key wins"), Entries.FindRef(TEXT("Anthropic")), FString(TEXT("n2c-test-user-anthropic")));
		TestEqual(TEXT("the project's Anthropic key is not lost"), Entries.FindRef(TEXT("Anthropic.Conflict.1")), FString(TEXT("n2c-test-project-anthropic")));
		TestEqual(TEXT("DeepSeek from the project file"), Entries.FindRef(TEXT("DeepSeek")), FString(TEXT("n2c-test-project-deepseek")));
		TestFalse(TEXT("an empty key makes no entry"), Entries.Contains(TEXT("Gemini")));

		TestFalse(TEXT("the per-user file is deleted"), FPaths::FileExists(UserFile));
		TestFalse(TEXT("its backup is deleted"), FPaths::FileExists(FN2CFileUtils::BackupPathFor(UserFile)));
		TestFalse(TEXT("the project file is deleted"), FPaths::FileExists(LegacyFile));

		// A store over the target now has the keys; a second start finds no file and changes nothing
		TestEqual(TEXT("the store reads the moved key"), FN2CSecretStore(MakeVault(), [](const FString&) { return FString(); }).GetApiKey(EN2CLLMProvider::DeepSeek),
			FString(TEXT("n2c-test-project-deepseek")));
		const FN2CSecretMigrationResult Again = FN2CSecretStore::MigrateFileToBackend(UserFile, LegacyFile, *Vault);
		TestFalse(TEXT("a second start finds no file"), Again.bFoundFile);
		TMap<FString, FString> After;
		Vault->LoadAll(After);
		TestTrue(TEXT("and changes nothing"), After.OrderIndependentCompareEqual(Entries));
		TestFalse(TEXT("and writes no file"), FPaths::FileExists(UserFile));
	});

	It(TEXT("keeps the file when a key cannot be written or does not read back"), [this]()
	{
		const FString Text = TEXT("{\"Version\":1,\"OpenAI_API_Key\":\"n2c-test-user-openai\",\"DeepSeek_API_Key\":\"n2c-test-user-deepseek\"}");
		WriteFile(UserFile, Text);

		N2CCredentialManagerSpec::FFakeBackend Failing;
		Failing.FailId = TEXT("OpenAI");
		AddExpectedError(TEXT("the OpenAI key could not be written"), EAutomationExpectedErrorFlags::Contains, 1);
		const FN2CSecretMigrationResult Failed = FN2CSecretStore::MigrateFileToBackend(UserFile, FString(), Failing);
		TestTrue(TEXT("a failed write is reported"), Failed.bFailed);
		TestFalse(TEXT("and does not complete"), Failed.bCompleted);
		TestTrue(TEXT("the file stays"), FPaths::FileExists(UserFile));

		N2CCredentialManagerSpec::FFakeBackend Garbling;
		Garbling.GarbleId = TEXT("OpenAI");
		AddExpectedError(TEXT("the OpenAI key did not read back unchanged"), EAutomationExpectedErrorFlags::Contains, 1);
		const FN2CSecretMigrationResult Garbled = FN2CSecretStore::MigrateFileToBackend(UserFile, FString(), Garbling);
		TestTrue(TEXT("a read-back mismatch is reported"), Garbled.bFailed);
		TestTrue(TEXT("the file still stays"), FPaths::FileExists(UserFile));

		FString Now;
		FFileHelper::LoadFileToString(Now, *UserFile);
		TestTrue(TEXT("with both keys in it"), Now.Contains(TEXT("n2c-test-user-openai")) && Now.Contains(TEXT("n2c-test-user-deepseek")));

		// The next start, with a working target, moves them
		N2CCredentialManagerSpec::FFakeBackend Working;
		const FN2CSecretMigrationResult Retried = FN2CSecretStore::MigrateFileToBackend(UserFile, FString(), Working);
		TestTrue(TEXT("a retry completes"), Retried.bCompleted);
		TestEqual(TEXT("with both keys"), Working.Keys.Num(), 2);
		TestFalse(TEXT("and deletes the file"), FPaths::FileExists(UserFile));
	});

	It(TEXT("leaves a secrets file it cannot parse in place"), [this]()
	{
		WriteFile(UserFile, TEXT("{\"OpenAI_API_Key\":"));
		AddExpectedError(TEXT("does not parse"), EAutomationExpectedErrorFlags::Contains, 0);
		AddExpectedError(TEXT("were not moved"), EAutomationExpectedErrorFlags::Contains, 1);
		N2CCredentialManagerSpec::FFakeBackend Target;
		const FN2CSecretMigrationResult Result = FN2CSecretStore::MigrateFileToBackend(UserFile, FString(), Target);
		TestTrue(TEXT("reported as failed"), Result.bFailed);
		TestEqual(TEXT("nothing written"), Target.Keys.Num(), 0);
		TArray<FString> Corrupt;
		IFileManager::Get().FindFiles(Corrupt, *(UserFile + TEXT(".corrupt-*")), true, false);
		TestTrue(TEXT("the damaged file is kept (set aside, never deleted)"), FPaths::FileExists(UserFile) || Corrupt.Num() == 1);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
