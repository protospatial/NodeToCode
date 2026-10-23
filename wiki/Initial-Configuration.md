### Plugin Setup → Initial Configuration
***

## Accessing Plugin Settings

1. Open Project Settings (Edit → Project Settings)
2. Navigate to Plugins → Node to Code
3. You'll see several categories of settings to configure

<p align="left">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_PluginSettings.jpg" alt="Node to Code plugin settings" width="750">
</p>

## Settings

1. [LLM Provider Setup](Provider-Configuration)

2. [Target Language Selection](Target-Language-Settings)

3. [Translation Depth Configuration](Max-Translation-Depth)

4. [Reference Source Files](Reference-Source-Files)

5. [Logging Configuration](Logging-Configuration)

6. [Theme Customization](Theme-&-Syntax-Highlighting)

## Plugins Config Files & Source Control

Node to Code uses two separate files:
- DefaultNodeToCode.ini
  - Stores all of your modified non-sensitive plugin settings
  - Saved in your Project's **Config** path
  - Can be committed to source control without leaking API Keys

- secrets.json
  - Stores your LLM service provider API keys, in plain text
  - Saved in your user folder, outside every project, so the keys are never in a project you commit, zip or sync:
    - Windows: `%LOCALAPPDATA%\NodeToCode\secrets.json`
    - macOS: `~/Library/Application Support/Epic/NodeToCode/secrets.json`
    - Linux: `~/.config/Epic/NodeToCode/secrets.json`
  - The keys belong to you, not to a project: every project you open on this machine uses the same keys
  - Never share this file. Node to Code keeps the previous version next to it as `secrets.json.bak`
  - If the file is damaged so it cannot be read, Node to Code renames it to `secrets.json.corrupt-<date>` instead of
    writing over it, and the Output Log says so; re-enter your keys, or repair that file and rename it back

Earlier versions saved the keys in the project, at `Saved/NodeToCode/User/secrets.json`. The first time a project
opens with this version, its keys move to the file in your user folder and the project copy is deleted. A key already
in your user folder is kept; if the project held a different key for the same provider, the Output Log names the
provider and the project's key is kept in the user file under `MigrationConflicts`, so no key is lost.