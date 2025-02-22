// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "LLM/N2CSystemPromptManager.h"

#include "Core/N2CSettings.h"
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

FString UN2CSystemPromptManager::GetLanguageSpecificPrompt(const FString& BasePromptKey, EN2CTargetLanguage Language) const
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

FString UN2CSystemPromptManager::GetLanguagePromptKey(const FString& BasePromptKey, EN2CTargetLanguage Language) const
{
    switch (Language)
    {
        case EN2CTargetLanguage::Cpp:
            return FString::Printf(TEXT("%s_CPP"), *BasePromptKey);
        case EN2CTargetLanguage::Python:
            return FString::Printf(TEXT("%s_Python"), *BasePromptKey);
        case EN2CTargetLanguage::JavaScript:
            return FString::Printf(TEXT("%s_JavaScript"), *BasePromptKey);
        case EN2CTargetLanguage::CSharp:
            return FString::Printf(TEXT("%s_CSharp"), *BasePromptKey);
        case EN2CTargetLanguage::Swift:
            return FString::Printf(TEXT("%s_Swift"), *BasePromptKey);
        default:
            return BasePromptKey;
    }
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

    // Base prompts
    TArray<FString> BasePromptFiles;
    BasePromptFiles.Add(TEXT("CodeGen"));

    // Languages to load prompts for
    TArray<EN2CTargetLanguage> Languages;
    Languages.Add(EN2CTargetLanguage::Cpp);
    Languages.Add(EN2CTargetLanguage::Python);
    Languages.Add(EN2CTargetLanguage::JavaScript);
    Languages.Add(EN2CTargetLanguage::CSharp);
    Languages.Add(EN2CTargetLanguage::Swift);

    // Load base and language-specific prompts
    for (const FString& PromptKey : BasePromptFiles)
    {
        // Load base prompt
        FString PromptContent;
        if (LoadPromptFromFile(GetPromptFilePath(PromptKey), PromptContent))
        {
            LoadedPrompts.Add(PromptKey, PromptContent);
        }

        // Load language-specific prompts
        for (EN2CTargetLanguage Language : Languages)
        {
            const FString LanguageKey = GetLanguagePromptKey(PromptKey, Language);
            if (LoadPromptFromFile(GetPromptFilePath(LanguageKey), PromptContent))
            {
                LoadedPrompts.Add(LanguageKey, PromptContent);
            }
        }
    }

    if (LoadedPrompts.Num() == 0)
    {
        FN2CLogger::Get().LogError(TEXT("Failed to load any CodeGen system prompt files from Docs/Prompting. Translation will fail!"), TEXT("SystemPromptManager"));
        LoadedPrompts.Add(TEXT("CodeGen"), TEXT("You are an expert developer specializing in Unreal Engine Blueprint to code conversion."));
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
