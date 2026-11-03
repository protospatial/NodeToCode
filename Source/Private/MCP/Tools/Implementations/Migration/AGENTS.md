# Migration Tools

This directory holds `create-migration-plan` (card 0323), a `TranslationTools` tool that writes the first review gate
of a Blueprint-to-C++ migration: a plan built from the dependency index (`FN2CDependencyIndex`, card 0318). The plan
service is `FN2CMigrationPlan` (`Source/Public/Core/N2CMigrationPlan.h`, `Source/Private/Core/N2CMigrationPlan.cpp`),
which the later migration stages (cards 0324-0326: convert one Blueprint, redirect call sites, a commandlet) read.

## Files

- `N2CMcpCreateMigrationPlanTool.h/.cpp`: the tool, its schemas and argument checks; it finds the target module with
  `FN2CCppPlacement::FindModule` (a module of the project or of a plugin in the project) and calls
  `FN2CMigrationPlan::Create`

### create-migration-plan
- **Description**: Builds the dependency index of the assets under asset paths or content folders (assets loaded) and
  writes a plan to `<Project>/Saved/NodeToCode/Migration/<planId>/` (`plan.json` and `plan.md`). It changes no asset
  and no source file. `planId` is `<yyyyMMdd-HHmmss>-<6 hex>` (UTC), so every call writes a new folder. It is not
  read-only (it writes the plan) but keeps working during Play In Editor
- **Arguments** (types, required and defaults: [reference](../../../../../../Context/mcp-server/tools.generated.md#create-migration-plan)):
  - `paths` (required, 1 to 100): asset paths and content folders to migrate
  - `targetModule` (required): the C++ module the native types are proposed in
  - `exclude` (up to 100): asset paths to leave out; they stay in the steps marked `excluded`
  - `includeReferenced` (default true): also plan the project's own assets the paths depend on
  - `estimateTokens` (default true): one capture per graph (`FN2CTokenEstimationService`, what the Translate button
    would send at the configured Translation Depth); false leaves the estimates 0
- **plan.json** (`schemaVersion` 1): `planId`, `status` (`draft`; the owner sets `approved`, and later stages run
  only an approved plan), `approval` (that instruction), `createdAt`, `roots`, `targetModule`, `moduleSourceDir`,
  `exclude` (object paths; the owner edits it), `options`, `stepCount`, `assetCount`, `blueprintCount`,
  `excludedCount`, `estimatedTokens` (of the Blueprints not excluded), `riskCounts` (flag: assets), `steps` and
  `warnings` (index warnings and exclude paths that named nothing).
  - Each step: `step` (from 1), `kind` (`asset`, or `cycle-group` for assets that depend on each other, planned as
    one step), `excluded` (every asset of it excluded) and `assets`. Steps follow the index's dependencies-first order:
    enums, structs, interfaces and macro libraries before the Blueprints that use them
  - Each asset: `path`, `name`, `kind`, `root`, `excluded`, `native` (`name`: `E`, `F`, `U` or, for a Blueprint, `A`
    when its parent is an Actor and `U` otherwise, plus the asset name without its prefix (`FN2CMigrationPlan::NativeStem`);
    `interfaceName` for an interface; `header`, and `source` for Blueprints and interfaces, under the module's
    `Public/` and `Private/` when it has both, else its Source folder, relative to the project). A macro library has
    no `native`. A Blueprint also has `parentClass`, `nativeBase` (the parent's native class, or the planned class of
    a parent Blueprint in the plan), `graphs` (event graphs, functions and macros of a supported type: `name`,
    `graphGuid`, `kind`, `nodeCount`, `estimatedTokens`) and `estimatedTokens`
  - Each asset's `risks` (`flag`, `count`, `detail`): `latent` (latent calls and async actions), `timeline`,
    `macro` (macro instances), `blueprint-only-nodes` (node classes without a direct C++ counterpart, listed with
    counts), `cycle` (the other members), `parent-not-in-plan`, `name-collision` (two assets would share a file name
    once their prefixes are dropped; the later one gets 2, 3, ...), `macro-library`
- **plan.md**: the summary, how to approve or exclude, and one table row per asset in step order (cycle steps marked)
- **Returns** (structured content, `outputSchema`; marked as untrusted data, card 0124): `planId`, `status`,
  `planDir`, `planJson`, `planMd`, `targetModule`, the counts, `estimatedTokens`, `riskCounts`, `steps` (`step`,
  `kind`, `excluded`, `assets` as object paths) and `warnings`. Read `plan.json` for the rest
- **Errors**: `INVALID_ARGUMENT` (no `paths` or `targetModule`; a path list that is not an array of strings or has
  more than 100 entries; a path without a leading `/`; a flag that is not a JSON boolean), `MODULE_NOT_FOUND` (not a
  module of the project or of a project plugin), `NOT_FOUND` (nothing to plan), `PLAN_WRITE_FAILED`
- **Use Case**: Before converting many Blueprints: show the user plan.md, wait for approval in plan.json
- **Tests**: `NodeToCode.Migration.Plan` (`Source/Private/Tests/Migration/N2CMigrationPlan.spec.cpp`) over the
  fixtures in `Tests/NodeToCodeTestContent/Content/Dependencies` with the host project's module: the seven steps
  (enum, struct, interface, macro library, the cycle {B, C}, A, the child), native names with the collision flag,
  graphs and risks, the exclude list, nothing written outside the plan folder, and the tool's errors
