Node to Code 2.0 opens a project set up with 1.2.x and upgrades it the first time the editor starts: model choices
become model IDs, prices come from the model catalog, and API keys move into Windows Credential Manager. Most
settings carry over as they were; the per-language code editor themes do not. This page lists every 1.2.x setting
and what 2.0 does with it.

## Before you upgrade

- **Engine.** 2.0 supports Unreal Engine 5.6, 5.7 and 5.8. Upgrade a project on 5.5 or older first.
- **Platform.** The 2.0 beta is for Windows only. On a Mac, stay on 1.2.x.
- **Back up or commit** `Config/DefaultNodeToCode.ini` and `Config/DefaultEngine.ini`. The first 2.0 start
  rewrites some of their lines; commit the result so your teammates' editors start from the upgraded file.

## What happens at the first start

Node to Code reads the old settings, converts them, and saves the result to `Config/DefaultNodeToCode.ini` (it
changes only the lines it upgrades). The Output Log (category `LogNodeToCode`) has one line listing what was saved,
dropped and removed, starting "Updated settings left by an earlier NodeToCode version", plus one warning for each
thing you may want to act on, such as a model that is no longer offered. The second start logs none of these, and a
project with nothing to upgrade is not written. If source control keeps either file read-only, the log says so and
the upgrade runs again at every start until you check the file out.

## Every 1.2.x setting

Names as in **Project Settings → Plugins → Node to Code**, with the key in `DefaultNodeToCode.ini` in brackets.

| 1.2.x setting | In 2.0 |
|---|---|
| **Provider** (`Provider`) | Kept |
| Anthropic **Model** (`AnthropicModel`) | Every 1.2.x Claude model is retired, so it becomes `claude-sonnet-5-5`, with a warning. Saved as `AnthropicModelId`; the old key is removed |
| OpenAI **Model** (`OpenAI_Model`) | GPT-4o, GPT-4o mini, GPT-4.1 and o3 keep their model (`gpt-4o-2024-08-06`, `gpt-4o-mini-2024-07-18`, `gpt-4.1`, `o3`). o1, o1-preview, o1-mini, o3-mini and o4-mini are retired and become `gpt-6.1-sol`, with a warning. Saved as `OpenAIModelId`; the old key is removed |
| Gemini **Model** (`Gemini_Model`) | Every 1.2.x Gemini model is retired or gone, so it becomes `gemini-3.8-flash`, with a warning. Saved as `GeminiModelId`; the old key is removed |
| DeepSeek **Model** (`DeepSeekModel`) | Both become `deepseek-flash`, the model DeepSeek now serves: R1 with **Thinking** `high`, V3 with **Thinking** `none`. No warning. Saved as `DeepSeekModelId` and `DeepSeekReasoningEffort`; the old key is removed |
| Ollama settings (`OllamaConfig`) | **Ollama Endpoint**, **Prepended Model Command** and **Keep Alive Duration** are kept. **Context Window** and **Max Tokens** still at 1.2.x's 8192 become 0 (the server's own default); other values are kept. **Use System Prompts** is turned on. Each sampler (**Temperature**, **Top P**, **Top K** and the rest) that you changed from 1.2.x's default keeps your value and is switched on; one left at the default stays off, so the model's own default applies |
| Ollama **Model Name** (`OllamaModel`) | Kept |
| LM Studio **Model Name**, **Server Endpoint**, **Prepended Model Command** (`LMStudioModel`, `LMStudioEndpoint`, `LMStudioPrependedModelCommand`) | Kept |
| Model prices (`OpenAIModelPricing`, `AnthropicModelPricing`, `GeminiModelPricing`, `DeepSeekModelPricing`) | Replaced by one **Model Price Overrides** list keyed by model ID; prices come from the model catalog. A price you changed for a model 2.0 still offers stays as an override. Entries still at 1.2.x's built-in price, and entries for retired models, are dropped (so a price you typed that equals 1.2.x's own is dropped too). The four old keys are removed |
| **Target Language** (`TargetLanguage`) | Kept |
| **Max Translation Depth** (`TranslationDepth`) | Kept |
| **Min Severity** (`MinSeverity`) | Kept |
| Code editor themes (`CPPThemes`, `PythonThemes`, `JavaScriptThemes`, `CSharpThemes`, `SwiftThemes`, `PseudocodeThemes`) | Not used. 2.0 draws every language with one theme, **Theming → Code Editor Theme**. One warning names the keys; they are left in the file so you can copy your colours across, then delete them |
| **Reference Source Files** (`ReferenceSourceFilePaths`) | Kept, including a path to a file that no longer exists (each translation then warns about it) |
| **Estimated Reference File Tokens** (`EstimatedReferenceTokens`) | Kept; the saved number shows until you edit **Reference Source Files** |
| **Custom Translation Output Directory** (`CustomTranslationOutputDirectory`) | Kept |

When a model is replaced, choose another one in the provider's **Model** list, or type an ID into **Custom Model ID**.

## New settings

These have no 1.2.x value, so they start at their defaults: **Max Output Tokens** for each provider (0, Auto),
**Custom Model ID** (empty), **HTTP Activity Timeout** (300 s), **UI Colors** and **Code Editor Theme** under
**Theming**, and the **MCP Server** category: **Server Port** 27000, **Port Fallback Range** 10, **Tool Profile**
Default toolsets, **Tool Exposure** List every tool and **Overlapping Tools** Automatic. Under **Node to Code (User)** (in **Editor Preferences → Plugins**), **Enable MCP Server** is on and **Allow MCP Python
Execution** and **Allow MCP Writes Outside Project** are off. The file also gains `SettingsVersion=4`, which marks it as upgraded. What these settings mean
for your machine: [Security Model](Security-Model).

## API keys

1.2.x saved keys as plain text in each project, at `Saved/NodeToCode/User/secrets.json`. At the first 2.0 start on
Windows, Node to Code moves them into Windows Credential Manager (`NodeToCode/<Provider>`), reads each one back, then
deletes the file and the empty `User` folder. The Output Log says "Moved API keys". If anything fails, the file stays
and the move is tried again at the next start. Each provider's **API Key Status** then reads "Stored in Windows
Credential Manager".

Keys now belong to you, not the project, so every project uses the same ones. When a second 1.2.x project has a
different key for a provider, the key already stored wins; the second project's key is kept as
`NodeToCode/<Provider>.Conflict.<n>` and the Output Log warns about it. Copy it back by hand if you want it.

New in 2.0: with no saved key, a provider uses its environment variable (`ANTHROPIC_API_KEY`, `OPENAI_API_KEY`,
`GEMINI_API_KEY`, `DEEPSEEK_API_KEY`). 1.2.x had no sign-in (OAuth) option and 2.0 has none either: the sign-in
fields of early 2.0 previews are deleted on load and the API key is used. See
[Initial Configuration](Initial-Configuration).

## Other files and editor settings

- **`Config/DefaultEngine.ini`.** 1.2.x wrote `HttpConnectionTimeout=300` and `HttpActivityTimeout=3600` under
  `[HTTP]`. 2.0 removes them when they still hold exactly those values, and keeps any other value as yours. Timeouts
  are now set per request.
- **Use Less CPU when in Background.** 1.2.x turned this editor preference off at every start. 2.0 leaves it alone
  and keeps the editor at full speed only while a translation runs. If it is still off, one warning says where to
  turn it back on (**Editor Preferences → General → Performance**).
- **Saved translations.** Folders under `Saved/NodeToCode/Translations` stay on disk untouched, but the 2.0 window
  lists only translations made by 2.0; open older ones from disk. Their JSON files still read in 2.0.
- **The 1.2.x window.** The Editor Utility Widget window under `/NodeToCode/UI/` is gone; 2.0 opens its own window
  from the Blueprint editor toolbar, and your key bindings for the toolbar commands still work. A Blueprint calling
  the code editor widget's **Set Theme**, **Get Available Themes**, **Get Theme From Name** or **Get Current Theme**
  fails to compile until you remove those nodes, and **Get Selection**'s outputs are now **Out Start Line** and
  **Out End Line**.
