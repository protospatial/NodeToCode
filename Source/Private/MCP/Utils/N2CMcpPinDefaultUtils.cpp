// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpPinDefaultUtils.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Misc/DefaultValueHelper.h"
#include "UObject/Class.h"

namespace N2CMcpPinDefaultPrivate
{
	const UEnum* PinEnum(const UEdGraphPin& Pin)
	{
		const FName Category = Pin.PinType.PinCategory;
		if (Category != UEdGraphSchema_K2::PC_Byte && Category != UEdGraphSchema_K2::PC_Enum)
		{
			return nullptr;
		}
		return Cast<const UEnum>(Pin.PinType.PinSubCategoryObject.Get());
	}

	/** The enum's entries an agent may name: every one but the generated _MAX */
	int32 UsableEnumCount(const UEnum& Enum)
	{
		// The generated entry is last (EDrawDebugTrace::MAX, EMyEnum_MAX)
		const int32 Count = Enum.NumEnums();
		return Count > 0 && (Enum.ContainsExistingMax() || Enum.GetNameStringByIndex(Count - 1).EndsWith(TEXT("MAX"), ESearchCase::CaseSensitive))
			? Count - 1 : Count;
	}

	/** An entry by its name (short or Enum::Name), its display name, or its index, as the entry's name; else Value */
	FString EnumEntryName(const UEnum& Enum, const FString& Value)
	{
		const int32 Usable = UsableEnumCount(Enum);
		const int32 ByName = Enum.GetIndexByNameString(Value);
		if (ByName != INDEX_NONE && ByName < Usable)
		{
			return Enum.GetNameStringByIndex(ByName);
		}
		for (int32 Index = 0; Index < Usable; ++Index)
		{
			if (Enum.GetDisplayNameTextByIndex(Index).ToString().Equals(Value, ESearchCase::IgnoreCase))
			{
				return Enum.GetNameStringByIndex(Index);
			}
		}
		int32 AsIndex = INDEX_NONE;
		if (FDefaultValueHelper::ParseInt(Value, AsIndex) && AsIndex >= 0 && AsIndex < Usable)
		{
			return Enum.GetNameStringByIndex(AsIndex);
		}
		return Value;
	}

	bool IsObjectReference(const FName Category)
	{
		return Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_Interface;
	}
}

FString FN2CMcpPinDefault::EnumEntryName(const UEnum& Enum, const FString& Value)
{
	return N2CMcpPinDefaultPrivate::EnumEntryName(Enum, Value);
}

bool FN2CMcpPinDefault::NormalizeBool(const FString& Value, FString& OutValue)
{
	const FString Trimmed = Value.TrimStartAndEnd();
	if (Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Trimmed == TEXT("1") || Trimmed.Equals(TEXT("yes"), ESearchCase::IgnoreCase))
	{
		OutValue = TEXT("true");
		return true;
	}
	if (Trimmed.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Trimmed == TEXT("0") || Trimmed.Equals(TEXT("no"), ESearchCase::IgnoreCase))
	{
		OutValue = TEXT("false");
		return true;
	}
	return false;
}

bool FN2CMcpPinDefault::Normalize(const UEdGraphPin& Pin, const FString& Value, FString& OutValue, FString& OutError)
{
	using namespace N2CMcpPinDefaultPrivate;
	const FName Category = Pin.PinType.PinCategory;
	OutValue = Value.TrimStartAndEnd();
	if (Category == UEdGraphSchema_K2::PC_String || Category == UEdGraphSchema_K2::PC_Name || Category == UEdGraphSchema_K2::PC_Text)
	{
		// Text is taken as sent, spaces included
		OutValue = Value;
		return true;
	}
	if (Category == UEdGraphSchema_K2::PC_Boolean)
	{
		// The schema accepts any text for a bool (and reads anything else as false), so the spelling is checked here
		if (!NormalizeBool(OutValue, OutValue))
		{
			OutError = FString::Printf(TEXT("'%s' is not a boolean. Use 'true' or 'false'"), *Value);
			return false;
		}
		return true;
	}
	if (const UEnum* Enum = PinEnum(Pin))
	{
		OutValue = EnumEntryName(*Enum, OutValue);
		return true;
	}
	if (Category == UEdGraphSchema_K2::PC_Struct)
	{
		const UObject* Struct = Pin.PinType.PinSubCategoryObject.Get();
		const FString StructName = Struct ? Struct->GetName() : FString();
		if (StructName == TEXT("Vector") || StructName == TEXT("Vector3f") || StructName == TEXT("Vector2D") || StructName == TEXT("Rotator"))
		{
			OutValue.ReplaceInline(TEXT(" "), TEXT(""));
		}
	}
	return true;
}

FString FN2CMcpPinDefault::Validate(const UEdGraphPin& Pin, const FString& NormalizedValue)
{
	using namespace N2CMcpPinDefaultPrivate;
	const UEdGraphSchema* Schema = Pin.GetSchema();
	if (!Schema)
	{
		return TEXT("The pin's graph has no schema");
	}
	const FName Category = Pin.PinType.PinCategory;
	if (Category == UEdGraphSchema_K2::PC_Text)
	{
		return Schema->IsPinDefaultValid(&Pin, FString(), nullptr, FText::FromString(NormalizedValue));
	}
	const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(Schema);
	if (!K2Schema)
	{
		return Schema->IsPinDefaultValid(&Pin, NormalizedValue, nullptr, FText::GetEmpty());
	}

	// What TrySetDefaultValue would store: an object or class path is loaded here
	FString UseValue;
	TObjectPtr<UObject> UseObject = nullptr;
	FText UseText;
	K2Schema->GetPinDefaultValuesFromString(Pin.PinType, Pin.GetOwningNodeUnchecked(), NormalizedValue, UseValue, UseObject, UseText,
		/*bPreserveTextIdentity*/ false);

	FString Error = K2Schema->IsPinDefaultValid(&Pin, UseValue, UseObject, UseText);
	if (!Error.IsEmpty())
	{
		// The usual slip on a class pin: the Blueprint asset rather than its generated class
		const UBlueprint* Blueprint = Cast<const UBlueprint>(UseObject.Get());
		if (Category == UEdGraphSchema_K2::PC_Class && Blueprint && Blueprint->GeneratedClass)
		{
			Error += FString::Printf(TEXT(" '%s' is a Blueprint asset; a class pin takes its generated class, '%s'."),
				*NormalizedValue, *Blueprint->GeneratedClass->GetPathName());
		}
		return Error;
	}
	if (IsObjectReference(Category) && !NormalizedValue.IsEmpty() && NormalizedValue != TEXT("None")
		&& UseObject == nullptr && UseValue.IsEmpty())
	{
		// A full path that does not load: the schema would accept it and store None
		return FString::Printf(TEXT("'%s' could not be loaded: no %s exists at that path"), *NormalizedValue,
			Category == UEdGraphSchema_K2::PC_Class ? TEXT("class") : TEXT("object"));
	}
	return FString();
}

bool FN2CMcpPinDefault::Prepare(const UEdGraphPin& Pin, const FString& Value, FString& OutPrepared, FString& OutError)
{
	OutPrepared.Reset();
	if (!Pin.GetSchema() || !Pin.GetOwningNodeUnchecked())
	{
		OutError = TEXT("The pin has no node or schema");
		return false;
	}
	if (Value.IsEmpty())
	{
		// Reset to Default
		return true;
	}
	if (!Normalize(Pin, Value, OutPrepared, OutError))
	{
		return false;
	}
	if (OutPrepared.IsEmpty())
	{
		// Only spaces: as empty, a reset
		return true;
	}
	OutError = Validate(Pin, OutPrepared);
	return OutError.IsEmpty();
}

void FN2CMcpPinDefault::Write(UEdGraphPin& Pin, const FString& Prepared)
{
	const UEdGraphSchema* Schema = Pin.GetSchema();
	UEdGraphNode* Node = Pin.GetOwningNodeUnchecked();
	if (!Schema || !Node)
	{
		return;
	}
	Node->Modify();
	if (Prepared.IsEmpty())
	{
		// Reset to Default, as the editor's pin context menu does
		Schema->ResetPinToAutogeneratedDefaultValue(&Pin, /*bCallModifyCallbacks*/ true);
	}
	else if (Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		Schema->TrySetDefaultText(Pin, FText::FromString(Prepared), /*bMarkAsModified*/ false);
	}
	else
	{
		// No TrySetDefaultObject(nullptr) first: Prepare accepted the value, so it replaces the old default
		Schema->TrySetDefaultValue(Pin, Prepared, /*bMarkAsModified*/ false);
	}
}

bool FN2CMcpPinDefault::Apply(UEdGraphPin& Pin, const FString& Value, FString& OutError)
{
	FString Prepared;
	if (!Prepare(Pin, Value, Prepared, OutError))
	{
		return false;
	}
	Write(Pin, Prepared);
	return true;
}

FString FN2CMcpPinDefault::Read(const UEdGraphPin& Pin)
{
	return Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Text ? Pin.DefaultTextValue.ToString() : Pin.GetDefaultAsString();
}

FString FN2CMcpPinDefault::RefusalMessage(const UEdGraphPin& Pin, const FString& Value, const FString& Reason, const FString& OldValue)
{
	FString Why = Reason.TrimStartAndEnd();
	Why.RemoveFromEnd(TEXT("."));
	return FString::Printf(TEXT("Pin '%s' refused the value '%s': %s. The pin keeps its default '%s'."),
		*Pin.GetDisplayName().ToString(), *Value, *Why, *OldValue);
}
