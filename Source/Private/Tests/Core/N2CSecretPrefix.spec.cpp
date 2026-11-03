// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Tests/Core/N2CTestCredentials.h"
#include "Tests/N2CTestHelpers.h"

/**
 * The test prefix switch of the editor's secret store (card 0523): a non-shipping build started with
 * -N2CSecretPrefix=NodeToCodeTest/<guid>/ keeps API keys in Credential Manager entries under that prefix, so a windowed
 * check of Save Key or the API Key field never touches the user's NodeToCode/* entries. FN2CSecretStore::Get() reads
 * the switch once through FN2CCredentialManagerBackend::ResolveTargetPrefix, which this spec calls with made-up command
 * lines; the live store itself is left alone.
 */
BEGIN_DEFINE_SPEC(FN2CSecretPrefixSpec, "NodeToCode.Secrets.TestPrefix", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CSecretPrefixSpec)

void FN2CSecretPrefixSpec::Define()
{
	It(TEXT("uses NodeToCode/ without the switch"), [this]()
	{
		FString Warning;
		TestEqual(TEXT("no switch"), FN2CCredentialManagerBackend::ResolveTargetPrefix(TEXT("Host.uproject -unattended -nullrhi"), &Warning),
			FString(FN2CCredentialManagerBackend::DefaultTargetPrefix));
		TestEqual(TEXT("no command line"), FN2CCredentialManagerBackend::ResolveTargetPrefix(nullptr, &Warning),
			FString(FN2CCredentialManagerBackend::DefaultTargetPrefix));
		TestTrue(TEXT("and no warning"), Warning.IsEmpty());
		TestEqual(TEXT("the test root is the specs' credential root"), FString(FN2CCredentialManagerBackend::TestTargetRoot), FString(N2CTest::TestCredentialRoot));
	});

	It(TEXT("honours a prefix under NodeToCodeTest/"), [this]()
	{
		FString Warning;
		TestEqual(TEXT("as given"), FN2CCredentialManagerBackend::ResolveTargetPrefix(TEXT("Host.uproject -N2CSecretPrefix=NodeToCodeTest/0123abcd/ -nosplash"), &Warning),
			FString(TEXT("NodeToCodeTest/0123abcd/")));
		TestEqual(TEXT("with the trailing slash added"), FN2CCredentialManagerBackend::ResolveTargetPrefix(TEXT("-N2CSecretPrefix=NodeToCodeTest/gate0516"), &Warning),
			FString(TEXT("NodeToCodeTest/gate0516/")));
		TestEqual(TEXT("quoted"), FN2CCredentialManagerBackend::ResolveTargetPrefix(TEXT("-N2CSecretPrefix=\"NodeToCodeTest/q/\""), &Warning),
			FString(TEXT("NodeToCodeTest/q/")));
		TestTrue(TEXT("without a warning"), Warning.IsEmpty());

		// What the API Key Status rows show for that store
		const FN2CCredentialManagerBackend Backend(TEXT("NodeToCodeTest/0123abcd/"));
		TestEqual(TEXT("the store's name names the test prefix"), Backend.GetDisplayName(), FString(TEXT("Windows Credential Manager (test prefix NodeToCodeTest/0123abcd/)")));
		TestEqual(TEXT("the default store's name is unchanged"), FN2CCredentialManagerBackend().GetDisplayName(), FString(TEXT("Windows Credential Manager")));
	});

	It(TEXT("refuses a prefix outside NodeToCodeTest/ and keeps NodeToCode/"), [this]()
	{
		for (const TCHAR* Bad : { TEXT("NodeToCode/"), TEXT("NodeToCode/Test/"), TEXT("Other/"), TEXT("NodeToCodeTest/"), TEXT("nodetocodetest/x/"),
			TEXT("NodeToCodeTest/*/"), TEXT("NodeToCodeTest/../NodeToCode/") })
		{
			FString Warning;
			const FString CommandLine = FString::Printf(TEXT("Host.uproject -N2CSecretPrefix=%s"), Bad);
			TestEqual(FString::Printf(TEXT("%s is refused"), Bad), FN2CCredentialManagerBackend::ResolveTargetPrefix(*CommandLine, &Warning),
				FString(FN2CCredentialManagerBackend::DefaultTargetPrefix));
			TestTrue(FString::Printf(TEXT("%s: the warning says why"), Bad), Warning.Contains(TEXT("Ignored -N2CSecretPrefix")));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
