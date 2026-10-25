# Blueprint Class Tools

This directory contains the MCP tools in the 'Blueprint Classes' category (the string both tools register in
`GetDefinition`): `search-blueprint-classes` finds parent classes and `create-blueprint-class` makes a new Blueprint
asset from one.

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
- **Description**: Creates a Blueprint asset with `FKismetEditorUtilities::CreateBlueprint` (always a `UBlueprint`
  with a `UBlueprintGeneratedClass`), compiles it, saves its package to disk and, by default, opens it
  (`FN2CMcpCreateBlueprintClassTool::Execute`)
- **Parameters**:
  - `blueprintName` (required): Asset name. `BP_` is put in front unless the name already starts with it
    (`ValidateAndPrepareAssetPath`): `MyPlayerCharacter` becomes `BP_MyPlayerCharacter`, `BP_Prefixed` stays
  - `parentClassPath` (required): Object path of the parent class, found with `FindObject` then `LoadObject`
    (`ValidateParentClass`): `/Script/Engine.Character`, or a Blueprint class as
    `/Game/Folder/BP_Base.BP_Base_C` (the export-text `classPath` search-blueprint-classes returns for a Blueprint
    asset is accepted too). A bare name (`Actor`) is not found. The class must pass
    `FKismetEditorUtilities::CanCreateBlueprintOfClass`
  - `assetPath` (required): Content folder for the new asset. The folder must not exist yet: the tool creates it
    with `FN2CMcpContentBrowserUtils::CreateContentFolder`, which fails for an existing folder
  - `openInEditor` (optional, default true): Open the new Blueprint after saving
  - `openInFullEditor` (optional, default true): true calls `FKismetEditorUtilities::BringKismetToFocusAttentionOnObject`,
    false `UAssetEditorSubsystem::OpenEditorForAsset` (`OpenBlueprintInEditor`)
  - `description` (optional): Stored as the Blueprint's `BlueprintDescription`
  - `generateConstructionScript` (optional, default true): Has no effect. `ApplyBlueprintSettings` only makes sure an
    Actor Blueprint has a `SimpleConstructionScript`, which `CreateBlueprint` already gave it, and an Actor Blueprint
    gets its `UserConstructionScript` graph either way
  - `blueprintType` (optional, default `auto`): `normal`, `const` or `interface` are used as given, whatever the
    parent; any other value, `auto` included, gives `Interface` for an interface parent and `Normal` otherwise
    (`DetermineBlueprintType`)
- **Returns**: `success`, `blueprintName` (the final name), `packagePath`, `assetPath` (object path),
  `blueprintInfo` (`className`, `parentClass` as the class's short name, `blueprintType`, `generatedClass`) and
  `nextSteps`:

  ```json
  {
    "success": true,
    "blueprintName": "BP_MyPlayerCharacter",
    "packagePath": "/Game/Blueprints/Characters/BP_MyPlayerCharacter",
    "assetPath": "/Game/Blueprints/Characters/BP_MyPlayerCharacter.BP_MyPlayerCharacter",
    "blueprintInfo": {
      "className": "BP_MyPlayerCharacter",
      "parentClass": "Character",
      "blueprintType": "Normal",
      "generatedClass": "/Game/Blueprints/Characters/BP_MyPlayerCharacter.BP_MyPlayerCharacter_C"
    },
    "nextSteps": ["Use 'create-blueprint-function' to add functions", "..."]
  }
  ```
- **Errors**: `Missing required parameter: <name>`, `Parent class not found: <path>`,
  `Cannot create Blueprint from class: <path>. Class may not be Blueprintable.`,
  `Failed to create directory: Path already exists: <folder>`
- **Known issue (card 0166):** an existing folder, `/Game` included, is refused, so a second Blueprint cannot go into
  the folder of the first; a `UserWidget` or `AnimInstance` parent gives a plain Blueprint, not a Widget or Animation
  Blueprint; `interface` is applied to any parent and unknown `blueprintType` values fall back without an error; a
  failed save is only logged and the call still reports success; there is no undo transaction
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

# Step 2: create the Blueprint from the classPath found, in a folder that does not exist yet
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
path), or a Blueprint's generated class (`/Game/Blueprints/BP_Base.BP_Base_C`) for a child Blueprint. Do not use
`/Script/UMG.UserWidget` expecting a Widget Blueprint (card 0166).

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
