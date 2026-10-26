// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Async/ParallelFor.h"
#include "Misc/Guid.h"
#include "Tests/Core/N2CTestCredentials.h"
#include "Tests/N2CTestHelpers.h"

namespace N2CSecretStoreKeychainSpec
{
	/**
	 * Credential Manager whose listings leave out one entry, as a CredEnumerateW taken while another process writes
	 * can (card 0496); reading that entry by name still finds it
	 */
	class FListingMissesBackend : public FN2CCredentialManagerBackend
	{
	public:
		using FN2CCredentialManagerBackend::FN2CCredentialManagerBackend;
		FString Hidden;

		virtual bool LoadAll(TMap<FString, FString>& OutKeys) override
		{
			const bool bRead = FN2CCredentialManagerBackend::LoadAll(OutKeys);
			OutKeys.Remove(Hidden);
			return bRead;
		}
		virtual bool StoreKey(const FString& ProviderId, const FString& Key, TMap<FString, FString>& OutKeys) override
		{
			const bool bStored = FN2CCredentialManagerBackend::StoreKey(ProviderId, Key, OutKeys);
			OutKeys.Remove(Hidden);
			return bStored;
		}
	};
}

/**
 * The secret store as the editor runs it on Windows: FN2CSecretStore over Windows Credential Manager (card 0161).
 * Round trip, two editors saving different keys, writers on several threads, entries the store does not know, and the
 * environment fallback. Every case works under its own target prefix "NodeToCodeTest/<guid>/" and removes it; each
 * case also checks that the user's own "NodeToCode/<provider>" entries are exactly as they were (compared in memory,
 * never logged). The file backend's versions of these cases are NodeToCode.Secrets.Store and .Environment.
 */
BEGIN_DEFINE_SPEC(FN2CSecretStoreKeychainSpec, "NodeToCode.Secrets.Keychain", N2C_TEST_FLAGS)

	FString Prefix;
	TMap<FString, FString> FakeEnvironment;
	TMap<FString, FString> OwnerEntriesBefore;
	bool bOwnerEntriesRead = false;

	TUniquePtr<FN2CCredentialManagerBackend> MakeVault() const
	{
		return MakeUnique<FN2CCredentialManagerBackend>(Prefix);
	}

	/** A store over this case's entries; its environment is FakeEnvironment, looked up at call time */
	TUniquePtr<FN2CSecretStore> MakeStore()
	{
		return MakeStore(MakeVault());
	}

	TUniquePtr<FN2CSecretStore> MakeStore(TUniquePtr<IN2CSecretBackend> Vault)
	{
		return MakeUnique<FN2CSecretStore>(MoveTemp(Vault), [this](const FString& Name)
		{
			const FString* Value = FakeEnvironment.Find(Name);
			return Value ? *Value : FString();
		});
	}

	TMap<FString, FString> Entries() const
	{
		TMap<FString, FString> Result;
		MakeVault()->LoadAll(Result);
		return Result;
	}

	/** False (with an info line) where Credential Manager does not exist; the case then has nothing to check */
	bool RequireVault()
	{
		if (!FN2CCredentialManagerBackend::IsSupported())
		{
			AddInfo(TEXT("Windows Credential Manager exists only on Windows"));
			return false;
		}
		return true;
	}


END_DEFINE_SPEC(FN2CSecretStoreKeychainSpec)

void FN2CSecretStoreKeychainSpec::Define()
{
	BeforeEach([this]()
	{
		N2CTest::SweepStaleTestCredentialsOnce();
		Prefix = N2CTest::MakeTestCredentialPrefix();
		FakeEnvironment.Reset();
		OwnerEntriesBefore.Reset();
		bOwnerEntriesRead = FN2CCredentialManagerBackend::IsSupported() && FN2CCredentialManagerBackend().LoadAll(OwnerEntriesBefore);
	});

	AfterEach([this]()
	{
		if (!FN2CCredentialManagerBackend::IsSupported())
		{
			return;
		}
		TestEqual(TEXT("no test entry is left"), N2CTest::RemoveTestCredentials(Prefix), 0);
		if (bOwnerEntriesRead)
		{
			TMap<FString, FString> OwnerEntriesAfter;
			FN2CCredentialManagerBackend().LoadAll(OwnerEntriesAfter);
			// Names only in the message: the values are the user's keys
			TArray<FString> Names;
			OwnerEntriesBefore.GetKeys(Names);
			TestTrue(FString::Printf(TEXT("the user's NodeToCode/ entries (%s) are unchanged"), *FString::Join(Names, TEXT(", "))),
				OwnerEntriesAfter.OrderIndependentCompareEqual(OwnerEntriesBefore));
		}
		OwnerEntriesBefore.Reset();
	});

	It(TEXT("round-trips every provider's key through a restart, and refuses a key Credential Manager cannot hold"), [this]()
	{
		if (!RequireVault()) { return; }
		const TPair<EN2CLLMProvider, FString> Keys[] = {
			{ EN2CLLMProvider::OpenAI, TEXT("n2c-test-openai-0001") },
			{ EN2CLLMProvider::Anthropic, TEXT("n2c-test-anthropic-0002") },
			{ EN2CLLMProvider::Gemini, TEXT("n2c-test-gemini-0003") },
			{ EN2CLLMProvider::DeepSeek, TEXT("n2c-test-deepseek-0004") },
		};
		{
			TUniquePtr<FN2CSecretStore> Store = MakeStore();
			for (const TPair<EN2CLLMProvider, FString>& Key : Keys)
			{
				TestTrue(FString::Printf(TEXT("%s saves"), *FN2CSecretStore::GetProviderId(Key.Key)), Store->SetApiKey(Key.Key, Key.Value));
			}
		}
		TUniquePtr<FN2CSecretStore> Restarted = MakeStore();
		for (const TPair<EN2CLLMProvider, FString>& Key : Keys)
		{
			const FString Id = FN2CSecretStore::GetProviderId(Key.Key);
			TestEqual(FString::Printf(TEXT("%s reads back"), *Id), Restarted->GetApiKey(Key.Key), Key.Value);
			TestEqual(FString::Printf(TEXT("%s comes from the store"), *Id), Restarted->GetApiKeySource(Key.Key), EN2CApiKeySource::Store);
		}
		TestEqual(TEXT("one entry per provider"), Entries().Num(), 4);
		TestEqual(TEXT("local providers have no key"), Restarted->GetApiKey(EN2CLLMProvider::Ollama), FString());

		// The largest blob Credential Manager takes is saved; one byte more is refused and the saved key is kept
		const FString Largest = FString::ChrN(FN2CCredentialManagerBackend::MaxKeyBytes, TEXT('k'));
		TestTrue(TEXT("a key of MaxKeyBytes saves"), Restarted->SetApiKey(EN2CLLMProvider::OpenAI, Largest));
		TestEqual(TEXT("and reads back whole"), MakeStore()->GetApiKey(EN2CLLMProvider::OpenAI), Largest);
		AddExpectedError(TEXT("Windows Credential Manager takes at most"), EAutomationExpectedErrorFlags::Contains, 1);
		AddExpectedError(TEXT("could not be saved"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("one byte more is refused"), Restarted->SetApiKey(EN2CLLMProvider::OpenAI, Largest + TEXT("k")));
		TestEqual(TEXT("the saved key is unchanged"), MakeStore()->GetApiKey(EN2CLLMProvider::OpenAI), Largest);

		// Clearing deletes the entry
		for (const TPair<EN2CLLMProvider, FString>& Key : Keys)
		{
			Restarted->SetApiKey(Key.Key, FString());
		}
		TestEqual(TEXT("cleared keys leave no entry"), Entries().Num(), 0);
		TestEqual(TEXT("a restarted store has no key"), MakeStore()->GetApiKeySource(EN2CLLMProvider::Gemini), EN2CApiKeySource::None);
	});

	It(TEXT("keeps both keys when two editors loaded before either save each save a different one"), [this]()
	{
		if (!RequireVault()) { return; }
		TUniquePtr<FN2CSecretStore> First = MakeStore();
		TUniquePtr<FN2CSecretStore> Second = MakeStore();
		TestTrue(TEXT("first editor saves OpenAI"), First->SetApiKey(EN2CLLMProvider::OpenAI, TEXT("n2c-test-first-openai")));
		TestTrue(TEXT("second editor saves Gemini"), Second->SetApiKey(EN2CLLMProvider::Gemini, TEXT("n2c-test-second-gemini")));

		TestEqual(TEXT("the second editor sees the first key after its own save"), Second->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("n2c-test-first-openai")));

		// The first editor still has its startup view (no Gemini key); its next save must not write that view back
		TestTrue(TEXT("first editor replaces OpenAI"), First->SetApiKey(EN2CLLMProvider::OpenAI, TEXT("n2c-test-first-openai-2")));
		TestEqual(TEXT("and then sees the second editor's key"), First->GetApiKey(EN2CLLMProvider::Gemini), FString(TEXT("n2c-test-second-gemini")));
		Second->Reload();
		TestEqual(TEXT("the second editor sees the replaced key after a reload"), Second->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("n2c-test-first-openai-2")));
		TUniquePtr<FN2CSecretStore> Restarted = MakeStore();
		TestEqual(TEXT("OpenAI after a restart"), Restarted->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("n2c-test-first-openai-2")));
		TestEqual(TEXT("Gemini after a restart"), Restarted->GetApiKey(EN2CLLMProvider::Gemini), FString(TEXT("n2c-test-second-gemini")));

		// Clearing in one editor does not bring back a key the other cleared
		Second->SetApiKey(EN2CLLMProvider::Gemini, FString());
		First->SetApiKey(EN2CLLMProvider::Anthropic, TEXT("n2c-test-first-anthropic"));
		TestFalse(TEXT("the cleared Gemini key stays cleared"), Entries().Contains(TEXT("Gemini")));
	});

	It(TEXT("keeps every key when writers on several threads save at once"), [this]()
	{
		if (!RequireVault()) { return; }
		// Writers 0-3 each own a store (four editors); writers 4-11 share one store (one editor, many callers)
		constexpr int32 OwnStores = 4;
		constexpr int32 SharedWriters = 8;
		TArray<TUniquePtr<FN2CSecretStore>> Stores;
		for (int32 Index = 0; Index < OwnStores + 1; ++Index)
		{
			Stores.Add(MakeStore());
		}
		ParallelFor(OwnStores + SharedWriters, [&Stores](int32 Index)
		{
			FN2CSecretStore& Store = Index < OwnStores ? *Stores[Index] : *Stores[OwnStores];
			Store.SetApiKey(FString::Printf(TEXT("Writer%02d"), Index), FString::Printf(TEXT("n2c-test-writer-%02d"), Index));
		});

		const TMap<FString, FString> Saved = Entries();
		TestEqual(TEXT("one entry per writer"), Saved.Num(), OwnStores + SharedWriters);
		for (int32 Index = 0; Index < OwnStores + SharedWriters; ++Index)
		{
			TestEqual(FString::Printf(TEXT("writer %d's key"), Index), Saved.FindRef(FString::Printf(TEXT("Writer%02d"), Index)),
				FString::Printf(TEXT("n2c-test-writer-%02d"), Index));
		}
		FN2CSecretStore& Shared = *Stores[OwnStores];
		for (int32 Index = OwnStores; Index < OwnStores + SharedWriters; ++Index)
		{
			TestEqual(FString::Printf(TEXT("the shared store holds writer %d's key"), Index), Shared.GetStoredApiKey(FString::Printf(TEXT("Writer%02d"), Index)),
				FString::Printf(TEXT("n2c-test-writer-%02d"), Index));
		}
	});

	It(TEXT("writes a key again when Windows reports it saved but does not keep it"), [this]()
	{
		if (!RequireVault()) { return; }
		// Another process's write at the same moment can undo this one (card 0496); here the first two are undone
		TUniquePtr<FN2CCredentialManagerBackend> Vault = MakeVault();
		int32 Writes = 0;
		Vault->AfterWriteForTests = [this, &Writes](const FString& Target, bool bDelete)
		{
			if (!bDelete && ++Writes <= 2)
			{
				TMap<FString, FString> Unused;
				MakeVault()->StoreKey(Target.RightChop(Prefix.Len()), FString(), Unused);
			}
		};
		FN2CCredentialManagerBackend* Hooked = Vault.Get();
		TUniquePtr<FN2CSecretStore> Store = MakeStore(MoveTemp(Vault));
		TestTrue(TEXT("the save reports success"), Store->SetApiKey(EN2CLLMProvider::OpenAI, TEXT("n2c-test-lost-openai")));
		// At least the two undone writes and the one that held (another process's write can undo one more)
		TestTrue(FString::Printf(TEXT("written until it held (%d writes)"), Writes), Writes >= 3);
		TestEqual(TEXT("Credential Manager holds it"), Entries().FindRef(TEXT("OpenAI")), FString(TEXT("n2c-test-lost-openai")));
		TestEqual(TEXT("a restarted store reads it"), MakeStore()->GetApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("n2c-test-lost-openai")));

		// A removal Windows undoes is repeated too
		int32 Deletes = 0;
		Hooked->AfterWriteForTests = [this, &Deletes](const FString& Target, bool bDelete)
		{
			if (bDelete && ++Deletes == 1)
			{
				TMap<FString, FString> Unused;
				MakeVault()->StoreKey(Target.RightChop(Prefix.Len()), TEXT("n2c-test-lost-openai"), Unused);
			}
		};
		TestTrue(TEXT("the clear reports success"), Store->SetApiKey(EN2CLLMProvider::OpenAI, FString()));
		TestTrue(FString::Printf(TEXT("removed until it held (%d removals)"), Deletes), Deletes >= 2);
		TestFalse(TEXT("the entry is gone"), Entries().Contains(TEXT("OpenAI")));
		Hooked->AfterWriteForTests = nullptr;
	});

	It(TEXT("reports a save that Windows never keeps as failed"), [this]()
	{
		if (!RequireVault()) { return; }
		TUniquePtr<FN2CCredentialManagerBackend> Vault = MakeVault();
		int32 Writes = 0;
		Vault->AfterWriteForTests = [this, &Writes](const FString& Target, bool bDelete)
		{
			++Writes;
			TMap<FString, FString> Unused;
			MakeVault()->StoreKey(Target.RightChop(Prefix.Len()), FString(), Unused);
		};
		TMap<FString, FString> After;
		AddExpectedError(TEXT("did not keep it"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("the save fails"), Vault->StoreKey(TEXT("OpenAI"), TEXT("n2c-test-never-kept"), After));
		TestEqual(TEXT("after MaxWriteAttempts writes"), Writes, FN2CCredentialManagerBackend::MaxWriteAttempts);
		Vault->AfterWriteForTests = nullptr;
	});

	It(TEXT("keeps a known key that a listing after a save leaves out while it is still stored"), [this]()
	{
		if (!RequireVault()) { return; }
		TUniquePtr<N2CSecretStoreKeychainSpec::FListingMissesBackend> Vault = MakeUnique<N2CSecretStoreKeychainSpec::FListingMissesBackend>(Prefix);
		N2CSecretStoreKeychainSpec::FListingMissesBackend* Listing = Vault.Get();
		TUniquePtr<FN2CSecretStore> Store = MakeStore(MoveTemp(Vault));
		TestTrue(TEXT("OpenAI saves"), Store->SetApiKey(EN2CLLMProvider::OpenAI, TEXT("n2c-test-listed-openai")));

		// The next listings miss OpenAI's entry, which is still stored
		Listing->Hidden = TEXT("OpenAI");
		TestTrue(TEXT("Gemini saves"), Store->SetApiKey(EN2CLLMProvider::Gemini, TEXT("n2c-test-listed-gemini")));
		TestEqual(TEXT("the store still has OpenAI's key"), Store->GetStoredApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("n2c-test-listed-openai")));
		Store->Reload();
		TestEqual(TEXT("and after a reload"), Store->GetStoredApiKey(EN2CLLMProvider::OpenAI), FString(TEXT("n2c-test-listed-openai")));

		// A key another editor removed is dropped
		TMap<FString, FString> Unused;
		MakeVault()->StoreKey(TEXT("OpenAI"), FString(), Unused);
		TestTrue(TEXT("Anthropic saves"), Store->SetApiKey(EN2CLLMProvider::Anthropic, TEXT("n2c-test-listed-anthropic")));
		TestEqual(TEXT("a removed key is dropped"), Store->GetStoredApiKey(EN2CLLMProvider::OpenAI), FString());
	});

	It(TEXT("keeps entries it does not know through saves and clears, and never reads them as a provider's key"), [this]()
	{
		if (!RequireVault()) { return; }
		// Written by another tool or a later NodeToCode: a future provider and a migration conflict copy
		TUniquePtr<FN2CCredentialManagerBackend> Vault = MakeVault();
		TMap<FString, FString> Unused;
		TestTrue(TEXT("future provider entry written"), Vault->StoreKey(TEXT("Mistral"), TEXT("n2c-test-mistral"), Unused));
		TestTrue(TEXT("conflict entry written"), Vault->StoreKey(TEXT("OpenAI.Conflict.1"), TEXT("n2c-test-openai-conflict"), Unused));

		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		TestEqual(TEXT("an unknown entry is readable by its id"), Store->GetApiKey(TEXT("Mistral")), FString(TEXT("n2c-test-mistral")));
		TestEqual(TEXT("a conflict copy is not OpenAI's key"), Store->GetApiKey(EN2CLLMProvider::OpenAI), FString());

		Store->SetApiKey(EN2CLLMProvider::OpenAI, TEXT("n2c-test-openai"));
		Store->SetApiKey(EN2CLLMProvider::Anthropic, TEXT("n2c-test-anthropic"));
		Store->SetApiKey(EN2CLLMProvider::Anthropic, FString());
		Store->Reload();

		const TMap<FString, FString> After = Entries();
		TestEqual(TEXT("the future provider's entry is kept"), After.FindRef(TEXT("Mistral")), FString(TEXT("n2c-test-mistral")));
		TestEqual(TEXT("the conflict copy is kept"), After.FindRef(TEXT("OpenAI.Conflict.1")), FString(TEXT("n2c-test-openai-conflict")));
		TestEqual(TEXT("OpenAI's own key is its own entry"), After.FindRef(TEXT("OpenAI")), FString(TEXT("n2c-test-openai")));
		TestEqual(TEXT("three entries"), After.Num(), 3);
		TestEqual(TEXT("a restarted store still reads the unknown entry"), MakeStore()->GetApiKey(TEXT("Mistral")), FString(TEXT("n2c-test-mistral")));
	});

	It(TEXT("falls back to the environment when Credential Manager has no key, and never saves the environment key"), [this]()
	{
		if (!RequireVault()) { return; }
		TUniquePtr<FN2CSecretStore> Store = MakeStore();
		FakeEnvironment.Add(TEXT("ANTHROPIC_API_KEY"), TEXT("n2c-test-env-anthropic"));
		FakeEnvironment.Add(TEXT("GOOGLE_API_KEY"), TEXT("n2c-test-env-google"));

		FString Variable;
		TestEqual(TEXT("the environment key is used"), Store->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("n2c-test-env-anthropic")));
		TestEqual(TEXT("its source is the environment"), Store->GetApiKeySource(EN2CLLMProvider::Anthropic, &Variable), EN2CApiKeySource::Environment);
		TestEqual(TEXT("named by its variable"), Variable, FString(TEXT("ANTHROPIC_API_KEY")));
		TestEqual(TEXT("Gemini falls back to GOOGLE_API_KEY"), Store->GetApiKey(EN2CLLMProvider::Gemini), FString(TEXT("n2c-test-env-google")));
		TestEqual(TEXT("the stored key reads empty"), Store->GetStoredApiKey(EN2CLLMProvider::Anthropic), FString());
		TestEqual(TEXT("nothing is written to Credential Manager"), Entries().Num(), 0);

		TestTrue(TEXT("a key saved in settings"), Store->SetApiKey(EN2CLLMProvider::Anthropic, TEXT("n2c-test-stored-anthropic")));
		TestEqual(TEXT("wins over the environment"), Store->GetApiKey(EN2CLLMProvider::Anthropic), FString(TEXT("n2c-test-stored-anthropic")));
		TestEqual(TEXT("and is the source"), Store->GetApiKeySource(EN2CLLMProvider::Anthropic), EN2CApiKeySource::Store);
		const TMap<FString, FString> Saved = Entries();
		TestEqual(TEXT("only the typed key is saved"), Saved.Num(), 1);
		TestFalse(TEXT("no environment value is saved"), Saved.FindKey(TEXT("n2c-test-env-google")) != nullptr || Saved.FindKey(TEXT("n2c-test-env-anthropic")) != nullptr);

		TestTrue(TEXT("removing the saved key"), Store->SetApiKey(EN2CLLMProvider::Anthropic, FString()));
		TestEqual(TEXT("goes back to the environment"), MakeStore()->GetApiKeySource(EN2CLLMProvider::Anthropic), EN2CApiKeySource::Environment);
		FakeEnvironment.Reset();
		TestEqual(TEXT("and to none without it"), Store->GetApiKeySource(EN2CLLMProvider::Anthropic), EN2CApiKeySource::None);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
