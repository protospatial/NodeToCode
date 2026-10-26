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

## API Keys from Environment Variables

When a provider's **API Key** field is empty, Node to Code uses the matching environment variable of the editor
process instead. This lets build machines, CI and agents run translations without typing a key into Project Settings.

| Provider | Environment variable |
|---|---|
| Anthropic | `ANTHROPIC_API_KEY` |
| OpenAI | `OPENAI_API_KEY` |
| Gemini | `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) |
| DeepSeek | `DEEPSEEK_API_KEY` (or `DEEPSEEK_API`) |

- A key entered in the **API Key** field always wins over the environment variable
- The key is read when each request is sent, so a key entered, changed or cleared in Project Settings applies to the
  next request, including the next Blueprint of a batch that is already running; no editor restart is needed
- A key from the environment is used for requests only: it is never shown in the **API Key** field and never saved
  to `secrets.json`
- Each provider's **API Key Source** line shows where its key comes from: the saved key, the environment variable it
  uses (by name; the value is never shown), or no key
- The Output Log names the variable the first time it is used, never its value, and each "Sending request" line says
  whether the request used the saved key or which variable
- The MCP tool `get-available-llm-providers` lists a provider whose key is only in the environment, with
  `keySource: "environment"` and the variable's name in `keyVariable` (`"saved"` for a key from Project Settings)
- Set the variable before the editor starts (for example in the shell or CI job that launches it); the editor reads
  its own environment, so a variable set after launch is not seen
