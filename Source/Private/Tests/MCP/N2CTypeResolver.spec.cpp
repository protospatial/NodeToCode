// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/UserDefinedEnum.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace N2CTypeResolverSpecPrivate
{
	struct FResolved
	{
		bool bOk = false;
		FEdGraphPinType PinType;
		FString Error;
	};

	static FResolved Resolve(const FString& Type, const FString& SubType = FString(), const FString& Container = TEXT("none"),
		const FString& Key = FString())
	{
		FResolved Result;
		Result.bOk = FN2CMcpTypeResolver::ResolvePinType(Type, SubType, Container, Key, false, false, Result.PinType, Result.Error);
		return Result;
	}

	static FString PinTypeText(const FEdGraphPinType& PinType)
	{
		const UObject* SubType = PinType.PinSubCategoryObject.Get();
		return FString::Printf(TEXT("%s/%s (%s) container %d"), *PinType.PinCategory.ToString(), *PinType.PinSubCategory.ToString(),
			SubType ? *SubType->GetPathName() : TEXT("no sub-type"), static_cast<int32>(PinType.ContainerType));
	}

	static UEnum* CollisionChannel() { return StaticEnum<ECollisionChannel>(); }
	static UScriptStruct* HitResult() { return FHitResult::StaticStruct(); }
	static UScriptStruct* Guid() { return TBaseStructure<FGuid>::Get(); }
	static UClass* SoundInterface() { return FindObject<UClass>(nullptr, TEXT("/Script/Engine.ActorSoundParameterInterface")); }

	/** A name no other case or leftover package uses, so a short-name lookup cannot find a stale copy */
	static FName UniqueName(const TCHAR* Prefix)
	{
		return FName(FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12)));
	}

	/** The compiled class's property for a variable, or null */
	static FProperty* CompiledProperty(UBlueprint* Blueprint, const FName Name)
	{
		return Blueprint && Blueprint->GeneratedClass ? Blueprint->GeneratedClass->FindPropertyByName(Name) : nullptr;
	}

	static const UEnum* EnumOf(const FProperty* Property)
	{
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
		{
			return Byte->Enum;
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
		{
			return Enum->GetEnum();
		}
		return nullptr;
	}

	static const UScriptStruct* StructOf(const FProperty* Property)
	{
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		return Struct ? Struct->Struct : nullptr;
	}
}

/**
 * FN2CMcpTypeResolver, the type resolution behind create-variable, create-local-variable, the function pin tools,
 * create-blueprint-function and the Python bridge. Every input form must resolve to the pin type the editor's own type
 * picker would make, or be refused with an error code, before anything touches a Blueprint. The compile cases add the
 * resolved type as a member variable of a transient Blueprint (only after checking the pin type, so a wrong
 * resolution fails here instead of asserting in FBlueprintEditorUtils::AddMemberVariable) and check the compiled
 * property. Review ids: mcp-tools-variables-3, -5, -7, -13, -14, -16 (card 0153; the UE 5.8 crash on 'HitResult'
 * found by card 0144).
 */
BEGIN_DEFINE_SPEC(FN2CTypeResolverSpec, "NodeToCode.MCP.TypeResolver", N2C_TEST_FLAGS)

	/** Resolve, expect success with this category and sub-type object, and the result to pass the variable checks */
	void ExpectType(const FString& Label, const N2CTypeResolverSpecPrivate::FResolved& Resolved, const FName Category, const UObject* SubType)
	{
		using namespace N2CTypeResolverSpecPrivate;
		if (!TestTrue(FString::Printf(TEXT("%s resolves (error: %s)"), *Label, *Resolved.Error), Resolved.bOk))
		{
			return;
		}
		TestEqual(FString::Printf(TEXT("%s category (%s)"), *Label, *PinTypeText(Resolved.PinType)), Resolved.PinType.PinCategory, Category);
		TestTrue(FString::Printf(TEXT("%s sub-type is %s (%s)"), *Label, SubType ? *SubType->GetPathName() : TEXT("none"),
			*PinTypeText(Resolved.PinType)), Resolved.PinType.PinSubCategoryObject.Get() == SubType);
		FString Error;
		const bool bValid = FN2CMcpTypeResolver::ValidateVariablePinType(Resolved.PinType, Error);
		TestTrue(FString::Printf(TEXT("%s passes ValidateVariablePinType (%s)"), *Label, *Error), bValid);
	}

	void ExpectRefused(const FString& Label, const N2CTypeResolverSpecPrivate::FResolved& Resolved, const FString& Code)
	{
		using namespace N2CTypeResolverSpecPrivate;
		if (!TestFalse(FString::Printf(TEXT("%s is refused (got %s)"), *Label, *PinTypeText(Resolved.PinType)), Resolved.bOk))
		{
			return;
		}
		TestTrue(FString::Printf(TEXT("%s error starts with %s: '%s'"), *Label, *Code, *Resolved.Error), Resolved.Error.StartsWith(Code));
	}

	/**
	 * Adds Type (already resolved and checked) as variable Name to a fresh transient Blueprint under MountRoot,
	 * compiles it and returns the compiled property (null on any failure, which is reported)
	 */
	FProperty* CompileVariable(N2CTest::FTestBlueprint& Blueprint, const FName Name, const N2CTypeResolverSpecPrivate::FResolved& Resolved)
	{
		if (!TestTrue(FString::Printf(TEXT("%s resolves before it reaches the Blueprint (%s)"), *Name.ToString(), *Resolved.Error), Resolved.bOk))
		{
			return nullptr;
		}
		FString Error;
		const bool bValid = FN2CMcpTypeResolver::ValidateVariablePinType(Resolved.PinType, Error);
		if (!TestTrue(FString::Printf(TEXT("%s is a valid variable type before AddMemberVariable (%s; %s)"), *Name.ToString(),
				*N2CTypeResolverSpecPrivate::PinTypeText(Resolved.PinType), *Error), bValid))
		{
			// Never hand a pin type that failed the checks to the engine: it asserts on a mismatched sub-type
			return nullptr;
		}
		UBlueprint* BP = Blueprint.GetBlueprint();
		if (!TestNotNull(TEXT("the transient Blueprint exists"), BP) || !TestTrue(TEXT("the variable is added"), Blueprint.AddMemberVariable(Name, Resolved.PinType)))
		{
			return nullptr;
		}
		FCompilerResultsLog Log;
		Log.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
		TestEqual(FString::Printf(TEXT("%s: the Blueprint compiles with 0 errors"), *Name.ToString()), Log.NumErrors, 0);
		TestNotEqual(FString::Printf(TEXT("%s: the Blueprint is not in error"), *Name.ToString()), static_cast<int32>(BP->Status), static_cast<int32>(BS_Error));
		FProperty* Property = N2CTypeResolverSpecPrivate::CompiledProperty(BP, Name);
		TestNotNull(FString::Printf(TEXT("%s: the compiled class has the property"), *Name.ToString()), Property);
		return Property;
	}

END_DEFINE_SPEC(FN2CTypeResolverSpec)

void FN2CTypeResolverSpec::Define()
{
	using namespace N2CTypeResolverSpecPrivate;

	Describe(TEXT("resolves a struct"), [this]()
	{
		It(TEXT("by bare name, F-prefixed name and path (HitResult, FHitResult, /Script/Engine.HitResult)"), [this]()
		{
			ExpectType(TEXT("HitResult"), Resolve(TEXT("HitResult")), UEdGraphSchema_K2::PC_Struct, HitResult());
			ExpectType(TEXT("FHitResult"), Resolve(TEXT("FHitResult")), UEdGraphSchema_K2::PC_Struct, HitResult());
			ExpectType(TEXT("/Script/Engine.HitResult"), Resolve(TEXT("/Script/Engine.HitResult")), UEdGraphSchema_K2::PC_Struct, HitResult());
			ExpectType(TEXT("struct + HitResult"), Resolve(TEXT("struct"), TEXT("HitResult")), UEdGraphSchema_K2::PC_Struct, HitResult());
		});

		It(TEXT("FGuid as Guid, FGuid and /Script/CoreUObject.Guid"), [this]()
		{
			ExpectType(TEXT("Guid"), Resolve(TEXT("Guid")), UEdGraphSchema_K2::PC_Struct, Guid());
			ExpectType(TEXT("FGuid"), Resolve(TEXT("FGuid")), UEdGraphSchema_K2::PC_Struct, Guid());
			ExpectType(TEXT("/Script/CoreUObject.Guid"), Resolve(TEXT("/Script/CoreUObject.Guid")), UEdGraphSchema_K2::PC_Struct, Guid());
		});

		It(TEXT("the math structs by short name"), [this]()
		{
			ExpectType(TEXT("FVector"), Resolve(TEXT("FVector")), UEdGraphSchema_K2::PC_Struct, TBaseStructure<FVector>::Get());
			ExpectType(TEXT("rotator"), Resolve(TEXT("rotator")), UEdGraphSchema_K2::PC_Struct, TBaseStructure<FRotator>::Get());
		});
	});

	Describe(TEXT("resolves an enum to a byte pin with the enum as sub-type"), [this]()
	{
		It(TEXT("native, by name and by path (ECollisionChannel)"), [this]()
		{
			ExpectType(TEXT("ECollisionChannel"), Resolve(TEXT("ECollisionChannel")), UEdGraphSchema_K2::PC_Byte, CollisionChannel());
			ExpectType(TEXT("/Script/Engine.ECollisionChannel"), Resolve(TEXT("/Script/Engine.ECollisionChannel")), UEdGraphSchema_K2::PC_Byte, CollisionChannel());
			ExpectType(TEXT("enum + ECollisionChannel"), Resolve(TEXT("enum"), TEXT("ECollisionChannel")), UEdGraphSchema_K2::PC_Byte, CollisionChannel());
		});

		It(TEXT("user-defined, by name and by path in a plugin-mounted package"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeUserEnum"), TEXT("/NodeToCode"));
			const FName EnumName = UniqueName(TEXT("E_N2CTypeState"));
			UUserDefinedEnum* Enum = Blueprint.AddUserEnum(EnumName, { TEXT("Idle"), TEXT("Moving") });
			if (!TestNotNull(TEXT("the user enum exists"), Enum))
			{
				return;
			}
			ExpectType(TEXT("user enum name"), Resolve(EnumName.ToString()), UEdGraphSchema_K2::PC_Byte, Enum);
			ExpectType(TEXT("user enum path"), Resolve(Enum->GetPathName()), UEdGraphSchema_K2::PC_Byte, Enum);
		});
	});

	Describe(TEXT("resolves a user struct"), [this]()
	{
		It(TEXT("by name and by path in a plugin-mounted package"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeUserStruct"), TEXT("/NodeToCode"));
			const FName StructName = UniqueName(TEXT("S_N2CTypeLoot"));
			UUserDefinedStruct* Struct = Blueprint.AddUserStruct(StructName, { { TEXT("Count"), N2CTest::FTestBlueprint::IntPinType() } });
			if (!TestNotNull(TEXT("the user struct exists"), Struct))
			{
				return;
			}
			ExpectType(TEXT("user struct name"), Resolve(StructName.ToString()), UEdGraphSchema_K2::PC_Struct, Struct);
			ExpectType(TEXT("user struct path"), Resolve(Struct->GetPathName()), UEdGraphSchema_K2::PC_Struct, Struct);
		});
	});

	Describe(TEXT("resolves a class"), [this]()
	{
		It(TEXT("as an object reference by bare name, A-prefixed name, pointer spelling and path"), [this]()
		{
			ExpectType(TEXT("Actor"), Resolve(TEXT("Actor")), UEdGraphSchema_K2::PC_Object, AActor::StaticClass());
			ExpectType(TEXT("AActor"), Resolve(TEXT("AActor")), UEdGraphSchema_K2::PC_Object, AActor::StaticClass());
			ExpectType(TEXT("AActor*"), Resolve(TEXT("AActor*")), UEdGraphSchema_K2::PC_Object, AActor::StaticClass());
			ExpectType(TEXT("/Script/Engine.Actor"), Resolve(TEXT("/Script/Engine.Actor")), UEdGraphSchema_K2::PC_Object, AActor::StaticClass());
			ExpectType(TEXT("UObject"), Resolve(TEXT("UObject")), UEdGraphSchema_K2::PC_Object, UObject::StaticClass());
		});

		It(TEXT("as class, soft object and soft class references through the generic categories"), [this]()
		{
			ExpectType(TEXT("object + Actor"), Resolve(TEXT("object"), TEXT("Actor")), UEdGraphSchema_K2::PC_Object, AActor::StaticClass());
			ExpectType(TEXT("class + AActor"), Resolve(TEXT("class"), TEXT("AActor")), UEdGraphSchema_K2::PC_Class, AActor::StaticClass());
			ExpectType(TEXT("softobject + /Script/Engine.Actor"), Resolve(TEXT("softobject"), TEXT("/Script/Engine.Actor")), UEdGraphSchema_K2::PC_SoftObject, AActor::StaticClass());
			ExpectType(TEXT("softclass + Actor"), Resolve(TEXT("softclass"), TEXT("Actor")), UEdGraphSchema_K2::PC_SoftClass, AActor::StaticClass());
			ExpectType(TEXT("softobject"), Resolve(TEXT("softobject")), UEdGraphSchema_K2::PC_SoftObject, UObject::StaticClass());
			ExpectType(TEXT("softclass"), Resolve(TEXT("softclass")), UEdGraphSchema_K2::PC_SoftClass, UObject::StaticClass());
		});

		It(TEXT("an interface as an interface reference (IActorSoundParameterInterface)"), [this]()
		{
			UClass* Interface = SoundInterface();
			if (!TestNotNull(TEXT("ActorSoundParameterInterface exists"), Interface))
			{
				return;
			}
			ExpectType(TEXT("IActorSoundParameterInterface"), Resolve(TEXT("IActorSoundParameterInterface")), UEdGraphSchema_K2::PC_Interface, Interface);
			ExpectType(TEXT("interface + ActorSoundParameterInterface"), Resolve(TEXT("interface"), TEXT("ActorSoundParameterInterface")), UEdGraphSchema_K2::PC_Interface, Interface);
			ExpectRefused(TEXT("interface + Actor"), Resolve(TEXT("interface"), TEXT("Actor")), TEXT("INVALID_TYPE"));
		});

		It(TEXT("a Blueprint by path to its generated class, and by its _C name"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeTarget"), TEXT("/NodeToCode"));
			UBlueprint* BP = Blueprint.GetBlueprint();
			if (!TestNotNull(TEXT("the Blueprint exists"), BP) || !TestNotNull(TEXT("it has a generated class"), BP->GeneratedClass.Get()))
			{
				return;
			}
			ExpectType(TEXT("Blueprint asset path"), Resolve(BP->GetPathName()), UEdGraphSchema_K2::PC_Object, BP->GeneratedClass);
			ExpectType(TEXT("generated class path"), Resolve(BP->GeneratedClass->GetPathName()), UEdGraphSchema_K2::PC_Object, BP->GeneratedClass);
		});
	});

	Describe(TEXT("refuses with an error code"), [this]()
	{
		It(TEXT("a struct or enum asked for as an object, class or soft reference (the 0144 crash shape)"), [this]()
		{
			ExpectRefused(TEXT("object + HitResult"), Resolve(TEXT("object"), TEXT("HitResult")), TEXT("INVALID_TYPE"));
			ExpectRefused(TEXT("class + Guid"), Resolve(TEXT("class"), TEXT("Guid")), TEXT("INVALID_TYPE"));
			ExpectRefused(TEXT("softobject + ECollisionChannel"), Resolve(TEXT("softobject"), TEXT("ECollisionChannel")), TEXT("INVALID_TYPE"));
			ExpectRefused(TEXT("struct + Actor"), Resolve(TEXT("struct"), TEXT("Actor")), TEXT("INVALID_TYPE"));
			ExpectRefused(TEXT("enum + HitResult"), Resolve(TEXT("enum"), TEXT("HitResult")), TEXT("INVALID_TYPE"));
		});

		It(TEXT("wildcard, exec, delegate and multicastdelegate"), [this]()
		{
			for (const TCHAR* Type : { TEXT("wildcard"), TEXT("exec"), TEXT("delegate"), TEXT("multicastdelegate"), TEXT("MulticastDelegate") })
			{
				ExpectRefused(Type, Resolve(Type), TEXT("INVALID_TYPE"));
			}
		});

		It(TEXT("C++ template spellings"), [this]()
		{
			ExpectRefused(TEXT("TArray<int32>"), Resolve(TEXT("TArray<int32>")), TEXT("INVALID_TYPE"));
			ExpectRefused(TEXT("TSubclassOf<AActor>"), Resolve(TEXT("TSubclassOf<AActor>")), TEXT("INVALID_TYPE"));
		});

		It(TEXT("unknown names, missing paths and non-type objects"), [this]()
		{
			ExpectRefused(TEXT("unknown name"), Resolve(TEXT("N2CNoSuchType_7f3a")), TEXT("TYPE_NOT_FOUND"));
			ExpectRefused(TEXT("unknown native path"), Resolve(TEXT("/Script/Engine.N2CNoSuchType_7f3a")), TEXT("TYPE_NOT_FOUND"));
			ExpectRefused(TEXT("unknown asset path"), Resolve(TEXT("/Game/N2CNoSuchFolder_7f3a/S_Nope.S_Nope")), TEXT("ASSET_NOT_FOUND"));
			ExpectRefused(TEXT("a class default object"), Resolve(TEXT("/Script/Engine.Default__Actor")), TEXT("INVALID_TYPE"));
			ExpectRefused(TEXT("empty"), Resolve(TEXT("  ")), TEXT("INVALID_TYPE"));
			ExpectRefused(TEXT("a name over the FName limit"), Resolve(FString::ChrN(NAME_SIZE + 10, TEXT('x'))), TEXT("INVALID_TYPE"));
		});

		It(TEXT("a class the editor does not allow as a variable type (LevelScriptActor)"), [this]()
		{
			ExpectRefused(TEXT("/Script/Engine.LevelScriptActor"), Resolve(TEXT("/Script/Engine.LevelScriptActor")), TEXT("INVALID_TYPE"));
		});

		It(TEXT("a C++ prefix that names another kind (FActor is not the class Actor)"), [this]()
		{
			ExpectRefused(TEXT("FActor"), Resolve(TEXT("FActor")), TEXT("TYPE_NOT_FOUND"));
			ExpectRefused(TEXT("AHitResult"), Resolve(TEXT("AHitResult")), TEXT("TYPE_NOT_FOUND"));
		});
	});

	Describe(TEXT("checks containers"), [this]()
	{
		It(TEXT("refuses sets of bool and text, and accepts a set of FGuid"), [this]()
		{
			ExpectRefused(TEXT("set of bool"), Resolve(TEXT("bool"), FString(), TEXT("set")), TEXT("INVALID_SET_ELEMENT_TYPE"));
			ExpectRefused(TEXT("set of text"), Resolve(TEXT("FText"), FString(), TEXT("set")), TEXT("INVALID_SET_ELEMENT_TYPE"));
			ExpectRefused(TEXT("set of HitResult"), Resolve(TEXT("HitResult"), FString(), TEXT("set")), TEXT("INVALID_SET_ELEMENT_TYPE"));
			const FResolved GuidSet = Resolve(TEXT("Guid"), FString(), TEXT("set"));
			ExpectType(TEXT("set of Guid"), GuidSet, UEdGraphSchema_K2::PC_Struct, Guid());
			TestTrue(TEXT("set of Guid is a set"), GuidSet.PinType.ContainerType == EPinContainerType::Set);
		});

		It(TEXT("accepts an FGuid map key and refuses bool and HitResult keys"), [this]()
		{
			const FResolved GuidMap = Resolve(TEXT("int32"), FString(), TEXT("map"), TEXT("/Script/CoreUObject.Guid"));
			ExpectType(TEXT("map Guid -> int32 (key)"), GuidMap, UEdGraphSchema_K2::PC_Struct, Guid());
			TestEqual(TEXT("map value category"), GuidMap.PinType.PinValueType.TerminalCategory, UEdGraphSchema_K2::PC_Int);
			ExpectRefused(TEXT("map bool -> int32"), Resolve(TEXT("int32"), FString(), TEXT("map"), TEXT("bool")), TEXT("INVALID_MAP_KEY_TYPE"));
			ExpectRefused(TEXT("map HitResult -> int32"), Resolve(TEXT("int32"), FString(), TEXT("map"), TEXT("HitResult")), TEXT("INVALID_MAP_KEY_TYPE"));
			ExpectRefused(TEXT("map int32 -> delegate"), Resolve(TEXT("delegate"), FString(), TEXT("map"), TEXT("int32")), TEXT("INVALID_TYPE"));

			FString Error;
			TestTrue(TEXT("ValidateContainerTypeParameters accepts a Guid map key"), FN2CMcpVariableUtils::ValidateContainerTypeParameters(TEXT("map"), TEXT("Guid"), Error));
			TestFalse(TEXT("ValidateContainerTypeParameters refuses a bool map key"), FN2CMcpVariableUtils::ValidateContainerTypeParameters(TEXT("map"), TEXT("bool"), Error));
		});

		It(TEXT("resolves an array of a struct by bare name"), [this]()
		{
			const FResolved HitArray = Resolve(TEXT("HitResult"), FString(), TEXT("array"));
			ExpectType(TEXT("array of HitResult"), HitArray, UEdGraphSchema_K2::PC_Struct, HitResult());
			TestTrue(TEXT("array of HitResult is an array"), HitArray.PinType.ContainerType == EPinContainerType::Array);
		});
	});

	Describe(TEXT("ValidateVariablePinType"), [this]()
	{
		It(TEXT("refuses an object pin whose sub-type is a struct, and the enum category"), [this]()
		{
			FEdGraphPinType Crash;
			Crash.PinCategory = UEdGraphSchema_K2::PC_Object;
			Crash.PinSubCategoryObject = HitResult();
			FString Error;
			TestFalse(TEXT("object pin with the HitResult struct as sub-type"), FN2CMcpTypeResolver::ValidateVariablePinType(Crash, Error));

			FEdGraphPinType EnumCategory;
			EnumCategory.PinCategory = UEdGraphSchema_K2::PC_Enum;
			EnumCategory.PinSubCategoryObject = CollisionChannel();
			TestFalse(TEXT("enum category"), FN2CMcpTypeResolver::ValidateVariablePinType(EnumCategory, Error));
		});
	});

	Describe(TEXT("compiles a member variable of the resolved type"), [this]()
	{
		It(TEXT("HitResult to a struct property"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeHit"));
			const FProperty* Property = CompileVariable(Blueprint, TEXT("Hit"), Resolve(TEXT("HitResult")));
			TestTrue(TEXT("FStructProperty of FHitResult"), Property && StructOf(Property) == HitResult());
		});

		It(TEXT("/Script/Engine.ECollisionChannel and ECollisionChannel to enum byte properties"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeChannel"));
			const FProperty* ByPath = CompileVariable(Blueprint, TEXT("ChannelByPath"), Resolve(TEXT("/Script/Engine.ECollisionChannel")));
			TestTrue(TEXT("by path: an enum property of ECollisionChannel"), ByPath && EnumOf(ByPath) == CollisionChannel());
			const FProperty* ByName = CompileVariable(Blueprint, TEXT("ChannelByName"), Resolve(TEXT("ECollisionChannel")));
			TestTrue(TEXT("by name: an enum property of ECollisionChannel"), ByName && EnumOf(ByName) == CollisionChannel());
		});

		It(TEXT("Guid, AActor and a class reference to the right properties"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeMixed"));
			const FProperty* GuidProperty = CompileVariable(Blueprint, TEXT("Id"), Resolve(TEXT("Guid")));
			TestTrue(TEXT("FStructProperty of FGuid"), GuidProperty && StructOf(GuidProperty) == Guid());
			const FProperty* ActorProperty = CompileVariable(Blueprint, TEXT("Target"), Resolve(TEXT("AActor")));
			const FObjectProperty* Object = CastField<FObjectProperty>(ActorProperty);
			TestTrue(TEXT("FObjectProperty of AActor"), Object && !ActorProperty->IsA<FClassProperty>() && Object->PropertyClass == AActor::StaticClass());
			const FClassProperty* Class = CastField<FClassProperty>(CompileVariable(Blueprint, TEXT("TargetClass"), Resolve(TEXT("class"), TEXT("Actor"))));
			TestTrue(TEXT("FClassProperty with AActor as meta class"), Class && Class->MetaClass == AActor::StaticClass());
		});

		It(TEXT("a user struct and a user enum from a plugin-mounted package"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeUser"), TEXT("/NodeToCode"));
			UUserDefinedStruct* Struct = Blueprint.AddUserStruct(UniqueName(TEXT("S_N2CTypeData")), { { TEXT("Count"), N2CTest::FTestBlueprint::IntPinType() } });
			UUserDefinedEnum* Enum = Blueprint.AddUserEnum(UniqueName(TEXT("E_N2CTypeMode")), { TEXT("A"), TEXT("B") });
			if (!TestNotNull(TEXT("the user struct exists"), Struct) || !TestNotNull(TEXT("the user enum exists"), Enum))
			{
				return;
			}
			const FProperty* StructProperty = CompileVariable(Blueprint, TEXT("Data"), Resolve(Struct->GetName()));
			TestTrue(TEXT("FStructProperty of the user struct"), StructProperty && StructOf(StructProperty) == Struct);
			const FProperty* EnumProperty = CompileVariable(Blueprint, TEXT("Mode"), Resolve(Enum->GetPathName()));
			TestTrue(TEXT("an enum property of the user enum"), EnumProperty && EnumOf(EnumProperty) == Enum);
		});

		It(TEXT("a set of FGuid and a map keyed by FGuid"), [this]()
		{
			N2CTest::FTestBlueprint Blueprint(TEXT("BP_N2CTypeContainers"));
			const FSetProperty* Set = CastField<FSetProperty>(CompileVariable(Blueprint, TEXT("Ids"), Resolve(TEXT("FGuid"), FString(), TEXT("set"))));
			TestTrue(TEXT("FSetProperty of FGuid"), Set && StructOf(Set->ElementProp) == Guid());
			const FMapProperty* Map = CastField<FMapProperty>(CompileVariable(Blueprint, TEXT("Scores"), Resolve(TEXT("int32"), FString(), TEXT("map"), TEXT("Guid"))));
			TestTrue(TEXT("FMapProperty keyed by FGuid with int32 values"), Map && StructOf(Map->KeyProp) == Guid() && Map->ValueProp->IsA<FIntProperty>());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
