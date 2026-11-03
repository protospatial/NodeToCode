// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpCreateMigrationPlanTool.h"
#include "Core/N2CMigrationPlan.h"
#include "MCP/Tools/N2CMcpOutputSchema.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/Implementations/Translation/N2CCppPlacement.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

REGISTER_MCP_TOOL(FN2CMcpCreateMigrationPlanTool)

namespace N2CMcpCreateMigrationPlanToolPrivate
{
	TSharedRef<FJsonObject> StringArray(const FString& Description, int32 MinItems, int32 MaxItems)
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("type"), TEXT("string"));
		const TSharedRef<FJsonObject> Array = MakeShared<FJsonObject>();
		Array->SetStringField(TEXT("type"), TEXT("array"));
		Array->SetObjectField(TEXT("items"), Item);
		Array->SetNumberField(TEXT("minItems"), MinItems);
		Array->SetNumberField(TEXT("maxItems"), MaxItems);
		Array->SetStringField(TEXT("description"), Description);
		return Array;
	}

	/** The strings of an optional array argument; false with OutError when it is there but not an array of strings */
	bool ReadStrings(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, int32 MaxItems, TArray<FString>& Out, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Field);
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return true;
		}
		if (Value->Type != EJson::Array)
		{
			OutError = FString::Printf(TEXT("INVALID_ARGUMENT: %s must be an array of asset paths"), Field);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
		if (Items.Num() > MaxItems)
		{
			OutError = FString::Printf(TEXT("INVALID_ARGUMENT: %s has %d entries, at most %d"), Field, Items.Num(), MaxItems);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			if (!Item.IsValid() || Item->Type != EJson::String)
			{
				OutError = FString::Printf(TEXT("INVALID_ARGUMENT: every entry of %s must be a string"), Field);
				return false;
			}
			const FString Path = Item->AsString().TrimStartAndEnd();
			if (!Path.StartsWith(TEXT("/")))
			{
				OutError = FString::Printf(TEXT("INVALID_ARGUMENT: '%s' in %s is not an asset path or content folder (it starts with /, e.g. /Game/Blueprints)"),
					*Item->AsString(), Field);
				return false;
			}
			Out.AddUnique(Path);
		}
		return true;
	}

	bool ReadFlag(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Field, bool& InOutValue, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Field);
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return true;
		}
		if (Value->Type != EJson::Boolean)
		{
			OutError = FString::Printf(TEXT("INVALID_ARGUMENT: %s must be true or false"), Field);
			return false;
		}
		InOutValue = Value->AsBool();
		return true;
	}
}

FMcpToolDefinition FN2CMcpCreateMigrationPlanTool::GetDefinition() const
{
	using namespace N2CMcpCreateMigrationPlanToolPrivate;

	FMcpToolDefinition Definition(
		TEXT("create-migration-plan"),
		TEXT("Writes a reviewable plan for migrating Blueprints to C++, built from the dependency index of the assets under ")
		TEXT("asset paths or content folders. It changes no asset and no source file: it writes plan.json and plan.md to ")
		TEXT("Saved/NodeToCode/Migration/<planId>. Steps are in dependency order (enums, structs, interfaces and macro ")
		TEXT("libraries before the Blueprints that use them; a cycle group is one step). Each asset gets a proposed native ")
		TEXT("type and header and source paths in targetModule; each Blueprint lists its graphs with estimated tokens and its ")
		TEXT("risk flags (latent, timeline, macro, blueprint-only-nodes, cycle, parent-not-in-plan, name-collision). The plan ")
		TEXT("starts as status draft: show plan.md to the user, who approves it by setting status to approved in plan.json ")
		TEXT("and can add asset paths to its exclude list. The result summarizes the steps; read plan.json for every detail."),
		N2CMcpToolsets::Translation
	);

	const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("paths"), StringArray(FString::Printf(
		TEXT("Required, 1 to %d entries. Asset paths (/Game/Blueprints/BP_Door) and content folders (/Game/Blueprints, with its subfolders) to migrate."), MaxPaths), 1, MaxPaths));
	{
		const TSharedRef<FJsonObject> Module = MakeShared<FJsonObject>();
		Module->SetStringField(TEXT("type"), TEXT("string"));
		Module->SetStringField(TEXT("description"), TEXT("Required. The C++ module of the project (or of a plugin in the project) the native classes are proposed in, by name, e.g. MyGame."));
		Properties->SetObjectField(TEXT("targetModule"), Module);
	}
	Properties->SetObjectField(TEXT("exclude"), StringArray(FString::Printf(
		TEXT("Optional, up to %d entries. Asset paths to leave out; they stay in the steps marked excluded, so the plan shows what needs them."), MaxPaths), 0, MaxPaths));
	auto Flag = [&Properties](const TCHAR* Name, const TCHAR* Description)
	{
		const TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
		Property->SetStringField(TEXT("type"), TEXT("boolean"));
		Property->SetBoolField(TEXT("default"), true);
		Property->SetStringField(TEXT("description"), Description);
		Properties->SetObjectField(Name, Property);
	};
	Flag(TEXT("includeReferenced"), TEXT("Optional, default true. Also plan the project's own assets the paths depend on outside them, so they come first."));
	Flag(TEXT("estimateTokens"), TEXT("Optional, default true. Estimate each graph's tokens (one capture per graph); false is faster and leaves them 0."));

	const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Properties);
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("paths")));
	Required.Add(MakeShared<FJsonValueString>(TEXT("targetModule")));
	Schema->SetArrayField(TEXT("required"), Required);
	Definition.InputSchema = Schema;

	{
		using namespace N2CMcpOutputSchema;
		Definition.OutputSchema = UntrustedObject({
			{ TEXT("planId"), String(TEXT("<yyyyMMdd-HHmmss>-<6 hex>, the plan folder's name")) },
			{ TEXT("status"), Enum({ TEXT("draft") }, TEXT("A new plan is a draft until the user approves it in plan.json")) },
			{ TEXT("planDir"), String() },
			{ TEXT("planJson"), String(TEXT("plan.json: the full plan, which later stages read")) },
			{ TEXT("planMd"), String(TEXT("plan.md: the plan for a person to review")) },
			{ TEXT("targetModule"), String() },
			{ TEXT("stepCount"), Integer() },
			{ TEXT("assetCount"), Integer() },
			{ TEXT("blueprintCount"), Integer() },
			{ TEXT("excludedCount"), Integer() },
			{ TEXT("estimatedTokens"), Integer(TEXT("Of the Blueprints not excluded")) },
			{ TEXT("riskCounts"), Object({}, {}, TEXT("Per risk flag, how many assets have it")) },
			{ TEXT("steps"), Array(Object({
				{ TEXT("step"), Integer() },
				{ TEXT("kind"), Enum({ TEXT("asset"), TEXT("cycle-group") }) },
				{ TEXT("excluded"), Boolean() },
				{ TEXT("assets"), Array(String(), TEXT("Object paths")) },
			}, { TEXT("step"), TEXT("kind"), TEXT("excluded"), TEXT("assets") })) },
			{ TEXT("warnings"), Array(String()) },
		}, { TEXT("planId"), TEXT("status"), TEXT("planJson"), TEXT("planMd"), TEXT("stepCount"), TEXT("steps"), TEXT("warnings") });
	}

	// Writes only under Saved/NodeToCode/Migration, a new folder per call: not read-only, not destructive, not idempotent
	SetAnnotations(Definition, /*bReadOnly*/ false, /*bDestructive*/ false, /*bIdempotent*/ false, /*bOpenWorld*/ false);
	// It changes no Blueprint, asset or level, so it keeps working during Play In Editor (card 0170)
	Definition.bAllowedDuringPlaySession = true;

	// Results carry asset names from the project: marked as untrusted data (card 0124)
	Definition.bReturnsUntrustedContent = true;

	return Definition;
}

FMcpToolCallResult FN2CMcpCreateMigrationPlanTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
	using namespace N2CMcpCreateMigrationPlanToolPrivate;

	FN2CMigrationPlanOptions Options;
	FString Error;
	if (!Arguments.IsValid())
	{
		return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: paths and targetModule are required"));
	}
	if (!ReadStrings(Arguments, TEXT("paths"), MaxPaths, Options.Roots, Error)
		|| !ReadStrings(Arguments, TEXT("exclude"), MaxPaths, Options.Exclude, Error)
		|| !ReadFlag(Arguments, TEXT("includeReferenced"), Options.bIncludeReferenced, Error)
		|| !ReadFlag(Arguments, TEXT("estimateTokens"), Options.bEstimateTokens, Error))
	{
		return FMcpToolCallResult::CreateErrorResult(Error);
	}
	if (Options.Roots.Num() == 0)
	{
		return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: paths is required, asset paths or content folders such as [\"/Game/Blueprints\"]"));
	}
	if (!Arguments->TryGetStringField(TEXT("targetModule"), Options.TargetModule) || Options.TargetModule.TrimStartAndEnd().IsEmpty())
	{
		return FMcpToolCallResult::CreateErrorResult(TEXT("INVALID_ARGUMENT: targetModule is required, the name of a C++ module of the project"));
	}
	Options.TargetModule.TrimStartAndEndInline();

	return ExecuteOnGameThread([Options]() mutable -> FMcpToolCallResult
	{
		FString ModuleError;
		if (!FN2CCppPlacement::FindModule(Options.TargetModule, Options.ModuleSourceDir, ModuleError))
		{
			return FMcpToolCallResult::CreateErrorResult(TEXT("MODULE_NOT_FOUND: ") + ModuleError);
		}

		const FN2CMigrationPlanResult Result = FN2CMigrationPlan::Create(Options);
		if (!Result.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(Result.Error);
		}

		const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetStringField(TEXT("planId"), Result.PlanId);
		Summary->SetStringField(TEXT("status"), Result.Plan->GetStringField(TEXT("status")));
		Summary->SetStringField(TEXT("planDir"), Result.PlanDir);
		Summary->SetStringField(TEXT("planJson"), Result.PlanJsonPath);
		Summary->SetStringField(TEXT("planMd"), Result.PlanMdPath);
		Summary->SetStringField(TEXT("targetModule"), Options.TargetModule);
		for (const TCHAR* Field : { TEXT("stepCount"), TEXT("assetCount"), TEXT("blueprintCount"), TEXT("excludedCount"), TEXT("estimatedTokens") })
		{
			Summary->SetNumberField(Field, Result.Plan->GetNumberField(Field));
		}
		Summary->SetObjectField(TEXT("riskCounts"), Result.Plan->GetObjectField(TEXT("riskCounts")));
		TArray<TSharedPtr<FJsonValue>> Steps;
		for (const TSharedPtr<FJsonValue>& StepValue : Result.Plan->GetArrayField(TEXT("steps")))
		{
			const TSharedPtr<FJsonObject> Step = StepValue->AsObject();
			const TSharedRef<FJsonObject> Brief = MakeShared<FJsonObject>();
			Brief->SetNumberField(TEXT("step"), Step->GetNumberField(TEXT("step")));
			Brief->SetStringField(TEXT("kind"), Step->GetStringField(TEXT("kind")));
			Brief->SetBoolField(TEXT("excluded"), Step->GetBoolField(TEXT("excluded")));
			TArray<TSharedPtr<FJsonValue>> Assets;
			for (const TSharedPtr<FJsonValue>& Asset : Step->GetArrayField(TEXT("assets")))
			{
				Assets.Add(MakeShared<FJsonValueString>(Asset->AsObject()->GetStringField(TEXT("path"))));
			}
			Brief->SetArrayField(TEXT("assets"), Assets);
			Steps.Add(MakeShared<FJsonValueObject>(Brief));
		}
		Summary->SetArrayField(TEXT("steps"), Steps);
		Summary->SetArrayField(TEXT("warnings"), Result.Plan->GetArrayField(TEXT("warnings")));
		return FMcpToolCallResult::CreateJsonResult(Summary);
	}, TimeoutSeconds);
}
