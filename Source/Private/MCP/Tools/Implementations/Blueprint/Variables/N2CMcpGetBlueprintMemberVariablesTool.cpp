// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetBlueprintMemberVariablesTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "Utils/N2CLogger.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

REGISTER_MCP_TOOL(FN2CMcpGetBlueprintMemberVariablesTool)

FMcpToolDefinition FN2CMcpGetBlueprintMemberVariablesTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("get-blueprint-member-variables"),
        TEXT("Retrieves all member variables from the currently focused Blueprint, including their types, categories, and properties")
    ,

    	N2CMcpToolsets::BlueprintVariable

    );
    
    // This tool takes no input parameters
    Definition.InputSchema = BuildEmptyObjectSchema();
    
    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
    // Results carry Blueprint text (variable tooltips, categories and default values): marked as untrusted data (card 0124)
    Definition.bReturnsUntrustedContent = true;

    return Definition;
}

FMcpToolCallResult FN2CMcpGetBlueprintMemberVariablesTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this]() -> FMcpToolCallResult
    {
        // Get the currently focused Blueprint
        FString ErrorMsg;
        UBlueprint* FocusedBlueprint = FN2CMcpBlueprintUtils::ResolveBlueprint(TEXT(""), ErrorMsg);
        
        if (!FocusedBlueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("Failed to get focused Blueprint: %s"), *ErrorMsg)
            );
        }
        
        // Build the result JSON
        TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
        Result->SetStringField(TEXT("blueprintName"), FocusedBlueprint->GetName());
        Result->SetStringField(TEXT("blueprintPath"), FocusedBlueprint->GetPathName());
        
        // Get parent class info
        if (FocusedBlueprint->ParentClass)
        {
            Result->SetStringField(TEXT("parentClass"), FocusedBlueprint->ParentClass->GetPathName());
        }
        
        // Create array for variables
        TArray<TSharedPtr<FJsonValue>> VariablesArray;
        
        // Process each member variable
        for (const FBPVariableDescription& VarDesc : FocusedBlueprint->NewVariables)
        {
            TSharedPtr<FJsonObject> VarInfo = BuildVariableInfo(VarDesc, VarDesc.VarName);
            if (VarInfo.IsValid())
            {
                VariablesArray.Add(MakeShareable(new FJsonValueObject(VarInfo)));
            }
        }
        
        Result->SetArrayField(TEXT("variables"), VariablesArray);
        Result->SetNumberField(TEXT("variableCount"), VariablesArray.Num());
        
        // Add categories summary
        TSet<FString> UniqueCategories;
        for (const FBPVariableDescription& VarDesc : FocusedBlueprint->NewVariables)
        {
            UniqueCategories.Add(VarDesc.Category.ToString());
        }
        
        TArray<TSharedPtr<FJsonValue>> CategoriesArray;
        for (const FString& Category : UniqueCategories)
        {
            CategoriesArray.Add(MakeShareable(new FJsonValueString(Category)));
        }
        Result->SetArrayField(TEXT("categories"), CategoriesArray);
        
        // Convert result to string
        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
        
        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Retrieved %d member variables from Blueprint '%s'"), 
                VariablesArray.Num(), *FocusedBlueprint->GetName()),
            EN2CLogSeverity::Info
        );
        
        return FMcpToolCallResult::CreateTextResult(ResultString);
    });
}

TSharedPtr<FJsonObject> FN2CMcpGetBlueprintMemberVariablesTool::BuildVariableInfo(
    const FBPVariableDescription& VarDesc, FName VariableName) const
{
    TSharedPtr<FJsonObject> VarInfo = MakeShareable(new FJsonObject);
    
    // Basic info
    VarInfo->SetStringField(TEXT("name"), VariableName.ToString());
    VarInfo->SetStringField(TEXT("category"), VarDesc.Category.ToString());
    VarInfo->SetStringField(TEXT("friendlyName"), VarDesc.FriendlyName);
    VarInfo->SetStringField(TEXT("defaultValue"), VarDesc.DefaultValue);
    VarInfo->SetStringField(TEXT("guid"), VarDesc.VarGuid.ToString());
    
    // Type information
    TSharedPtr<FJsonObject> TypeInfo = PinTypeToJson(VarDesc.VarType);
    VarInfo->SetObjectField(TEXT("type"), TypeInfo);
    
    // Property flags
    TSharedPtr<FJsonObject> Flags = MakeShareable(new FJsonObject);
    Flags->SetBoolField(TEXT("instanceEditable"), (VarDesc.PropertyFlags & CPF_Edit) != 0);
    Flags->SetBoolField(TEXT("blueprintVisible"), (VarDesc.PropertyFlags & CPF_BlueprintVisible) != 0);
    Flags->SetBoolField(TEXT("blueprintReadOnly"), (VarDesc.PropertyFlags & CPF_BlueprintReadOnly) != 0);
    Flags->SetBoolField(TEXT("exposeOnSpawn"), (VarDesc.PropertyFlags & CPF_ExposeOnSpawn) != 0);
    Flags->SetBoolField(TEXT("private"), (VarDesc.PropertyFlags & CPF_DisableEditOnInstance) != 0);
    Flags->SetBoolField(TEXT("transient"), (VarDesc.PropertyFlags & CPF_Transient) != 0);
    Flags->SetBoolField(TEXT("saveGame"), (VarDesc.PropertyFlags & CPF_SaveGame) != 0);
    Flags->SetBoolField(TEXT("advancedDisplay"), (VarDesc.PropertyFlags & CPF_AdvancedDisplay) != 0);
    Flags->SetBoolField(TEXT("deprecated"), (VarDesc.PropertyFlags & CPF_Deprecated) != 0);
    Flags->SetBoolField(TEXT("config"), (VarDesc.PropertyFlags & CPF_Config) != 0);
    
    // Replication flags
    TSharedPtr<FJsonObject> Replication = MakeShareable(new FJsonObject);
    Replication->SetBoolField(TEXT("replicated"), (VarDesc.PropertyFlags & CPF_Net) != 0);
    Replication->SetBoolField(TEXT("repNotify"), (VarDesc.PropertyFlags & CPF_RepNotify) != 0);
    if ((VarDesc.PropertyFlags & CPF_RepNotify) != 0 && !VarDesc.RepNotifyFunc.IsNone())
    {
        Replication->SetStringField(TEXT("repNotifyFunc"), VarDesc.RepNotifyFunc.ToString());
    }
    
    Flags->SetObjectField(TEXT("replication"), Replication);
    VarInfo->SetObjectField(TEXT("flags"), Flags);
    
    // Metadata
    TSharedPtr<FJsonObject> Metadata = MakeShareable(new FJsonObject);
    
    // Add known metadata
    for (const auto& MetaPair : VarDesc.MetaDataArray)
    {
        Metadata->SetStringField(MetaPair.DataKey.ToString(), MetaPair.DataValue);
    }
    
    VarInfo->SetObjectField(TEXT("metadata"), Metadata);
    
    return VarInfo;
}

TSharedPtr<FJsonObject> FN2CMcpGetBlueprintMemberVariablesTool::ExtractVariableMetadata(
    const UBlueprint* Blueprint, FName VariableName) const
{
    TSharedPtr<FJsonObject> Metadata = MakeShareable(new FJsonObject);
    
    // Get common metadata fields
    FString MetaValue;
    
    // Tooltip
    if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, TEXT("tooltip"), MetaValue))
    {
        Metadata->SetStringField(TEXT("tooltip"), MetaValue);
    }
    
    // Display name
    if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, TEXT("DisplayName"), MetaValue))
    {
        Metadata->SetStringField(TEXT("DisplayName"), MetaValue);
    }
    
    // Clamp min/max
    if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, TEXT("ClampMin"), MetaValue))
    {
        Metadata->SetStringField(TEXT("ClampMin"), MetaValue);
    }
    if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, TEXT("ClampMax"), MetaValue))
    {
        Metadata->SetStringField(TEXT("ClampMax"), MetaValue);
    }
    
    // Units
    if (FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, TEXT("Units"), MetaValue))
    {
        Metadata->SetStringField(TEXT("Units"), MetaValue);
    }
    
    return Metadata;
}

TSharedPtr<FJsonObject> FN2CMcpGetBlueprintMemberVariablesTool::PinTypeToJson(const FEdGraphPinType& PinType) const
{
    TSharedPtr<FJsonObject> TypeInfo = MakeShareable(new FJsonObject);
    
    // Basic type category
    FString CategoryString;
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
        CategoryString = TEXT("bool");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
        CategoryString = TEXT("byte");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
        CategoryString = TEXT("int");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
        CategoryString = TEXT("int64");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Float)
        CategoryString = TEXT("float");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Double)
        CategoryString = TEXT("double");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
        CategoryString = TEXT("name");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
        CategoryString = TEXT("string");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
        CategoryString = TEXT("text");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
        CategoryString = TEXT("struct");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
        CategoryString = TEXT("object");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
        CategoryString = TEXT("class");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
        CategoryString = TEXT("softobject");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
        CategoryString = TEXT("softclass");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
        CategoryString = TEXT("interface");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
        CategoryString = TEXT("enum");
    else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
        CategoryString = TEXT("wildcard");
    else
        CategoryString = TEXT("unknown");
    
    TypeInfo->SetStringField(TEXT("category"), CategoryString);
    
    // Subcategory (used for objects/structs/enums)
    if (!PinType.PinSubCategory.IsNone())
    {
        TypeInfo->SetStringField(TEXT("subCategory"), PinType.PinSubCategory.ToString());
    }
    
    // Object/struct/enum reference
    if (PinType.PinSubCategoryObject.IsValid())
    {
        TypeInfo->SetStringField(TEXT("typeObject"), PinType.PinSubCategoryObject->GetPathName());
        TypeInfo->SetStringField(TEXT("typeName"), PinType.PinSubCategoryObject->GetName());
    }
    
    // Container type
    if (PinType.ContainerType == EPinContainerType::Array)
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("array"));
    }
    else if (PinType.ContainerType == EPinContainerType::Set)
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("set"));
    }
    else if (PinType.ContainerType == EPinContainerType::Map)
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("map"));
        
        // Map key type
        TSharedPtr<FJsonObject> KeyType = MakeShareable(new FJsonObject);
        KeyType->SetStringField(TEXT("category"), PinType.PinValueType.TerminalCategory.ToString());
        if (!PinType.PinValueType.TerminalSubCategory.IsNone())
        {
            KeyType->SetStringField(TEXT("subCategory"), PinType.PinValueType.TerminalSubCategory.ToString());
        }
        if (PinType.PinValueType.TerminalSubCategoryObject.IsValid())
        {
            KeyType->SetStringField(TEXT("typeObject"), PinType.PinValueType.TerminalSubCategoryObject->GetPathName());
            KeyType->SetStringField(TEXT("typeName"), PinType.PinValueType.TerminalSubCategoryObject->GetName());
        }
        TypeInfo->SetObjectField(TEXT("keyType"), KeyType);
    }
    else
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("none"));
    }
    
    // Pass-by-reference
    TypeInfo->SetBoolField(TEXT("isReference"), PinType.bIsReference);
    TypeInfo->SetBoolField(TEXT("isConst"), PinType.bIsConst);
    
    // Build a human-readable type string
    FString TypeString = CategoryString;
    if (PinType.PinSubCategoryObject.IsValid())
    {
        TypeString = PinType.PinSubCategoryObject->GetName();
    }
    
    // Add container prefix
    if (PinType.ContainerType == EPinContainerType::Array)
    {
        TypeString = FString::Printf(TEXT("TArray<%s>"), *TypeString);
    }
    else if (PinType.ContainerType == EPinContainerType::Set)
    {
        TypeString = FString::Printf(TEXT("TSet<%s>"), *TypeString);
    }
    else if (PinType.ContainerType == EPinContainerType::Map)
    {
        FString KeyTypeString = TEXT("?");
        if (!PinType.PinValueType.TerminalCategory.IsNone())
        {
            KeyTypeString = PinType.PinValueType.TerminalCategory.ToString();
        }
        TypeString = FString::Printf(TEXT("TMap<%s, %s>"), *KeyTypeString, *TypeString);
    }
    
    TypeInfo->SetStringField(TEXT("displayString"), TypeString);
    
    return TypeInfo;
}