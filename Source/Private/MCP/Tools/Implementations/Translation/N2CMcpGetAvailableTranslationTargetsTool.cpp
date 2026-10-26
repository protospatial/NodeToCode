// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetAvailableTranslationTargetsTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSettings.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/EnumProperty.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetAvailableTranslationTargetsTool)

FMcpToolDefinition FN2CMcpGetAvailableTranslationTargetsTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("get-available-translation-targets"),
        TEXT("Returns the list of programming languages that NodeToCode can translate Blueprints into, including metadata about each language."),
        TEXT("Translation")
    );
    
    // This tool takes no input parameters
    Definition.InputSchema = BuildEmptyObjectSchema();
    
    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
    return Definition;
}

FMcpToolCallResult FN2CMcpGetAvailableTranslationTargetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    FN2CLogger::Get().Log(TEXT("Executing get-available-translation-targets tool"), EN2CLogSeverity::Debug);
    
    // Get the enum type for reflection
    const UEnum* CodeLanguageEnum = StaticEnum<EN2CCodeLanguage>();
    if (!CodeLanguageEnum)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to get EN2CCodeLanguage enum type"));
        return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to retrieve language enum information"));
    }
    
    // Get current settings to determine default and current language
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to get NodeToCode settings"));
        return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to retrieve plugin settings"));
    }
    
    // Build the response JSON
    TSharedPtr<FJsonObject> ResponseObject = MakeShareable(new FJsonObject);
    TArray<TSharedPtr<FJsonValue>> LanguagesArray;
    
    // Iterate through all enum values using reflection
    for (int32 EnumIndex = 0; EnumIndex < CodeLanguageEnum->NumEnums() - 1; ++EnumIndex) // -1 to exclude MAX value
    {
        // Skip hidden enum entries
        if (CodeLanguageEnum->HasMetaData(TEXT("Hidden"), EnumIndex))
        {
            continue;
        }
        
        EN2CCodeLanguage Language = static_cast<EN2CCodeLanguage>(CodeLanguageEnum->GetValueByIndex(EnumIndex));
        
        // Create language object
        TSharedPtr<FJsonObject> LanguageObject = MakeShareable(new FJsonObject);
        
        // Get the enum name as the ID (e.g., "Cpp", "Python")
        FString LanguageId = CodeLanguageEnum->GetNameStringByIndex(EnumIndex);
        // Remove the enum prefix if present (e.g., "EN2CCodeLanguage::" -> "")
        if (LanguageId.Contains(TEXT("::")))
        {
            LanguageId = LanguageId.RightChop(LanguageId.Find(TEXT("::")) + 2);
        }
        LanguageObject->SetStringField(TEXT("id"), LanguageId.ToLower());
        
        // Get display name from enum metadata
        FString DisplayName = CodeLanguageEnum->GetDisplayNameTextByIndex(EnumIndex).ToString();
        LanguageObject->SetStringField(TEXT("displayName"), DisplayName);
        
        // Add description
        LanguageObject->SetStringField(TEXT("description"), GetLanguageDescription(Language));
        
        // Add file extensions
        TArray<TSharedPtr<FJsonValue>> ExtensionsArray;
        for (const FString& Extension : GetLanguageFileExtensions(Language))
        {
            ExtensionsArray.Add(MakeShareable(new FJsonValueString(Extension)));
        }
        LanguageObject->SetArrayField(TEXT("fileExtensions"), ExtensionsArray);
        
        // Add category
        LanguageObject->SetStringField(TEXT("category"), GetLanguageCategory(Language));
        
        // Add features/notes
        LanguageObject->SetStringField(TEXT("features"), GetLanguageFeatures(Language));
        
        // Add syntax highlighting support flag
        LanguageObject->SetBoolField(TEXT("syntaxHighlightingSupported"), true); // All our languages have syntax highlighting
        
        LanguagesArray.Add(MakeShareable(new FJsonValueObject(LanguageObject)));
    }
    
    ResponseObject->SetArrayField(TEXT("languages"), LanguagesArray);
    
    // Add default language (always C++)
    ResponseObject->SetStringField(TEXT("defaultLanguage"), TEXT("cpp"));
    
    // Add current language from settings
    FString CurrentLanguageId = CodeLanguageEnum->GetNameStringByValue(static_cast<int64>(Settings->TargetLanguage));
    if (CurrentLanguageId.Contains(TEXT("::")))
    {
        CurrentLanguageId = CurrentLanguageId.RightChop(CurrentLanguageId.Find(TEXT("::")) + 2);
    }
    ResponseObject->SetStringField(TEXT("currentLanguage"), CurrentLanguageId.ToLower());
    
    // Add language count
    ResponseObject->SetNumberField(TEXT("languageCount"), LanguagesArray.Num());
    
    FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully retrieved %d translation target languages"), LanguagesArray.Num()), EN2CLogSeverity::Info);
    
    // Convert JSON to string for the result
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
    
    return FMcpToolCallResult::CreateTextResult(OutputString);
}

FString FN2CMcpGetAvailableTranslationTargetsTool::GetLanguageDescription(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return TEXT("C++ with Unreal Engine conventions and best practices");
        case EN2CCodeLanguage::Python:
            return TEXT("Python 3 with type hints and PEP 8 compliance");
        case EN2CCodeLanguage::JavaScript:
            return TEXT("Modern JavaScript (ECMAScript 2022+) with clean syntax");
        case EN2CCodeLanguage::CSharp:
            return TEXT("C# with Unity-compatible conventions");
        case EN2CCodeLanguage::Swift:
            return TEXT("Swift 5+ for iOS/macOS development");
        case EN2CCodeLanguage::Pseudocode:
            return TEXT("Human-readable algorithmic representation for documentation");
        default:
            return TEXT("Unknown language");
    }
}

FString FN2CMcpGetAvailableTranslationTargetsTool::GetLanguageCategory(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
        case EN2CCodeLanguage::CSharp:
        case EN2CCodeLanguage::Swift:
            return TEXT("compiled");
        case EN2CCodeLanguage::Python:
        case EN2CCodeLanguage::JavaScript:
            return TEXT("scripted");
        case EN2CCodeLanguage::Pseudocode:
            return TEXT("pseudocode");
        default:
            return TEXT("unknown");
    }
}

TArray<FString> FN2CMcpGetAvailableTranslationTargetsTool::GetLanguageFileExtensions(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return {TEXT(".h"), TEXT(".cpp")};
        case EN2CCodeLanguage::Python:
            return {TEXT(".py")};
        case EN2CCodeLanguage::JavaScript:
            return {TEXT(".js")};
        case EN2CCodeLanguage::CSharp:
            return {TEXT(".cs")};
        case EN2CCodeLanguage::Swift:
            return {TEXT(".swift")};
        case EN2CCodeLanguage::Pseudocode:
            return {TEXT(".md"), TEXT(".txt")};
        default:
            return {TEXT(".txt")};
    }
}

FString FN2CMcpGetAvailableTranslationTargetsTool::GetLanguageFeatures(EN2CCodeLanguage Language)
{
    switch (Language)
    {
        case EN2CCodeLanguage::Cpp:
            return TEXT("Header/source separation, UPROPERTY/UFUNCTION macros, full UE5 API compatibility");
        case EN2CCodeLanguage::Python:
            return TEXT("Type annotations, async/await support, clean pythonic idioms");
        case EN2CCodeLanguage::JavaScript:
            return TEXT("ES6+ features, arrow functions, destructuring, async/await");
        case EN2CCodeLanguage::CSharp:
            return TEXT("Properties, LINQ-style operations, Unity MonoBehaviour patterns");
        case EN2CCodeLanguage::Swift:
            return TEXT("Optionals, protocols, SwiftUI compatibility, modern Swift patterns");
        case EN2CCodeLanguage::Pseudocode:
            return TEXT("Plain English descriptions, structured flow, ideal for documentation");
        default:
            return TEXT("Standard language features");
    }
}