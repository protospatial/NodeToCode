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

    /**
     * The key of a language's prompt in a prompt family: <BasePromptKey>_<PromptKey> from the target-language table
     * (CodeGen_CPP, CodeGen_Python, ...), or BasePromptKey for a value the table does not describe
     */
    static FString GetLanguagePromptKey(const FString& BasePromptKey, EN2CCodeLanguage Language);

    /** Whether a prompt was loaded under this key (no fallback, no warning) */
    bool HasPrompt(const FString& PromptKey) const;

    /** Merge system and user prompts if needed */
    UFUNCTION(BlueprintCallable, Category = "Node to Code | LLM Prompting")
    FString MergePrompts(const FString& SystemPrompt, const FString& UserMessage) const;
    
    /** Prepend reference source files to user message */                                                                                                                                                 
    bool PrependSourceFilesToUserMessage(FString& UserMessage) const; 

    /** Initialize with configuration */
    void Initialize(const FN2CLLMConfig& Config);

    /**
     * Placeholders a CodeGen_*.md prompt holds for the shared fragments in Content/Prompting/Shared, replaced when
     * the prompts load: the N2C JSON specification (Shared/N2CJsonSpec.md) and the response contract
     * (Shared/N2CResponseFormat.md, which matches UN2CLLMPayloadBuilder::GetN2CResponseSchema).
     */
    static constexpr const TCHAR* JsonSpecPlaceholder = TEXT("{{N2C_JSON_SPEC}}");
    static constexpr const TCHAR* ResponseFormatPlaceholder = TEXT("{{N2C_RESPONSE_FORMAT}}");

    /** Fragment names (file names without .md under Content/Prompting/Shared) */
    static constexpr const TCHAR* JsonSpecFragment = TEXT("N2CJsonSpec");
    static constexpr const TCHAR* ResponseFormatFragment = TEXT("N2CResponseFormat");

    /**
     * A shared prompt fragment, Content/Prompting/Shared/<Name>.md, with Windows line endings normalised and the
     * trailing line break removed. Empty (and an error logged) when the file is missing.
     */
    static FString LoadSharedFragment(const FString& Name);

    /** The N2C JSON specification every CodeGen prompt and the batch export's Combined_Blueprints.md include */
    static FString GetN2CJsonSpec() { return LoadSharedFragment(JsonSpecFragment); }

    /**
     * The user message for a translation: the N2C JSON inside <n2c_json> tags, which the specification tells the
     * model to read as data, not instructions. Every "</" in the JSON (only possible in a string value, such as a
     * node comment) is written "<\/", the same JSON string, so nothing in it can close the tag early.
     */
    static FString WrapN2CJson(const FString& N2CJson);

private:
    /** Load prompts from configuration */
    void LoadPrompts();

    /** Load prompt from file */
    static bool LoadPromptFromFile(const FString& FilePath, FString& OutContent);

    /** Replace the shared-fragment placeholders in a loaded prompt (each fragment loaded once per LoadPrompts) */
    static FString AssemblePrompt(const FString& PromptKey, const FString& Template, const TMap<FString, FString>& Fragments);

    /** Get prompt file path */
    FString GetPromptFilePath(const FString& PromptKey) const;

    /** Load and format reference source files */
    FString LoadReferenceSourceFiles() const;

    /** Format a single source file for inclusion */
    FString FormatSourceFileContent(const FString& FilePath, const FString& Content) const;


    /** Map of loaded prompts */
    TMap<FString, FString> LoadedPrompts;

    /** Base directory for prompt files */
    FString PromptsDirectory;
};
