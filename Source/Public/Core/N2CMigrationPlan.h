// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FN2CDependencyIndex;

/** What to plan (card 0323) */
struct NODETOCODE_API FN2CMigrationPlanOptions
{
	/** Asset paths and content folders, the roots of the dependency index (FN2CDependencyIndex::Build) */
	TArray<FString> Roots;
	/** The C++ module the native classes go into (its name; ModuleSourceDir is its Source/<Module> folder) */
	FString TargetModule;
	/** <Module>/Source/<Module>, absolute: the generated files are proposed under it */
	FString ModuleSourceDir;
	/** Asset paths to leave out; they stay in the order, marked excluded, so the plan still shows what needs them */
	TArray<FString> Exclude;
	/** Also plan the project's own assets the roots depend on outside the roots (FN2CDependencyIndexOptions) */
	bool bIncludeReferenced = true;
	/** Estimate each graph's tokens (FN2CTokenEstimationService, a capture per graph); false leaves them 0 */
	bool bEstimateTokens = true;
};

/** A plan written to disk, or why it was not */
struct NODETOCODE_API FN2CMigrationPlanResult
{
	/** "<CODE>: <message>" on failure (NOT_FOUND, PLAN_WRITE_FAILED) */
	FString Error;
	FString PlanId;
	/** Saved/NodeToCode/Migration/<PlanId>, absolute */
	FString PlanDir;
	FString PlanJsonPath;
	FString PlanMdPath;
	/** plan.json as written */
	TSharedPtr<FJsonObject> Plan;

	bool IsValid() const { return Error.IsEmpty() && Plan.IsValid(); }
};

/**
 * A reviewable migration plan built from the dependency index (card 0323, review rec-agentic-landscape-10 steps 1-2,
 * issue-12): the first review gate of a Blueprint-to-C++ migration. It proposes and changes nothing outside
 * Saved/NodeToCode/Migration/<plan-id>, where it writes plan.json (the machine-readable plan, which later stages read
 * and require to be "approved") and plan.md (the same for a person).
 *
 * Steps follow FN2CDependencyIndex's dependencies-first order: enums, structs, interfaces and macro libraries before the
 * Blueprints that use them, and each cycle group as one step. Each asset gets a proposed native type (F/E/U/I/A plus
 * the asset name without its prefix) and header and source paths in the target module; each Blueprint also lists the
 * graphs to migrate with estimated tokens and its risk flags (latent nodes, timelines, macro instances, node classes
 * without a direct C++ counterpart, cycles, name collisions). The plan starts as status "draft" with the given
 * exclude list; the owner edits status and exclude in plan.json.
 */
struct NODETOCODE_API FN2CMigrationPlan
{
	/** Version of plan.json's layout */
	static constexpr int32 SchemaVersion = 1;

	/** Builds the index over Options.Roots (assets loaded) and writes the plan. Game thread. */
	static FN2CMigrationPlanResult Create(const FN2CMigrationPlanOptions& Options);

	/** The plan for an index already built (no files written); exclude paths that name nothing go to its warnings */
	static TSharedRef<FJsonObject> Build(const FN2CDependencyIndex& Index, const FN2CMigrationPlanOptions& Options,
		const FString& PlanId);

	/** plan.md: a summary, how to approve or exclude, and one table row per asset in step order */
	static FString ToMarkdown(const TSharedRef<FJsonObject>& Plan);

	/** Saved/NodeToCode/Migration, absolute (a test may point it elsewhere) */
	static FString PlansDirectory();

	/** The asset name without a known prefix (BP_, BPI_, BPML_, WBP_, ABP_, S_, F_, E_, ...) and with only letters, digits and _ */
	static FString NativeStem(const FString& AssetName);

#if WITH_DEV_AUTOMATION_TESTS
	/** Points PlansDirectory at Directory (empty: back to Saved/NodeToCode/Migration) */
	static void SetPlansDirectoryForTests(const FString& Directory);
#endif
};
