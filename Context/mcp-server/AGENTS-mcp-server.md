# NodeToCode MCP Server

Model Context Protocol server implementation for exposing Blueprint functionality to AI assistants.

## Threat model

Sources, sinks, controls, evidence and residual risks: [threat-model.md](threat-model.md). A new or changed tool
names its untrusted inputs and its sinks there in the same change.

## MCP Tools Development Guide

For creating new MCP tools, see: `@Source/Private/MCP/Tools/AGENTS.md`

## Directory Structure

```
Source/
├── Public/MCP/                           # Public headers
│   ├── Server/                           # HTTP server components
│   │   ├── N2CMcpHttpServerManager.h     # HTTP transport (HTTPServer routes, responder)
│   │   └── N2CMcpJsonRpcTypes.h          # JSON-RPC types
│   ├── Async/                            # Long-running call system
│   │   └── N2CToolAsyncTaskManager.h     # Runs long-running tool calls
│   ├── Tools/                            # Tool framework
│   │   ├── IN2CMcpTool.h                 # Tool interface (Execute, InvokeAsync)
│   │   ├── N2CToolCallContext.h          # Progress and cancellation for one call
│   │   ├── N2CMcpToolBase.h              # Tool base class
│   │   ├── N2CMcpToolTypes.h             # Tool definitions
│   │   ├── N2CMcpToolManager.h           # Tool execution
│   │   └── N2CMcpToolRegistry.h          # Auto-registration
│   ├── Resources/                        # Resource system
│   │   ├── N2CMcpResourceTypes.h         # Resource definitions
│   │   ├── N2CMcpResourceManager.h       # Resource registry
│   │   └── Implementations/              # Resource implementations
│   ├── Prompts/                          # Prompt system
│   │   ├── N2CMcpPromptTypes.h           # Prompt definitions
│   │   ├── N2CMcpPromptManager.h         # Prompt registry
│   │   └── Implementations/              # Prompt implementations
│   └── Validation/                       # Request validation
│       └── N2CMcpRequestValidator.h      # Validators
│
└── Private/MCP/                          # Private implementations
    ├── Server/                           # Server implementations
    │   ├── N2CMcpHttpServerManager.cpp
    │   ├── N2CMcpProtocol.h/.cpp         # Transport-neutral protocol core
    │   └── N2CMcpJsonRpcTypes.cpp
    ├── Async/                            # Long-running call implementation
    │   └── N2CToolAsyncTaskManager.cpp
    ├── Tools/                            # Tool framework and implementations
    │   ├── N2CMcpToolBase.cpp
    │   ├── N2CMcpToolTypes.cpp
    │   ├── N2CMcpToolManager.cpp
    │   ├── N2CMcpToolRegistry.cpp
    │   ├── N2CMcpFunctionGuidUtils.h/.cpp
    │   └── Implementations/              # Tool implementations (see below)
    ├── Utils/                            # Utility classes
    │   ├── N2CMcpArgumentParser.h/.cpp
    │   ├── N2CMcpBlueprintUtils.h/.cpp
    │   ├── N2CMcpComponentUtils.h/.cpp
    │   ├── N2CMcpContentBrowserUtils.h/.cpp
    │   ├── N2CMcpFunctionPinUtils.h/.cpp
    │   ├── N2CMcpTagUtils.h/.cpp
    │   ├── N2CMcpTypeResolver.h/.cpp
    │   └── N2CMcpVariableUtils.h/.cpp
    ├── Python/                           # Python bridge
    │   └── N2CPythonBridge.h/.cpp
    ├── Resources/Implementations/        # Resource implementations
    ├── Prompts/Implementations/          # Prompt implementations
    └── Validation/                       # Validation implementations
```

## MCP Server Architecture

The MCP server is built on HTTP transport with full JSON-RPC 2.0 compliance:

*   **Transport Layer**: HTTP server on port 27000 (configurable via settings)
*   **Long-running tools**: answered on the `tools/call` POST itself: `text/event-stream` with progress then the
    result on UE 5.8 (when Accept lists it and a progressToken is sent), else one held `application/json` response
*   **One listener**: only the engine HTTPServer on the MCP port. The port+1 notification server (cpp-httplib,
    `/mcp/events` and `/mcp/notifications`) was removed in card 0113, so the server sends no unsolicited
    notifications (`tools.listChanged` is false)
*   **Protocol**: JSON-RPC 2.0; batches only in sessions negotiated at 2025-03-26 (see Sessions and versions)
*   **Lifecycle**: Auto-starts with plugin, graceful shutdown on exit
*   **Session Management**: one session per client (`Mcp-Session-Id`), each with its own negotiated protocol version

### Sessions and versions (MCP 2025-11-25, Streamable HTTP on one `/mcp` endpoint)

*   **Versions**: `initialize` negotiates `2025-11-25`, `2025-06-18`, `2025-03-26` or `2024-11-05` (newest first):
    the requested version when supported, otherwise `2025-11-25` (so a `2026-07-28` client is answered with
    `2025-11-25`). `FN2CMcpProtocol::GetSupportedProtocolVersions()`.
*   **Sessions**: every successful `initialize` creates a new session with a new random `Mcp-Session-Id` (a GUID,
    returned in the response header) holding the negotiated version, the client's `clientInfo` and `capabilities`, and
    whether `notifications/initialized` arrived (`FN2CMcpSession`). Two clients on one editor get two sessions. At
    most `FN2CMcpProtocol::MaxSessions` (64) are kept; a new `initialize` beyond that drops the least recently used.
*   **Every other POST** must send `Mcp-Session-Id`: none gives **400** (JSON-RPC -32600, null id), an unknown or
    ended session gives **404** (-32001 "Session not found"), after which the client must `initialize` again.
*   **`MCP-Protocol-Version` header**: an unsupported value gives **400**; a missing header is accepted (2025-03-26
    clients do not send it) and the session's negotiated version applies.
*   **GET** `/mcp` (and PUT, PATCH) gives **405** with `Allow: POST, DELETE`: there is no server-to-client stream.
    The old `/mcp/sse` 204 route is gone (a GET there now reaches the `/mcp` handler and gets 405).
*   **DELETE** `/mcp` with `Mcp-Session-Id` ends the session (**204**) and cancels its running long-running calls
    (each still answers its own request, with the cancelled result); unknown id **404**, no id **400**.
*   **Client JSON-RPC responses and notifications** get **202** with no body.
*   **Batches** (a JSON array body) are accepted only on sessions negotiated at `2025-03-26` (batching was added in
    2025-03-26 and removed in 2025-06-18); on any other session they get **400** with -32600.
*   Order of checks: the transport's Origin and Host checks (403) and, for POST, Content-Type (415) run first; then
    the core's verb, `MCP-Protocol-Version`, session and batch checks; then dispatch. The version check comes before
    the session check so that a stateless 2026-07-28 probe (Claude Code first sends `server/discover` with
    `MCP-Protocol-Version: 2026-07-28` and no session) gets 400 for the version; the client then falls back to
    `initialize`.
*   Tests: `NodeToCode.MCP.Core` (sessions, verbs, batches), `NodeToCode.MCP.Transport.LongRunningCall` (the same
    through `HandleMcpRequest`), probes P3 and P8 in `Tests/mcp_probe`, and the conformance suite's
    `server-session-lifecycle` scenario. Probe scripts get their session from `Tests/mcp_probe/mcp_session.py`.
*   Not yet per session: the dynamic tool set (`assess-needed-tools`) is still one global list, and no client is
    told when it changes (no `notifications/tools/list_changed`; see Capabilities).

### Capabilities, errors and cancellation (card 0110)

*   **Capabilities**: `initialize` advertises `tools`, `resources` and `prompts`, each with `listChanged: false`:
    with no server-to-client channel (GET is 405, and the port+1 notification server is gone since card 0113) the
    server never sends `notifications/tools/list_changed`. With Enable Dynamic Tool Discovery on, the server logs a
    warning that clients see the tools `assess-needed-tools` enables only when they list tools again; card 0165
    replaces that mode. The initialize result has no `_meta`. It does not
    advertise `logging` (the server sends no `notifications/message`, and logging is deprecated in 2026-07-28), so
    `logging/setLevel` is an unknown method (-32601). `InitializeResult.instructions` tells the agent which tools to
    start with (`FN2CMcpProtocol::GetServerInstructions()`).
*   **Resource templates**: `resources/templates/list` answers `{resourceTemplates: [...]}`
    (`nodetocode://blueprint/{name}`); `resources/list` lists only `resources`.
*   **Error codes** (JSON-RPC errors, `data` built with `FJsonObject`): an unknown tool is **-32602** "Unknown tool:
    <name>" with `data.name` (the 2025-11-25 tools page's example); an unknown prompt and a missing required prompt
    argument are **-32602**; an unknown resource uri is **-32002** "Resource not found" with `data.uri`; a resource or
    prompt handler that timed out on the game thread is **-32603**. **-32601** is only for an unknown method. The
    managers report these through `FMcpResourceContents::ErrorCode` and `FMcpPromptResult::ErrorCode`.
*   **Cancellation** (basic/utilities/cancellation): `notifications/cancelled {requestId, reason?}` cancels the
    running long-running call with that JSON-RPC id (string and number ids are distinct) in the same session, through
    its call context (`FN2CToolAsyncTaskManager::CancelTaskByRequestId`). No response is sent for that request,
    whatever the tool still reports: a streamed call's `text/event-stream` closes without the response event, a held
    call (UE 5.6/5.7, or no progressToken) ends with **202** and no body. Unknown ids, finished calls, other
    sessions' calls and malformed notifications are ignored (the notification itself gets 202). Calls answered at
    once (every tool that is not long-running) cannot be cancelled.
*   **`nodetocode/cancelTask {progressToken}`** still works and is **deprecated**: it logs a warning, cancels the
    same way, and the call answers its own request with the cancelled result (as DELETE of a session and editor exit
    do).
*   Tests: `NodeToCode.MCP.Protocol` (capabilities, codes, templates, handler timeouts), `NodeToCode.MCP.Core`
    (`cancellation`), probe P9, `Tests/mcp_probe/live_translate_cancel.py` (a live cancel, `--via notification` or
    `--via cancelTask`), and the conformance baseline (`logging-set-level` is not applicable).

## Core Server Components (`Source/Public/MCP/Server/`)

*   **`N2CMcpHttpServerManager.h/.cpp`**: the HTTP transport
    *   Singleton pattern for server lifecycle control
    *   HTTP routing: `/mcp` (POST, DELETE; GET, PUT and PATCH answered 405 by the core), `/mcp/health` (health check)
    *   Loopback-only request policy: Origin and Host must be localhost, 127.0.0.1 or [::1] (else 403), POST /mcp needs Content-Type application/json (else 415), no CORS headers
    *   `HandleMcpRequest` copies the request into an `FN2CMcpInbound` and calls the protocol core with an
        HTTPServer responder: one `application/json` response, or on UE 5.8 a `text/event-stream` response drained
        from a queue (`CanStreamToolProgress()`)
    *   Sets no broadcast sink (there is no notification channel), so the core logs and drops server notifications

*   **`N2CMcpProtocol.h/.cpp`** (Private): the transport-neutral protocol core, `FN2CMcpProtocol`
    *   `Handle(const FN2CMcpInbound&, TSharedRef<IN2CMcpResponder>)`: the inbound message is the verb, the headers
        and the body bytes; the responder has `Reply` (one body), `Stream` (one message on a stream) and `Close`
    *   Parses the body once (`ParseBody`), classifies JSON-RPC messages (a non-string method or a null, boolean or
        object id is -32600), answers batches and dispatches methods to the catalogs given at construction
        (`FN2CMcpCatalogs`: tool, resource and prompt managers and the long-running call manager)
    *   Owns the long-running `tools/call` branch (streamed when the responder can stream, the client accepts
        `text/event-stream` and sent a progressToken; else one reply when the call finishes) and the per-client
        session table (`FN2CMcpSession`, see Sessions and versions)
    *   Server notifications (`notifications/tools/list_changed`, from `FN2CMcpToolManager::OnToolListChanged`) go
        to the sink set with `SetBroadcastSink`; tools never call the transport. No transport sets one today
    *   Protocol version negotiation (supports "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05")
    *   Tests drive it without a listener: `NodeToCode.MCP.Core` (in-memory responder in
        `Source/Private/Tests/MCP/N2CMcpMemoryResponder.h`) and `NodeToCode.MCP.Protocol`

*   **`N2CMcpJsonRpcTypes.h/.cpp`**: JSON-RPC type system
    *   `FJsonRpcRequest/Response/Error/Notification` structures
    *   `FJsonRpcUtils` for serialization (the core parses bodies itself, `FN2CMcpProtocol::ParseBody`)
    *   Standard error codes (-32700 to -32603)
    *   Message type detection and validation

## Long-Running Calls (`Source/Public/MCP/Tools/N2CToolCallContext.h`, `Source/Public/MCP/Async/`)

Supports long-running tool operations with progress and cancellation through one API:

*   **`IN2CMcpTool::InvokeAsync(Context, Arguments)`**: returns `TFuture<FMcpToolCallResult>`. The default wraps
    `Execute()`; a long-running tool (`bIsLongRunning`) overrides it, starts its work on the game thread without
    blocking and sets the future exactly once (result or cancelled result). `Execute()` defaults to an error, and
    the tool manager refuses a long-running tool inside a JSON-RPC batch.

*   **`FN2CToolCallContext`**: one call as the tool sees it
    *   `ReportProgress(float, FString)` - any thread; delivered on the game thread to the caller's sink; dropped
        once the call is completed
    *   `IsCancelled()`, `OnCancelled(TFunction<void()>)` - cancel callbacks run once on the game thread
    *   `Cancel(Reason)`, `MarkCompleted()` - caller side (the manager)

*   **`N2CToolAsyncTaskManager.h/.cpp`**: runs long-running calls; no tool-specific code
    *   `LaunchTask()` - finds the registered tool and calls `InvokeAsync` with a context whose progress goes to the
        call's `FN2CAsyncTaskResponder` (as `notifications/progress` with the client's progressToken as sent) and
        writes the one JSON-RPC response when the future is set; `LaunchToolCall()` runs a given tool instance,
        `SetToolResolverForTesting()` swaps in test tools
    *   `CancelTask()`, `CancelTaskByProgressToken()` - cancel through the context;
        `FindTaskByRequestId(SessionId, RequestId)`, `CancelTaskByRequestId()` - for `notifications/cancelled`, which
        also suppresses the call's response (`FN2CToolCallRecord::bResponseSuppressed`; the responder's
        `EndWithoutResponse` ends the request instead)
    *   `GetTaskContext()` (an `FN2CToolCallRecord`), `IsTaskRunning()`, `GetRunningTaskIds()`
    *   A call stops being tracked once it has delivered its result (`OnTaskCompleted`); its progress token is remembered for 10 min so `nodetocode/cancelTask` answers `task_already_completed` (`WasProgressTokenCompleted()`)
    *   `CleanupCompletedTasks()` (safety net), `CancelAllTasks()` - called from `FCoreDelegates::OnPreExit` (before UObjects are purged) and in `ShutdownModule` before the MCP routes are unbound
    *   `GetTrackedTaskCount()`, `GetTrackedProgressTokenCount()`
    *   Thread-safe with FCriticalSection

## Validation Framework (`Source/Public/MCP/Validation/`)

*   **`N2CMcpRequestValidator.h/.cpp`**: request validation used by the protocol core
    *   `ValidateToolsCallRequest`, `ValidateResourcesReadRequest`, `ValidatePromptsGetRequest`
    *   `ValidateParamsIsObject` (params present and an object) and the helpers they use
        (`ValidateRequiredString`, `ValidateOptionalObject`, `ValidateParamsNotNull`)
    *   Progress for long-running calls goes through `FN2CToolCallContext` (below); there is no separate tracker

## Tool Subsystem (`Source/Private/MCP/Tools/`)

The tool system provides a scalable framework for exposing Blueprint operations:

### Core Infrastructure

*   `IN2CMcpTool.h`: Tool interface contract
*   `N2CMcpToolBase.h/.cpp`: Base class with common functionality
*   `N2CMcpToolTypes.h/.cpp`: Tool definition and result structures
*   `N2CMcpToolManager.h/.cpp`: Thread-safe tool registry
*   `N2CMcpToolRegistry.h/.cpp`: Automatic registration system

### Tool Registration

*   `REGISTER_MCP_TOOL` macro for static initialization
*   Self-contained tools auto-register on plugin load
*   Game Thread execution support with timeouts
*   Input schema validation framework

### Tool Implementations (`Implementations/`)

**Blueprint/Analysis/** - Blueprint inspection tools:
*   `get-focused-blueprint` - Get currently focused Blueprint
*   `list-blueprint-functions` - List functions in a Blueprint
*   `list-overridable-functions` - List parent class overridable functions
*   `get-open-blueprint-editors` - Get all open Blueprint editors

**Blueprint/Functions/** - Function management:
*   `create-blueprint-function` - Create a new function
*   `open-blueprint-function` - Open/focus a function graph
*   `delete-blueprint-function` - Delete a function
*   `add-function-return-pin` - Add return pin to function
*   `remove-function-return-pin` - Remove function return pin
*   `add-function-entry-pin` - Add input parameter to function
*   `remove-function-entry-pin` - Remove function input parameter

**Blueprint/Variables/** - Variable management:
*   `create-variable` - Create member variable
*   `create-local-variable` - Create local function variable
*   `search-variable-types` - Search for variable types
*   `get-blueprint-member-variables` - List Blueprint member variables
*   `create-set-member-variable-node` - Create setter node for member var
*   `create-get-member-variable-node` - Create getter node for member var
*   `set-member-variable-default-value` - Set member variable default
*   `get-blueprint-function-local-variables` - List function local variables
*   `create-set-local-function-variable-node` - Create setter for local var
*   `create-get-local-function-variable-node` - Create getter for local var
*   `set-local-function-variable-default-value` - Set local variable default

**Blueprint/Graph/** - Node and graph operations:
*   `search-blueprint-nodes` - Search available node types
*   `add-bp-node-to-active-graph` - Add a node to the graph
*   `connect-pins` - Connect two pins
*   `set-input-pin-value` - Set literal value on input pin
*   `delete-blueprint-node` - Delete a node from graph
*   `create-comment-node` - Create a comment box
*   `find-nodes-in-graph` - Find nodes in current graph

**Blueprint/Organization/** - Tagging and organization:
*   `tag-blueprint-graph` - Apply tag to a graph
*   `list-blueprint-tags` - List all tags
*   `remove-tag-from-graph` - Remove tag from graph

**Blueprint/Compilation/** - Build operations:
*   `save-blueprint` - Save Blueprint asset
*   `compile-blueprint` - Compile Blueprint

**Blueprint/Classes/** - Class management:
*   `search-blueprint-classes` - Search for Blueprint parent classes
*   `create-blueprint-class` - Create new Blueprint class

**Blueprint/Components/** - Component management:
*   `list-components-in-actor` - List components in an Actor Blueprint
*   `add-component-class-to-actor` - Add component to Actor
*   `delete-component-in-actor` - Remove component from Actor

**Translation/** - Code translation tools:
*   `translate-focused-blueprint` - Translate Blueprint to code (async, long-running)
*   `get-available-translation-targets` - List target languages
*   `get-available-llm-providers` - List LLM providers
*   `get-translation-output-directory` - Get output directory path

**FileSystem/** - File operations:
*   `read-path` - List directory contents
*   `read-file` - Read file contents

**ContentBrowser/** - Content browser operations:
*   `open-content-browser-path` - Navigate to path
*   `read-content-browser-path` - List assets at path
*   `open-blueprint-asset` - Open Blueprint by path
*   `search-content-browser` - Search for assets
*   `create-folder` - Create new folder
*   `move-asset` - Move asset to new location
*   `copy-asset` - Copy asset
*   `rename-asset` - Rename asset
*   `move-folder` - Move folder
*   `rename-folder` - Rename folder

**Python/** - Python script management:
*   `run-python` - Execute Python code in UE environment
*   `list-python-scripts` - List scripts by category
*   `search-python-scripts` - Search by name/description/tags
*   `get-python-script` - Get full script code and metadata
*   `get-script-functions` - Get function signatures via AST (token-efficient)
*   `save-python-script` - Save script to library
*   `delete-python-script` - Remove script from library

**ToolManagement/** - Dynamic tool management:
*   `assess-needed-tools` - Request activation of tool categories (for dynamic discovery mode)

## MCP Utility Classes (`Source/Private/MCP/Utils/`)

Common utilities shared across MCP tools:

*   **`N2CMcpArgumentParser`**: JSON argument parsing helpers
*   **`N2CMcpBlueprintUtils`**: Blueprint manipulation utilities
*   **`N2CMcpComponentUtils`**: Actor component utilities
*   **`N2CMcpContentBrowserUtils`**: Content browser operations
*   **`N2CMcpFunctionPinUtils`**: Function pin manipulation
*   **`N2CMcpTagUtils`**: Graph tagging utilities
*   **`N2CMcpTypeResolver`**: UE type resolution (class/struct lookups)
*   **`N2CMcpVariableUtils`**: Variable creation and manipulation

## Resource System (`Source/Public/MCP/Resources/`)

Resources expose Blueprint data in a structured, queryable format:

### Core Infrastructure

*   `N2CMcpResourceTypes.h/.cpp`: Resource definitions and contents
*   `N2CMcpResourceManager.h/.cpp`: Resource registry with subscription support
*   `IN2CMcpResource.h`: Resource interface for implementations

### Resource Types

*   Static resources: Fixed URIs with direct handlers
*   Dynamic resources: Template-based URI patterns
*   Subscription support for real-time updates

### Implemented Resources (`Implementations/`)

*   `N2CMcpBlueprintResource.h/.cpp`:
    *   `nodetocode://blueprint/current`: Current focused Blueprint
    *   `nodetocode://blueprints/all`: List of open Blueprints
    *   `nodetocode://blueprint/{name}`: Blueprint by name (template)

## Prompt System (`Source/Public/MCP/Prompts/`)

Prompts provide guided interactions for common Blueprint tasks:

### Core Infrastructure

*   `N2CMcpPromptTypes.h`: Prompt structures with type alias for arguments
*   `N2CMcpPromptManager.h/.cpp`: Prompt registry and execution
*   `IN2CMcpPrompt.h`: Interface for prompt implementations

### Type Safety

*   `FMcpPromptArguments` type alias for `TMap<FString, FString>`
*   Avoids macro expansion issues with template parameters
*   Consistent argument passing across the system

### Implemented Prompts (`Implementations/`)

*   `N2CMcpCodeGenerationPrompt`: Generate code with language/style options

## MCP Protocol Implementation

The server implements these MCP methods:

### Core Methods
*   `initialize`: Connection setup with capability negotiation
*   `notifications/initialized`: Connection confirmation
*   `ping`: Keep-alive testing

### Tool Methods
*   `tools/list`: Enumerate available tools with schemas
*   `tools/call`: Execute tools with arguments

### Resource Methods
*   `resources/list`: List available resources
*   `resources/templates/list`: List resource templates
*   `resources/read`: Read resource contents

### Prompt Methods
*   `prompts/list`: List available prompts
*   `prompts/get`: Get prompt with argument substitution

### Advanced Features
*   Batch request processing (sessions negotiated at 2025-03-26 only)
*   Progress notifications on the tools/call's own text/event-stream response (UE 5.8)
*   Protocol version negotiation (2025-11-25, 2025-06-18, 2025-03-26, 2024-11-05)
*   Per-client sessions (`Mcp-Session-Id`), ended with HTTP DELETE
*   Request cancellation (`notifications/cancelled`; the custom `nodetocode/cancelTask` is deprecated)

## Dynamic Tool Management

The MCP server supports dynamic tool discovery (configurable in plugin settings):

*   **Disabled (Default)**: All tools registered at startup
*   **Enabled**: Only `assess-needed-tools` initially available
*   Use `assess-needed-tools` with category names to activate tool sets
*   No `notifications/tools/list_changed` is sent (no notification channel, `tools.listChanged` false): the client
    must call `tools/list` again; the server logs a warning at start when this mode is on (card 0165 replaces it)

Available tool categories for `assess-needed-tools`:
*   Tool Management
*   Blueprint Discovery
*   Blueprint Graph Editing
*   Blueprint Function Management
*   Blueprint Variable Management
*   Blueprint Organization
*   Blueprint Compilation
*   Blueprint Class Management
*   Blueprint Component Management
*   Content Browser
*   File System
*   Translation
*   Python Scripting

## Editor Integration Updates

*   **`FN2CEditorIntegration` Refactoring**:
    *   Modularized Blueprint collection and translation
    *   Helper methods for MCP tool usage:
        *   `GetFocusedBlueprintAsJson()`: High-level API
        *   `CollectNodesFromGraph()`: Node collection
        *   `TranslateNodesToN2CBlueprint()`: Translation
        *   `SerializeN2CBlueprintToJson()`: Serialization

*   **Plugin Settings** (`N2CSettings.h/.cpp`):
    *   `McpServerPort`: Configurable server port (default 27000, 1024-65535);
        `McpServerPortRange` (default 10): ports tried from there when one is taken; `McpServerStatus` (transient,
        read-only): where the server listens now. A port change restarts the server on the next tick
        (`FN2CMcpHttpServerManager::HandlePortSettingsChanged`), no editor restart
    *   `bEnableDynamicToolDiscovery`: Toggle dynamic tool management (logs a warning: no `tools/list_changed` is
        sent, clients see newly enabled tools when they list again)
    *   Exposed in Editor UI for runtime configuration

## Testing and Development

Example test commands for the MCP server:

```bash
# Basic connectivity
curl -X GET http://localhost:27000/mcp/health

# Protocol initialization: keep the Mcp-Session-Id response header; every later request sends it
SID=$(curl -s -D - -o /dev/null -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc": "2.0", "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "test", "version": "1.0"}}, "id": 1}' \
  | tr -d '\r' | sed -n 's/^[Mm]cp-[Ss]ession-[Ii]d: //p')
curl -X POST http://localhost:27000/mcp -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "notifications/initialized"}'

# List tools with schemas
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 2}'

# Execute get-focused-blueprint tool
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "get-focused-blueprint", "arguments": {}}, "id": 3}'

# List resources
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "resources/list", "id": 4}'

# Read current blueprint resource
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "resources/read", "params": {"uri": "nodetocode://blueprint/current"}, "id": 5}'

# Get code generation prompt
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "prompts/get", "params": {"name": "generate-code", "arguments": {"language": "python", "style": "concise"}}, "id": 6}'

# Batch request example (only in a session initialized with protocolVersion 2025-03-26; others get 400)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID_2025_03_26" \
  -d '[{"jsonrpc": "2.0", "method": "ping", "id": 1}, {"jsonrpc": "2.0", "method": "tools/list", "id": 2}]'

# Long-running tool with progress (the same response streams on UE 5.8; -N shows events as they arrive)
curl -N -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "translate-focused-blueprint", "arguments": {"language": "cpp"}, "_meta": {"progressToken": "unique-token-123"}}, "id": 7}'

# Cancel the running call (id 7): answered 202; the call's own response then ends without a JSON-RPC response
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" -H "MCP-Protocol-Version: 2025-11-25" \
  -d '{"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 7, "reason": "user pressed Stop"}}'

# End the session
curl -X DELETE http://localhost:27000/mcp -H "Mcp-Session-Id: $SID"
```

## Related Projects

*   **NodeToCode MCP Bridge**: Python stdio-to-HTTP bridge for MCP clients
    *   Location: `Content/Python/mcp_bridge/nodetocode_bridge.py`
    *   Enables Claude for Desktop and other MCP client integration
    *   Handles protocol translation and session management

## Server switches (per-user)

`UN2CUserSettings` (Project Settings > Plugins > Node to Code (User), saved to the user's
`Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini`; Unreal also reads a committed
`Config/DefaultEditorPerProjectUserSettings.ini` as the default layer, so a project file can still set them, card
0423):

- **Enable MCP Server** (default on): starts the MCP server on `McpServerPort`.

Ports and shutdown (card 0112): `FN2CMcpHttpServerManager::StartServer(Port, Range)` checks each port with a bind of
its own before handing it to HTTPServer (`GetHttpRouter(Port, true)`), so a port another process or plugin holds is
skipped with a warning and an editor notification naming the port it took and the Server Port setting; when none is
free it returns false with an error notification. It calls `StartAllListeners` only when HTTPServer's listeners are
not enabled yet (the module starts with them off and has no per-listener start). `StopServer` unbinds only its own
route handles (`/mcp`, `/mcp/health`) and never calls `StopAllListeners`, so Remote Control and UE 5.8's Unreal MCP
keep answering; the listener on the old port stays bound (404) until the editor exits. While the server runs it
writes a discovery file, `<Project>/Saved/NodeToCode/mcp-server.json` plus
`<UserSettingsDir>/NodeToCode/servers/<pid>.json` (port, requestedPort, url, pid, projectFile, projectName,
engineVersion, pluginVersion, protocolVersions, startedAt), removed on stop; index entries of processes that are gone are pruned on the next start. Spec:
`NodeToCode.MCP.ServerLifecycle`; live check: `Tests/mcp_probe/live_port_restart.py`.
- **Allow MCP Python Execution** (default off): registers `run-python`, `save-python-script` and
  `delete-python-script`. Filtered in `FN2CMcpToolManager::RegisterTool`, so dynamic discovery cannot add them either.

The server does not start in commandlets or `-unattended` runs (build agents, the smoke test) unless the command
line has `-N2CMcp`. `bEnablePythonScriptOnlyMode` now defaults to false.
