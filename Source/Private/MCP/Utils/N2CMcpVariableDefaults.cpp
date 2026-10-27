// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Utils/N2CMcpVariableDefaults.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpPinDefaultUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace N2CMcpVariableDefaultsPrivate
{
	/** One value of a property's type, initialized to the type's default and destroyed with the scope */
	struct FScratchValue
	{
		const FProperty* Property;
		void* Memory;

		explicit FScratchValue(const FProperty* InProperty)
			: Property(InProperty)
			, Memory(FMemory::Malloc(InProperty->GetSize(), InProperty->GetMinAlignment()))
		{
			Property->InitializeValue(Memory);
		}

		~FScratchValue()
		{
			Property->DestroyValue(Memory);
			FMemory::Free(Memory);
		}

		FScratchValue(const FScratchValue&) = delete;
		FScratchValue& operator=(const FScratchValue&) = delete;

		uint8* Bytes() const { return static_cast<uint8*>(Memory); }
	};

	const UEnum* PropertyEnum(const FProperty* Property)
	{
		if (const FByteProperty* Byte = CastField<const FByteProperty>(Property))
		{
			return Byte->Enum;
		}
		if (const FEnumProperty* Enum = CastField<const FEnumProperty>(Property))
		{
			return Enum->GetEnum();
		}
		return nullptr;
	}

	/** "Float", "Integer", "Vector (Structure)", ...: the type as the editor names it */
	FString TypeName(const FProperty* Property)
	{
		FEdGraphPinType PinType;
		if (GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
		{
			return UEdGraphSchema_K2::TypeToText(PinType).ToString();
		}
		return Property->GetCPPType();
	}

	/** Parses Value into Out (already initialized); an empty Value leaves the type's default there */
	bool ParseInto(const FProperty* Property, const FString& Value, UObject* Owner, uint8* Out, FString& OutError)
	{
		if (Value.IsEmpty())
		{
			return true;
		}
		FString Text = Value;
		if (CastField<const FBoolProperty>(Property))
		{
			if (!FN2CMcpPinDefault::NormalizeBool(Value, Text))
			{
				OutError = FString::Printf(TEXT("'%s' is not a Boolean value. Use 'true' or 'false'"), *Value);
				return false;
			}
		}
		else if (const UEnum* Enum = PropertyEnum(Property))
		{
			Text = FN2CMcpPinDefault::EnumEntryName(*Enum, Value.TrimStartAndEnd());
		}
		else if (CastField<const FNameProperty>(Property) && Value.Len() >= NAME_SIZE)
		{
			OutError = FString::Printf(TEXT("A Name value is limited to %d characters (this one has %d)"), NAME_SIZE - 1, Value.Len());
			return false;
		}

		if (FBlueprintEditorUtils::PropertyValueFromString_Direct(Property, Text, Out, Owner))
		{
			return true;
		}
		OutError = FString::Printf(TEXT("'%s' is not a valid %s value"), *Value, *TypeName(Property));
		if (const UEnum* Enum = PropertyEnum(Property))
		{
			TArray<FString> Entries;
			for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
			{
				const FString Entry = Enum->GetNameStringByIndex(Index);
				if (!Entry.EndsWith(TEXT("MAX"), ESearchCase::CaseSensitive))
				{
					Entries.Add(FString::Printf(TEXT("%s ('%s')"), *Entry, *Enum->GetDisplayNameTextByIndex(Index).ToString()));
				}
			}
			OutError += FString::Printf(TEXT(". Use an entry's name, display name or index: %s"), *FString::Join(Entries, TEXT(", ")));
		}
		return false;
	}

	bool IsPendingCompile(const UBlueprint* Blueprint)
	{
		return Blueprint->Status == BS_Dirty || Blueprint->Status == BS_Unknown;
	}
}

FProperty* FN2CMcpVariableDefaults::FindMemberProperty(const UBlueprint* Blueprint, const FName VariableName)
{
	return Blueprint && Blueprint->GeneratedClass && !VariableName.IsNone()
		? FindFProperty<FProperty>(Blueprint->GeneratedClass, VariableName) : nullptr;
}

bool FN2CMcpVariableDefaults::ReadMemberDefault(const UBlueprint* Blueprint, const FName VariableName, FString& OutValue)
{
	OutValue.Reset();
	const FProperty* Property = FindMemberProperty(Blueprint, VariableName);
	UObject* Defaults = Property ? Blueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/ false) : nullptr;
	return Defaults && FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(Defaults), OutValue, Defaults);
}

bool FN2CMcpVariableDefaults::ParseValue(const FProperty* Property, const FString& Value, UObject* Owner, FString& OutCanonical, FString& OutError)
{
	using namespace N2CMcpVariableDefaultsPrivate;
	OutCanonical.Reset();
	if (!Property)
	{
		OutError = TEXT("The variable has no compiled property");
		return false;
	}
	FScratchValue Parsed(Property);
	if (!ParseInto(Property, Value, Owner, Parsed.Bytes(), OutError))
	{
		return false;
	}
	FBlueprintEditorUtils::PropertyValueToString_Direct(Property, Parsed.Bytes(), OutCanonical, Owner);
	return true;
}

bool FN2CMcpVariableDefaults::SetMemberDefault(UBlueprint* Blueprint, const FName VariableName, const FString& Value,
	FMemberResult& OutResult, FString& OutError)
{
	using namespace N2CMcpVariableDefaultsPrivate;
	OutResult = FMemberResult();
	const int32 VarIndex = Blueprint ? FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) : INDEX_NONE;
	if (VarIndex == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found in Blueprint '%s'"), *VariableName.ToString(), *GetNameSafe(Blueprint));
		return false;
	}

	// A value the compiled property refuses fails before anything is compiled or changed
	FProperty* Property = FindMemberProperty(Blueprint, VariableName);
	if (Property)
	{
		FString Ignored;
		UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
		if (!ParseValue(Property, Value, Defaults, Ignored, OutError))
		{
			return false;
		}
	}

	// The class defaults are only the variable's defaults once the class is compiled from the current description: a
	// description default no compile has applied yet would overwrite the CDO at the next compile. Compiled here,
	// before the transaction, so the undo step holds only the default.
	if (!Property || !Blueprint->NewVariables[VarIndex].DefaultValue.IsEmpty() || IsPendingCompile(Blueprint))
	{
		FN2CCompileDiagnostics Diagnostics;
		FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);
		OutResult.bCompiledFirst = true;
		Property = FindMemberProperty(Blueprint, VariableName);
	}
	UObject* Defaults = (Property && Blueprint->GeneratedClass) ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	if (!Defaults)
	{
		OutError = FString::Printf(TEXT("Variable '%s' has no property on the compiled class of '%s'; fix the Blueprint's compile errors and retry"),
			*VariableName.ToString(), *Blueprint->GetName());
		return false;
	}

	FScratchValue NewValue(Property);
	if (!ParseInto(Property, Value, Defaults, NewValue.Bytes(), OutError))
	{
		return false;
	}
	FScratchValue OldValue(Property);
	void* const DefaultsValue = Property->ContainerPtrToValuePtr<void>(Defaults);
	Property->CopyCompleteValue(OldValue.Memory, DefaultsValue);
	FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(Defaults), OutResult.OldValue, Defaults);

	{
		const FScopedTransaction Transaction(NSLOCTEXT("NodeToCode", "SetVariableDefaultValue", "Set Variable Default Value"));

		// The description keeps no default once compiled; if this one still has one (its compile failed), it gets the
		// new value too, so a later compile applies the same value
		FBPVariableDescription& VarDesc = Blueprint->NewVariables[VarIndex];
		if (!VarDesc.DefaultValue.IsEmpty())
		{
			Blueprint->Modify();
			FBlueprintEditorUtils::PropertyValueToString_Direct(Property, NewValue.Bytes(), VarDesc.DefaultValue, Defaults);
		}

		const FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
		auto WriteTo = [&](UObject* Object)
		{
			// Undo restores the object only when it is transactional
			Object->SetFlags(RF_Transactional);
			Object->PreEditChange(Property);
			Object->Modify();
			Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Object), NewValue.Memory);
			FPropertyChangedEvent Event = ChangedEvent;
			Object->PostEditChangeProperty(Event);
		};

		// Instances (placed actors, child classes' defaults) still on the old default follow it, as the property
		// editor propagates a class-default change; collected before the write
		TArray<UObject*> Instances;
		Defaults->GetArchetypeInstances(Instances);
		TArray<UObject*> Following;
		for (UObject* Instance : Instances)
		{
			if (IsValid(Instance) && Instance->IsA(Blueprint->GeneratedClass)
				&& Property->Identical(Property->ContainerPtrToValuePtr<void>(Instance), OldValue.Memory, PPF_DeepComparison))
			{
				Following.Add(Instance);
			}
		}

		WriteTo(Defaults);
		for (UObject* Instance : Following)
		{
			WriteTo(Instance);
		}
		OutResult.InstancesUpdated = Following.Num();
	}

	// A class-default change compiles to nothing new: the package is dirty, the Blueprint's status unchanged
	FN2CMcpBlueprintUtils::MarkCosmeticChange(Blueprint);
	FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<const uint8*>(Defaults), OutResult.NewValue, Defaults);
	return true;
}

bool FN2CMcpVariableDefaults::PrepareLocalDefault(UBlueprint* Blueprint, const UEdGraph* FunctionGraph, UObject* EntryNode,
	const FName VariableName, const FString& Value, FString& OutCanonical, FString& OutError)
{
	OutCanonical.Reset();
	if (Value.IsEmpty())
	{
		// No default: the local starts as the type's default value
		return true;
	}
	auto FindLocal = [Blueprint, FunctionGraph, VariableName]() -> FProperty*
	{
		UClass* Skeleton = Blueprint && FunctionGraph ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
		UFunction* Function = Skeleton ? FindUField<UFunction>(Skeleton, FunctionGraph->GetFName()) : nullptr;
		return Function ? FindFProperty<FProperty>(Function, VariableName) : nullptr;
	};
	FProperty* Property = FindLocal();
	if (!Property)
	{
		FN2CCompileDiagnostics Diagnostics;
		FN2CMcpBlueprintUtils::Compile(Blueprint, Diagnostics);
		Property = FindLocal();
	}
	if (!Property)
	{
		OutError = FString::Printf(TEXT("Local variable '%s' is not in the compiled function '%s'; fix the Blueprint's compile errors and retry"),
			*VariableName.ToString(), FunctionGraph ? *FunctionGraph->GetName() : TEXT("?"));
		return false;
	}
	return ParseValue(Property, Value, EntryNode, OutCanonical, OutError);
}
