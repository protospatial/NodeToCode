// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Server/N2CMcpCompletion.h"
#include "MCP/Prompts/N2CMcpPromptManager.h"
#include "MCP/Resources/Implementations/N2CMcpBlueprintResource.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Modules/ModuleManager.h"

TArray<FString> N2CMcpCompletion::BlueprintPaths(const FString& Prefix)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	TSet<FString> Paths;
	for (const FAssetData& Asset : Assets)
	{
		FString Path = Asset.PackageName.ToString();
		// Engine content and transient packages are never offered
		if (Path.StartsWith(TEXT("/Engine/")) || Path.StartsWith(TEXT("/Script/")) || Path.StartsWith(TEXT("/Temp/"))
			|| Path.StartsWith(TEXT("/Memory/")))
		{
			continue;
		}
		Path.RemoveFromStart(TEXT("/"));
		if (Prefix.IsEmpty() || Path.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			Paths.Add(Path);
		}
	}
	TArray<FString> Sorted = Paths.Array();
	Sorted.Sort();
	return Sorted;
}

TSharedPtr<FJsonObject> N2CMcpCompletion::Complete(const TSharedPtr<FJsonObject>& Params, const FN2CMcpPromptManager& Prompts, FString& OutError)
{
	const TSharedPtr<FJsonObject>* Ref = nullptr;
	const TSharedPtr<FJsonObject>* Argument = nullptr;
	FString RefType, ArgumentName, Value;
	if (!Params.IsValid() || !Params->TryGetObjectField(TEXT("ref"), Ref) || !Ref->IsValid() || !(*Ref)->TryGetStringField(TEXT("type"), RefType))
	{
		OutError = TEXT("ref is required: {type: \"ref/prompt\", name} or {type: \"ref/resource\", uri}");
		return nullptr;
	}
	if (!Params->TryGetObjectField(TEXT("argument"), Argument) || !Argument->IsValid() || !(*Argument)->TryGetStringField(TEXT("name"), ArgumentName))
	{
		OutError = TEXT("argument is required: {name, value}");
		return nullptr;
	}
	(*Argument)->TryGetStringField(TEXT("value"), Value);

	TArray<FString> Candidates;
	if (RefType == TEXT("ref/prompt"))
	{
		FString PromptName;
		(*Ref)->TryGetStringField(TEXT("name"), PromptName);
		const TArray<FMcpPromptDefinition> Definitions = Prompts.ListPrompts();
		const FMcpPromptDefinition* Definition = Definitions.FindByPredicate([&PromptName](const FMcpPromptDefinition& Each) { return Each.Name == PromptName; });
		if (!Definition)
		{
			OutError = FString::Printf(TEXT("ref names no prompt of this server: '%s'"), *PromptName.Left(80));
			return nullptr;
		}
		if (const FMcpPromptArgument* Found = Definition->Arguments.FindByPredicate([&ArgumentName](const FMcpPromptArgument& Each) { return Each.Name == ArgumentName; }))
		{
			for (const FString& Candidate : Found->CompletionValues)
			{
				if (Value.IsEmpty() || Candidate.StartsWith(Value, ESearchCase::IgnoreCase))
				{
					Candidates.Add(Candidate);
				}
			}
		}
	}
	else if (RefType == TEXT("ref/resource"))
	{
		FString Uri;
		(*Ref)->TryGetStringField(TEXT("uri"), Uri);
		if (Uri != FN2CMcpBlueprintByNameResource::UriTemplate)
		{
			OutError = FString::Printf(TEXT("ref names no resource template of this server: '%s' (completion covers %s)"),
				*Uri.Left(120), FN2CMcpBlueprintByNameResource::UriTemplate);
			return nullptr;
		}
		if (ArgumentName == TEXT("path"))
		{
			Candidates = BlueprintPaths(Value);
		}
	}
	else
	{
		OutError = FString::Printf(TEXT("ref.type must be ref/prompt or ref/resource, not '%s'"), *RefType.Left(40));
		return nullptr;
	}

	const int32 Total = Candidates.Num();
	TArray<TSharedPtr<FJsonValue>> Values;
	for (int32 Index = 0; Index < FMath::Min(Total, MaxValues); ++Index)
	{
		Values.Add(MakeShared<FJsonValueString>(Candidates[Index]));
	}
	const TSharedRef<FJsonObject> Completion = MakeShared<FJsonObject>();
	Completion->SetArrayField(TEXT("values"), Values);
	Completion->SetNumberField(TEXT("total"), Total);
	Completion->SetBoolField(TEXT("hasMore"), Total > MaxValues);
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("completion"), Completion);
	return Result;
}
