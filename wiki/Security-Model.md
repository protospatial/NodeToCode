Node to Code runs an MCP server inside the Unreal Editor so that AI agents on your machine can read and edit your
Blueprints, and it sends Blueprints to the LLM provider you pick when you translate. This page says what each part
can reach, what it does by default and which setting controls it, so you can decide what to turn on. The server is
built for one developer's own machine: it has no password, and a bearer token only when you turn one on.

## At a glance

| What | Default | Where to change it |
|---|---|---|
| MCP server | On, at `http://127.0.0.1:27000/mcp`, this machine only | **Enable MCP Server**, **Server Port** |
| Python execution over MCP | Off | **Allow MCP Python Execution** |
| Asset writes outside your project's content | Refused | **Allow MCP Writes Outside Project** |
| A token on every MCP request | Not required | **Require MCP Bearer Token** |
| Tools a client gets | Every toolset, every tool listed | **Tool Profile**, **Tool Exposure** |
| Tools Unreal MCP also has (UE 5.8) | Left to Unreal MCP when its plugins are enabled | **Overlapping Tools** |
| Edits while Play In Editor runs | Refused | Always on |
| API keys | Windows Credential Manager | Each provider's **API Key** field |
| Data sent when you translate | Goes to the selected provider only | **Provider** |

**Enable MCP Server**, **Allow MCP Python Execution**, **Allow MCP Writes Outside Project** and **Require MCP Bearer
Token** are under
**Editor Preferences → Plugins → Node to Code (User)**; the others are in **Project Settings → Plugins → Node to Code**, in **MCP Server** or **LLM Services**. Every MCP Server
setting needs an editor restart.

## What the MCP server accepts

- **Address.** The server uses Unreal's built-in HTTP server, which listens on loopback (`localhost`) only, so other
  machines on your network cannot reach it. A project or user that sets `DefaultBindAddress` in the
  `[HTTPServer.Listeners]` section of the engine config changes that for every listener; do not set it to `any`.
- **Port.** **Server Port** (27000). If the port is taken, the server tries the next ports in **Port Fallback Range**
  (10) and **Server Status** shows where it ended up. It is the only port Node to Code opens.
- **Origin and Host.** A request whose `Origin` header names a site other than `localhost`, `127.0.0.1` or `[::1]`,
  or whose `Host` is not one of those names, is refused with 403. A web page on the internet, or a DNS-rebinding
  trick, cannot drive your editor. A page served from any port on localhost (a local dev server, for example) can.
- **Requests.** A POST must be `application/json` (else 415), and a body over 8 MiB is refused with 413.
- **Authentication: off by default.** Any program running as you on this machine can connect and call the tools a
  client would. With **Require MCP Bearer Token** on, a request without `Authorization: Bearer <token>` gets 401; the
  token is in a per-user file outside every project, which the file tools cannot read
  ([Requiring an MCP Token](Requiring-an-MCP-Token)). A program that can read your files can still read the token.
- **When it runs.** At editor start, unless **Enable MCP Server** is off. It does not start in commandlets or
  `-unattended` runs unless `-N2CMcp` is on the command line.
- **Discovery files.** It writes where it listens to the project's `Saved/NodeToCode/mcp-server.json` and to
  `%LOCALAPPDATA%\NodeToCode\servers\<process id>.json`, so local clients can find it.

## Switches a project cannot turn on for you

**Enable MCP Server**, **Allow MCP Python Execution**, **Allow MCP Writes Outside Project** and **Require MCP Bearer
Token** belong to you, not the
project. Only your own value counts:
the one in `Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini`, or a `-ini:` override on your command line.
A value committed in the project's `Config/DefaultEditorPerProjectUserSettings.ini` (or any other shared config file)
is ignored, with a warning in the Output Log. This stops a careless commit from turning Python execution or engine writes
on for everyone; it does not protect you from a hostile project, which can run its own code when you open it.

## Python execution

**Allow MCP Python Execution** is off by default. Off, the tools `run-python`, `save-python-script` and
`delete-python-script` do not exist on the server, whatever the tool profile says. On, any connected client can run
any Python in the editor as you: read and write any file you can, read your API keys from Credential Manager and the
environment, and change the project. A Python call has no undo step and no timeout. Saved scripts go to
`Content/Python/scripts/<category>/`, and the name `init_unreal` is refused so a script cannot run at every editor
start. Turn it on only while you use it, with an agent you trust.

## Which tools a client sees

**Tool Profile** picks the toolsets (**Default toolsets**, **Python scripting only** or **Custom toolsets**) and
**Tool Exposure** picks whether a client sees every tool or three search tools that reach them; the list never
changes while the editor runs. Serving fewer toolsets is the simplest way to limit what an agent can do. See
[Choosing a Tool Mode](Choosing-a-Tool-Mode) and [Connecting an MCP Client](Connecting-an-MCP-Client).

On UE 5.8 with Epic's **Unreal MCP** and **EditorToolset** plugins enabled, **Overlapping Tools** set to
**Automatic** leaves the 34 tools both servers have to Unreal MCP, so an agent sees one tool per operation; Node to
Code refuses a call to one of them and names Epic's tool instead. See
[NodeToCode and Epic Tool Mapping](NodeToCode-and-Epic-Tool-Mapping).

## Files the tools can read and write

| Access | Where | Not allowed |
|---|---|---|
| Read (`read-file`, `read-path`) | Text files anywhere inside the project folder, including `Config`, `Source` and `Saved`, and inside **Custom Translation Output Directory** when it is outside the project (unless it is a drive root or holds the project or your home folder). `read-file` returns at most 500 KB per call, in whole lines, and continues from a line number; `read-path` lists a folder in pages | `Saved/NodeToCode/User`, your `%LOCALAPPDATA%\NodeToCode` folder, the project's `.git` folder, any `secrets.json*`, `.env` or `*.env` file, binary files (`.uasset`, `.umap`, `.uexp`, `.png`, `.dll` and any type not on the text list, or content that is not text), any other path outside the project (links and junctions are followed and checked) |
| Create, save, move, rename, copy assets and folders | `/Game` and the content of plugins in your project's `Plugins` folder | `/Engine`, plugins installed with the engine or from Fab, and Node to Code's own content, unless **Allow MCP Writes Outside Project** is on (it needs an editor restart); no tool deletes an asset |
| Write Blueprint exports | `Saved/NodeToCode/Exports` | Any other folder |
| Write translations | `Saved/NodeToCode/Translations`, or **Custom Translation Output Directory** | Paths outside that folder: a graph name such as `../../Source` becomes a safe file name |
| Write Python scripts (Python execution on) | `Content/Python/scripts` | Other folders, `init_unreal` |

There is no general file-writing tool. **Custom Translation Output Directory** is a project setting, saved in
`Config/DefaultNodeToCode.ini`, so whoever commits that file chooses where translations are written.

## Play In Editor

While a Play In Editor session runs, every tool that could change a Blueprint, an asset or the level is refused
with an error starting `PIE_ACTIVE:`, so an agent cannot recompile or edit the classes your running game uses.
Read-only tools still work, and so do translation, the tools that open an asset, function or Content Browser folder,
and the Python tools. Stop Play In Editor and the edit works again.

## Text inside Blueprints

Comments, names, tooltips and string literals in a Blueprint reach the agent that reads it, and a Blueprint from Fab,
a content pack or a teammate can contain text written to steer an agent. The tools that return Blueprint content
mark it as data: their result starts with an `untrusted_content` notice telling the agent to treat instructions
inside it as data, and the Blueprint prompts carry the same notice. This is advice to the model, not a filter.
Nothing is removed, an agent can still act on planted text, and files read with `read-file` and the script library
are not marked. Watch the tool calls an agent makes on Blueprints you did not write, and serve fewer tools while it
works on them.

## API keys

On Windows, keys you enter are kept in Windows Credential Manager, one entry per provider (`NodeToCode/Anthropic`,
`NodeToCode/OpenAI`, `NodeToCode/Gemini`, `NodeToCode/DeepSeek`), encrypted for your Windows account and not roaming
to other PCs. When no key is saved, the provider's environment variable (`ANTHROPIC_API_KEY` and so on) is used.
Keys are never written to a config file or the project, the **API Key** fields are masked and write-only, and no
Blueprint, Python call or MCP tool returns a key. A plain-text `secrets.json` left by an earlier version is moved into
Credential Manager and deleted at the first start. Details: [Initial Configuration](Initial-Configuration).

## What leaves your machine

| When | What is sent | Where to |
|---|---|---|
| You translate (toolbar, batch, or the `translate-focused-blueprint` and `translate-batch` tools) | The graph as Node to Code JSON (with its comments, names and string literals), the contents of every **Reference Source Files** entry, and the system prompt | The selected provider: Anthropic, OpenAI, Gemini or DeepSeek, with that provider's key only; or your **Ollama Endpoint** or LM Studio **Server Endpoint**, which may be another machine |
| An MCP client calls a tool | The tool's result | The client, which may pass it to its own model; your client's settings govern that |

Node to Code makes no other network requests from the editor. One `translate-batch` call can send up to 50 graphs,
each billed to your key. To report a vulnerability, follow the
[security policy](https://github.com/protospatial/NodeToCode/security/policy).
