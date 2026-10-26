<p align="center">
    <img src="assets/Image_NodeToCode_Header_Docs.png" alt="Node to Code header" width="800">
</p>

# Node to Code

**Node to Code** is an Unreal Engine editor plugin that turns Blueprint graphs into code with the LLM of your choice,
and lets AI agents read and edit Blueprints through a built-in MCP server.

> [!IMPORTANT]
> This branch (`dev-2.0`) is **Node to Code 2.0, in beta** (`2.0.0-beta.1`). Expect rough edges and report them in
> [Issues](https://github.com/protospatial/NodeToCode/issues). The stable release is 1.2.x, on the `main` branch
> and on [Fab](https://www.fab.com/listings/29955a71-cd04-4111-ac43-6a0264429ce6).

## What 2.0 does

### Translate Blueprints

- Translates the focused Blueprint graph, and the functions it calls up to a depth you choose, into **C++, Python,
  JavaScript, C#, Swift or pseudocode**, with implementation notes, in a dockable editor window.
- Works with **Anthropic, OpenAI, Gemini and DeepSeek** (your own API key) or runs locally with **Ollama** or
  **LM Studio** (no key, nothing leaves your machine).
- Serializes graphs to a compact JSON format, so a translation uses far fewer tokens than Unreal's Blueprint text
  format; **Copy Blueprint JSON** puts the same JSON on your clipboard for any chat or agent.
- Saves every translation under your project's `Saved/NodeToCode/Translations` folder, tags graphs, and translates
  tagged graphs in a batch.
- Lets you add your own source files as reference so the output follows your project's style.

Default models in Project Settings: Claude Sonnet 5.5, GPT-6.1 Sol, Gemini 3.8 Flash and DeepSeek Flash (thinking).
Every model Node to Code offers is listed in **Project Settings → Plugins → Node to Code**.

### MCP server and agent tools

While the editor is open, Node to Code runs an [MCP](https://modelcontextprotocol.io) server at
`http://127.0.0.1:27000/mcp`. An MCP client such as Claude Code, Codex CLI or Antigravity CLI can then use Node to
Code's tools to work in your editor:

- read the focused Blueprint, its functions, variables and components (`get-focused-blueprint`,
  `list-blueprint-functions`, ...), and any Blueprint by asset path without opening it (`list-blueprint-graphs`,
  `get-blueprint-n2cjson`), and many graphs at once to files (`export-n2cjson-batch`);
- add and connect nodes, create functions and variables, compile and save (`add-bp-node-to-active-graph`,
  `connect-pins`, `compile-blueprint`, ...);
- search and organize the Content Browser, and read project files;
- run Node to Code's own translation (`translate-focused-blueprint`, or `translate-batch` for many graphs by path or
  tag, saved for the translation viewer), billed to the API key in Project Settings.

The agent does its own reasoning with its own model, so you can use a Claude, ChatGPT or Google AI subscription
through that vendor's agent. Node to Code itself does not sign in with a subscription; inside the editor it uses API
keys only. The client's `tools/list` request returns the current tool set.

### Python bridge

- **`nodetocode` Python module** (`Content/Python/nodetocode`): Blueprint helpers for the editor's Python, such as
  `get_focused_blueprint()`, `search_blueprint_nodes()` and `compile_blueprint()`, plus a script library that agents
  can search, save and rerun.
- **`run-python`** over MCP lets an agent run Python in the editor. It is **off by default**: the
  `run-python`, `save-python-script` and `delete-python-script` tools exist only when you turn on
  **Allow MCP Python Execution** (**Project Settings → Plugins → Node to Code (User) → MCP Server**), a per-user
  setting: a value in a project's committed config files is ignored. With it on, any connected MCP client can run arbitrary Python in your editor.
- **stdio bridge** (`Content/Python/mcp_bridge`): for MCP clients that only start stdio servers.
  `launch_bridge.bat` (Windows) or `launch_bridge.sh` finds Python (the newest Unreal Engine install's, or Python 3
  on your PATH) and forwards to the HTTP server.

## Requirements

| | 2.0 (this branch) |
|---|---|
| Unreal Engine | 5.6, 5.7 and 5.8. On 5.5 or older, stay on 1.2.x |
| Platform | Windows (Win64), editor only. This beta does not list Mac or Linux; they are added once a build on that platform is verified ([decision record](.agents/decisions/0001-platforms-for-2-0-beta.md)) |
| Project | Any project when you install a prebuilt package (it ships Win64 binaries); a C++ (code) project to build the plugin from source |
| Translation in the editor | An API key for a cloud provider, or a local Ollama or LM Studio server |

## Get started

1. Install the plugin: copy it to your project's `Plugins/NodeToCode` folder (see
   [Plugin Installation](wiki/Plugin-Installation.md)), or install a 1.2.x release from
   [Releases](https://github.com/protospatial/NodeToCode/releases) or Fab.
2. Open **Project Settings → Plugins → Node to Code**, choose a **Provider** and enter its **API Key** (see
   [Initial Configuration](wiki/Initial-Configuration.md) and
   [Provider Configuration](wiki/Provider-Configuration.md)).
3. Open a Blueprint graph and click **Translate** in the Node to Code overlay on the graph. The result opens in the
   Node to Code window.

### API keys

Keys entered in Project Settings are saved outside your project, in Windows Credential Manager (entries
`NodeToCode/<Provider>`; a per-user file on other platforms), so they never end up in source control. The field is
masked and empties once the key is saved; **API Key Status** says where the key in use comes from. If no key is
saved, Node to Code uses the editor's environment instead:

| Provider | Environment variable |
|---|---|
| Anthropic | `ANTHROPIC_API_KEY` |
| OpenAI | `OPENAI_API_KEY` |
| Gemini | `GEMINI_API_KEY` (or `GOOGLE_API_KEY`) |
| DeepSeek | `DEEPSEEK_API_KEY` (or `DEEPSEEK_API`) |

A key in Project Settings wins over the variable.Details:
[API Keys from Environment Variables](wiki/Initial-Configuration.md#api-keys-from-environment-variables).

## Connect an MCP client

Open your project in the editor first; the server runs only while the editor is open, and answers only on this
machine (`127.0.0.1` / `localhost`). For Claude Code, from your project's folder:

```
claude mcp add --transport http nodetocode http://127.0.0.1:27000/mcp
claude mcp list
```

Or install the [Claude Code plugin](wiki/developers/Claude-Code-Plugin.md), which adds the server and NodeToCode's
Agent Skills in one step. Setup for Cursor, VS Code, Codex, Claude Desktop (through the stdio bridge) and Epic's MCP
Client Toolset, and what to do when a client does not connect:
[Connecting an MCP Client](wiki/developers/Connecting-an-MCP-Client.md). A client sees 63 tools by default (66 with
**Allow MCP Python Execution**); to serve fewer, or only three search tools for a client with a tool limit, see
[Choosing a Tool Mode](wiki/developers/Choosing-a-Tool-Mode.md).

Using a Claude, ChatGPT or Google AI subscription through its vendor's agent:
[Using a Claude or Gemini Subscription](wiki/developers/Using-a-Claude-or-Gemini-Subscription.md).
Security model of the server: [SECURITY.md](SECURITY.md#the-mcp-server-20).

## Upgrading from 1.2.x

Open the project with 2.0 on UE 5.6 or newer. On the first start, 2.0 moves your API keys to the per-user file,
replaces models that providers have retired with the current defaults, and logs each change in the **Output Log**
(**LogNodeToCode**). The 1.2.x editor window (an Editor Utility Widget) and the per-language code themes are gone.

- What changes for users: [Updating from 1.2.x to 2.0](wiki/Plugin-Updates.md#updating-from-12x-to-20).
- Every setting, file and API, row by row: the
  [migration matrix](Context/release/migration-1.2-to-2.0.md).
- Everything that changed in 2.0, with pull request numbers: [CHANGELOG.md](CHANGELOG.md).

## Documentation

The user guide for 2.0 is in [`wiki/`](wiki/Home.md) on this branch and is published to the
[GitHub wiki](https://github.com/protospatial/NodeToCode/wiki), which still shows the 1.2.x pages until 2.0 is
released. Pages for agents and MCP clients are in [`wiki/developers/`](wiki/developers/).

## Contributing

Pull requests go to **`dev-2.0`**; `main` stays at 1.2.x and takes no new work. Every commit needs a DCO
sign-off (`git commit -s`). Building, testing on each engine version and the commit format are in
[CONTRIBUTING.md](CONTRIBUTING.md). Report security problems privately as described in [SECURITY.md](SECURITY.md),
never in a public issue.

## Licence

Node to Code is licensed under the [Apache License 2.0](LICENSE). [NOTICE](NOTICE) credits community contributors
and third-party code (cpp-httplib, MIT). The licence and contribution model are set out in
[decision record 0002](.agents/decisions/0002-licence-and-contribution-model.md).

## Community and support

- [Discord](https://discord.gg/4t3Syvk4AG): questions, help and discussion
- [Node to Code on Fab](https://www.fab.com/listings/29955a71-cd04-4111-ac43-6a0264429ce6)
- [Roadmap](https://trello.com/b/iPOyaSvb)
- Support the project: [Buy me a coffee](https://buymeacoffee.com/protospatial) or
  [sponsor on GitHub](https://github.com/sponsors/NCMcClure)
