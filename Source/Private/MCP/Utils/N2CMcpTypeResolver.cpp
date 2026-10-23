// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "EdGraphSchema_K2.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h" // For FProperty, FStructProperty etc.
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utils/N2CLogger.h"

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
    OutPinType.ResetToDefaults(); // Ensure clean state

    // 1. Handle full object paths first (e.g., /Script/CoreUObject.Vector)
    if (TypeIdentifier.StartsWith(TEXT("/Script/")) || TypeIdentifier.StartsWith(TEXT("/Game/")) || TypeIdentifier.StartsWith(TEXT("/Plugin/")))
    {
        if (!ResolveFullObjectPathInternal(TypeIdentifier, OutPinType, OutErrorMsg))
        {
            return false;
        }
    }
    // 2. Handle generic type categories (object, class, struct, enum) which require SubTypeIdentifier
    else if (TypeIdentifier.Equals(TEXT("object"), ESearchCase::IgnoreCase) ||
             TypeIdentifier.Equals(TEXT("class"), ESearchCase::IgnoreCase) ||
             TypeIdentifier.Equals(TEXT("struct"), ESearchCase::IgnoreCase) ||
             TypeIdentifier.Equals(TEXT("enum"), ESearchCase::IgnoreCase) ||
             TypeIdentifier.Equals(TEXT("interface"), ESearchCase::IgnoreCase))
    {
        if (SubTypeIdentifier.IsEmpty())
        {
            OutErrorMsg = FString::Printf(TEXT("INVALID_SUBTYPE: Type '%s' requires a SubTypeIdentifier."), *TypeIdentifier);
            return false;
        }
        if (!ResolveObjectLikeTypeInternal(TypeIdentifier, SubTypeIdentifier, OutPinType, OutErrorMsg))
        {
            return false;
        }
    }
    // 3. Handle named primitive, math, and special types
    else
    {
        if (!ResolvePrimitiveTypeInternal(TypeIdentifier, OutPinType) &&
            !ResolveMathTypeInternal(TypeIdentifier, OutPinType) &&
            !ResolveSpecialTypeInternal(TypeIdentifier, OutPinType))
        {
            // If none of the above, try to resolve as an object-like type using TypeIdentifier as SubTypeIdentifier
            // This covers cases where "FVector" or "MyActor" is passed directly as TypeIdentifier
            if (!ResolveObjectLikeTypeInternal(TEXT("object"), TypeIdentifier, OutPinType, OutErrorMsg)) // Default to 'object' category
            {
                 // Final fallback: if it's not a known primitive/math/special and not resolvable as an object path
                OutErrorMsg = FString::Printf(TEXT("INVALID_PARAMETER_TYPE: Unknown type '%s'"), *TypeIdentifier);
                return false;
            }
        }
    }

    // 4. Apply container type
    if (!ContainerTypeStr.IsEmpty() && !ContainerTypeStr.Equals(TEXT("none"), ESearchCase::IgnoreCase))
    {
        if (ContainerTypeStr.Equals(TEXT("array"), ESearchCase::IgnoreCase))
        {
            OutPinType.ContainerType = EPinContainerType::Array;
        }
        else if (ContainerTypeStr.Equals(TEXT("set"), ESearchCase::IgnoreCase))
        {
            OutPinType.ContainerType = EPinContainerType::Set;
        }
        else if (ContainerTypeStr.Equals(TEXT("map"), ESearchCase::IgnoreCase))
        {
            FEdGraphPinType ResolvedValuePinType;
            FString ValueResolveError;
            // Explicitly resolve the map's VALUE type (TypeIdentifier + SubTypeIdentifier) as a standalone type.
            // The SubTypeIdentifier is passed along from the original call to ResolvePinType.
            if (!ResolvePinType(TypeIdentifier, SubTypeIdentifier, TEXT("none"), TEXT(""), false, false, ResolvedValuePinType, ValueResolveError))
            {
                OutErrorMsg = FString::Printf(TEXT("INVALID_MAP_VALUE_TYPE: Failed to resolve map value type '%s' (SubType: '%s'). Error: %s"), *TypeIdentifier, *SubTypeIdentifier, *ValueResolveError);
                return false;
            }

            FEdGraphPinType ResolvedKeyPinType;
            FString KeyResolveError;
            // Explicitly resolve the map's KEY type (KeyTypeIdentifierStr) as a standalone type.
            if (KeyTypeIdentifierStr.IsEmpty())
            {
                OutErrorMsg = TEXT("INVALID_MAP_KEY_TYPE: Map container specified but KeyTypeIdentifier is empty.");
                return false;
            }
            if (!ResolvePinType(KeyTypeIdentifierStr, TEXT(""), TEXT("none"), TEXT(""), false, false, ResolvedKeyPinType, KeyResolveError))
            {
                OutErrorMsg = FString::Printf(TEXT("INVALID_MAP_KEY_TYPE: Failed to resolve map key type '%s'. Error: %s"), *KeyTypeIdentifierStr, *KeyResolveError);
                return false;
            }

            // Now, construct OutPinType for the map.
            // For FEdGraphPinType used in variable declarations,
            // the main PinCategory/PinSubCategoryObject fields represent the KEY type,
            // and PinValueType represents the VALUE type.
            OutPinType.PinCategory = ResolvedKeyPinType.PinCategory; // KEY's category
            OutPinType.ContainerType = EPinContainerType::Map;

            // Assign KEY type's properties to OutPinType's main fields
            OutPinType.PinSubCategory = ResolvedKeyPinType.PinSubCategory;
            OutPinType.PinSubCategoryObject = ResolvedKeyPinType.PinSubCategoryObject;
            // Note: bIsReference and bIsConst for the overall PinType (applied later) will apply to the Key.

            // Assign VALUE type's properties to OutPinType's PinValueType
            OutPinType.PinValueType.TerminalCategory = ResolvedValuePinType.PinCategory;
            OutPinType.PinValueType.TerminalSubCategory = ResolvedValuePinType.PinSubCategory;
            OutPinType.PinValueType.TerminalSubCategoryObject = ResolvedValuePinType.PinSubCategoryObject;
            OutPinType.PinValueType.bTerminalIsConst = ResolvedValuePinType.bIsConst;
            OutPinType.PinValueType.bTerminalIsWeakPointer = ResolvedValuePinType.bIsWeakPointer;
        }
        else
        {
            OutErrorMsg = FString::Printf(TEXT("INVALID_CONTAINER_TYPE: Unknown container type '%s'"), *ContainerTypeStr);
            return false;
        }
    }

    // 5. Apply reference and const flags
    OutPinType.bIsReference = bIsReference;
    OutPinType.bIsConst = bIsConst;

    return true;
}

bool FN2CMcpTypeResolver::ResolvePrimitiveTypeInternal(const FString& TypeIdentifier, FEdGraphPinType& OutPinType)
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

bool FN2CMcpTypeResolver::ResolveMathTypeInternal(const FString& TypeIdentifier, FEdGraphPinType& OutPinType)
{
    const FString LowerType = TypeIdentifier.ToLower();
    OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct; // Math types are structs
    if (LowerType == TEXT("vector") || LowerType == TEXT("vector3") || LowerType == TEXT("fvector")) OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    else if (LowerType == TEXT("vector2d") || LowerType == TEXT("fvector2d")) OutPinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
    else if (LowerType == TEXT("vector4") || LowerType == TEXT("fvector4")) OutPinType.PinSubCategoryObject = TBaseStructure<FVector4>::Get();
    else if (LowerType == TEXT("rotator") || LowerType == TEXT("frotator")) OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
    else if (LowerType == TEXT("transform") || LowerType == TEXT("ftransform")) OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
    else if (LowerType == TEXT("quat") || LowerType == TEXT("fquat")) OutPinType.PinSubCategoryObject = TBaseStructure<FQuat>::Get();
    else if (LowerType == TEXT("color") || LowerType == TEXT("fcolor")) OutPinType.PinSubCategoryObject = TBaseStructure<FColor>::Get();
    else if (LowerType == TEXT("linearcolor") || LowerType == TEXT("flinearcolor")) OutPinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
    else { OutPinType.PinCategory = NAME_None; return false; } // Reset category if not a math type
    return true;
}

bool FN2CMcpTypeResolver::ResolveSpecialTypeInternal(const FString& TypeIdentifier, FEdGraphPinType& OutPinType)
{
    const FString LowerType = TypeIdentifier.ToLower();
    if (LowerType == TEXT("wildcard")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
    else if (LowerType == TEXT("delegate")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_Delegate;
    else if (LowerType == TEXT("multicastdelegate")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
    else if (LowerType == TEXT("softobject")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
    else if (LowerType == TEXT("softclass")) OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
    else return false;
    return true;
}

UObject* FN2CMcpTypeResolver::FindObjectByPathOrName(const FString& PathOrName, UClass* ExpectedClass)
{
    UObject* FoundObject = nullptr;
    if (PathOrName.Contains(TEXT("/"))) // Likely a path
    {
        FoundObject = StaticFindObject(ExpectedClass ? ExpectedClass : UObject::StaticClass(), nullptr, *PathOrName);
        if (!FoundObject)
        {
            FoundObject = LoadObject<UObject>(nullptr, *PathOrName);
        }
    }
    else // Likely a name
    {
        // Try common engine types by name first for performance
        if (ExpectedClass == UClass::StaticClass()) FoundObject = FindObject<UClass>(nullptr, *PathOrName);
        else if (ExpectedClass == UScriptStruct::StaticClass()) FoundObject = FindObject<UScriptStruct>(nullptr, *PathOrName);
        else if (ExpectedClass == UEnum::StaticClass()) FoundObject = FindObject<UEnum>(nullptr, *PathOrName);

        if (!FoundObject) // Fallback to iterating all objects if not found by common name
        {
            for (TObjectIterator<UObject> It; It; ++It)
            {
                if (It->GetName() == PathOrName)
                {
                    if (ExpectedClass == nullptr || It->IsA(ExpectedClass))
                    {
                        FoundObject = *It;
                        break;
                    }
                }
            }
        }
    }
    return FoundObject;
}


bool FN2CMcpTypeResolver::ResolveObjectLikeTypeInternal(const FString& TypeIdentifier, const FString& SubTypeIdentifier, FEdGraphPinType& OutPinType, FString& OutErrorMsg)
{
    UObject* SubTypeObject = nullptr;
    const FString LowerType = TypeIdentifier.ToLower();

    if (LowerType == TEXT("object"))
    {
        SubTypeObject = FindObjectByPathOrName(SubTypeIdentifier, UClass::StaticClass());
        if (!SubTypeObject) SubTypeObject = FindObjectByPathOrName(SubTypeIdentifier, UObject::StaticClass()); // Fallback for generic UObject
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
    }
    else if (LowerType == TEXT("class"))
    {
        SubTypeObject = FindObjectByPathOrName(SubTypeIdentifier, UClass::StaticClass());
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
    }
    else if (LowerType == TEXT("struct"))
    {
        SubTypeObject = FindObjectByPathOrName(SubTypeIdentifier, UScriptStruct::StaticClass());
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    }
    else if (LowerType == TEXT("enum"))
    {
        SubTypeObject = FindObjectByPathOrName(SubTypeIdentifier, UEnum::StaticClass());
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte; // Enums are often represented as bytes in BPs, but PC_Enum is also valid. K2 uses PC_Byte for UUserDefinedEnum.
        if (Cast<UUserDefinedEnum>(SubTypeObject)) {
             OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
        } else if (SubTypeObject) { // Native enums
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Enum;
        }
    }
    else if (LowerType == TEXT("interface"))
    {
        SubTypeObject = FindObjectByPathOrName(SubTypeIdentifier, UClass::StaticClass()); // Interfaces are UClasses
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Interface;
    }
    else
    {
        // This case should ideally be caught by the primary ResolvePinType logic
        OutErrorMsg = FString::Printf(TEXT("INTERNAL_ERROR: ResolveObjectLikeTypeInternal called with unknown TypeIdentifier '%s'"), *TypeIdentifier);
        return false;
    }

    if (!SubTypeObject)
    {
        OutErrorMsg = FString::Printf(TEXT("INVALID_SUBTYPE: Could not resolve %s type '%s'"), *TypeIdentifier, *SubTypeIdentifier);
        return false;
    }
    OutPinType.PinSubCategoryObject = SubTypeObject;
    return true;
}

bool FN2CMcpTypeResolver::ResolveFullObjectPathInternal(const FString& FullPath, FEdGraphPinType& OutPinType, FString& OutErrorMsg)
{
    UObject* TypeObject = FindObjectByPathOrName(FullPath);

    if (!TypeObject)
    {
        OutErrorMsg = FString::Printf(TEXT("ASSET_NOT_FOUND: Could not load or find object at path '%s'"), *FullPath);
        return false;
    }

    if (UScriptStruct* Struct = Cast<UScriptStruct>(TypeObject))
    {
        OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        OutPinType.PinSubCategoryObject = Struct;
    }
    else if (UClass* Class = Cast<UClass>(TypeObject))
    {
        // Distinguish between class-of-class and object-of-class
        if (Class->IsChildOf(UClass::StaticClass())) // e.g. GetClassDefaults node
        {
             OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
        }
        else // Standard object reference
        {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
        }
        OutPinType.PinSubCategoryObject = Class;
    }
    else if (UEnum* Enum = Cast<UEnum>(TypeObject))
    {
        if (Cast<UUserDefinedEnum>(Enum)) {
             OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte; // UserDefinedEnums are bytes
        } else {
            OutPinType.PinCategory = UEdGraphSchema_K2::PC_Enum; // Native enums
        }
        OutPinType.PinSubCategoryObject = Enum;
    }
    else
    {
        OutErrorMsg = FString::Printf(TEXT("INVALID_TYPE_AT_PATH: Object at path '%s' is not a Class, Struct, or Enum. Actual type: %s"), *FullPath, *TypeObject->GetClass()->GetName());
        return false;
    }
    return true;
}
