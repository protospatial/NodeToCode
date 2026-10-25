// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Prompts/N2CMcpPromptTypes.h"
#include "Async/Async.h"
#include "Code Editor/Models/N2CCodeLanguage.h"

/** A target language the Blueprint prompts accept: its argument value, display name and CodeGen prompt language */
struct FN2CMcpPromptLanguage
{
	FString Id;
	FString DisplayName;
	EN2CCodeLanguage Language = EN2CCodeLanguage::Cpp;
};

/**
 * What generate-code, analyze-blueprint and refactor-blueprint share (card 0124): the language argument's allowed
 * values, the CodeGen instructions for a language, and the Blueprint messages, which put the untrusted-data notice
 * right before the embedded nodetocode://blueprint/current resource. Public for the NodeToCode.MCP.UntrustedContent spec.
 */
class NODETOCODE_API FN2CMcpBlueprintPrompts
{
public:
	/** cpp, python, csharp, javascript, swift and pseudocode, in that order */
	static const TArray<FN2CMcpPromptLanguage>& Languages();

	/** "cpp, python, csharp, javascript, swift, pseudocode" for argument descriptions and errors */
	static FString LanguageIdList();

	/**
	 * The language a prompt's "language" argument names (case-insensitive; cpp when absent or empty). False, with
	 * OutError set to a JSON-RPC invalid-params error listing the allowed values, for anything else.
	 */
	static bool ResolveLanguage(const FMcpPromptArguments& Arguments, FN2CMcpPromptLanguage& OutLanguage, FMcpPromptResult& OutError);

	/**
	 * The CodeGen system prompt for Language, assembled by UN2CSystemPromptManager exactly as the in-editor
	 * translation uses it (Content/Prompting/CodeGen_<Language>.md with the shared fragments). Game thread.
	 */
	static FString CodeGenInstructions(EN2CCodeLanguage Language);

	/**
	 * Appends the Blueprint to a prompt: a text message holding FN2CMcpUntrustedContent::Notice, then the resource
	 * as an embedded resource with annotations.audience ["assistant"] and its text marked (MarkJson, which also
	 * escapes "</"). These are always the last two messages, so every instruction comes before the untrusted text.
	 */
	static void AppendBlueprint(FMcpPromptResult& Result, const FMcpResourceContents& Blueprint);
};

/**
 * MCP Prompt for generating code from the current Blueprint
 */
class NODETOCODE_API FN2CMcpCodeGenerationPrompt : public IN2CMcpPrompt
{
public:
	virtual FMcpPromptDefinition GetDefinition() const override;
	virtual FMcpPromptResult GetPrompt(const FMcpPromptArguments& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

protected:
	/**
	 * Helper to execute code on the game thread
	 */
	template<typename ResultType>
	static ResultType ExecuteOnGameThread(TFunction<ResultType()> Function)
	{
		if (IsInGameThread())
		{
			return Function();
		}
		
		TPromise<ResultType> Promise;
		TFuture<ResultType> Future = Promise.GetFuture();
		
		AsyncTask(ENamedThreads::GameThread, [Function = MoveTemp(Function), Promise = MoveTemp(Promise)]() mutable
		{
			Promise.SetValue(Function());
		});
		
		return Future.Get();
	}
};

/**
 * MCP Prompt for analyzing a Blueprint
 */
class NODETOCODE_API FN2CMcpBlueprintAnalysisPrompt : public IN2CMcpPrompt
{
public:
	virtual FMcpPromptDefinition GetDefinition() const override;
	virtual FMcpPromptResult GetPrompt(const FMcpPromptArguments& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

protected:
	template<typename ResultType>
	static ResultType ExecuteOnGameThread(TFunction<ResultType()> Function)
	{
		if (IsInGameThread())
		{
			return Function();
		}
		
		TPromise<ResultType> Promise;
		TFuture<ResultType> Future = Promise.GetFuture();
		
		AsyncTask(ENamedThreads::GameThread, [Function = MoveTemp(Function), Promise = MoveTemp(Promise)]() mutable
		{
			Promise.SetValue(Function());
		});
		
		return Future.Get();
	}
};

/**
 * MCP Prompt for refactoring Blueprint code
 */
class NODETOCODE_API FN2CMcpRefactorPrompt : public IN2CMcpPrompt
{
public:
	virtual FMcpPromptDefinition GetDefinition() const override;
	virtual FMcpPromptResult GetPrompt(const FMcpPromptArguments& Arguments) override;
	virtual bool RequiresGameThread() const override { return true; }

protected:
	template<typename ResultType>
	static ResultType ExecuteOnGameThread(TFunction<ResultType()> Function)
	{
		if (IsInGameThread())
		{
			return Function();
		}

		TPromise<ResultType> Promise;
		TFuture<ResultType> Future = Promise.GetFuture();

		AsyncTask(ENamedThreads::GameThread, [Function = MoveTemp(Function), Promise = MoveTemp(Promise)]() mutable
		{
			Promise.SetValue(Function());
		});

		return Future.Get();
	}
};

/**
 * MCP Prompt for writing Unreal Engine Python scripts with Context7 documentation lookup.
 * Enforces usage of Context7 MCP to look up the unreal-python-stubhub library documentation
 * and ensures proper usage of the script management tools for saving/reusing scripts.
 */
class NODETOCODE_API FN2CMcpPythonScriptingPrompt : public IN2CMcpPrompt
{
public:
	virtual FMcpPromptDefinition GetDefinition() const override;
	virtual FMcpPromptResult GetPrompt(const FMcpPromptArguments& Arguments) override;
	virtual bool RequiresGameThread() const override { return false; }
};