// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/Timespan.h"

/**
 * Windows Credential Manager entries written by the specs (cards 0496, 0497). Every spec that writes a credential
 * works under its own "NodeToCodeTest/<guid>/" prefix, never the user's "NodeToCode/" entries, and:
 * - calls SweepStaleTestCredentialsOnce() in BeforeEach, which removes what an aborted, killed or raced run in any
 *   checkout left behind (entries under "NodeToCodeTest/" last written more than an hour ago);
 * - calls RemoveTestCredentials(Prefix) in AfterEach, which deletes by name, reads each delete back, and lists again
 *   until nothing is left, because Windows can bring a deleted entry back while another process writes credentials.
 * Nothing here reads or logs a credential's value.
 */
namespace N2CTest
{
	/** Root of every test target name */
	inline const TCHAR* const TestCredentialRoot = TEXT("NodeToCodeTest/");

	/** "NodeToCodeTest/<guid>/": a fresh prefix for one case */
	FString MakeTestCredentialPrefix();

	/**
	 * Deletes every entry under Prefix (which must start with TestCredentialRoot).
	 * @return the number of entries still listed after the last round (0 when it is clean)
	 */
	int32 RemoveTestCredentials(const FString& Prefix);

	/**
	 * Deletes the entries under TestCredentialRoot last written more than MaxAge ago. A running case's entries are
	 * seconds old, so another checkout's run in progress is never touched.
	 * @return the number deleted
	 */
	int32 SweepStaleTestCredentials(FTimespan MaxAge = FTimespan::FromHours(1));

	/** SweepStaleTestCredentials once per editor process */
	void SweepStaleTestCredentialsOnce();
}

#endif // WITH_DEV_AUTOMATION_TESTS
