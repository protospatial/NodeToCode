# Auth and secrets

Where NodeToCode keeps provider API keys, how a request gets its key, which settings only the user (not a committed
project file) can change, and the MCP server's bearer token. User pages: `wiki/Security-Model.md`,
`wiki/Initial-Configuration.md` (where keys are stored, environment variables), `wiki/developers/Requiring-an-MCP-Token.md`.

## API keys: `FN2CSecretStore`

`Source/Public/Core/N2CSecretStore.h`, `Source/Private/Core/N2CSecretStore.cpp`. Provider ids: `OpenAI`, `Anthropic`,
`Gemini`, `DeepSeek` (Ollama and LM Studio have none).

| Backend | Where | Notes |
|---|---|---|
| `FN2CCredentialManagerBackend` (Windows) | Windows Credential Manager, one generic credential per provider: target `NodeToCode/<ProviderId>`, user `NodeToCode`, `CRED_PERSIST_LOCAL_MACHINE` | `cmdkey /list`, `cmdkey /delete:NodeToCode/OpenAI` |
| `FN2CSecretFileBackend` (macOS, Linux) | `FPlatformProcess::UserSettingsDir()/NodeToCode/secrets.json`, `{"Version":1, "<Provider>_API_Key": ...}` | Atomic writes (`.tmp`, `.bak`, move); a file that does not parse becomes `.corrupt-<UTC>` and is never overwritten |

- `FN2CSecretStore::Get()` picks the backend. On Windows it moves the per-user file and a project's 1.x
  `Saved/NodeToCode/User/secrets.json` into Credential Manager once (`MigrateFileToBackend`: a differing value is
  kept as `<ProviderId>.Conflict.<n>`, the value is read back, and the file is deleted only on success).
- `GetApiKey` returns the stored key, else the first set environment variable: `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`,
  `GEMINI_API_KEY` then `GOOGLE_API_KEY`, `DEEPSEEK_API_KEY` then `DEEPSEEK_API` (`GetEnvironmentVariableNames`).
  A variable is read, never saved; logs name the variable, never its value. `GetApiKeySource` says which
  (`EN2CApiKeySource`: `None`, `Store`, `Environment`).
- Settings UI: the API Key fields of `UN2CSettings` are masked (`PasswordField`), transient and write-only, with a
  status row (`DescribeApiKeySource`: at most the last 4 characters, only for keys of 16 or more). Blueprints and
  Python get only `UN2CSettings::SetApiKey` (an empty key removes it) and `HasApiKey`; nothing reflected returns a key.
- Account sign-in for Claude and Gemini subscriptions was removed in 2.0 (`N2CSignInRemoval::DocsUrl`); a subscription
  is used through the vendor's own agent connected to the MCP server (`wiki/developers/Using-a-Claude-or-Gemini-Subscription.md`).

## Per request: `IN2CCredentialSource`

`Source/Public/LLM/N2CCredentialSource.h`. `UN2CBaseLLMService::SendRequest` resolves the key just before each
request (`Resolve`), so a key changed in the settings applies to the next request and no service object holds one.
`FN2CCredential` (per request only) carries the key and its `EN2CAuthStyle`: `XApiKey` (Anthropic), `Bearer` (OpenAI,
DeepSeek, and LM Studio or Ollama when a key is set), `XGoogApiKey` (Gemini), `None`. The default source is
`FN2CSecretStoreCredentialSource`; tests and integrations set `FN2CFixedCredentialSource` with `SetCredentialSource`.
Adding a provider's key: [LLM providers](../llm-providers/AGENTS-llm-providers.md).

Logging: the console variable `n2c.LogLLMPayloads` (off by default; turning it on logs a warning) is the only way
request payloads, prompts and responses reach the log; with it off they are logged as sizes. `NodeToCode.LLM.KeyHygiene`
asserts a key travels in exactly one header, never in a URL or the log, and is redacted from an echoed error body.

## Settings by scope

| Class | Saved to | Holds |
|---|---|---|
| `UN2CSettings` (`Source/Public/Core/N2CSettings.h`) | `Config/DefaultNodeToCode.ini`, shared with the project | Provider, models, target language, theme, MCP port and tool mode, ... (never a key) |
| `UN2CUserSettings` (`Source/Public/Core/N2CUserSettings.h`) | `Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini`, this user only; shown in **Editor Preferences > Plugins > Node to Code (User)** | The MCP switches below |

| Switch | Default | Read through |
|---|---|---|
| `bEnableMcpServer` | on | `IsMcpServerEnabled()` |
| `bAllowMcpPythonExecution` | off | `IsMcpPythonExecutionAllowed()` (registers `run-python`, `save-python-script`, `delete-python-script`) |
| `bAllowMcpWritesOutsideProject` | off | `AreMcpWritesOutsideProjectAllowed()` |
| `bRequireMcpBearerToken` | off | `IsMcpBearerTokenRequired()` |

Code reads the switches only through those functions, never the properties: `ResolvePerUserBool` takes the value
from the user's own Saved layer and `-ini:` command-line overrides, so a value committed in a project-shared layer is
ignored with one warning (a cloned project cannot turn Python execution on). Spec: `NodeToCode.Security.UserSettings`.

## MCP bearer token and path denial

- `FN2CMcpAuthToken` (`Source/Private/MCP/Server/N2CMcpAuthToken.h`): 32 bytes from the OS CSPRNG as 64 hex characters
  in `UserSettingsDir()/NodeToCode/mcp-token` (owner-only on POSIX). With `bRequireMcpBearerToken` on, `/mcp` needs
  `Authorization: Bearer <token>` (constant-time compare, 401 otherwise); `/mcp/health` stays open and never shows it.
  Console commands `N2C.Mcp.CopyToken`, `N2C.Mcp.RegenerateToken`; generated client configs read it from
  `NODETOCODE_MCP_TOKEN` (`N2C.Mcp.WriteClientConfig`). Detail: "Server switches (per-user)" in
  [MCP server](../mcp-server/AGENTS-mcp-server.md).
- `FN2CPathUtils::IsDeniedForAgentAccess` (`Source/Public/Utils/N2CPathUtils.h`): the file tools never read
  `<UserSettingsDir>/NodeToCode` (keys and token), `<ProjectSaved>/NodeToCode/User`, any `secrets.json*`,
  `<Project>/.git` or `.env` files. The wider controls: [threat model](../mcp-server/threat-model.md).

## Specs

`NodeToCode.Secrets.Store`, `.Environment`, `.Keychain`, `.CredentialManager` (`Source/Private/Tests/Core/`),
`NodeToCode.Security.UserSettings`, `NodeToCode.LLM.CredentialSource`, `NodeToCode.LLM.KeyHygiene`, `NodeToCode.LLM.Auth`,
`NodeToCode.MCP.AuthToken`, `NodeToCode.Security.PathUtils.*`. Test keys come from
`Source/Private/Tests/Core/N2CTestCredentials.h`; never a real key in a spec.
