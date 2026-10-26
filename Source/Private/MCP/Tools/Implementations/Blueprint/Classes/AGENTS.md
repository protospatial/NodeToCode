# Blueprint Class Tools

This directory contains the MCP tools in the `BlueprintClassTools` toolset (`N2CMcpToolsets::BlueprintClass`, named in
`GetDefinition`): `search-blueprint-classes` finds parent classes and `create-blueprint-class` makes a new Blueprint
asset from one.

## Status: frozen, bug fixes only (dec-5)

Every tool here (`BlueprintClassTools`) is frozen. Decision record [0007](../../../../../../../.agents/decisions/0007-crud-tool-families-fix-then-freeze.md): owner decision card 0140, applied
provisionally as the review's option 2, with card 0089 applied provisionally as option (b).

- **Accepted:** bug fixes, meaning a change that fixes a crash or hang (a modal dialog in an unattended call
  counts), data loss, Blueprint or project corruption, or a result that misreports what happened (success for an
  edit that did not happen, wrong counts, duplicates, a wrong type in a read). Also accepted, since they add no
  capability: explicit targeting arguments on existing tools (review next-6), security hardening, building on the
  supported engines, tests, docs that match the code, and removing dead code.
- **Not accepted:** new tools in this family, and new arguments, modes or result fields that
  add capability. On UE 5.8 that work belongs to Epic's EditorToolset (`BlueprintTools.create`, `ObjectTools.search_subclasses`); on 5.6 and 5.7 it waits for the
  owner.
- **Not frozen:** comprehension, export, translation, batch and tag tools: `BlueprintDiscoveryTools` (except the
  tools in `N2CMcpEpicOverlap::Tools`), `BlueprintTagTools`, `TranslationTools`, and also `FileSystemTools` and
  `PythonTools`.
- **Fix cards still owed here:** 0288 (search-blueprint-classes filtering, names and counts).

The whole policy, and how the frozen set is read from the code, is "Frozen tool families" in
`Source/Private/MCP/Tools/AGENTS.md`.

## Available Tools

### search-blueprint-classes
- **Description**: Searches the classes a Blueprint can derive from, scored by how well their names match
  (read-only, `readOnlyHint`). Runs over every loaded class and every Blueprint asset on each call
- **Parameters**:
  - `searchTerm` (required): Text matched case-insensitively against the class name, then the display name, the
    description and the category (`CalculateRelevanceScore`). An empty string matches every class
  - `classType` (optional, default `all`): `all`, `actor`, `actorComponent`, `object` or `userWidget`. Native
    classes are tested with `IsChildOf` (`PassesClassTypeFilter`); Blueprint assets only by a substring of their
    parent class name (`Actor`, `Component`, `Widget`) in `CollectBlueprintClasses`
  - `includeEngineClasses` (optional, default true): With false, classes under `/Script/Engine`, `/Script/CoreUObject`,
    `/Script/UMG` and `/Engine/` are left out (`IsEngineClass`); other engine modules (`/Script/Paper2D`,
    `/Script/AIModule`, ...) stay
  - `includePluginClasses` (optional, default true): With false, other `/Script/` modules and paths containing
    `/Plugins/` are left out (`IsPluginClass`)
  - `includeDeprecated` (optional, default false), `includeAbstract` (optional, default false): Keep deprecated or
    abstract native classes. `includeAbstract` has no effect on Blueprint assets
  - `maxResults` (optional, default 50): Clamped to 1-200 (`0` returns one result, `500` returns 200)
- **Returns**: `classes`, `totalFound`, `hasMore` (see "Result Shape")
- **Errors**: `Missing required parameter: searchTerm`
- **Known issue (card 0288):** the common parent classes (below) are added before any filter runs, so they appear
  whatever `classType`, `includeAbstract` or `includeEngineClasses` say (`AddCommonClasses`), and a common class that
  also matches the native scan is listed twice (`Character` with `category` `Common`, then again with
  `Actor|Pawn|Character`). A Blueprint class loaded in the editor is listed twice too: once from the asset registry
  and once as a native class. Display names drop a leading `A` or `U` even when it is part of the name (`Actor` is
  shown as `ctor`, `UserWidget` as `ser Widget`; `GetClassDisplayName`). A Blueprint asset's `className` and
  `classPath` keep the asset registry's export text, with a trailing quote (see "Result Shape"). Abstract
  valid parents such as `UserWidget` are only found with `includeAbstract` true unless they are common classes.

### create-blueprint-class
- **Description**: Creates a Blueprint asset, compiles it, saves its package to disk and, by default, opens it
  (`FN2CMcpCreateBlueprintClassTool::Execute`). Every argument is checked before anything is created, so a refused
  call leaves no folder and no asset. The asset is made by `IAssetTools::CreateAsset` with a `UBlueprintFactory`: the factory asks the Kismet compiler which Blueprint class the parent needs,
  so a `UserWidget` parent gives a `UWidgetBlueprint` (with its widget tree), and the asset registry and the editor's
  asset-created hooks see the new asset (card 0166). As in the Content Browser, creating the asset is not an undo
  step: undoing a transaction that created a Blueprint destroys it in memory while its saved package stays on disk
- **Parameters**:
  - `blueprintName` (required): Asset name, checked with `FKismetNameValidator` (at most 100 characters, no `.`), as
    an object, package and file name (no spaces, `/` or other reserved characters, no device names) and against
    `IAssetTools::IsNameAllowed` (`ValidateBlueprintName`)
  - `addPrefix` (optional, default true): Put `BP_` in front of the name unless it already starts with it
    (`MyPlayerCharacter` becomes `BP_MyPlayerCharacter`, `BP_Prefixed` stays); false uses the name as given
  - `parentClassPath` (required): Object path of the parent class, found with `FindObject` then `LoadObject`
    (`ValidateParentClass`): `/Script/Engine.Character`, `/Script/UMG.UserWidget` for a Widget Blueprint,
    `/Script/CoreUObject.Interface` for a Blueprint interface, or a Blueprint class as
    `/Game/Folder/BP_Base.BP_Base_C` (the export-text `classPath` search-blueprint-classes returns for a Blueprint
    asset is accepted too). A bare name (`Actor`) is not found. The class must pass
    `FKismetEditorUtilities::CanCreateBlueprintOfClass`. An `AnimInstance` parent is refused: an Animation Blueprint
    needs a target skeleton, which the tool cannot set
  - `assetPath` (required): Content folder for the new asset, checked with
    `FN2CMcpContentBrowserUtils::ValidateContentPath`. An existing folder (`/Game` included) is used; a missing one is
    created (`EnsureDirectoryExists`) once every check has passed
  - `ifExists` (optional, default `error`, any case): What an existing asset of that name in the folder does. `error`
    fails; `unique` picks a free name with `IAssetTools::CreateUniqueAssetName` (`BP_Door` becomes `BP_Door1`) and
    returns it as `blueprintName`. An existing asset is never overwritten. Other values are refused
  - `blueprintType` (optional, default `auto`, any case): `auto` gives `Interface` for an interface parent and
    `Normal` otherwise; `normal`, `const` and `interface` are used as given. `interface` needs the parent
    `/Script/CoreUObject.Interface`, and an interface parent needs `interface` (or `auto`). Other values are refused
    (`ResolveBlueprintType`)
  - `openInEditor` (optional, default true): Open the new Blueprint after saving
  - `openInFullEditor` (optional, default true): true calls `FKismetEditorUtilities::BringKismetToFocusAttentionOnObject`,
    false `UAssetEditorSubsystem::OpenEditorForAsset` (`OpenBlueprintInEditor`)
  - `description` (optional): Stored as the Blueprint's `BlueprintDescription`
- **Returns**: `success`, `saved`, `blueprintName` (the final name), `packagePath`, `assetPath` (object path),
  `blueprintInfo` (`className`, `assetClass` as the Blueprint asset's class, `Blueprint` or `WidgetBlueprint`,
  `parentClass` as the class's short name, `blueprintType`, `generatedClass`), `nextSteps`, and the `compile` object
  of the one compile the call runs:

  ```json
  {
    "success": true,
    "saved": true,
    "blueprintName": "BP_MyPlayerCharacter",
    "packagePath": "/Game/Blueprints/Characters/BP_MyPlayerCharacter",
    "assetPath": "/Game/Blueprints/Characters/BP_MyPlayerCharacter.BP_MyPlayerCharacter",
    "blueprintInfo": {
      "className": "BP_MyPlayerCharacter",
      "assetClass": "Blueprint",
      "parentClass": "Character",
      "blueprintType": "Normal",
      "generatedClass": "/Game/Blueprints/Characters/BP_MyPlayerCharacter.BP_MyPlayerCharacter_C"
    },
    "nextSteps": ["Use 'create-blueprint-function' to add functions", "..."]
  }
  ```

  When the package cannot be saved (a read-only file, a source control lock) the result is an error (`isError`) with
  the same fields, `success` and `saved` false, no `nextSteps`, and `error` saying the Blueprint exists in memory only;
  the Blueprint is not opened.
- **Errors**: `Missing required parameter: <name>`, `Parent class not found: <path>`,
  `Cannot create Blueprint from class: <path>. Class may not be Blueprintable.`,
  `Cannot create an Animation Blueprint from <path>: ...`, `Unknown blueprintType '<value>': ...`,
  `blueprintType interface needs parentClassPath '/Script/CoreUObject.Interface', not <path>`,
  `The interface parent <path> needs blueprintType interface (or auto), not <value>`, `Unknown ifExists '<value>': ...`,
  `Invalid blueprintName '<name>': <reason>`, `Invalid assetPath: <reason>`,
  `An asset already exists at <object path>. Pass ifExists 'unique' ...`
- **Spec**: `NodeToCode.MCP.CreateBlueprintClass` (`Source/Private/Tests/MCP/N2CCreateBlueprintClass.spec.cpp`)
  saves real Blueprints under `/Game/__N2CTest` and deletes the folder after each case
- **Known issue (card 0182):** `assetPath` is not limited to `/Game` and project plugin content

## Result Shape (search-blueprint-classes)

Each entry of `classes` (`ClassInfoToJson`):

```json
{
  "className": "Character",
  "classPath": "/Script/Engine.Character",
  "displayName": "Character",
  "category": "Common",
  "description": "Characters are Pawns that have a mesh, collision, and built-in movement logic. ...",
  "parentClass": "Pawn",
  "isAbstract": false,
  "isDeprecated": false,
  "isBlueprintable": true,
  "isNative": true,
  "module": "Engine",
  "icon": "Character",
  "commonClass": true
}
```

- `className` is the `UClass` name without its C++ prefix (`Actor`, not `AActor`); `parentClass` likewise.
- `displayName` is the class's `DisplayName` metadata when it has one (`TextBlock` shows as `Text`), otherwise the
  name split at capitals.
- `category` is `Common` for a common class, the class's `Category` metadata, a hierarchy string (`Actor|Pawn`,
  `Component|Scene`, `Widget`) or the module name; for a Blueprint asset, its folder name.
- `description` is the class tooltip (engine text), or `Class derived from <parent>`.
- A Blueprint asset found in the asset registry has `isNative` false, `module` `Game`, `icon` `Blueprint`, and
  `className`/`classPath` taken from the registry's export text, for example
  `"className": "BP_Base.BP_Base_C'"` and
  `"classPath": "/Script/Engine.BlueprintGeneratedClass'/Game/Folder/BP_Base.BP_Base_C'"`.
- `totalFound` is the number of classes collected before the search term is applied, duplicates included
  (`BuildJsonResult` receives `AllClasses.Num()`), not the number that matched. `hasMore` is
  `classes.length < totalFound`, so it is true for almost every search: a search for `zzqqnothing` returns no classes
  with `totalFound` 974 and `hasMore` true on the UE 5.8 host.

Results are sorted with common classes first, then by score, then by display name (`FilterAndScoreClasses`).

## Common Parent Classes

`AddCommonClasses` puts these at the top of the results, as `commonClass: true` with `category` `Common`, when
`classType` is `all` or their own type:

- **actor**: `/Script/Engine.Actor`, `Pawn`, `Character`, `GameModeBase`, `GameStateBase`, `PlayerController`,
  `PlayerState`, `HUD`
- **actorComponent**: `/Script/Engine.ActorComponent`, `SceneComponent`, `PrimitiveComponent`,
  `StaticMeshComponent`, `SkeletalMeshComponent`, `CapsuleComponent`, `SphereComponent`, `BoxComponent`
- **object**: `/Script/CoreUObject.Object`, `/Script/Engine.DataAsset`, `/Script/Engine.SaveGame`
- **userWidget**: `/Script/UMG.UserWidget`, `/Script/UMG.Button`, `/Script/UMG.TextBlock`, `/Script/UMG.Image`

A common class is skipped only when it cannot be loaded or fails `CanCreateBlueprintOfClass`.

## Examples

The curl examples assume `$SID` holds a session id from `initialize` (see "Calling the Server with curl" in
`Source/Private/MCP/Tools/AGENTS.md`); every POST after `initialize` must send it.

```bash
# Step 1: find a parent class (the first results are the common Character entry and its native duplicate)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-blueprint-classes",
      "arguments": {
        "searchTerm": "Character",
        "classType": "actor",
        "maxResults": 5
      }
    },
    "id": 1
  }'

# Step 2: create the Blueprint from the classPath found (the folder may exist already)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "create-blueprint-class",
      "arguments": {
        "blueprintName": "MyPlayerCharacter",
        "parentClassPath": "/Script/Engine.Character",
        "assetPath": "/Game/Blueprints/Characters",
        "description": "Player character with custom movement abilities"
      }
    },
    "id": 2
  }'
```

Other parents use the same call: `/Script/Engine.Actor` for an Actor Blueprint, `/Script/Engine.ActorComponent` for a
component Blueprint (it can then be added to an Actor with `add-component-class-to-actor` and its generated class
path), a Blueprint's generated class (`/Game/Blueprints/BP_Base.BP_Base_C`) for a child Blueprint, or
`/Script/UMG.UserWidget` for a Widget Blueprint.

```bash
# Component classes for add-component-class-to-actor
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Accept: application/json, text/event-stream" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "search-blueprint-classes",
      "arguments": {
        "searchTerm": "Mesh",
        "classType": "actorComponent"
      }
    },
    "id": 3
  }'
```

## Integration with Other Tools

1. `search-blueprint-classes` to find a parent, `create-blueprint-class` to make the Blueprint (it opens and
   focuses it by default).
2. Then the focused-Blueprint tools: `create-blueprint-function`, `create-variable`, `add-bp-node-to-active-graph`,
   `add-component-class-to-actor` (Actor Blueprints). The result's `nextSteps` lists the first three and
   `translate-focused-blueprint`.

Neither tool returns text from the project's Blueprints other than asset names, so neither result is marked as
untrusted content (card 0124).
