// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CPinTypeCodec.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "EdGraphSchema_K2.h"

namespace N2CPinTypeCodecPrivate
{
	/** "Head<Inner>": the head (trimmed) and what is between the first '<' and the last '>'; false for anything else */
	static bool SplitWrapper(const FString& Text, FString& OutHead, FString& OutInner)
	{
		int32 Open = INDEX_NONE;
		if (!Text.EndsWith(TEXT(">")) || !Text.FindChar(TEXT('<'), Open) || Open == 0)
		{
			return false;
		}
		OutHead = Text.Left(Open).TrimStartAndEnd();
		OutInner = Text.Mid(Open + 1, Text.Len() - Open - 2).TrimStartAndEnd();
		return !OutHead.IsEmpty();
	}

	/** Splits "K, V" at the comma that is not inside '<...>'; false when there is no such comma */
	static bool SplitTopLevelComma(const FString& Text, FString& OutLeft, FString& OutRight)
	{
		int32 Depth = 0;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Char = Text[Index];
			if (Char == TEXT('<'))
			{
				++Depth;
			}
			else if (Char == TEXT('>'))
			{
				--Depth;
			}
			else if (Char == TEXT(',') && Depth == 0)
			{
				OutLeft = Text.Left(Index).TrimStartAndEnd();
				OutRight = Text.Mid(Index + 1).TrimStartAndEnd();
				return true;
			}
		}
		return false;
	}

	/** The resolver's generic category a reference wrapper names (class<T>, TSubclassOf<T>, ...), or empty */
	static FString ReferenceCategory(const FString& Head)
	{
		static const TPair<const TCHAR*, const TCHAR*> Heads[] = {
			{TEXT("class"), TEXT("class")}, {TEXT("tsubclassof"), TEXT("class")},
			{TEXT("softobject"), TEXT("softobject")}, {TEXT("tsoftobjectptr"), TEXT("softobject")},
			{TEXT("softclass"), TEXT("softclass")}, {TEXT("tsoftclassptr"), TEXT("softclass")},
			{TEXT("interface"), TEXT("interface")}, {TEXT("tscriptinterface"), TEXT("interface")},
			{TEXT("object"), TEXT("object")}, {TEXT("tobjectptr"), TEXT("object")},
			{TEXT("struct"), TEXT("struct")}, {TEXT("enum"), TEXT("enum")}};
		for (const TPair<const TCHAR*, const TCHAR*>& Entry : Heads)
		{
			if (Head.Equals(Entry.Key, ESearchCase::IgnoreCase))
			{
				return Entry.Value;
			}
		}
		return FString();
	}

	/** One non-container type through the resolver: a reference wrapper becomes its category plus sub-type */
	static bool ResolveTerminalText(const FString& InText, FEdGraphPinType& OutPinType, FString& OutError)
	{
		const FString Text = InText.TrimStartAndEnd();
		FString Head, Inner;
		if (SplitWrapper(Text, Head, Inner))
		{
			const FString Category = ReferenceCategory(Head);
			if (!Category.IsEmpty())
			{
				return FN2CMcpTypeResolver::ResolvePinType(Category, Inner, TEXT("none"), TEXT(""), false, false, OutPinType, OutError);
			}
			OutError = FString::Printf(TEXT("INVALID_TYPE: '%s': %s<...> is not a type this codec reads here. Containers are array<T>, set<T> and map<K, V> (not nested); references class<T>, softobject<T>, softclass<T>, interface<T>."), *Text, *Head);
			return false;
		}
		return FN2CMcpTypeResolver::ResolvePinType(Text, TEXT(""), TEXT("none"), TEXT(""), false, false, OutPinType, OutError);
	}

	/** A map pin type: the key's fields with Map, and the value as PinValueType, as the editor's variable types are */
	static bool ComposeMap(const FEdGraphPinType& Key, const FEdGraphPinType& Value, FEdGraphPinType& OutPinType, FString& OutError)
	{
		if (Key.ContainerType != EPinContainerType::None || Value.ContainerType != EPinContainerType::None)
		{
			OutError = TEXT("INVALID_CONTAINER_TYPE: a map's key and value cannot be containers.");
			return false;
		}
		OutPinType = Key;
		OutPinType.ContainerType = EPinContainerType::Map;
		OutPinType.PinValueType.TerminalCategory = Value.PinCategory;
		OutPinType.PinValueType.TerminalSubCategory = Value.PinSubCategory;
		OutPinType.PinValueType.TerminalSubCategoryObject = Value.PinSubCategoryObject;
		OutPinType.PinValueType.bTerminalIsConst = Value.bIsConst;
		OutPinType.PinValueType.bTerminalIsWeakPointer = Value.bIsWeakPointer;
		return FN2CMcpTypeResolver::ValidateVariablePinType(OutPinType, OutError);
	}

	static FString ObjectPath(const UObject* Object)
	{
		return Object ? Object->GetPathName() : FString();
	}

	/** The canonical text of one terminal type; empty when no variable can have it */
	static FString TerminalText(const FName& Category, const FName& SubCategory, const UObject* SubObject)
	{
		if (SubCategory == UEdGraphSchema_K2::PSC_Self)
		{
			return FString();
		}
		if (Category == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
		if (Category == UEdGraphSchema_K2::PC_Byte) return Cast<UEnum>(SubObject) ? ObjectPath(SubObject) : FString(TEXT("byte"));
		if (Category == UEdGraphSchema_K2::PC_Enum) return Cast<UEnum>(SubObject) ? ObjectPath(SubObject) : FString();
		if (Category == UEdGraphSchema_K2::PC_Int) return TEXT("int");
		if (Category == UEdGraphSchema_K2::PC_Int64) return TEXT("int64");
		if (Category == UEdGraphSchema_K2::PC_Real) return SubCategory == UEdGraphSchema_K2::PC_Float ? TEXT("float") : TEXT("double");
		if (Category == UEdGraphSchema_K2::PC_Float) return TEXT("float");
		if (Category == UEdGraphSchema_K2::PC_Double) return TEXT("double");
		if (Category == UEdGraphSchema_K2::PC_String) return TEXT("string");
		if (Category == UEdGraphSchema_K2::PC_Name) return TEXT("name");
		if (Category == UEdGraphSchema_K2::PC_Text) return TEXT("text");
		if (Category == UEdGraphSchema_K2::PC_Struct) return Cast<UScriptStruct>(SubObject) ? ObjectPath(SubObject) : FString();
		if (!Cast<UClass>(SubObject))
		{
			return FString();
		}
		const FString Path = ObjectPath(SubObject);
		if (Category == UEdGraphSchema_K2::PC_Object) return Path;
		if (Category == UEdGraphSchema_K2::PC_Class) return FString::Printf(TEXT("class<%s>"), *Path);
		if (Category == UEdGraphSchema_K2::PC_SoftObject) return FString::Printf(TEXT("softobject<%s>"), *Path);
		if (Category == UEdGraphSchema_K2::PC_SoftClass) return FString::Printf(TEXT("softclass<%s>"), *Path);
		if (Category == UEdGraphSchema_K2::PC_Interface) return FString::Printf(TEXT("interface<%s>"), *Path);
		return FString();
	}

	/** The resolver identifier and sub-type of an N2C JSON v2 terminal type (N2CStableNames' spellings) */
	static bool N2CTerminal(const FString& Type, const FString& SubType, FString& OutIdentifier, FString& OutSubType, FString& OutError)
	{
		OutSubType.Reset();
		const FString T = Type.TrimStartAndEnd();
		static const TPair<const TCHAR*, const TCHAR*> Plain[] = {
			{TEXT("Boolean"), TEXT("bool")}, {TEXT("Byte"), TEXT("byte")}, {TEXT("Integer"), TEXT("int")},
			{TEXT("Integer64"), TEXT("int64")}, {TEXT("Float"), TEXT("float")}, {TEXT("Double"), TEXT("double")},
			{TEXT("String"), TEXT("string")}, {TEXT("Name"), TEXT("name")}, {TEXT("Text"), TEXT("text")},
			{TEXT("Vector"), TEXT("Vector")}, {TEXT("Vector2D"), TEXT("Vector2D")}, {TEXT("Vector4D"), TEXT("Vector4")},
			{TEXT("Rotator"), TEXT("Rotator")}, {TEXT("Transform"), TEXT("Transform")}, {TEXT("Quat"), TEXT("Quat")}};
		for (const TPair<const TCHAR*, const TCHAR*>& Entry : Plain)
		{
			if (T.Equals(Entry.Key, ESearchCase::IgnoreCase))
			{
				OutIdentifier = Entry.Value;
				return true;
			}
		}
		if (T.Equals(TEXT("Real"), ESearchCase::IgnoreCase))
		{
			OutIdentifier = SubType.TrimStartAndEnd().Equals(TEXT("float"), ESearchCase::IgnoreCase) ? TEXT("float") : TEXT("double");
			return true;
		}
		static const TCHAR* Generic[] = {TEXT("Struct"), TEXT("Enum"), TEXT("Object"), TEXT("Class"), TEXT("Interface"),
			TEXT("SoftObject"), TEXT("SoftClass")};
		for (const TCHAR* Category : Generic)
		{
			if (T.Equals(Category, ESearchCase::IgnoreCase))
			{
				OutIdentifier = FString(Category).ToLower();
				OutSubType = SubType.TrimStartAndEnd();
				return true;
			}
		}
		OutError = FString::Printf(TEXT("INVALID_TYPE: the N2C JSON pin type '%s' is not a type a variable or parameter can have."), *Type);
		return false;
	}

	static TValueOrError<FEdGraphPinType, FText> Fail(const FString& Error)
	{
		return MakeError(FText::FromString(Error));
	}
}

namespace N2C::PinType
{
	TValueOrError<FEdGraphPinType, FText> Parse(const FString& InText)
	{
		using namespace N2CPinTypeCodecPrivate;
		const FString Text = InText.TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			return Fail(TEXT("INVALID_TYPE: the type is empty."));
		}

		FEdGraphPinType PinType;
		FString Error;
		FString Head, Inner;
		if (SplitWrapper(Text, Head, Inner))
		{
			const bool bArray = Head.Equals(TEXT("array"), ESearchCase::IgnoreCase) || Head.Equals(TEXT("TArray"), ESearchCase::IgnoreCase);
			const bool bSet = Head.Equals(TEXT("set"), ESearchCase::IgnoreCase) || Head.Equals(TEXT("TSet"), ESearchCase::IgnoreCase);
			const bool bMap = Head.Equals(TEXT("map"), ESearchCase::IgnoreCase) || Head.Equals(TEXT("TMap"), ESearchCase::IgnoreCase);
			if (bMap)
			{
				FString KeyText, ValueText;
				if (!SplitTopLevelComma(Inner, KeyText, ValueText))
				{
					return Fail(FString::Printf(TEXT("INVALID_TYPE: '%s': a map is map<KeyType, ValueType>."), *Text));
				}
				FEdGraphPinType Key, Value;
				FString KeyError;
				if (!ResolveTerminalText(KeyText, Key, KeyError))
				{
					return Fail(FString::Printf(TEXT("INVALID_MAP_KEY_TYPE: '%s': %s"), *KeyText, *KeyError));
				}
				if (!ResolveTerminalText(ValueText, Value, Error) || !ComposeMap(Key, Value, PinType, Error))
				{
					return Fail(Error);
				}
				return MakeValue(PinType);
			}
			if (bArray || bSet)
			{
				if (!ResolveTerminalText(Inner, PinType, Error))
				{
					return Fail(Error);
				}
				PinType.ContainerType = bArray ? EPinContainerType::Array : EPinContainerType::Set;
				if (!FN2CMcpTypeResolver::ValidateVariablePinType(PinType, Error))
				{
					return Fail(Error);
				}
				return MakeValue(PinType);
			}
		}

		if (!ResolveTerminalText(Text, PinType, Error))
		{
			return Fail(Error);
		}
		return MakeValue(PinType);
	}

	FString Format(const FEdGraphPinType& PinType)
	{
		using namespace N2CPinTypeCodecPrivate;
		const FString Terminal = TerminalText(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
		if (Terminal.IsEmpty())
		{
			return FString();
		}
		switch (PinType.ContainerType)
		{
		case EPinContainerType::Array:
			return FString::Printf(TEXT("array<%s>"), *Terminal);
		case EPinContainerType::Set:
			return FString::Printf(TEXT("set<%s>"), *Terminal);
		case EPinContainerType::Map:
		{
			const FEdGraphTerminalType& Value = PinType.PinValueType;
			const FString ValueText = TerminalText(Value.TerminalCategory, Value.TerminalSubCategory, Value.TerminalSubCategoryObject.Get());
			return ValueText.IsEmpty() ? FString() : FString::Printf(TEXT("map<%s, %s>"), *Terminal, *ValueText);
		}
		default:
			return Terminal;
		}
	}

	TValueOrError<FEdGraphPinType, FText> ParseN2C(const FString& Type, const FString& SubType, bool bIsArray, bool bIsSet,
		bool bIsMap, const FString& ValueType, const FString& ValueSubType)
	{
		using namespace N2CPinTypeCodecPrivate;
		FString Identifier, Sub, Error;
		if (!N2CTerminal(Type, SubType, Identifier, Sub, Error))
		{
			return Fail(Error);
		}
		FEdGraphPinType PinType;
		if (bIsMap)
		{
			FString ValueIdentifier, ValueSub;
			if (!N2CTerminal(ValueType, ValueSubType, ValueIdentifier, ValueSub, Error))
			{
				return Fail(Error);
			}
			FEdGraphPinType Key, Value;
			if (!FN2CMcpTypeResolver::ResolvePinType(Identifier, Sub, TEXT("none"), TEXT(""), false, false, Key, Error)
				|| !FN2CMcpTypeResolver::ResolvePinType(ValueIdentifier, ValueSub, TEXT("none"), TEXT(""), false, false, Value, Error)
				|| !ComposeMap(Key, Value, PinType, Error))
			{
				return Fail(Error);
			}
			return MakeValue(PinType);
		}
		const TCHAR* Container = bIsArray ? TEXT("array") : (bIsSet ? TEXT("set") : TEXT("none"));
		if (!FN2CMcpTypeResolver::ResolvePinType(Identifier, Sub, Container, TEXT(""), false, false, PinType, Error))
		{
			return Fail(Error);
		}
		return MakeValue(PinType);
	}

	bool SameType(const FEdGraphPinType& A, const FEdGraphPinType& B)
	{
		if (A.PinCategory != B.PinCategory || A.PinSubCategory != B.PinSubCategory
			|| A.PinSubCategoryObject.Get() != B.PinSubCategoryObject.Get() || A.ContainerType != B.ContainerType)
		{
			return false;
		}
		if (A.ContainerType != EPinContainerType::Map)
		{
			return true;
		}
		const FEdGraphTerminalType& VA = A.PinValueType;
		const FEdGraphTerminalType& VB = B.PinValueType;
		return VA.TerminalCategory == VB.TerminalCategory && VA.TerminalSubCategory == VB.TerminalSubCategory
			&& VA.TerminalSubCategoryObject.Get() == VB.TerminalSubCategoryObject.Get();
	}
}
