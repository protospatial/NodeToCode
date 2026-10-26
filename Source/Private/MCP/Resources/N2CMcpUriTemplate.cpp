// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Resources/N2CMcpUriTemplate.h"
#include "Containers/StringConv.h"

namespace N2CMcpUriTemplatePrivate
{
	bool IsUnreserved(uint8 Byte)
	{
		return (Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z') || (Byte >= '0' && Byte <= '9')
			|| Byte == '-' || Byte == '.' || Byte == '_' || Byte == '~';
	}

	int32 HexValue(TCHAR Char)
	{
		if (Char >= '0' && Char <= '9') { return Char - '0'; }
		if (Char >= 'A' && Char <= 'F') { return Char - 'A' + 10; }
		if (Char >= 'a' && Char <= 'f') { return Char - 'a' + 10; }
		return -1;
	}

	/** A character a variable may span: '?' and '#' end the path; '/' also ends a "{var}" segment */
	bool IsValueChar(TCHAR Char, bool bPath)
	{
		return Char != '?' && Char != '#' && (bPath || Char != '/');
	}

	bool IsVariableName(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : Name)
		{
			if (!FChar::IsAlnum(Char) && Char != '_' && Char != '.')
			{
				return false;
			}
		}
		return true;
	}
}

FN2CMcpUriTemplate::FN2CMcpUriTemplate(const FString& Template)
{
	using namespace N2CMcpUriTemplatePrivate;

	bValid = true;
	int32 Index = 0;
	while (Index < Template.Len())
	{
		const int32 Open = Template.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
		const int32 LiteralEnd = Open == INDEX_NONE ? Template.Len() : Open;
		if (LiteralEnd > Index)
		{
			const FString Literal = Template.Mid(Index, LiteralEnd - Index);
			if (Literal.Contains(TEXT("}")))
			{
				bValid = false;
			}
			Parts.Add({ EPartKind::Literal, Literal });
		}
		if (Open == INDEX_NONE)
		{
			break;
		}

		const int32 Close = Template.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
		if (Close == INDEX_NONE)
		{
			bValid = false;
			break;
		}
		FString Expression = Template.Mid(Open + 1, Close - Open - 1);
		EPartKind Kind = EPartKind::Segment;
		if (Expression.StartsWith(TEXT("+")))
		{
			Kind = EPartKind::Path;
			Expression.RightChopInline(1);
		}
		if (!IsVariableName(Expression))
		{
			bValid = false;
		}
		Parts.Add({ Kind, Expression });
		Index = Close + 1;
	}
	if (!bValid)
	{
		Parts.Reset();
	}
}

TArray<FString> FN2CMcpUriTemplate::GetVariableNames() const
{
	TArray<FString> Names;
	for (const FPart& Part : Parts)
	{
		if (Part.Kind != EPartKind::Literal)
		{
			Names.Add(Part.Text);
		}
	}
	return Names;
}

bool FN2CMcpUriTemplate::Match(const FString& Uri, TMap<FString, FString>& OutParams) const
{
	OutParams.Reset();
	if (!bValid || Parts.Num() == 0)
	{
		return false;
	}
	if (MatchFrom(Uri, 0, 0, OutParams))
	{
		return true;
	}
	OutParams.Reset();
	return false;
}

bool FN2CMcpUriTemplate::MatchFrom(const FString& Uri, int32 UriIndex, int32 PartIndex, TMap<FString, FString>& OutParams) const
{
	using namespace N2CMcpUriTemplatePrivate;

	if (PartIndex == Parts.Num())
	{
		return UriIndex == Uri.Len();
	}

	const FPart& Part = Parts[PartIndex];
	if (Part.Kind == EPartKind::Literal)
	{
		if (Uri.Len() - UriIndex < Part.Text.Len()
			|| FCString::Strncmp(*Uri + UriIndex, *Part.Text, Part.Text.Len()) != 0)
		{
			return false;
		}
		return MatchFrom(Uri, UriIndex + Part.Text.Len(), PartIndex + 1, OutParams);
	}

	// The longest run of characters the variable may span, then shorter ones when what follows does not match
	const bool bPath = Part.Kind == EPartKind::Path;
	int32 RunEnd = UriIndex;
	while (RunEnd < Uri.Len() && IsValueChar(Uri[RunEnd], bPath))
	{
		++RunEnd;
	}
	for (int32 End = RunEnd; End > UriIndex; --End)
	{
		FString Value;
		if (!DecodeValue(Part.Kind, Uri.Mid(UriIndex, End - UriIndex), Value))
		{
			continue;
		}
		if (MatchFrom(Uri, End, PartIndex + 1, OutParams))
		{
			OutParams.Add(Part.Text, MoveTemp(Value));
			return true;
		}
	}
	return false;
}

bool FN2CMcpUriTemplate::DecodeValue(EPartKind Kind, const FString& Raw, FString& OutValue)
{
	OutValue.Reset();
	if (Raw.IsEmpty())
	{
		return false;
	}
	if (Kind == EPartKind::Segment)
	{
		// One segment, also after decoding: an encoded '/' does not smuggle in a second one
		return Decode(Raw, OutValue) && !OutValue.IsEmpty() && !OutValue.Contains(TEXT("/"));
	}

	// The rest of the path: every segment non-empty and decoded on its own
	TArray<FString> Segments;
	Raw.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/false);
	TArray<FString> Decoded;
	for (const FString& Segment : Segments)
	{
		FString Value;
		if (Segment.IsEmpty() || !Decode(Segment, Value) || Value.IsEmpty() || Value.Contains(TEXT("/")))
		{
			return false;
		}
		Decoded.Add(MoveTemp(Value));
	}
	OutValue = FString::Join(Decoded, TEXT("/"));
	return true;
}

FString FN2CMcpUriTemplate::EncodeSegment(const FString& Value)
{
	using namespace N2CMcpUriTemplatePrivate;

	static const TCHAR* const Hex = TEXT("0123456789ABCDEF");
	const FTCHARToUTF8 Utf8(*Value, Value.Len());
	FString Encoded;
	Encoded.Reserve(Utf8.Length() * 3);
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		const uint8 Byte = static_cast<uint8>(Utf8.Get()[Index]);
		if (IsUnreserved(Byte))
		{
			Encoded.AppendChar(static_cast<TCHAR>(Byte));
		}
		else
		{
			Encoded.AppendChar(TEXT('%'));
			Encoded.AppendChar(Hex[Byte >> 4]);
			Encoded.AppendChar(Hex[Byte & 0xF]);
		}
	}
	return Encoded;
}

FString FN2CMcpUriTemplate::EncodePath(const FString& Path)
{
	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/false);
	for (FString& Segment : Segments)
	{
		Segment = EncodeSegment(Segment);
	}
	return FString::Join(Segments, TEXT("/"));
}

bool FN2CMcpUriTemplate::Decode(const FString& Encoded, FString& OutValue)
{
	using namespace N2CMcpUriTemplatePrivate;

	OutValue.Reset();
	TArray<uint8> Bytes;
	Bytes.Reserve(Encoded.Len());
	for (int32 Index = 0; Index < Encoded.Len(); ++Index)
	{
		const TCHAR Char = Encoded[Index];
		if (Char == '%')
		{
			if (Index + 2 >= Encoded.Len())
			{
				return false;
			}
			const int32 High = HexValue(Encoded[Index + 1]);
			const int32 Low = HexValue(Encoded[Index + 2]);
			if (High < 0 || Low < 0)
			{
				return false;
			}
			Bytes.Add(static_cast<uint8>((High << 4) | Low));
			Index += 2;
		}
		else if (Char < 0x80)
		{
			Bytes.Add(static_cast<uint8>(Char));
		}
		else
		{
			// A character outside ASCII written as is (an IRI): its UTF-8 bytes, a UTF-16 surrogate pair as one
			const int32 Count = (StringConv::IsHighSurrogate(Char) && Index + 1 < Encoded.Len() && StringConv::IsLowSurrogate(Encoded[Index + 1])) ? 2 : 1;
			const FTCHARToUTF8 Utf8(&Encoded[Index], Count);
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
			Index += Count - 1;
		}
	}
	const FUTF8ToTCHAR Wide(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
	OutValue = FString(Wide.Length(), Wide.Get());
	return true;
}
