// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Tests/Core/N2CTestCredentials.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSecretStore.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Utils/N2CLogger.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include <wincred.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace N2CTest
{
	FString MakeTestCredentialPrefix()
	{
		return FString::Printf(TEXT("%s%s/"), TestCredentialRoot, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	int32 RemoveTestCredentials(const FString& Prefix)
	{
		if (!FN2CCredentialManagerBackend::IsSupported() || !Prefix.StartsWith(TestCredentialRoot) || Prefix.Len() <= FCString::Strlen(TestCredentialRoot))
		{
			return 0;
		}
		FN2CCredentialManagerBackend Vault(Prefix);
		TMap<FString, FString> Left;
		for (int32 Round = 0; Round < 5; ++Round)
		{
			if (!Vault.LoadAll(Left) || Left.Num() == 0)
			{
				return Left.Num();
			}
			for (const TPair<FString, FString>& Entry : Left)
			{
				TMap<FString, FString> Unused;
				Vault.StoreKey(Entry.Key, FString(), Unused);
			}
		}
		Vault.LoadAll(Left);
		return Left.Num();
	}

	int32 SweepStaleTestCredentials(FTimespan MaxAge)
	{
		int32 Deleted = 0;
#if PLATFORM_WINDOWS
		const FString Filter = FString(TestCredentialRoot) + TEXT("*");
		DWORD Count = 0;
		PCREDENTIALW* Credentials = nullptr;
		if (!::CredEnumerateW(*Filter, 0, &Count, &Credentials))
		{
			return 0;
		}
		// FILETIME counts 100 ns ticks from 1601-01-01 UTC, FDateTime from 0001-01-01
		const int64 FileTimeEpochTicks = FDateTime(1601, 1, 1).GetTicks();
		const FDateTime Oldest = FDateTime::UtcNow() - MaxAge;
		TArray<FString> Stale;
		for (DWORD Index = 0; Index < Count; ++Index)
		{
			const PCREDENTIALW Credential = Credentials[Index];
			if (!Credential || Credential->Type != CRED_TYPE_GENERIC || !Credential->TargetName)
			{
				continue;
			}
			const FString Target(Credential->TargetName);
			// Only test entries: the filter already matched, this guards against a filter that ever widens
			if (!Target.StartsWith(TestCredentialRoot, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const int64 FileTime = (static_cast<int64>(Credential->LastWritten.dwHighDateTime) << 32) | Credential->LastWritten.dwLowDateTime;
			if (FDateTime(FileTimeEpochTicks + FileTime) < Oldest)
			{
				Stale.Add(Target);
			}
		}
		::CredFree(Credentials);
		for (const FString& Target : Stale)
		{
			if (::CredDeleteW(*Target, CRED_TYPE_GENERIC, 0))
			{
				++Deleted;
			}
		}
		if (Deleted > 0)
		{
			FN2CLogger::Get().Log(FString::Printf(TEXT("Removed %d test credentials under %s left by earlier runs"), Deleted, TestCredentialRoot),
				EN2CLogSeverity::Info, TEXT("Tests"));
		}
#endif
		return Deleted;
	}

	void SweepStaleTestCredentialsOnce()
	{
		static bool bSwept = false;
		if (!bSwept)
		{
			bSwept = true;
			SweepStaleTestCredentials();
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
