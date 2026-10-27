// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Utils/N2CPinTypeCodec.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Models/N2CJsonContract.h"
#include "BlueprintLibraries/N2CStructUtilsLibrary.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace N2CPinTypeCodecSpec
{
	FEdGraphPinType Make(const FName Category, const FName SubCategory = NAME_None, UObject* Object = nullptr,
		EPinContainerType Container = EPinContainerType::None)
	{
		FEdGraphPinType Type;
		Type.PinCategory = Category;
		Type.PinSubCategory = SubCategory;
		Type.PinSubCategoryObject = Object;
		Type.ContainerType = Container;
		return Type;
	}

	FEdGraphPinType MakeMap(const FEdGraphPinType& Key, const FEdGraphPinType& Value)
	{
		FEdGraphPinType Type = Key;
		Type.ContainerType = EPinContainerType::Map;
		Type.PinValueType.TerminalCategory = Value.PinCategory;
		Type.PinValueType.TerminalSubCategory = Value.PinSubCategory;
		Type.PinValueType.TerminalSubCategoryObject = Value.PinSubCategoryObject;
		return Type;
	}

	FString TypeText(const FEdGraphPinType& Type)
	{
		const UObject* Object = Type.PinSubCategoryObject.Get();
		const UObject* ValueObject = Type.PinValueType.TerminalSubCategoryObject.Get();
		return FString::Printf(TEXT("%s/%s (%s) container %d value %s/%s (%s)"), *Type.PinCategory.ToString(),
			*Type.PinSubCategory.ToString(), Object ? *Object->GetPathName() : TEXT("-"), static_cast<int32>(Type.ContainerType),
			*Type.PinValueType.TerminalCategory.ToString(), *Type.PinValueType.TerminalSubCategory.ToString(),
			ValueObject ? *ValueObject->GetPathName() : TEXT("-"));
	}

	UClass* SoundInterface() { return FindObject<UClass>(nullptr, TEXT("/Script/Engine.ActorSoundParameterInterface")); }

	/** Every kind of variable type: primitives, enums, structs, objects, classes, soft references, interfaces, containers */
	TArray<TPair<FString, FEdGraphPinType>> Table()
	{
		using K2 = UEdGraphSchema_K2;
		UClass* Actor = AActor::StaticClass();
		UScriptStruct* Vector = TBaseStructure<FVector>::Get();
		UScriptStruct* HitResult = FHitResult::StaticStruct();
		UEnum* Channel = StaticEnum<ECollisionChannel>();
		TArray<TPair<FString, FEdGraphPinType>> Out;
		Out.Add({ TEXT("bool"), Make(K2::PC_Boolean) });
		Out.Add({ TEXT("byte"), Make(K2::PC_Byte) });
		Out.Add({ TEXT("int"), Make(K2::PC_Int) });
		Out.Add({ TEXT("int64"), Make(K2::PC_Int64) });
		Out.Add({ TEXT("float"), Make(K2::PC_Real, K2::PC_Float) });
		Out.Add({ TEXT("double"), Make(K2::PC_Real, K2::PC_Double) });
		Out.Add({ TEXT("string"), Make(K2::PC_String) });
		Out.Add({ TEXT("name"), Make(K2::PC_Name) });
		Out.Add({ TEXT("text"), Make(K2::PC_Text) });
		Out.Add({ TEXT("enum"), Make(K2::PC_Byte, NAME_None, Channel) });
		Out.Add({ TEXT("math struct"), Make(K2::PC_Struct, NAME_None, Vector) });
		Out.Add({ TEXT("struct"), Make(K2::PC_Struct, NAME_None, HitResult) });
		Out.Add({ TEXT("object"), Make(K2::PC_Object, NAME_None, Actor) });
		Out.Add({ TEXT("class"), Make(K2::PC_Class, NAME_None, Actor) });
		Out.Add({ TEXT("soft object"), Make(K2::PC_SoftObject, NAME_None, Actor) });
		Out.Add({ TEXT("soft class"), Make(K2::PC_SoftClass, NAME_None, Actor) });
		if (UClass* Interface = SoundInterface())
		{
			Out.Add({ TEXT("interface"), Make(K2::PC_Interface, NAME_None, Interface) });
		}
		Out.Add({ TEXT("array of int"), Make(K2::PC_Int, NAME_None, nullptr, EPinContainerType::Array) });
		Out.Add({ TEXT("array of struct"), Make(K2::PC_Struct, NAME_None, Vector, EPinContainerType::Array) });
		Out.Add({ TEXT("array of class"), Make(K2::PC_Class, NAME_None, Actor, EPinContainerType::Array) });
		Out.Add({ TEXT("set of name"), Make(K2::PC_Name, NAME_None, nullptr, EPinContainerType::Set) });
		Out.Add({ TEXT("set of enum"), Make(K2::PC_Byte, NAME_None, Channel, EPinContainerType::Set) });
		Out.Add({ TEXT("map name to object"), MakeMap(Make(K2::PC_Name), Make(K2::PC_Object, NAME_None, Actor)) });
		Out.Add({ TEXT("map int to float"), MakeMap(Make(K2::PC_Int), Make(K2::PC_Real, K2::PC_Float)) });
		Out.Add({ TEXT("map enum to soft class"), MakeMap(Make(K2::PC_Byte, NAME_None, Channel), Make(K2::PC_SoftClass, NAME_None, Actor)) });
		return Out;
	}
}

/**
 * N2C::PinType (card 0347, review arch-arch-core-llm-ui-12): one codec between a type string and FEdGraphPinType.
 * Parse(Format(t)) is t for every kind of variable type; Parse takes every alias the two old vocabularies took
 * (integer from UN2CStructUtilsLibrary, int32 from FN2CMcpTypeResolver) and the container and reference spellings;
 * ParseN2C reads the type fields of a captured N2C JSON v2 document back to the variable's own type; the struct
 * library adds a member of a type only the MCP vocabulary knew. No editor is opened: every engine.
 */
BEGIN_DEFINE_SPEC(FN2CPinTypeCodecSpec, "NodeToCode.PinTypeCodec", N2C_TEST_FLAGS)
	void ExpectParses(const FString& Text, const FEdGraphPinType& Expected);
END_DEFINE_SPEC(FN2CPinTypeCodecSpec)

void FN2CPinTypeCodecSpec::ExpectParses(const FString& Text, const FEdGraphPinType& Expected)
{
	using namespace N2CPinTypeCodecSpec;
	const TValueOrError<FEdGraphPinType, FText> Parsed = N2C::PinType::Parse(Text);
	if (!TestTrue(FString::Printf(TEXT("'%s' parses: %s"), *Text, Parsed.HasError() ? *Parsed.GetError().ToString() : TEXT("")), Parsed.HasValue()))
	{
		return;
	}
	TestTrue(FString::Printf(TEXT("'%s' is %s, got %s"), *Text, *TypeText(Expected), *TypeText(Parsed.GetValue())),
		N2C::PinType::SameType(Parsed.GetValue(), Expected));
}

void FN2CPinTypeCodecSpec::Define()
{
	using namespace N2CPinTypeCodecSpec;
	using K2 = UEdGraphSchema_K2;

	Describe(TEXT("Parse(Format(t))"), [this]()
	{
		It(TEXT("is t for primitives, enums, structs, objects, classes, soft references, interfaces and each container"), [this]()
		{
			TestNotNull(TEXT("ActorSoundParameterInterface exists (the interface row)"), SoundInterface());
			for (const TPair<FString, FEdGraphPinType>& Row : Table())
			{
				const FString Text = N2C::PinType::Format(Row.Value);
				if (!TestFalse(Row.Key + TEXT(": Format writes something"), Text.IsEmpty()))
				{
					continue;
				}
				ExpectParses(Text, Row.Value);
			}
		});

		It(TEXT("writes nothing for a pin type no variable can have"), [this]()
		{
			TestTrue(TEXT("exec"), N2C::PinType::Format(Make(K2::PC_Exec)).IsEmpty());
			TestTrue(TEXT("wildcard"), N2C::PinType::Format(Make(K2::PC_Wildcard)).IsEmpty());
			TestTrue(TEXT("self"), N2C::PinType::Format(Make(K2::PC_Object, K2::PSC_Self)).IsEmpty());
			TestTrue(TEXT("object without a class"), N2C::PinType::Format(Make(K2::PC_Object)).IsEmpty());
		});
	});

	Describe(TEXT("Parse"), [this]()
	{
		It(TEXT("takes every alias of both old vocabularies"), [this]()
		{
			ExpectParses(TEXT("integer"), Make(K2::PC_Int));
			ExpectParses(TEXT("int32"), Make(K2::PC_Int));
			ExpectParses(TEXT("Integer64"), Make(K2::PC_Int64));
			ExpectParses(TEXT("boolean"), Make(K2::PC_Boolean));
			ExpectParses(TEXT("uint8"), Make(K2::PC_Byte));
			ExpectParses(TEXT("real"), Make(K2::PC_Real, K2::PC_Double));
			ExpectParses(TEXT("FString"), Make(K2::PC_String));
			ExpectParses(TEXT("FName"), Make(K2::PC_Name));
			ExpectParses(TEXT("FText"), Make(K2::PC_Text));
			ExpectParses(TEXT("FVector"), Make(K2::PC_Struct, NAME_None, TBaseStructure<FVector>::Get()));
			ExpectParses(TEXT("LinearColor"), Make(K2::PC_Struct, NAME_None, TBaseStructure<FLinearColor>::Get()));
			ExpectParses(TEXT("/Script/Engine.Actor"), Make(K2::PC_Object, NAME_None, AActor::StaticClass()));
			ExpectParses(TEXT("Actor"), Make(K2::PC_Object, NAME_None, AActor::StaticClass()));
			ExpectParses(TEXT("AActor*"), Make(K2::PC_Object, NAME_None, AActor::StaticClass()));
			ExpectParses(TEXT("FHitResult"), Make(K2::PC_Struct, NAME_None, FHitResult::StaticStruct()));
			ExpectParses(TEXT("ECollisionChannel"), Make(K2::PC_Byte, NAME_None, StaticEnum<ECollisionChannel>()));
		});

		It(TEXT("takes the C++ container and reference spellings"), [this]()
		{
			ExpectParses(TEXT("TArray<int32>"), Make(K2::PC_Int, NAME_None, nullptr, EPinContainerType::Array));
			ExpectParses(TEXT("TSet<FName>"), Make(K2::PC_Name, NAME_None, nullptr, EPinContainerType::Set));
			ExpectParses(TEXT("TMap<FName, AActor*>"), MakeMap(Make(K2::PC_Name), Make(K2::PC_Object, NAME_None, AActor::StaticClass())));
			ExpectParses(TEXT("TSubclassOf<AActor>"), Make(K2::PC_Class, NAME_None, AActor::StaticClass()));
			ExpectParses(TEXT("TSoftObjectPtr<AActor>"), Make(K2::PC_SoftObject, NAME_None, AActor::StaticClass()));
			ExpectParses(TEXT("TSoftClassPtr<AActor>"), Make(K2::PC_SoftClass, NAME_None, AActor::StaticClass()));
			ExpectParses(TEXT("array<class<Actor>>"), Make(K2::PC_Class, NAME_None, AActor::StaticClass(), EPinContainerType::Array));
		});

		It(TEXT("refuses, with an error code, what no variable can be"), [this]()
		{
			for (const TCHAR* Text : { TEXT(""), TEXT("exec"), TEXT("wildcard"), TEXT("delegate"), TEXT("NoSuchType_N2C"),
				TEXT("array<array<int>>"), TEXT("map<int>"), TEXT("map<bool, int>"), TEXT("set<text>"), TEXT("TFoo<int>"),
				TEXT("class<HitResult>") })
			{
				const TValueOrError<FEdGraphPinType, FText> Parsed = N2C::PinType::Parse(Text);
				const FString Error = Parsed.HasError() ? Parsed.GetError().ToString() : FString();
				TestTrue(FString::Printf(TEXT("'%s' refused with a code: %s"), Text, *Error), Parsed.HasError()
					&& (Error.StartsWith(TEXT("INVALID_")) || Error.StartsWith(TEXT("TYPE_NOT_FOUND")) || Error.StartsWith(TEXT("ASSET_NOT_FOUND"))));
			}
		});
	});

	Describe(TEXT("ParseN2C"), [this]()
	{
		It(TEXT("reads every variable of a captured N2C JSON v2 document back to the variable's type"), [this]()
		{
			N2CTest::FTestBlueprint Test(TEXT("BP_N2CPinTypeCodecSpec"));
			UBlueprint* Blueprint = Test.GetBlueprint();
			UEdGraph* Function = Blueprint ? Test.AddFunction(FName(TEXT("N2CCodecSpecFunction"))) : nullptr;
			if (!TestTrue(TEXT("test Blueprint with a function"), Blueprint && Function))
			{
				return;
			}
			TMap<FString, FEdGraphPinType> Expected;
			int32 Index = 0;
			for (const TPair<FString, FEdGraphPinType>& Row : Table())
			{
				const FString Name = FString::Printf(TEXT("N2CCodecVar%d"), Index++);
				if (TestTrue(Row.Key + TEXT(": variable added"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), Row.Value)))
				{
					Expected.Add(Name, Row.Value);
				}
			}

			FN2CCaptureOptions Options;
			Options.IncludeVariables = true;
			const TValueOrError<FN2CCapture, FString> Captured = N2C::CaptureGraphs({ Function }, Options);
			if (!TestTrue(TEXT("capture: ") + (Captured.HasError() ? Captured.GetError() : FString()), Captured.HasValue()))
			{
				return;
			}
			TSharedPtr<FJsonObject> Document;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
				FN2CSerializer::ToJson(Captured.GetValue().Blueprint, EN2CJsonStyle::Condensed));
			const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
			if (!TestTrue(TEXT("the document has variables"), FJsonSerializer::Deserialize(Reader, Document) && Document.IsValid()
				&& Document->TryGetArrayField(N2CJsonContract::VariablesField, Variables)))
			{
				return;
			}
			int32 Checked = 0;
			for (const TSharedPtr<FJsonValue>& Value : *Variables)
			{
				const TSharedPtr<FJsonObject> Entry = Value->AsObject();
				FString Name, Type, SubType, ValueType, ValueSubType;
				bool bArray = false, bSet = false, bMap = false;
				Entry->TryGetStringField(TEXT("name"), Name);
				const FEdGraphPinType* Want = Expected.Find(Name);
				if (!Want)
				{
					continue;
				}
				Entry->TryGetStringField(TEXT("type"), Type);
				Entry->TryGetStringField(TEXT("sub_type"), SubType);
				Entry->TryGetBoolField(TEXT("is_array"), bArray);
				Entry->TryGetBoolField(TEXT("is_set"), bSet);
				Entry->TryGetBoolField(TEXT("is_map"), bMap);
				Entry->TryGetStringField(N2CJsonContract::PinValueTypeField, ValueType);
				Entry->TryGetStringField(N2CJsonContract::PinValueSubTypeField, ValueSubType);
				const TValueOrError<FEdGraphPinType, FText> Parsed = N2C::PinType::ParseN2C(Type, SubType, bArray, bSet, bMap, ValueType, ValueSubType);
				const FString What = FString::Printf(TEXT("%s (%s %s)"), *Name, *Type, *SubType);
				if (TestTrue(What + TEXT(" parses: ") + (Parsed.HasError() ? Parsed.GetError().ToString() : FString()), Parsed.HasValue()))
				{
					TestTrue(FString::Printf(TEXT("%s is %s, got %s"), *What, *TypeText(*Want), *TypeText(Parsed.GetValue())),
						N2C::PinType::SameType(Parsed.GetValue(), *Want));
					++Checked;
				}
			}
			TestEqual(TEXT("every variable was in the document"), Checked, Expected.Num());
		});
	});

	Describe(TEXT("UN2CStructUtilsLibrary"), [this]()
	{
		It(TEXT("adds members of types only the MCP vocabulary knew, and of the old aliases"), [this]()
		{
			const FString Name = FString::Printf(TEXT("S_N2CCodecSpec_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
			UPackage* Package = CreatePackage(*(FString(TEXT("/Temp/N2CPinTypeCodecSpec/")) + Name));
			UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
			if (!TestNotNull(TEXT("scratch struct"), Struct))
			{
				return;
			}
			ON_SCOPE_EXIT
			{
				Package->SetDirtyFlag(false);
				Struct->ClearFlags(RF_Public | RF_Standalone);
				Struct->MarkAsGarbage();
			};
			for (const TCHAR* Type : { TEXT("integer"), TEXT("ECollisionChannel"), TEXT("HitResult"), TEXT("array<int>"), TEXT("class<Actor>") })
			{
				bool bSuccess = false;
				FString Error;
				UN2CStructUtilsLibrary::AddStructVariable(Struct, FString::Printf(TEXT("Member_%s"), *FString(Type).Replace(TEXT("<"), TEXT("_")).Replace(TEXT(">"), TEXT(""))), Type, bSuccess, Error);
				TestTrue(FString::Printf(TEXT("'%s' added: %s"), Type, *Error), bSuccess);
			}
			bool bSuccess = true;
			FString Error;
			UN2CStructUtilsLibrary::AddStructVariable(Struct, TEXT("Member_Bad"), TEXT("exec"), bSuccess, Error);
			TestTrue(TEXT("exec refused with the reason: ") + Error, !bSuccess && Error.Contains(TEXT("INVALID_TYPE")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
