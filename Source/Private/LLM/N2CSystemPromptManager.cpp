// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CSystemPromptManager.h"

#include "Core/N2CSettings.h"
#include "Core/N2CTargetLanguage.h"
#include "Utils/N2CLogger.h"
#include "Interfaces/IPluginManager.h"

void UN2CSystemPromptManager::Initialize(const FN2CLLMConfig& Config)
{
    bSupportsSystemPrompts = Config.bUseSystemPrompts;
    LoadPrompts();
}

FString UN2CSystemPromptManager::GetSystemPrompt(const FString& PromptKey) const
{
    if (const FString* Prompt = LoadedPrompts.Find(PromptKey))
    {
        return *Prompt;
    }

    FN2CLogger::Get().LogWarning(
        FString::Printf(TEXT("System prompt not found for key: %s"), *PromptKey),
        TEXT("SystemPromptManager")
    );
    return FString();
}

FString UN2CSystemPromptManager::MergePrompts(const FString& SystemPrompt, const FString& UserMessage) const
{
    // For LLMs that don't support separate system prompts, prepend it to the user message
    return FString::Printf(TEXT("##### NODE TO CODE JSON #####\n%s\n\n##### YOUR TASK #####\n\n%s"), *UserMessage, *SystemPrompt);
}

bool UN2CSystemPromptManager::PrependSourceFilesToUserMessage(FString& UserMessage) const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();                                                                                                                                            
     if (!Settings || Settings->ReferenceSourceFilePaths.Num() == 0)                                                                                                                                       
     {                                                                                                                                                                                                     
         return true; // No files to process is still considered successful                                                                                                                                
     }                                                                                                                                                                                                     
                                                                                                                                                                                                           
     FString ReferenceFiles;                                                                                                                                                                               
     bool bSuccess = true;                                                                                                                                                                                 
                                                                                                                                                                                                           
     for (const FFilePath& FilePath : Settings->ReferenceSourceFilePaths)                                                                                                                                  
     {                                                                                                                                                                                                     
         FString Content;                                                                                                                                                                                  
         if (FFileHelper::LoadFileToString(Content, *FilePath.FilePath))                                                                                                                                   
         {                                                                                                                                                                                                 
             if (!ReferenceFiles.IsEmpty())                                                                                                                                                                
             {                                                                                                                                                                                             
                 ReferenceFiles += TEXT("\n\n");                                                                                                                                                           
             }                                                                                                                                                                                             
             ReferenceFiles += FormatSourceFileContent(FilePath.FilePath, Content);                                                                                                                        
         }                                                                                                                                                                                                 
         else                                                                                                                                                                                              
         {                                                                                                                                                                                                 
             FN2CLogger::Get().LogWarning(                                                                                                                                                                 
                 FString::Printf(TEXT("Failed to load reference source file: %s"), *FilePath.FilePath),                                                                                                    
                 TEXT("SystemPromptManager")                                                                                                                                                               
             );                                                                                                                                                                                            
             bSuccess = false;                                                                                                                                                                             
         }                                                                                                                                                                                                 
     }                                                                                                                                                                                                     
                                                                                                                                                                                                           
     if (!ReferenceFiles.IsEmpty())                                                                                                                                                                        
     {                                                                                                                                                                                                     
         UserMessage = FString::Printf(TEXT("<referenceSourceFiles>\n%s\n</referenceSourceFiles>\n\n%s"),                                                                                                  
             *ReferenceFiles, *UserMessage);                                                                                                                                                               
     }                                                                                                                                                                                                     
                                                                                                                                                                                                           
     return bSuccess;
}

FString UN2CSystemPromptManager::GetLanguageSpecificPrompt(const FString& BasePromptKey, EN2CCodeLanguage Language) const
{
    const FString LanguageKey = GetLanguagePromptKey(BasePromptKey, Language);
    if (const FString* Prompt = LoadedPrompts.Find(LanguageKey))
    {
        return *Prompt;
    }

    // Fallback to base prompt if language-specific one not found
    FN2CLogger::Get().LogWarning(
        FString::Printf(TEXT("Language-specific prompt not found for key: %s, falling back to base prompt"), *LanguageKey),
        TEXT("SystemPromptManager")
    );
    
    return GetSystemPrompt(BasePromptKey);
}

FString UN2CSystemPromptManager::GetLanguagePromptKey(const FString& BasePromptKey, EN2CCodeLanguage Language)
{
    const FN2CTargetLanguage* Row = N2C::FindLanguageRow(Language);
    return Row ? FString::Printf(TEXT("%s_%s"), *BasePromptKey, Row->PromptKey) : BasePromptKey;
}

bool UN2CSystemPromptManager::HasPrompt(const FString& PromptKey) const
{
    return LoadedPrompts.Contains(PromptKey);
}

void UN2CSystemPromptManager::LoadPrompts()
{
    // Get plugin base directory using plugin manager
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
    if (!Plugin.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Could not find NodeToCode plugin! Translation will likely fail!"));
        return;
    }

    // Set prompts directory relative to plugin base
    PromptsDirectory = FPaths::Combine(Plugin->GetContentDir(), TEXT("Prompting"));
    
    if (!FPaths::DirectoryExists(PromptsDirectory))
    {
        FN2CLogger::Get().LogError(TEXT("Could not find NodeToCode Docs/Prompting directory! Translation will likely fail!"));
    }

    // Prompt families, one per translation source kind (only Blueprint graphs today). Each family has an optional base
    // prompt, <Family>.md, and one prompt per target language, <Family>_<PromptKey>.md, for every row of the table.
    const TArray<FString> BasePromptFiles = { N2C::BlueprintPromptFamily };

    // Shared fragments the prompts include by placeholder (one copy of the N2C JSON spec for every language)
    TMap<FString, FString> Fragments;
    Fragments.Add(JsonSpecPlaceholder, LoadSharedFragment(JsonSpecFragment));
    Fragments.Add(ResponseFormatPlaceholder, LoadSharedFragment(ResponseFormatFragment));

    // Load base and language-specific prompts
    for (const FString& PromptKey : BasePromptFiles)
    {
        // Load base prompt
        FString PromptContent;
        if (LoadPromptFromFile(GetPromptFilePath(PromptKey), PromptContent))
        {
            LoadedPrompts.Add(PromptKey, AssemblePrompt(PromptKey, PromptContent, Fragments));
        }

        // Load language-specific prompts
        for (const FN2CTargetLanguage& Language : N2C::GetLanguages())
        {
            const FString LanguageKey = GetLanguagePromptKey(PromptKey, Language.Enum);
            if (LoadPromptFromFile(GetPromptFilePath(LanguageKey), PromptContent))
            {
                LoadedPrompts.Add(LanguageKey, AssemblePrompt(LanguageKey, PromptContent, Fragments));
            }
        }
    }

    if (LoadedPrompts.Num() == 0)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to load any CodeGen system prompt files from Docs/Prompting. Translation will fail!"), TEXT("SystemPromptManager"));
        LoadedPrompts.Add(N2C::BlueprintPromptFamily, TEXT("You are an expert developer specializing in Unreal Engine Blueprint to code conversion."));
    }
    
}

bool UN2CSystemPromptManager::LoadPromptFromFile(const FString& FilePath, FString& OutContent)
{
    if (FPaths::FileExists(FilePath))
    {
        if (FFileHelper::LoadFileToString(OutContent, *FilePath))
        {
            // Clean up any Windows line endings
            OutContent.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
            return true;
        }
        
        FN2CLogger::Get().LogError(
            FString::Printf(TEXT("Failed to load prompt file: %s"), *FilePath),
            TEXT("SystemPromptManager")
        );
    }
    return false;
}

FString UN2CSystemPromptManager::GetPromptFilePath(const FString& PromptKey) const
{
    return FPaths::Combine(PromptsDirectory, PromptKey + TEXT(".md"));
}

FString UN2CSystemPromptManager::LoadSharedFragment(const FString& Name)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
    if (!Plugin.IsValid())
    {
        FN2CLogger::Get().LogError(TEXT("Could not find the NodeToCode plugin to load a shared prompt fragment"), TEXT("SystemPromptManager"));
        return FString();
    }

    const FString FilePath = FPaths::Combine(Plugin->GetContentDir(), TEXT("Prompting"), TEXT("Shared"), Name + TEXT(".md"));
    FString Content;
    if (!LoadPromptFromFile(FilePath, Content))
    {
        FN2CLogger::Get().LogError(FString::Printf(TEXT("Shared prompt fragment not found: %s. Translation prompts will lack it!"), *FilePath),
            TEXT("SystemPromptManager"));
        return FString();
    }

    // The placeholder sits on its own line in the prompt, so the fragment's own final line break is not needed
    Content.TrimEndInline();
    return Content;
}

FString UN2CSystemPromptManager::AssemblePrompt(const FString& PromptKey, const FString& Template, const TMap<FString, FString>& Fragments)
{
    FString Prompt = Template;
    for (const TPair<FString, FString>& Fragment : Fragments)
    {
        // Every CodeGen prompt includes each fragment once; a missing placeholder means a prompt edit dropped it
        if (!Prompt.Contains(Fragment.Key, ESearchCase::CaseSensitive))
        {
            FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Prompt %s has no %s placeholder"), *PromptKey, *Fragment.Key),
                TEXT("SystemPromptManager"));
            continue;
        }
        Prompt.ReplaceInline(*Fragment.Key, *Fragment.Value, ESearchCase::CaseSensitive);
    }
    return Prompt;
}

FString UN2CSystemPromptManager::WrapN2CJson(const FString& N2CJson)
{
    // "</" can only occur inside a JSON string (a node comment, a default value). "\/" is JSON's escape for "/", so
    // the JSON reads the same while no text in it can close the tag, in any letter case.
    const FString Escaped = N2CJson.Replace(TEXT("</"), TEXT("<\\/"), ESearchCase::CaseSensitive);
    return FString::Printf(TEXT("<n2c_json>\n%s\n</n2c_json>"), *Escaped);
}

FString UN2CSystemPromptManager::LoadReferenceSourceFiles() const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    if (!Settings || Settings->ReferenceSourceFilePaths.Num() == 0)
    {
        return FString();
    }

    FString Result;
    for (const FFilePath& FilePath : Settings->ReferenceSourceFilePaths)
    {
        FString Content;
        if (FFileHelper::LoadFileToString(Content, *FilePath.FilePath))
        {
            if (!Result.IsEmpty())
            {
                Result += TEXT("\n\n");
            }
            Result += FormatSourceFileContent(FilePath.FilePath, Content);
        }
        else
        {
            FN2CLogger::Get().LogWarning(
                FString::Printf(TEXT("Failed to load reference source file: %s"), *FilePath.FilePath),
                TEXT("SystemPromptManager")
            );
        }
    }

    return Result;
}

FString UN2CSystemPromptManager::FormatSourceFileContent(const FString& FilePath, const FString& Content) const
{
    // Extract filename from path
    FString Filename = FPaths::GetCleanFilename(FilePath);
    
    return FString::Printf(
        TEXT("File: %s\n```\n%s\n```"),
        *Filename,
        *Content
    );
}
