// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "MCP/Utils/N2CMcpTypeResolver.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "EdGraphSchema_K2.h"
#include "Serialization/JsonSerializer.h"

bool FN2CMcpVariableUtils::ValidateMapKeyType(const FString& KeyType, FString& OutError)
{
	// First resolve the key type to validate it properly
	FEdGraphPinType KeyPinType;
	FString ResolveError;
	if (!FN2CMcpTypeResolver::ResolvePinType(KeyType, TEXT(""), TEXT("none"), TEXT(""), false, false, KeyPinType, ResolveError))
	{
		OutError = FString::Printf(TEXT("Invalid map key type: %s"), *ResolveError);
		return false;
	}
	
	// Check if the resolved type is valid for map keys
	const FName& KeyCategory = KeyPinType.PinCategory;
	const bool bIsValidKeyType = 
		KeyCategory == UEdGraphSchema_K2::PC_Byte ||
		KeyCategory == UEdGraphSchema_K2::PC_Int ||
		KeyCategory == UEdGraphSchema_K2::PC_Int64 ||
		KeyCategory == UEdGraphSchema_K2::PC_Real ||  // Float/Double - Note: Can have precision issues
		KeyCategory == UEdGraphSchema_K2::PC_Name ||
		KeyCategory == UEdGraphSchema_K2::PC_String ||
		KeyCategory == UEdGraphSchema_K2::PC_Object ||   // Object pointers
		KeyCategory == UEdGraphSchema_K2::PC_Class ||    // Class pointers
		KeyCategory == UEdGraphSchema_K2::PC_SoftObject ||
		KeyCategory == UEdGraphSchema_K2::PC_SoftClass ||
		KeyCategory == UEdGraphSchema_K2::PC_Enum;       // Enums
	
	// Explicitly check for invalid types
	if (KeyCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		OutError = TEXT("Boolean types cannot be used as map keys in Blueprints");
		return false;
	}
	
	if (KeyCategory == UEdGraphSchema_K2::PC_Struct)
	{
		// Most structs cannot be used as keys (no hash function)
		OutError = FString::Printf(TEXT("Struct types like '%s' cannot be used as map keys in Blueprints"), *KeyType);
		return false;
	}
	
	if (!bIsValidKeyType)
	{
		OutError = FString::Printf(TEXT("Type '%s' (category: %s) is not a valid map key type in Blueprints"), 
			*KeyType, *KeyCategory.ToString());
		return false;
	}
	
	return true;
}

bool FN2CMcpVariableUtils::ValidateContainerTypeParameters(const FString& ContainerType, const FString& MapKeyTypeIdentifier, FString& OutError)
{
	// Validate container type and key type combination
	if (ContainerType.Equals(TEXT("map"), ESearchCase::IgnoreCase))
	{
		if (MapKeyTypeIdentifier.IsEmpty())
		{
			OutError = TEXT("mapKeyTypeIdentifier is required when containerType is 'map'");
			return false;
		}
		
		// Validate the key type
		return ValidateMapKeyType(MapKeyTypeIdentifier, OutError);
	}
	else if (!MapKeyTypeIdentifier.IsEmpty())
	{
		OutError = TEXT("mapKeyTypeIdentifier should only be provided when containerType is 'map'");
		return false;
	}
	
	return true;
}

void FN2CMcpVariableUtils::AddContainerTypeSchemaProperties(TSharedPtr<FJsonObject> Properties)
{
	// containerType property
	TSharedPtr<FJsonObject> ContainerTypeProp = MakeShareable(new FJsonObject);
	ContainerTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> ContainerEnum;
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("none"))));
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("array"))));
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("set"))));
	ContainerEnum.Add(MakeShareable(new FJsonValueString(TEXT("map"))));
	ContainerTypeProp->SetArrayField(TEXT("enum"), ContainerEnum);
	ContainerTypeProp->SetStringField(TEXT("default"), TEXT("none"));
	ContainerTypeProp->SetStringField(TEXT("description"), TEXT("Container type for the variable (none, array, set, map)"));
	Properties->SetObjectField(TEXT("containerType"), ContainerTypeProp);

	// mapKeyTypeIdentifier property (for map key type)
	TSharedPtr<FJsonObject> MapKeyTypeProp = MakeShareable(new FJsonObject);
	MapKeyTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	MapKeyTypeProp->SetStringField(TEXT("description"), TEXT("For 'map' containerType, this specifies the map's KEY type identifier (e.g., 'Name', 'int32', 'FString', '/Script/Engine.Actor'; bool, text and struct keys are refused). This is required if containerType is 'map'. The map's VALUE type is specified by 'typeIdentifier'. Example: For TMap<FName, FVector>, 'typeIdentifier' would be 'FVector' and 'mapKeyTypeIdentifier' would be 'FName'."));
	MapKeyTypeProp->SetStringField(TEXT("default"), TEXT("")); // No default, conditionally required
	Properties->SetObjectField(TEXT("mapKeyTypeIdentifier"), MapKeyTypeProp);
}

void FN2CMcpVariableUtils::ParseContainerTypeArguments(const FN2CMcpArgumentParser& ArgParser, FString& OutContainerType, FString& OutMapKeyTypeIdentifier)
{
	OutContainerType = ArgParser.GetOptionalString(TEXT("containerType"), TEXT("none"));
	OutMapKeyTypeIdentifier = ArgParser.GetOptionalString(TEXT("mapKeyTypeIdentifier"));
}

void FN2CMcpVariableUtils::AddContainerInfoToResult(TSharedPtr<FJsonObject> Result, const FString& ContainerType, bool bIsLocalVariable)
{
	if (!ContainerType.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Result->SetStringField(TEXT("containerType"), ContainerType);
		
		// Add note about Blueprint limitations for maps
		if (ContainerType.Equals(TEXT("map"), ESearchCase::IgnoreCase))
		{
			if (bIsLocalVariable)
			{
				Result->SetStringField(TEXT("note"), TEXT("Local map variables follow the same limitations as member variables. To modify map values at runtime, use: Find → Store locally → Modify → Add with same key."));
			}
			else
			{
				Result->SetStringField(TEXT("note"), TEXT("Map default values can be edited in the Details panel. To modify map values at runtime, use the pattern: Find → Store locally → Modify → Add with same key."));
			}
		}
	}
}

TSharedPtr<FJsonObject> FN2CMcpVariableUtils::BuildTypeInfo(const FEdGraphPinType& PinType)
{
	TSharedPtr<FJsonObject> TypeInfo = MakeShareable(new FJsonObject);
	
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object && PinType.PinSubCategoryObject.IsValid())
	{
		TypeInfo->SetStringField(TEXT("category"), TEXT("object"));
		TypeInfo->SetStringField(TEXT("className"), PinType.PinSubCategoryObject->GetName());
		TypeInfo->SetStringField(TEXT("path"), PinType.PinSubCategoryObject->GetPathName());
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && PinType.PinSubCategoryObject.IsValid())
	{
		TypeInfo->SetStringField(TEXT("category"), TEXT("struct"));
		TypeInfo->SetStringField(TEXT("structName"), PinType.PinSubCategoryObject->GetName());
		TypeInfo->SetStringField(TEXT("path"), PinType.PinSubCategoryObject->GetPathName());
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum && PinType.PinSubCategoryObject.IsValid())
	{
		TypeInfo->SetStringField(TEXT("category"), TEXT("enum"));
		TypeInfo->SetStringField(TEXT("enumName"), PinType.PinSubCategoryObject->GetName());
		TypeInfo->SetStringField(TEXT("path"), PinType.PinSubCategoryObject->GetPathName());
	}
	else
	{
		TypeInfo->SetStringField(TEXT("category"), TEXT("primitive"));
		TypeInfo->SetStringField(TEXT("typeName"), PinType.PinCategory.ToString());
	}
	
	return TypeInfo;
}
