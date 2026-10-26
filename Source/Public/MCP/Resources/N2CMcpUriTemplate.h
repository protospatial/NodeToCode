// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/**
 * A resource URI template (RFC 6570, the two forms MCP resource templates use here) compiled to a full-match pattern
 * (card 0208). "{var}" is exactly one non-empty path segment; "{+var}" is the rest of the path, one or more non-empty
 * segments. Neither matches '?' or '#', so a URI with a query or fragment does not match. Captured values are
 * percent-decoded (UTF-8); a value with a malformed escape, or a "{var}" whose decoded value holds a '/', does not
 * match. Literal text must match exactly. Any other expression ({?x}, {#x}, {a,b}, ...) makes the template invalid,
 * and an invalid template matches nothing.
 */
class NODETOCODE_API FN2CMcpUriTemplate
{
public:
	explicit FN2CMcpUriTemplate(const FString& Template);

	/** False when the template has an unsupported or unterminated expression */
	bool IsValid() const { return bValid; }

	/** The variable names in template order */
	TArray<FString> GetVariableNames() const;

	/** True when the whole Uri matches; OutParams then holds each variable's decoded value (emptied otherwise) */
	bool Match(const FString& Uri, TMap<FString, FString>& OutParams) const;

	/** Percent-encodes Value as one path segment: UTF-8, every byte but A-Z a-z 0-9 - . _ ~ as %XX */
	static FString EncodeSegment(const FString& Value);

	/** Percent-encodes each '/'-separated segment of Path and keeps the slashes (a "{+var}" value) */
	static FString EncodePath(const FString& Path);

	/** Decodes %XX escapes (UTF-8). False, with OutValue empty, on a '%' not followed by two hex digits. */
	static bool Decode(const FString& Encoded, FString& OutValue);

private:
	enum class EPartKind : uint8 { Literal, Segment, Path };

	struct FPart
	{
		EPartKind Kind = EPartKind::Literal;
		/** The literal text, or the variable name */
		FString Text;
	};

	bool MatchFrom(const FString& Uri, int32 UriIndex, int32 PartIndex, TMap<FString, FString>& OutParams) const;
	static bool DecodeValue(EPartKind Kind, const FString& Raw, FString& OutValue);

	TArray<FPart> Parts;
	bool bValid = false;
};
