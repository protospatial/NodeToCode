// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "UObject/Interface.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

namespace N2CMcpTypeResolverPrivate
{
	/** What an identifier must name: anything (Any), or one kind, as a generic category or a C++ prefix asks */
	enum class EKind : uint8
	{
		Any,
		Object,
		Class,
		SoftObject,
		SoftClass,
		Interface,
		Struct,
		Enum
	};

	static bool IsClassKind(EKind Kind)
	{
		return Kind == EKind::Object || Kind == EKind::Class || Kind == EKind::SoftObject || Kind == EKind::SoftClass
			|| Kind == EKind::Interface;
	}

	static const TCHAR* KindName(EKind Kind)
	{
		switch (Kind)
		{
		case EKind::Object: return TEXT("object");
		case EKind::Class: return TEXT("class");
		case EKind::SoftObject: return TEXT("softobject");
		case EKind::SoftClass: return TEXT("softclass");
		case EKind::Interface: return TEXT("interface");
		case EKind::Struct: return TEXT("struct");
		case EKind::Enum: return TEXT("enum");
		default: return TEXT("type");
		}
	}

	static bool ParseGenericCategory(const FString& Identifier, EKind& OutKind)
	{
		static const TPair<const TCHAR*, EKind> Categories[] = {
			{TEXT("object"), EKind::Object}, {TEXT("class"), EKind::Class}, {TEXT("softobject"), EKind::SoftObject},
			{TEXT("softclass"), EKind::SoftClass}, {TEXT("interface"), EKind::Interface}, {TEXT("struct"), EKind::Struct},
			{TEXT("enum"), EKind::Enum}};
		for (const TPair<const TCHAR*, EKind>& Category : Categories)
		{
			if (Identifier.Equals(Category.Key, ESearchCase::IgnoreCase))
			{
				OutKind = Category.Value;
				return true;
			}
		}
		return false;
	}

	static bool ResolvePrimitive(const FString& TypeIdentifier, FEdGraphPinType& OutPinType)
	{
		const FString LowerType = TypeIdentifier.ToLower();
		if (LowerType == TEXT("bool") || LowerType == TEXT("boolean")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		else if (LowerType == TEXT("byte") || LowerType == TEXT("uint8")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		else if (LowerType == TEXT("int") || LowerType == TEXT("int32")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		else if (LowerType == TEXT("int64")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		else if (LowerType == TEXT("float")) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real; OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float; }
		else if (LowerType == TEXT("double")) { OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real; OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double; }
		else if (LowerType == TEXT("string") || LowerType == TEXT("fstring")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
		else if (LowerType == TEXT("text") || LowerType == TEXT("ftext")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		else if (LowerType == TEXT("name") || LowerType == TEXT("fname")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		else return false;
		return true;
	}

	static bool ResolveMathStruct(const FString& TypeIdentifier, FEdGraphPinType& OutPinType)
	{
		const FString LowerType = TypeIdentifier.ToLower();
		UScriptStruct* Struct = nullptr;
		if (LowerType == TEXT("vector") || LowerType == TEXT("vector3") || LowerType == TEXT("fvector")) Struct = TBaseStructure<FVector>::Get();
		else if (LowerType == TEXT("vector2d") || LowerType == TEXT("fvector2d")) Struct = TBaseStructure<FVector2D>::Get();
		else if (LowerType == TEXT("vector4") || LowerType == TEXT("fvector4")) Struct = TBaseStructure<FVector4>::Get();
		else if (LowerType == TEXT("rotator") || LowerType == TEXT("frotator")) Struct = TBaseStructure<FRotator>::Get();
		else if (LowerType == TEXT("transform") || LowerType == TEXT("ftransform")) Struct = TBaseStructure<FTransform>::Get();
		else if (LowerType == TEXT("quat") || LowerType == TEXT("fquat")) Struct = TBaseStructure<FQuat>::Get();
		else if (LowerType == TEXT("color") || LowerType == TEXT("fcolor")) Struct = TBaseStructure<FColor>::Get();
		else if (LowerType == TEXT("linearcolor") || LowerType == TEXT("flinearcolor")) Struct = TBaseStructure<FLinearColor>::Get();
		if (!Struct)
		{
			return false;
		}
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = Struct;
		return true;
	}

	/** Pins a Blueprint variable can never have; the reason is the error text */
	static bool IsRefusedSpecialType(const FString& TypeIdentifier, FString& OutErrorMsg)
	{
		const FString LowerType = TypeIdentifier.ToLower();
		if (LowerType == TEXT("wildcard"))
		{
			OutErrorMsg = TEXT("INVALID_TYPE: 'wildcard' is not a variable or parameter type; wildcard pins only exist on engine nodes and macros.");
			return true;
		}
		if (LowerType == TEXT("exec"))
		{
			OutErrorMsg = TEXT("INVALID_TYPE: 'exec' is not a variable or parameter type.");
			return true;
		}
		if (LowerType == TEXT("delegate") || LowerType == TEXT("multicastdelegate"))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' cannot be created from a type name: a delegate needs a signature, which these tools cannot give it. Create event dispatchers in the Blueprint editor."), *TypeIdentifier);
			return true;
		}
		return false;
	}

	/**
	 * Fills OutPinType from a class, struct or enum (a Blueprint stands for its generated class), checking that it is
	 * the kind asked for and that the engine allows it as a Blueprint variable type
	 */
	static bool ApplyTypeObject(UObject* TypeObject, EKind Kind, const FString& Identifier, FEdGraphPinType& OutPinType, FString& OutErrorMsg)
	{
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(TypeObject))
		{
			TypeObject = Blueprint->GeneratedClass;
			if (!TypeObject)
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: Blueprint '%s' has no generated class; compile it first."), *Blueprint->GetPathName());
				return false;
			}
		}

		if (UClass* Class = Cast<UClass>(TypeObject))
		{
			if (Kind == EKind::Struct || Kind == EKind::Enum)
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' is a class (%s), not a %s."), *Identifier, *Class->GetPathName(), KindName(Kind));
				return false;
			}
			if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Class))
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: class '%s' (%s) cannot be a Blueprint variable type (it is not a BlueprintType, or it is a skeleton, level script or macro library class)."), *Identifier, *Class->GetPathName());
				return false;
			}
			const bool bIsInterface = Class->IsChildOf(UInterface::StaticClass()) || Class->HasAnyClassFlags(CLASS_Interface);
			if (bIsInterface && Kind != EKind::Any && Kind != EKind::Object && Kind != EKind::Interface)
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' is an interface; Blueprints can only hold an interface reference, not a %s reference to it."), *Identifier, KindName(Kind));
				return false;
			}
			if (!bIsInterface && Kind == EKind::Interface)
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' (%s) is not an interface."), *Identifier, *Class->GetPathName());
				return false;
			}
			switch (Kind)
			{
			case EKind::Class: OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class; break;
			case EKind::SoftObject: OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject; break;
			case EKind::SoftClass: OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass; break;
			default: OutPinType.PinCategory = bIsInterface ? UEdGraphSchema_K2::PC_Interface : UEdGraphSchema_K2::PC_Object; break;
			}
			OutPinType.PinSubCategoryObject = Class;
			return true;
		}

		if (UScriptStruct* Struct = Cast<UScriptStruct>(TypeObject))
		{
			if (Kind != EKind::Any && Kind != EKind::Struct)
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' is a struct (%s), not a %s; pass it without a category to get a struct variable."), *Identifier, *Struct->GetPathName(), KindName(Kind));
				return false;
			}
			if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: struct '%s' (%s) cannot be a Blueprint variable type (it is not a BlueprintType, is internal-only, or is a user struct that is not up to date)."), *Identifier, *Struct->GetPathName());
				return false;
			}
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = Struct;
			return true;
		}

		if (UEnum* Enum = Cast<UEnum>(TypeObject))
		{
			if (Kind != EKind::Any && Kind != EKind::Enum)
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' is an enum (%s), not a %s; pass it without a category to get an enum variable."), *Identifier, *Enum->GetPathName(), KindName(Kind));
				return false;
			}
			if (!UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: enum '%s' (%s) cannot be a Blueprint variable type (it is not a BlueprintType)."), *Identifier, *Enum->GetPathName());
				return false;
			}
			// The engine's convention for every Blueprint enum, native or user-defined: a byte pin with the enum as
			// sub-type. The compiler makes an enum property only from PC_Byte (PC_Enum compiles to int32).
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutPinType.PinSubCategoryObject = Enum;
			return true;
		}

		OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' is a %s, not a class, struct or enum."), *Identifier,
			TypeObject ? *TypeObject->GetClass()->GetName() : TEXT("null object"));
		return false;
	}

	static IAssetRegistry& AssetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}

	/** Asset classes that hold a type: Blueprints (their generated class), user structs and user enums */
	static bool IsTypeAssetClass(const FTopLevelAssetPath& ClassPath)
	{
		const UClass* AssetClass = FindObject<UClass>(ClassPath);
		return AssetClass && (AssetClass->IsChildOf(UBlueprint::StaticClass()) || AssetClass->IsChildOf(UScriptStruct::StaticClass())
			|| AssetClass->IsChildOf(UEnum::StaticClass()));
	}

	/** A path starting with '/': the object if loaded, else the asset registry decides whether it is a type to load */
	static bool ResolvePath(const FString& Path, EKind Kind, FEdGraphPinType& OutPinType, FString& OutErrorMsg)
	{
		FString ObjectPath = Path;
		if (!ObjectPath.Contains(TEXT(".")))
		{
			// /Game/Data/S_Loot means the asset of that name in that package
			ObjectPath = FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetShortName(Path));
		}
		if (!FPackageName::IsValidObjectPath(ObjectPath))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE_PATH: '%s' is not a valid object path (expected /Package/Path.ObjectName, for example /Script/Engine.HitResult)."), *Path);
			return false;
		}

		UObject* TypeObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (!TypeObject)
		{
			if (FPackageName::IsScriptPackage(ObjectPath))
			{
				OutErrorMsg = FString::Printf(TEXT("TYPE_NOT_FOUND: no native type at '%s'."), *Path);
				return false;
			}

			IAssetRegistry& Registry = AssetRegistry();
			FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
			if (!AssetData.IsValid() && ObjectPath.EndsWith(TEXT("_C")))
			{
				// /Game/BP_Enemy.BP_Enemy_C: the registry knows the Blueprint asset, not its generated class
				AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath.LeftChop(2)));
			}
			if (!AssetData.IsValid())
			{
				OutErrorMsg = FString::Printf(TEXT("ASSET_NOT_FOUND: no asset at '%s'."), *Path);
				return false;
			}
			if (!IsTypeAssetClass(AssetData.AssetClassPath))
			{
				OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE_AT_PATH: '%s' is a %s asset, not a class, struct or enum."), *Path, *AssetData.AssetClassPath.GetAssetName().ToString());
				return false;
			}
			TypeObject = AssetData.GetAsset();
			if (!TypeObject)
			{
				OutErrorMsg = FString::Printf(TEXT("ASSET_NOT_FOUND: could not load '%s'."), *Path);
				return false;
			}
		}
		return ApplyTypeObject(TypeObject, Kind, Path, OutPinType, OutErrorMsg);
	}

	/** The kind a C++ prefix names (F struct, U/A/I class, E enum), or false when Name does not start with one */
	static bool PrefixKind(const FString& Name, EKind& OutKind)
	{
		if (Name.Len() < 2 || !FChar::IsUpper(Name[1]))
		{
			return false;
		}
		switch (Name[0])
		{
		case TEXT('F'): OutKind = EKind::Struct; return true;
		case TEXT('U'): case TEXT('A'): OutKind = EKind::Object; return true;
		case TEXT('I'): OutKind = EKind::Interface; return true;
		case TEXT('E'): OutKind = EKind::Enum; return true;
		default: return false;
		}
	}

	/** Loaded classes, structs and enums named Name that Kind admits (native types first, no object walk) */
	static TArray<UObject*> FindLoadedTypes(const FString& Name, EKind Kind)
	{
		TArray<UObject*> Found;
		const EFindFirstObjectOptions Options = EFindFirstObjectOptions::NativeFirst;
		if (Kind == EKind::Any || IsClassKind(Kind))
		{
			if (UClass* Class = FindFirstObject<UClass>(*Name, Options))
			{
				Found.Add(Class);
			}
		}
		if (Kind == EKind::Any || Kind == EKind::Struct)
		{
			if (UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*Name, Options))
			{
				Found.Add(Struct);
			}
		}
		if (Kind == EKind::Any || Kind == EKind::Enum)
		{
			if (UEnum* Enum = FindFirstObject<UEnum>(*Name, Options))
			{
				Found.Add(Enum);
			}
		}
		return Found;
	}

	/** Unloaded user types named Name: Blueprints (Name or Name_C), user structs and user enums Kind admits */
	static TArray<FAssetData> FindTypeAssets(const FString& Name, EKind Kind)
	{
		FARFilter Filter;
		Filter.bRecursiveClasses = true;
		if (Kind == EKind::Any || IsClassKind(Kind))
		{
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		}
		if (Kind == EKind::Any || Kind == EKind::Struct)
		{
			Filter.ClassPaths.Add(UUserDefinedStruct::StaticClass()->GetClassPathName());
		}
		if (Kind == EKind::Any || Kind == EKind::Enum)
		{
			Filter.ClassPaths.Add(UUserDefinedEnum::StaticClass()->GetClassPathName());
		}

		const FName AssetName(Name.EndsWith(TEXT("_C")) ? Name.LeftChop(2) : Name);
		TArray<FAssetData> Matches;
		AssetRegistry().EnumerateAssets(Filter, [&Matches, AssetName](const FAssetData& AssetData)
		{
			if (AssetData.AssetName == AssetName)
			{
				Matches.Add(AssetData);
			}
			return true;
		});
		return Matches;
	}

	/** A short type name: loaded types first, then the asset registry; a C++ prefix narrows the kind */
	static bool ResolveShortName(const FString& Name, EKind Kind, FEdGraphPinType& OutPinType, FString& OutErrorMsg)
	{
		if (Name.Contains(TEXT("<")) || Name.Contains(TEXT(">")))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s': C++ template spellings (TArray<...>, TSet<...>, TMap<...>, TSubclassOf<...>, TSoftObjectPtr<...>) are not accepted. Pass the element type and set containerType for containers; 'softobject' and 'softclass' give soft references to UObject."), *Name);
			return false;
		}
		if (Name.Len() >= NAME_SIZE || !FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: '%s' is not a type name. Pass a type name (HitResult, Actor), a path (/Script/Engine.HitResult) or a primitive (bool, int32, float, FString). C++ template spellings such as TArray<...> or TSubclassOf<...> are not accepted: containers go in containerType."), *Name);
			return false;
		}

		TArray<UObject*> Found = FindLoadedTypes(Name, Kind);
		EKind StrippedKind = EKind::Any;
		if (Found.Num() == 0 && PrefixKind(Name, StrippedKind))
		{
			// FHitResult -> struct HitResult, AActor -> class Actor, IMyInterface -> interface MyInterface. The prefix
			// only names its own kind, and must agree with a kind the caller asked for
			const bool bAgrees = Kind == EKind::Any || Kind == StrippedKind || (IsClassKind(Kind) && IsClassKind(StrippedKind));
			if (bAgrees)
			{
				const EKind SearchKind = Kind == EKind::Any ? StrippedKind : Kind;
				TArray<UObject*> StrippedFound = FindLoadedTypes(Name.RightChop(1), SearchKind);
				for (UObject* Object : StrippedFound)
				{
					// The stripped name must be the kind the prefix names
					const bool bMatches = (StrippedKind == EKind::Struct && Object->IsA<UScriptStruct>())
						|| (StrippedKind == EKind::Enum && Object->IsA<UEnum>())
						|| (IsClassKind(StrippedKind) && Object->IsA<UClass>());
					if (bMatches)
					{
						Found.Add(Object);
					}
				}
				if (Found.Num() > 0 && Kind == EKind::Any && StrippedKind == EKind::Interface)
				{
					Kind = EKind::Interface;
				}
			}
		}

		if (Found.Num() > 1)
		{
			TArray<FString> Paths;
			for (const UObject* Object : Found)
			{
				Paths.Add(Object->GetPathName());
			}
			OutErrorMsg = FString::Printf(TEXT("AMBIGUOUS_TYPE: '%s' names more than one type (%s); pass the full path."), *Name, *FString::Join(Paths, TEXT(", ")));
			return false;
		}
		if (Found.Num() == 1)
		{
			return ApplyTypeObject(Found[0], Kind, Name, OutPinType, OutErrorMsg);
		}

		const TArray<FAssetData> Assets = FindTypeAssets(Name, Kind);
		if (Assets.Num() > 1)
		{
			TArray<FString> Paths;
			for (const FAssetData& AssetData : Assets)
			{
				Paths.Add(AssetData.GetObjectPathString());
			}
			OutErrorMsg = FString::Printf(TEXT("AMBIGUOUS_TYPE: '%s' names more than one asset (%s); pass the full path."), *Name, *FString::Join(Paths, TEXT(", ")));
			return false;
		}
		if (Assets.Num() == 1)
		{
			UObject* Asset = Assets[0].GetAsset();
			if (!Asset)
			{
				OutErrorMsg = FString::Printf(TEXT("ASSET_NOT_FOUND: could not load '%s'."), *Assets[0].GetObjectPathString());
				return false;
			}
			return ApplyTypeObject(Asset, Kind, Name, OutPinType, OutErrorMsg);
		}

		if (Kind != EKind::Any)
		{
			// The name may be a type of another kind (object + HitResult): say what it is instead of "unknown"
			const TArray<UObject*> OtherKinds = FindLoadedTypes(Name, EKind::Any);
			if (OtherKinds.Num() == 1)
			{
				return ApplyTypeObject(OtherKinds[0], Kind, Name, OutPinType, OutErrorMsg);
			}
		}

		OutErrorMsg = FString::Printf(TEXT("TYPE_NOT_FOUND: Unknown %s '%s'. Use search-variable-types to find a type and pass its typeIdentifier."), KindName(Kind), *Name);
		return false;
	}

	static bool ResolveNameOrPath(const FString& Identifier, EKind Kind, FEdGraphPinType& OutPinType, FString& OutErrorMsg)
	{
		if (Identifier.StartsWith(TEXT("/")))
		{
			return ResolvePath(Identifier, Kind, OutPinType, OutErrorMsg);
		}
		return ResolveShortName(Identifier, Kind, OutPinType, OutErrorMsg);
	}

	/** One non-container type: TypeIdentifier, with SubTypeIdentifier for the generic categories */
	static bool ResolveTerminal(const FString& InTypeIdentifier, const FString& InSubTypeIdentifier, FEdGraphPinType& OutPinType, FString& OutErrorMsg)
	{
		FString TypeIdentifier = InTypeIdentifier.TrimStartAndEnd();
		const FString SubTypeIdentifier = InSubTypeIdentifier.TrimStartAndEnd();
		if (TypeIdentifier.IsEmpty())
		{
			OutErrorMsg = TEXT("INVALID_TYPE: the type identifier is empty.");
			return false;
		}

		EKind Kind = EKind::Any;
		if (ParseGenericCategory(TypeIdentifier, Kind))
		{
			if (SubTypeIdentifier.IsEmpty())
			{
				if (Kind == EKind::SoftObject || Kind == EKind::SoftClass)
				{
					// A soft reference with no class named: TSoftObjectPtr<UObject> / TSoftClassPtr<UObject>
					OutPinType.PinCategory = Kind == EKind::SoftObject ? UEdGraphSchema_K2::PC_SoftObject : UEdGraphSchema_K2::PC_SoftClass;
					OutPinType.PinSubCategoryObject = UObject::StaticClass();
					return true;
				}
				OutErrorMsg = FString::Printf(TEXT("INVALID_SUBTYPE: Type '%s' requires a SubTypeIdentifier."), *TypeIdentifier);
				return false;
			}
			return ResolveNameOrPath(SubTypeIdentifier, Kind, OutPinType, OutErrorMsg);
		}

		if (TypeIdentifier.StartsWith(TEXT("/")))
		{
			return ResolvePath(TypeIdentifier, EKind::Any, OutPinType, OutErrorMsg);
		}
		if (ResolvePrimitive(TypeIdentifier, OutPinType) || ResolveMathStruct(TypeIdentifier, OutPinType))
		{
			return true;
		}
		if (IsRefusedSpecialType(TypeIdentifier, OutErrorMsg))
		{
			return false;
		}
		if (TypeIdentifier.EndsWith(TEXT("*")))
		{
			// AActor* is an object reference to Actor
			TypeIdentifier = TypeIdentifier.LeftChop(1).TrimEnd();
			return ResolveShortName(TypeIdentifier, EKind::Object, OutPinType, OutErrorMsg);
		}
		return ResolveShortName(TypeIdentifier, EKind::Any, OutPinType, OutErrorMsg);
	}

	/** The checks of ValidateVariablePinType for one terminal type (a container's element, a map's key or value) */
	static bool ValidateTerminal(const FName Category, const UObject* SubCategoryObject, const TCHAR* Role, FString& OutErrorMsg)
	{
		if (Category.IsNone() || Category == UEdGraphSchema_K2::PC_Wildcard || Category == UEdGraphSchema_K2::PC_Exec
			|| Category == UEdGraphSchema_K2::PC_Delegate || Category == UEdGraphSchema_K2::PC_MCDelegate)
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: the %s category '%s' is not a variable type."), Role, *Category.ToString());
			return false;
		}
		if (Category == UEdGraphSchema_K2::PC_Enum)
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: the %s uses the enum pin category; Blueprint enum variables are byte pins with the enum as sub-type."), Role);
			return false;
		}
		const bool bNeedsClass = Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Interface
			|| Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftObject
			|| Category == UEdGraphSchema_K2::PC_SoftClass;
		if (bNeedsClass && !Cast<UClass>(SubCategoryObject))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: the %s is a %s reference whose sub-type is %s, not a class."), Role, *Category.ToString(),
				SubCategoryObject ? *SubCategoryObject->GetPathName() : TEXT("missing"));
			return false;
		}
		if (Category == UEdGraphSchema_K2::PC_Struct && !Cast<UScriptStruct>(SubCategoryObject))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: the %s is a struct whose sub-type is %s, not a struct."), Role,
				SubCategoryObject ? *SubCategoryObject->GetPathName() : TEXT("missing"));
			return false;
		}
		if (Category == UEdGraphSchema_K2::PC_Byte && SubCategoryObject && !Cast<UEnum>(SubCategoryObject))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE: the %s is a byte whose sub-type %s is not an enum."), Role, *SubCategoryObject->GetPathName());
			return false;
		}
		return true;
	}
}

bool FN2CMcpTypeResolver::ValidateVariablePinType(const FEdGraphPinType& PinType, FString& OutErrorMsg)
{
	using namespace N2CMcpTypeResolverPrivate;

	const bool bIsMap = PinType.ContainerType == EPinContainerType::Map;
	const TCHAR* MainRole = bIsMap ? TEXT("map key") : (PinType.ContainerType == EPinContainerType::Set ? TEXT("set element") : TEXT("type"));
	if (!ValidateTerminal(PinType.PinCategory, PinType.PinSubCategoryObject.Get(), MainRole, OutErrorMsg))
	{
		return false;
	}
	if (bIsMap)
	{
		const FEdGraphTerminalType& Value = PinType.PinValueType;
		if (!ValidateTerminal(Value.TerminalCategory, Value.TerminalSubCategoryObject.Get(), TEXT("map value"), OutErrorMsg))
		{
			return false;
		}
	}
	if ((bIsMap || PinType.ContainerType == EPinContainerType::Set) && !FBlueprintEditorUtils::HasGetTypeHash(PinType))
	{
		const UObject* SubType = PinType.PinSubCategoryObject.Get();
		OutErrorMsg = FString::Printf(TEXT("%s: %s cannot be a %s in Blueprints (it has no hash function)."),
			bIsMap ? TEXT("INVALID_MAP_KEY_TYPE") : TEXT("INVALID_SET_ELEMENT_TYPE"),
			SubType ? *SubType->GetPathName() : *PinType.PinCategory.ToString(), MainRole);
		return false;
	}
	return true;
}

bool FN2CMcpTypeResolver::ResolvePinType(
	const FString& TypeIdentifier,
	const FString& SubTypeIdentifier,
	const FString& ContainerTypeStr,
	const FString& KeyTypeIdentifierStr,
	bool bIsReference,
	bool bIsConst,
	FEdGraphPinType& OutPinType,
	FString& OutErrorMsg)
{
	using namespace N2CMcpTypeResolverPrivate;

	OutPinType.ResetToDefaults();
	FEdGraphPinType Terminal;
	if (!ResolveTerminal(TypeIdentifier, SubTypeIdentifier, Terminal, OutErrorMsg))
	{
		return false;
	}

	const FString Container = ContainerTypeStr.TrimStartAndEnd();
	if (Container.IsEmpty() || Container.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		OutPinType = Terminal;
	}
	else if (Container.Equals(TEXT("array"), ESearchCase::IgnoreCase))
	{
		OutPinType = Terminal;
		OutPinType.ContainerType = EPinContainerType::Array;
	}
	else if (Container.Equals(TEXT("set"), ESearchCase::IgnoreCase))
	{
		OutPinType = Terminal;
		OutPinType.ContainerType = EPinContainerType::Set;
	}
	else if (Container.Equals(TEXT("map"), ESearchCase::IgnoreCase))
	{
		if (KeyTypeIdentifierStr.TrimStartAndEnd().IsEmpty())
		{
			OutErrorMsg = TEXT("INVALID_MAP_KEY_TYPE: Map container specified but KeyTypeIdentifier is empty.");
			return false;
		}
		FEdGraphPinType Key;
		FString KeyError;
		if (!ResolveTerminal(KeyTypeIdentifierStr, TEXT(""), Key, KeyError))
		{
			OutErrorMsg = FString::Printf(TEXT("INVALID_MAP_KEY_TYPE: Failed to resolve map key type '%s'. Error: %s"), *KeyTypeIdentifierStr, *KeyError);
			return false;
		}

		// For a map the main fields describe the KEY and PinValueType the VALUE, as in the editor's variable types
		OutPinType = Key;
		OutPinType.ContainerType = EPinContainerType::Map;
		OutPinType.PinValueType.TerminalCategory = Terminal.PinCategory;
		OutPinType.PinValueType.TerminalSubCategory = Terminal.PinSubCategory;
		OutPinType.PinValueType.TerminalSubCategoryObject = Terminal.PinSubCategoryObject;
		OutPinType.PinValueType.bTerminalIsConst = Terminal.bIsConst;
		OutPinType.PinValueType.bTerminalIsWeakPointer = Terminal.bIsWeakPointer;
	}
	else
	{
		OutErrorMsg = FString::Printf(TEXT("INVALID_CONTAINER_TYPE: Unknown container type '%s'"), *ContainerTypeStr);
		return false;
	}

	OutPinType.bIsReference = bIsReference;
	OutPinType.bIsConst = bIsConst;

	// Every result passes the same checks the engine's variable code relies on, so a mismatched sub-type is an error
	// here instead of an assertion in FBlueprintEditorUtils::AddMemberVariable
	if (!ValidateVariablePinType(OutPinType, OutErrorMsg))
	{
		OutPinType.ResetToDefaults();
		return false;
	}
	return true;
}
