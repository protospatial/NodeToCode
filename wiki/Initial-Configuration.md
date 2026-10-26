### Plugin Setup → Initial Configuration
***

Node to Code is configured in Project Settings, under Plugins → Node to Code. This page shows where the settings
are, which tools the MCP server offers to AI clients, and where the plugin keeps its settings and API keys.

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

## MCP Server Tools

The **MCP Server** category decides which tools an MCP client (Claude Code, VS Code, Cursor) gets from Node to Code.
Both settings take effect after an editor restart, and the tool list a client sees never changes while it is connected.

- **Tool Profile**: **Default toolsets** (every tool, the default), **Python scripting only** (run-python and the
  script library), or **Custom toolsets** (only the toolsets you pick under **Custom Toolsets**)
- **Tool Exposure**: **List every tool** (the default), or **Tool search**, which lists three tools (`list_toolsets`,
  `describe_toolset`, `call_tool`) that reach all the others. Use Tool search when your client has a tool limit
  (VS Code allows 128 tools, Windsurf 100, counted across all your MCP servers)
- **Overlapping Tools** (UE 5.8): what to do with the 34 tools Epic's **Unreal MCP** also has. **Automatic** (the
  default) leaves them to Unreal MCP when its **Unreal MCP** and **EditorToolset** plugins are enabled and Tool
  Profile is Default toolsets; **Always serve them** keeps them. See
  [NodeToCode and Epic Tool Mapping](NodeToCode-and-Epic-Tool-Mapping). No effect on UE 5.6 and 5.7

Projects that turned on **Enable Python Script-Only Mode** or **Enable Dynamic Tool Discovery** in an earlier build
open with Python scripting only or Tool search set for them.

## Plugins Config Files & Source Control

Node to Code keeps settings and API keys apart:
- DefaultNodeToCode.ini
  - Stores all of your modified non-sensitive plugin settings
  - Saved in your Project's **Config** path
  - Can be committed to source control without leaking API Keys

- API keys
  - Never written to a config file or to the project, so they are never in a project you commit, zip or sync
  - The keys belong to you, not to a project: every project you open on this machine uses the same keys

### Where API keys are stored

On Windows the keys are kept in **Windows Credential Manager**, encrypted for your Windows account and kept on this
machine only (they do not roam to other PCs). There is one entry per provider:

| Provider | Credential Manager entry |
|---|---|
| Anthropic | `NodeToCode/Anthropic` |
| OpenAI | `NodeToCode/OpenAI` |
| Gemini | `NodeToCode/Gemini` |
| DeepSeek | `NodeToCode/DeepSeek` |

- To enter or change a key, type or paste it into the provider's **API Key** field and press Enter. The field is
  masked and empties again once the key is saved; it never shows a saved key
- The **API Key Status** line under it says where the key in use comes from: `Stored in Windows Credential Manager
  (ends ...abcd)` (the last four characters, for keys of 16 characters or more), `From environment variable
  ANTHROPIC_API_KEY`, or `Not set`
- To remove a key, click **Remove Saved <Provider> Key** under the provider, or delete the entry yourself:
  `cmdkey /delete:NodeToCode/OpenAI` in a command prompt, or Control Panel → Credential Manager → Windows
  Credentials → Generic Credentials. `cmdkey /list:NodeToCode*` lists the entries
- An entry made with `cmdkey /generic:NodeToCode/OpenAI /user:NodeToCode /pass:<key>` is read too
- Blueprints and Python can set a key (`N2CSettings.set_api_key(provider, key)`, an empty key removes it) and ask
  whether a provider has one (`has_api_key`), but nothing returns a key

On macOS and Linux (not supported by the 2.0 beta), the keys stay in a plain-text `secrets.json` in your user folder,
outside every project:
- macOS: `~/Library/Application Support/Epic/NodeToCode/secrets.json`
- Linux: `~/.config/Epic/NodeToCode/secrets.json`
- Node to Code keeps the previous version next to it as `secrets.json.bak`, and renames a damaged file to
  `secrets.json.corrupt-<date>` instead of writing over it. Delete the file to remove the keys

### Keys saved by earlier versions

Earlier versions saved the keys as plain text, in the project at `Saved/NodeToCode/User/secrets.json` and later in
`%LOCALAPPDATA%\NodeToCode\secrets.json`. The first time the editor starts with this version on Windows, Node to Code
moves every key from those files into Credential Manager, reads each one back, and only then deletes the files (and
`secrets.json.bak`). If anything fails, the files are left as they are, the Output Log says why, and the move is tried
again on the next start, so no key is lost. When Credential Manager already holds a different key for a provider, that
key is kept and used, and the file's key is saved beside it as `NodeToCode/<Provider>.Conflict.1` (the Output Log
names it); delete that entry once you no longer need it. A damaged `secrets.json.corrupt-<date>` file is not moved:
the Output Log names it so you can re-enter its keys and delete it.

## API Keys from Environment Variables

When no key is saved for a provider, Node to Code uses the matching environment variable of the editor
process instead. This lets build machines, CI and agents run translations without typing a key into Project Settings.

| Provider | Environment variable |
|---|---|
| Anthropic | `ANTHROPIC_API_KEY` |
| OpenAI | `OPENAI_API_KEY` |
| Gemini | `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) |
| DeepSeek | `DEEPSEEK_API_KEY` (or `DEEPSEEK_API`) |

- A saved key (entered in the **API Key** field) always wins over the environment variable
- The key is read when each request is sent, so a key entered, changed or cleared in Project Settings applies to the
  next request, including the next Blueprint of a batch that is already running; no editor restart is needed
- A key from the environment is used for requests only: it is never shown and never saved
- Each provider's **API Key Status** line shows where its key comes from: the saved key, the environment variable it
  uses (by name; the value is never shown), or no key
- The Output Log names the variable the first time it is used, never its value, and each "Sending request" line says
  whether the request used the saved key or which variable
- The MCP tool `get-available-llm-providers` lists a provider whose key is only in the environment, with
  `keySource: "environment"` and the variable's name in `keyVariable` (`"saved"` for a key from Project Settings)
- Set the variable before the editor starts (for example in the shell or CI job that launches it); the editor reads
  its own environment, so a variable set after launch is not seen
