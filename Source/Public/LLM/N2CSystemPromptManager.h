// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CLLMTypes.h"
#include "N2CSystemPromptManager.generated.h"

/**
 * @class UN2CSystemPromptManager
 * @brief Manages system prompts for LLM interactions
 */
UCLASS()
class NODETOCODE_API UN2CSystemPromptManager : public UObject
{
    GENERATED_BODY()

public:
    /** Whether the target LLM supports separate system prompts */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Prompting")
    bool bSupportsSystemPrompts = true;

    /** Get system prompt by key */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Prompting")
    FString GetSystemPrompt(const FString& PromptKey) const;

    /** Get system prompt for specific language */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Prompting")
    FString GetLanguageSpecificPrompt(const FString& BasePromptKey, EN2CCodeLanguage Language) const;

    /** Merge system and user prompts if needed */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Prompting")
    FString MergePrompts(const FString& SystemPrompt, const FString& UserMessage) const;
    
    /** Prepend reference source files to user message */                                                                                                                                                 
    bool PrependSourceFilesToUserMessage(FString& UserMessage) const; 

    /** Initialize with configuration */
    void Initialize(const FN2CLLMConfig& Config);

private:
    /** Load prompts from configuration */
    void LoadPrompts();

    /** Load prompt from file */
    bool LoadPromptFromFile(const FString& FilePath, FString& OutContent);

    /** Get prompt file path */
    FString GetPromptFilePath(const FString& PromptKey) const;

    /** Load and format reference source files */
    FString LoadReferenceSourceFiles() const;

    /** Format a single source file for inclusion */
    FString FormatSourceFileContent(const FString& FilePath, const FString& Content) const;

    /** Get language-specific prompt key */
    FString GetLanguagePromptKey(const FString& BasePromptKey, EN2CCodeLanguage Language) const;

    /** Map of loaded prompts */
    TMap<FString, FString> LoadedPrompts;

    /** Base directory for prompt files */
    FString PromptsDirectory;
};
