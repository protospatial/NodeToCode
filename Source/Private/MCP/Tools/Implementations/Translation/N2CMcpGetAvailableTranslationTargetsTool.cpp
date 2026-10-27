// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpGetAvailableTranslationTargetsTool.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "Utils/N2CLogger.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpGetAvailableTranslationTargetsTool)

FMcpToolDefinition FN2CMcpGetAvailableTranslationTargetsTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("get-available-translation-targets"),
        TEXT("Returns the list of programming languages that NodeToCode can translate Blueprints into, including metadata about each language."),
        N2CMcpToolsets::Translation
    );
    
    // This tool takes no input parameters
    Definition.InputSchema = BuildEmptyObjectSchema();
    
    {
        using namespace N2CMcpOutputSchema;
        // The ids from the target-language table, the same list translate-focused-blueprint's language takes
        TArray<FString> Ids;
        for (const FN2CTargetLanguage& Language : N2C::GetLanguages())
        {
            Ids.Add(Language.GetMcpId());
        }
        Definition.OutputSchema = Object({
            { TEXT("languages"), Array(Object({
                { TEXT("id"), Enum(Ids, TEXT("Pass as translate-focused-blueprint's language")) },
                { TEXT("displayName"), String() },
                { TEXT("description"), String() },
                { TEXT("fileExtensions"), Array(String()) },
                { TEXT("category"), String() },
                { TEXT("features"), String() },
                { TEXT("syntaxHighlightingSupported"), Boolean() },
            }, { TEXT("id"), TEXT("displayName"), TEXT("fileExtensions") })) },
            { TEXT("defaultLanguage"), Enum(Ids) },
            { TEXT("currentLanguage"), Enum(Ids, TEXT("The language Project Settings selects")) },
            { TEXT("languageCount"), Integer() },
        }, { TEXT("languages"), TEXT("defaultLanguage"), TEXT("currentLanguage"), TEXT("languageCount") });
    }

    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
    
    return Definition;
}

FMcpToolCallResult FN2CMcpGetAvailableTranslationTargetsTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    FN2CLogger::Get().Log(TEXT("Executing get-available-translation-targets tool"), EN2CLogSeverity::Debug);
    
    // Get current settings to determine default and current language
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to get NodeToCode settings"));
        return FMcpToolCallResult::CreateErrorResult(TEXT("Failed to retrieve plugin settings"));
    }
    
    // One entry per row of the target-language table (Core/N2CTargetLanguage.h), in enum order
    TSharedPtr<FJsonObject> ResponseObject = MakeShareable(new FJsonObject);
    TArray<TSharedPtr<FJsonValue>> LanguagesArray;
    for (const FN2CTargetLanguage& Language : N2C::GetLanguages())
    {
        TSharedPtr<FJsonObject> LanguageObject = MakeShareable(new FJsonObject);
        LanguageObject->SetStringField(TEXT("id"), Language.GetMcpId());
        LanguageObject->SetStringField(TEXT("displayName"), Language.DisplayName);
        LanguageObject->SetStringField(TEXT("description"), Language.Description);
        
        TArray<TSharedPtr<FJsonValue>> ExtensionsArray;
        for (const FString& Extension : Language.GetFileExtensions())
        {
            ExtensionsArray.Add(MakeShareable(new FJsonValueString(Extension)));
        }
        LanguageObject->SetArrayField(TEXT("fileExtensions"), ExtensionsArray);
        LanguageObject->SetStringField(TEXT("category"), Language.Category);
        LanguageObject->SetStringField(TEXT("features"), Language.Features);
        
        // Every language has a code-editor syntax definition (NodeToCode.Languages checks it)
        LanguageObject->SetBoolField(TEXT("syntaxHighlightingSupported"), true);
        
        LanguagesArray.Add(MakeShareable(new FJsonValueObject(LanguageObject)));
    }
    
    ResponseObject->SetArrayField(TEXT("languages"), LanguagesArray);
    
    // Add default language (always C++)
    ResponseObject->SetStringField(TEXT("defaultLanguage"), N2C::GetLanguage(EN2CCodeLanguage::Cpp).GetMcpId());
    
    // Add current language from settings
    ResponseObject->SetStringField(TEXT("currentLanguage"), N2C::GetLanguage(Settings->TargetLanguage).GetMcpId());
    
    // Add language count
    ResponseObject->SetNumberField(TEXT("languageCount"), LanguagesArray.Num());
    
    FN2CLogger::Get().Log(FString::Printf(TEXT("Successfully retrieved %d translation target languages"), LanguagesArray.Num()), EN2CLogSeverity::Info);
    
    return FMcpToolCallResult::CreateJsonResult(ResponseObject.ToSharedRef(), /*bIndented*/ true);
}
