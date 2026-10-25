// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

/**
 * @file N2CJsonContract.h
 * @brief The N2C JSON contract's version and the field names this version introduced or reshaped
 *
 * Decision record .agents/decisions/0003-n2c-json-v2-contract.md (provisional, card 0117) chose them. Change them here
 * only, so the serializer, the reader, the validator and the tests follow; the prompts (Content/Prompting) and the
 * batch spec describe the same names in prose.
 */

#pragma once

#include "CoreMinimal.h"

namespace N2CJsonContract
{
	/** The version ToJson writes: the v2 contract. Additive optional fields bump the minor, breaking changes the major. */
	inline constexpr const TCHAR* Version = TEXT("2.0.0");

	/** Major version of Version */
	inline constexpr int32 Major = 2;

	/** The oldest major FromJson still reads (1.x: 1.2.x and earlier 2.0 builds); kept through the 2.x line */
	inline constexpr int32 OldestReadableMajor = 1;

	/** Field names */
	inline constexpr const TCHAR* VersionField = TEXT("version");
	inline constexpr const TCHAR* FlowsField = TEXT("flows");
	inline constexpr const TCHAR* ExecutionField = TEXT("execution");

	/**
	 * flows.data: an object keyed by the output pin ref ("N1.P2"). The value is the one input pin ref it feeds, or an
	 * array of input pin refs when it feeds several (fan-out). v1 always had one string, keeping one edge per output.
	 */
	inline constexpr const TCHAR* DataField = TEXT("data");

	/** The major of a version string ("2.0.0" -> 2), or INDEX_NONE when it does not start with a number */
	inline int32 ParseMajor(const FString& VersionString)
	{
		FString MajorText;
		if (!VersionString.Split(TEXT("."), &MajorText, nullptr))
		{
			MajorText = VersionString;
		}
		MajorText.TrimStartAndEndInline();
		return !MajorText.IsEmpty() && MajorText.IsNumeric() && !MajorText.Contains(TEXT("-")) && !MajorText.Contains(TEXT("."))
			? FCString::Atoi(*MajorText) : INDEX_NONE;
	}

	/** True when FromJson reads this version (majors OldestReadableMajor..Major) */
	inline bool IsReadable(const FString& VersionString)
	{
		const int32 ParsedMajor = ParseMajor(VersionString);
		return ParsedMajor >= OldestReadableMajor && ParsedMajor <= Major;
	}
}
