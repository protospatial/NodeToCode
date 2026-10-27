// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetBlueprintMemberVariablesTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpGraphTarget.h"
#include "MCP/Utils/N2CMcpVariableDefaults.h"
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
        TEXT("Retrieves all member variables of a Blueprint (blueprintPath, else the focused Blueprint), including their types, categories, and properties")
    ,

    	N2CMcpToolsets::BlueprintVariable

    );
    
    // Only the optional blueprintPath (card 0274)
    Definition.InputSchema = BuildEmptyObjectSchema();
    FN2CGraphTarget::AddSchemaProperties(Definition, /*bGraphArguments*/ false);
    
    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
    // Results carry Blueprint text (variable tooltips, categories and default values): marked as untrusted data (card 0124)
    Definition.bReturnsUntrustedContent = true;

    // Unknown arguments are refused, so a misspelled option is not ignored (card 0194)
    SetStrictArguments(Definition);

    return Definition;
}

FMcpToolCallResult FN2CMcpGetBlueprintMemberVariablesTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
    {
        // The Blueprint blueprintPath names, else the focused one (card 0274)
        FString ErrorMsg;
        UBlueprint* FocusedBlueprint = FN2CGraphTarget::LoadBlueprint(FN2CMcpArgumentParser(Arguments).GetOptionalString(TEXT("blueprintPath")), ErrorMsg);

        if (!FocusedBlueprint)
        {
            return FMcpToolCallResult::CreateErrorResult(ErrorMsg);
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
            TSharedPtr<FJsonObject> VarInfo = BuildVariableInfo(FocusedBlueprint, VarDesc);
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

namespace N2CMcpGetMemberVariablesPrivate
{
    /** The type name of one terminal type: a real pin by its sub-category (float or double), as UE5 makes them */
    FString CategoryName(const FName Category, const FName SubCategory)
    {
        if (Category == UEdGraphSchema_K2::PC_Real)
            return SubCategory == UEdGraphSchema_K2::PC_Float ? TEXT("float") : TEXT("double");
        if (Category == UEdGraphSchema_K2::PC_Boolean)
            return TEXT("bool");
        if (Category == UEdGraphSchema_K2::PC_Byte)
            return TEXT("byte");
        if (Category == UEdGraphSchema_K2::PC_Int)
            return TEXT("int");
        if (Category == UEdGraphSchema_K2::PC_Int64)
            return TEXT("int64");
        if (Category == UEdGraphSchema_K2::PC_Float)
            return TEXT("float");
        if (Category == UEdGraphSchema_K2::PC_Double)
            return TEXT("double");
        if (Category == UEdGraphSchema_K2::PC_Name)
            return TEXT("name");
        if (Category == UEdGraphSchema_K2::PC_String)
            return TEXT("string");
        if (Category == UEdGraphSchema_K2::PC_Text)
            return TEXT("text");
        if (Category == UEdGraphSchema_K2::PC_Struct)
            return TEXT("struct");
        if (Category == UEdGraphSchema_K2::PC_Object)
            return TEXT("object");
        if (Category == UEdGraphSchema_K2::PC_Class)
            return TEXT("class");
        if (Category == UEdGraphSchema_K2::PC_SoftObject)
            return TEXT("softobject");
        if (Category == UEdGraphSchema_K2::PC_SoftClass)
            return TEXT("softclass");
        if (Category == UEdGraphSchema_K2::PC_Interface)
            return TEXT("interface");
        if (Category == UEdGraphSchema_K2::PC_Enum)
            return TEXT("enum");
        if (Category == UEdGraphSchema_K2::PC_Wildcard)
            return TEXT("wildcard");
        return TEXT("unknown");
    }

    /** category, subCategory, typeObject and typeName of one terminal type into Out */
    void WriteTerminal(FJsonObject& Out, const FName Category, const FName SubCategory, const UObject* Object)
    {
        Out.SetStringField(TEXT("category"), CategoryName(Category, SubCategory));
        if (!SubCategory.IsNone())
        {
            Out.SetStringField(TEXT("subCategory"), SubCategory.ToString());
        }
        if (Object)
        {
            Out.SetStringField(TEXT("typeObject"), Object->GetPathName());
            Out.SetStringField(TEXT("typeName"), Object->GetName());
        }
    }

    /** The terminal type as it reads in a type string: the class, struct or enum name, else the category */
    FString TerminalDisplay(const FName Category, const FName SubCategory, const UObject* Object)
    {
        return Object ? Object->GetName() : CategoryName(Category, SubCategory);
    }
}

TSharedPtr<FJsonObject> FN2CMcpGetBlueprintMemberVariablesTool::BuildVariableInfo(
    const UBlueprint* Blueprint, const FBPVariableDescription& VarDesc) const
{
    TSharedPtr<FJsonObject> VarInfo = MakeShareable(new FJsonObject);

    // Basic info
    VarInfo->SetStringField(TEXT("name"), VarDesc.VarName.ToString());
    VarInfo->SetStringField(TEXT("category"), VarDesc.Category.ToString());
    VarInfo->SetStringField(TEXT("friendlyName"), VarDesc.FriendlyName);

    // The default lives on the class defaults: a full compile moves the description's DefaultValue there and empties
    // it. The description's value only until the class has the variable (card 0283)
    FString DefaultValue;
    if (!FN2CMcpVariableDefaults::ReadMemberDefault(Blueprint, VarDesc.VarName, DefaultValue))
    {
        DefaultValue = VarDesc.DefaultValue;
    }
    VarInfo->SetStringField(TEXT("defaultValue"), DefaultValue);
    VarInfo->SetStringField(TEXT("guid"), VarDesc.VarGuid.ToString());

    // Type information
    TSharedPtr<FJsonObject> TypeInfo = PinTypeToJson(VarDesc.VarType);
    VarInfo->SetObjectField(TEXT("type"), TypeInfo);

    // Property flags. Instance Editable is CPF_Edit without CPF_DisableEditOnInstance (every variable has CPF_Edit);
    // Private is the BlueprintPrivate metadata, as the details panel reads them (card 0283)
    TSharedPtr<FJsonObject> Flags = MakeShareable(new FJsonObject);
    Flags->SetBoolField(TEXT("instanceEditable"), (VarDesc.PropertyFlags & CPF_Edit) != 0 && (VarDesc.PropertyFlags & CPF_DisableEditOnInstance) == 0);
    Flags->SetBoolField(TEXT("blueprintVisible"), (VarDesc.PropertyFlags & CPF_BlueprintVisible) != 0);
    Flags->SetBoolField(TEXT("blueprintReadOnly"), (VarDesc.PropertyFlags & CPF_BlueprintReadOnly) != 0);
    Flags->SetBoolField(TEXT("exposeOnSpawn"), (VarDesc.PropertyFlags & CPF_ExposeOnSpawn) != 0);
    Flags->SetBoolField(TEXT("private"), VarDesc.HasMetaData(FBlueprintMetadata::MD_Private)
        && VarDesc.GetMetaData(FBlueprintMetadata::MD_Private).ToBool());
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

TSharedPtr<FJsonObject> FN2CMcpGetBlueprintMemberVariablesTool::PinTypeToJson(const FEdGraphPinType& PinType) const
{
    using namespace N2CMcpGetMemberVariablesPrivate;
    TSharedPtr<FJsonObject> TypeInfo = MakeShareable(new FJsonObject);

    // A variable's pin type keeps a map's KEY in its main fields and the value in PinValueType. The top-level fields
    // describe the element type, which for a map is the value (create-variable's typeIdentifier); the key is keyType
    // (its mapKeyTypeIdentifier) and the value is also valueType. Card 0283
    const bool bIsMap = PinType.ContainerType == EPinContainerType::Map;
    const FName Category = bIsMap ? PinType.PinValueType.TerminalCategory : PinType.PinCategory;
    const FName SubCategory = bIsMap ? PinType.PinValueType.TerminalSubCategory : PinType.PinSubCategory;
    const UObject* Object = bIsMap ? PinType.PinValueType.TerminalSubCategoryObject.Get() : PinType.PinSubCategoryObject.Get();
    WriteTerminal(*TypeInfo, Category, SubCategory, Object);

    // Container type
    if (PinType.ContainerType == EPinContainerType::Array)
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("array"));
    }
    else if (PinType.ContainerType == EPinContainerType::Set)
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("set"));
    }
    else if (bIsMap)
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("map"));

        TSharedPtr<FJsonObject> KeyType = MakeShareable(new FJsonObject);
        WriteTerminal(*KeyType, PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get());
        TypeInfo->SetObjectField(TEXT("keyType"), KeyType);

        TSharedPtr<FJsonObject> ValueType = MakeShareable(new FJsonObject);
        WriteTerminal(*ValueType, Category, SubCategory, Object);
        TypeInfo->SetObjectField(TEXT("valueType"), ValueType);
    }
    else
    {
        TypeInfo->SetStringField(TEXT("container"), TEXT("none"));
    }

    // Pass-by-reference
    TypeInfo->SetBoolField(TEXT("isReference"), PinType.bIsReference);
    TypeInfo->SetBoolField(TEXT("isConst"), PinType.bIsConst);

    // Build a human-readable type string
    FString TypeString = TerminalDisplay(Category, SubCategory, Object);
    if (PinType.ContainerType == EPinContainerType::Array)
    {
        TypeString = FString::Printf(TEXT("TArray<%s>"), *TypeString);
    }
    else if (PinType.ContainerType == EPinContainerType::Set)
    {
        TypeString = FString::Printf(TEXT("TSet<%s>"), *TypeString);
    }
    else if (bIsMap)
    {
        TypeString = FString::Printf(TEXT("TMap<%s, %s>"),
            *TerminalDisplay(PinType.PinCategory, PinType.PinSubCategory, PinType.PinSubCategoryObject.Get()), *TypeString);
    }

    TypeInfo->SetStringField(TEXT("displayString"), TypeString);

    return TypeInfo;
}
