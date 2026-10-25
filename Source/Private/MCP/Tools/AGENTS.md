# NodeToCode MCP Tools

This directory holds the MCP (Model Context Protocol) tool framework's implementation files and the tool
implementations of the NodeToCode plugin. The framework is interface-based with automatic registration, so a new
tool needs no change to existing code.

## Directory Structure

```
Source/Public/MCP/Tools/          - Framework headers
├── IN2CMcpTool.h                 - Tool interface (GetDefinition, Execute, InvokeAsync, RequiresGameThread)
├── N2CToolCallContext.h          - Progress and cancellation for one long-running call
├── N2CMcpToolBase.h              - Base class for all tools (schema and game-thread helpers)
├── N2CMcpToolRegistry.h          - Auto-registration (REGISTER_MCP_TOOL)
├── N2CMcpToolManager.h           - The registered (active) tool set and tool execution
└── N2CMcpToolTypes.h             - FMcpToolDefinition, FMcpToolCallResult

Source/Private/MCP/Tools/         - This directory
├── AGENTS.md                     - This documentation
├── N2CToolCallContext.cpp
├── N2CMcpToolBase.cpp
├── N2CMcpToolRegistry.cpp
├── N2CMcpToolManager.cpp
├── N2CMcpToolTypes.cpp
├── N2CMcpFunctionGuidUtils.h/.cpp - Function graph GUID helpers shared by the Blueprint tools
└── Implementations/              - Tool implementations by folder
    ├── Blueprint/                - Blueprint tools, one subfolder per area (AGENTS.md in each)
    │   ├── Analysis/  Classes/  Compilation/  Components/
    │   └── Functions/  Graph/  Organization/  Variables/
    ├── Translation/              - Code translation tools
    ├── FileSystem/               - File and directory reads
    ├── ContentBrowser/           - Content Browser navigation and asset operations
    ├── Python/                   - run-python and the script library tools
    └── ToolManagement/           - assess-needed-tools (dynamic tool discovery)
```

`Blueprint/`, each of its subfolders, `Translation/`, `FileSystem/` and `ContentBrowser/` have their own `AGENTS.md`;
`Python/` is described in `Context/python-scripting/AGENTS-python-scripting.md` and `ToolManagement/` below.

## Framework Overview

### Core Components

1. **`IN2CMcpTool`** - The interface that all MCP tools must implement
   - Defines the contract for tool definition and execution
   - `Execute` for quick tools; `InvokeAsync` (a `TFuture` plus an `FN2CToolCallContext`) for long-running ones

2. **`FN2CMcpToolBase`** - Base class providing common functionality
   - Implements helper methods for schema building
   - Provides Game Thread execution utilities
   - Handles common error scenarios

3. **`FN2CMcpToolRegistry`** - Static registry with automatic registration
   - Uses C++ static initialization for tool discovery
   - No manual registration required
   - Thread-safe tool lookup

4. **`FN2CMcpToolManager`** - Runtime tool management
   - Holds the active tool set that `tools/list` returns, filled at server start by the registration mode (below)
   - Handles tool execution lifecycle
   - Runs long-running tools through `FN2CToolAsyncTaskManager` (progress via `FN2CToolCallContext`)

### Registration System

The `REGISTER_MCP_TOOL` macro enables automatic tool registration:

```cpp
// In your tool's .cpp file
REGISTER_MCP_TOOL(FN2CMcpYourToolName)
```

This macro:
- Creates a static initializer that runs before main()
- Adds the tool class to the global registry (`FN2CMcpToolRegistry`)
- Makes the tool available to the registration modes; whether `tools/list` shows it depends on the mode

## Tool Registration Modes

When the MCP server starts, `FN2CMcpHttpServerManager::RegisterMcpTools()` fills the active tool set from the
registry according to two settings in Project Settings > Plugins > Node to Code > MCP Server (`UN2CSettings`,
project config `DefaultNodeToCode.ini`). The first that applies wins:

| Order | Mode | Setting | Tools registered | `tools/list` count |
|---|---|---|---|---|
| 1 | Python script-only | **Enable Python Script-Only Mode** (`bEnablePythonScriptOnlyMode`, default off) | `run-python`, `list-python-scripts`, `search-python-scripts`, `get-python-script`, `get-script-functions`, `save-python-script`, `delete-python-script` (`FN2CMcpToolManager::RegisterToolsByName`). No translation, Blueprint, Content Browser or file tools | 7 |
| 2 | Dynamic tool discovery | **Enable Dynamic Tool Discovery** (`bEnableDynamicToolDiscovery`, default off) | `assess-needed-tools` only (`SetDefaultToolSet`); it enables tool categories on request | 1 |
| 3 | All tools (the shipped default) | both off | every registered tool except `assess-needed-tools` (`RegisterAllToolsExceptAssess`) | 62 |

- **The default** is mode 3: both settings default to false in `N2CSettings.h`, so a fresh install lists every
  tool, `get-focused-blueprint` and `translate-focused-blueprint` included.
- **Python execution switch:** every path goes through `FN2CMcpToolManager::RegisterTool`, which skips `run-python`,
  `save-python-script` and `delete-python-script` (`UN2CUserSettings::IsPythonExecutionTool`) unless the per-user
  **Allow MCP Python Execution** is on (`UN2CUserSettings::IsMcpPythonExecutionAllowed()`, default off; only the
  user's own Saved/Config value counts, card 0423). With it off (the default) the counts are 59 for mode 3, 1 for
  mode 2 (and those three tools cannot be enabled by `assess-needed-tools`), and 4 for mode 1 (list, search, get and
  get-script-functions; the editor logs a warning at start).
- **Restart required:** the mode is read only when the server starts (editor start, or the server restart a Server
  Port change causes). Changing either setting takes effect after an editor restart.
- **Checked by:** `build.ps1 -Smoke` starts one editor per mode with Python execution allowed and compares
  `tools/list` with `Tests/smoke/expected.json` (`default` 62, `dynamic` 1, `python_only` 7). A card that adds or
  removes a tool updates those counts and the tables in this file.

### Tool Categories

`FMcpToolDefinition::Category` (the third constructor argument, or `Definition.Category`) groups tools for dynamic
discovery. The category strings at HEAD, with their tools and folders:

| Category | Tools | Folder |
|---|---|---|
| `Tool Management` | `assess-needed-tools` | `ToolManagement/` |
| `Blueprint Discovery` | `get-focused-blueprint`, `get-open-blueprint-editors`, `list-blueprint-functions`, `list-overridable-functions`, `search-blueprint-nodes` | `Blueprint/Analysis/`, `Blueprint/Graph/` |
| `Blueprint Graph Editing` | `add-bp-node-to-active-graph`, `connect-pins`, `create-comment-node`, `delete-blueprint-node`, `set-input-pin-value` | `Blueprint/Graph/` |
| `Blueprint Function Management` | `create-blueprint-function`, `delete-blueprint-function`, `open-blueprint-function`, `add-function-input-pin`, `add-function-return-pin`, `remove-function-entry-pin`, `remove-function-return-pin` | `Blueprint/Functions/` |
| `Blueprint Variable Management` | `create-variable`, `create-local-variable`, `search-variable-types`, `get-blueprint-member-variables`, `get-blueprint-function-local-variables`, `create-get-member-variable-node`, `create-set-member-variable-node`, `create-get-local-function-variable-node`, `create-set-local-function-variable-node`, `set-member-variable-default-value`, `set-local-function-variable-default-value` | `Blueprint/Variables/` |
| `Blueprint Organization` | `tag-blueprint-graph`, `list-blueprint-tags`, `remove-tag-from-graph` | `Blueprint/Organization/` |
| `Blueprint Compilation` | `compile-blueprint`, `save-blueprint` | `Blueprint/Compilation/` |
| `Blueprint Classes` | `search-blueprint-classes`, `create-blueprint-class` | `Blueprint/Classes/` |
| `Blueprint Components` | `list-components-in-actor`, `add-component-class-to-actor`, `delete-component-in-actor` | `Blueprint/Components/` |
| `Content Browser` | `open-content-browser-path`, `read-content-browser-path`, `search-content-browser`, `open-blueprint-asset`, `create-folder`, `move-folder`, `rename-folder`, `move-asset`, `copy-asset`, `rename-asset` | `ContentBrowser/` |
| `File System` | `read-path`, `read-file` | `FileSystem/` |
| `Translation` | `translate-focused-blueprint`, `get-available-llm-providers`, `get-available-translation-targets`, `get-translation-output-directory` | `Translation/` |
| `Python` | `run-python`, `list-python-scripts`, `search-python-scripts`, `get-python-script`, `get-script-functions`, `save-python-script`, `delete-python-script` | `Python/` |
| (none) | `find-nodes-in-graph` sets no category, so `assess-needed-tools` cannot enable it | `Blueprint/Graph/` |

That is 63 tools; mode 3 lists 62 of them (all but `assess-needed-tools`). Category strings are matched exactly.

## Long-Running Tools and Async Execution

NodeToCode runs long-running tools (LLM translation) on the game thread without blocking it and answers the client's
`tools/call` on that same POST, with an ordinary CallToolResult, once the call finishes. A long-running tool gets an
`FN2CToolCallContext` (`Source/Public/MCP/Tools/N2CToolCallContext.h`) for progress and cancellation and returns its
result as a `TFuture`; `FN2CToolAsyncTaskManager` runs the call and knows nothing about any one tool.

### How Async Tools Work

1. **Tool Declaration**: Mark a tool as long-running by setting `bIsLongRunning = true` in its definition, and
   override `InvokeAsync` instead of `Execute`
2. **Same-POST response**: the protocol core (`FN2CMcpProtocol::Handle`, which the HTTP transport's
   `FN2CMcpHttpServerManager::HandleMcpRequest` calls) starts the call and does not answer at once.
   On UE 5.8, when the request's `Accept` lists `text/event-stream` and `params._meta.progressToken` is set, the
   response is a `text/event-stream`: `notifications/progress` events (the token echoed with its JSON type, a number
   stays a number), then the JSON-RPC response with the CallToolResult, then the connection closes. Otherwise (no
   token, JSON-only Accept, or UE 5.6/5.7) the request is held and answered with one `application/json` response
   when the task finishes; no progress is sent. The 5.6/5.7 choice follows the open decision card 0085 and lives in
   `FN2CMcpHttpServerManager::CanStreamToolProgress()`. A long-running tool inside a JSON-RPC batch is refused.
3. **Progress Reporting**: Tools report progress through `Context->ReportProgress(Progress, Message)` (any thread;
   delivered on the game thread, dropped once the call has its result); the translation reports real stages
   (collecting the Blueprint, request sent, waiting with elapsed seconds, parsing) at least once per second, and each
   value sent is larger than the last, as MCP requires
4. **Cancellation**: Clients cancel a running call with the standard `notifications/cancelled` naming the
   tools/call's JSON-RPC `requestId`, in the same session. The manager (`CancelTaskByRequestId`) cancels the call's
   context; the tool's `OnCancelled` callbacks run on the game thread and stop the work (the translation cancels its
   provider HTTP request). No response is sent for a request cancelled this way: a stream closes without the
   response event, a held request ends with 202 and no body. The custom `nodetocode/cancelTask` with the call's
   progress token still works but is deprecated; there the tool's cancelled error result is the call's response.

### Creating a Long-Running Tool

1. Mark the tool as long-running in its definition (`Definition.bIsLongRunning = true`).
2. Override `InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments)`. It is
   called once per call, on the game thread, and must not block: start the work (for example an async HTTP request)
   and return a `TFuture<FMcpToolCallResult>`. Set its promise exactly once, on the game thread, with the result or a
   cancelled result. A broken promise (destroyed unset) asserts, so whatever owns the promise sets it on every path.
3. Report progress with `Context->ReportProgress`, and register `Context->OnCancelled` to stop the work. Hold your
   own state weakly in that callback: the context can outlive it.

There is no registration step and no tool-name switch: the server sees `bIsLongRunning` and the manager calls the
tool's `InvokeAsync`. `Execute` keeps its default (an error); inside a JSON-RPC batch the tool manager refuses a
long-running tool. `FN2CTranslateBlueprintCall` is the reference implementation: it keeps itself alive until its
result is set, holds the `FN2CLLMRequestHandle` that `ProcessN2CJsonWithOverrides` returns, reports progress from an
`FTSTicker`, and calls `Cancel()` on the handle on cancel and on timeout.

```cpp
// N2CMcpMyLongTool.cpp (the class derives from FN2CMcpToolBase and declares the InvokeAsync override)
#include "N2CMcpMyLongTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSerializer.h"
#include "LLM/N2CLLMModule.h"
#include "Async/Future.h"

REGISTER_MCP_TOOL(FN2CMcpMyLongTool)

TFuture<FMcpToolCallResult> FN2CMcpMyLongTool::InvokeAsync(const FN2CToolCallContextRef& Context,
    const TSharedPtr<FJsonObject>& Arguments)
{
    check(IsInGameThread());

    struct FState
    {
        TPromise<FMcpToolCallResult> Promise;
        bool bSet = false;
        FN2CLLMRequestHandlePtr Request;
        void Set(const FMcpToolCallResult& Result) { if (!bSet) { bSet = true; Promise.SetValue(Result); } }
        ~FState() { Set(FMcpToolCallResult::CreateErrorResult(TEXT("dropped"))); }
    };
    const TSharedRef<FState> State = MakeShared<FState>();
    TFuture<FMcpToolCallResult> Future = State->Promise.GetFuture();

    FString Json;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("json"), Json))
    {
        State->Set(FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: json")));
        return Future;
    }

    // The translation is saved with the Blueprint the JSON describes (a capture's FN2CCapture::Blueprint, when you have one)
    FN2CBlueprint Captured;
    FN2CSerializer::FromJson(Json, Captured);

    Context->ReportProgress(0.1f, TEXT("Sending request..."));
    State->Request = UN2CLLMModule::Get()->ProcessN2CJson(Json, Captured, FOnLLMResponseReceived::CreateLambda(
        [State](const FString& Response)
        {
            // Runs on the game thread, at most once, and never after Request->Cancel() succeeded
            State->Set(FMcpToolCallResult::CreateTextResult(Response));
            State->Request.Reset(); // the request holds this callback, which holds State
        }));

    const TWeakPtr<FState> WeakState = State;
    const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = Context;
    Context->OnCancelled([WeakState, WeakContext]()
    {
        const TSharedPtr<FState> Pinned = WeakState.Pin();
        const FN2CToolCallContextPtr PinnedContext = WeakContext.Pin();
        if (Pinned && PinnedContext && Pinned->Request.IsValid() && Pinned->Request->Cancel())
        {
            Pinned->Set(FMcpToolCallResult::CreateErrorResult(PinnedContext->GetCancelledMessage()));
            Pinned->Request.Reset();
        }
    });
    return Future;
}
```

### Client Usage with Progress

When calling a long-running tool, clients should provide a progress token (`$SID` is the session from
"Calling the Server with curl" below):

```bash
# Call with progress token
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "translate-focused-blueprint",
      "arguments": {"language": "cpp"},
      "_meta": {
        "progressToken": "unique-progress-token-123"
      }
    },
    "id": 1
  }'

# With -H "Accept: application/json, text/event-stream" on UE 5.8, the same response is a stream:
# event: message
# data: {"jsonrpc":"2.0","method":"notifications/progress","params":{"progressToken":"unique-progress-token-123","progress":0.1,"message":"Translation request sent to the LLM provider"}}
#
# event: message
# data: {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"..."}]}}
#
# Without it (or on UE 5.6/5.7) the call waits and returns one JSON response:
# {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"..."}]}}
```

### Cancelling a Task

To cancel a running call, send `notifications/cancelled` with the tools/call's id (here 1) in the same session:

```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "notifications/cancelled",
    "params": {
      "requestId": 1,
      "reason": "User pressed Stop"
    }
  }'

# Response: 202 Accepted, no body. The tools/call's own response then ends without a JSON-RPC response.
```

The deprecated `nodetocode/cancelTask` (`"params": {"progressToken": "unique-progress-token-123"}`) still answers
`{"status":"cancellation_initiated"}`, and the call then answers with the cancelled error result.

## Dynamic Tool Management

Dynamic tool discovery keeps the first `tools/list` small: with **Enable Dynamic Tool Discovery** on (and Python
script-only mode off, which takes precedence) the server starts with `assess-needed-tools` only. It is off by
default.

### How It Works

1. **Initial State**: only `assess-needed-tools` is registered when the server starts.

2. **Tool Assessment**: The LLM client calls `assess-needed-tools` with a list of required tool categories:
   ```json
   {
     "categories": ["Blueprint Discovery", "Blueprint Graph Editing"]
   }
   ```

3. **Registration**: `FN2CMcpToolManager::UpdateActiveTools` replaces the active set with `assess-needed-tools` plus
   every tool whose category is in the list (exact strings from the category table above; unknown strings match
   nothing). The set is one global list, shared by every session on the editor. An empty list resets it to
   `assess-needed-tools` only.

4. **No notification**: the server cannot tell the client the list changed. It has no server-to-client channel
   (GET `/mcp` is 405, and the port+1 notification server was removed in card 0113), so `initialize` advertises
   `tools.listChanged: false` and no `notifications/tools/list_changed` is sent. With this mode on, the editor logs
   a warning saying so at server start. Card 0165 replaces the mode with toolsets that never change mid-session.

5. **Client Refresh**: The client must call `tools/list` again to get the updated tool set.

### Configuration

To enable dynamic tool discovery:
1. Open Project Settings → Plugins → Node to Code
2. Under "MCP Server", check "Enable Dynamic Tool Discovery" (and leave "Enable Python Script-Only Mode" off)
3. Restart the editor for the change to take effect

**Note**: the mode only works with clients that call `tools/list` again after `assess-needed-tools` (or that the
user can tell to refresh); a client that lists tools once per session never sees the enabled tools.

### Categories in `assess-needed-tools`

The tool's description (`FN2CMcpAssessNeededToolsTool::GetCategoryDescriptions`) names nine categories: Tool
Management, Blueprint Discovery, Blueprint Graph Editing, Blueprint Function Management, Blueprint Variable
Management, Blueprint Organization, Content Browser, File System and Translation. The matching uses the category
strings themselves, so `Blueprint Compilation`, `Blueprint Classes`, `Blueprint Components` and `Python` also work
when a client sends them, although the description does not list them.

### Example Workflow

When dynamic tool discovery is enabled (after the `initialize` in "Calling the Server with curl"):

```bash
# 1. Initial connection - only assess-needed-tools is available
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'

# 2. Assess needed tools for a task
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "assess-needed-tools",
      "arguments": {
        "categories": ["Blueprint Discovery", "Blueprint Graph Editing"]
      }
    },
    "id": 2
  }'

# 3. No notification arrives (tools.listChanged is false): list the tools again

# 4. Get updated tool list (assess-needed-tools plus the 10 tools of the two categories)
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 3}'
```

## Creating a New MCP Tool

Follow these steps to add a new MCP tool:

### 1. Create the Tool Header File

Create a new header file in the appropriate subdirectory under `Source/Private/MCP/Tools/Implementations/`. Choose the subdirectory based on your tool's category:
- `Blueprint/Analysis/` - For Blueprint inspection tools
- `Blueprint/Functions/` - For function management tools
- `Blueprint/Variables/` - For variable creation tools
- `Blueprint/Graph/` - For node and graph manipulation
- `Blueprint/Organization/` - For tagging and organizing
- `Blueprint/Compilation/` - For save and compile tools
- `Blueprint/Classes/` - For Blueprint class tools
- `Blueprint/Components/` - For component management tools
- `Translation/` - For code translation tools
- `FileSystem/` - For file operations
- `ContentBrowser/` - For content browser operations
- `Python/` - For Python script tools
- `ToolManagement/` - For dynamic tool discovery

```cpp
// N2CMcpYourToolName.h
#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"

class FN2CMcpYourToolName : public FN2CMcpToolBase
{
public:
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    
    // Only override if your tool needs Game Thread execution
    // virtual bool RequiresGameThread() const override { return true; }
};
```

### 2. Create the Tool Implementation

Create the corresponding .cpp file in the same subdirectory as the header file:

```cpp
// N2CMcpYourToolName.cpp
#include "N2CMcpYourToolName.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Utils/N2CLogger.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpYourToolName)

FMcpToolDefinition FN2CMcpYourToolName::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("your-tool-name"),
        TEXT("Description of what your tool does"),
        TEXT("Blueprint Discovery") // An existing category string (see Tool Categories), so dynamic discovery finds it
    );
    
    // Define input schema
    TMap<FString, FString> Properties;
    Properties.Add(TEXT("param1"), TEXT("string"));
    Properties.Add(TEXT("param2"), TEXT("number"));
    
    TArray<FString> Required;
    Required.Add(TEXT("param1")); // Only param1 is required
    
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}

FMcpToolCallResult FN2CMcpYourToolName::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    // Extract parameters
    FString Param1;
    if (!Arguments->TryGetStringField(TEXT("param1"), Param1))
    {
        return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: param1"));
    }
    
    double Param2 = 0.0;
    Arguments->TryGetNumberField(TEXT("param2"), Param2);
    
    // Your tool logic here
    FString Result = FString::Printf(TEXT("Processed %s with value %f"), *Param1, Param2);
    
    return FMcpToolCallResult::CreateTextResult(Result);
}
```

A new tool changes the mode 3 count: update `Tests/smoke/expected.json` (`default`), the tables in this file and
the list in `Context/mcp-server/AGENTS-mcp-server.md` in the same change.

### 3. Tools Requiring Game Thread Execution

If your tool needs to access Unreal Editor APIs, it must run on the Game Thread:

```cpp
class FN2CMcpEditorTool : public FN2CMcpToolBase
{
public:
    virtual bool RequiresGameThread() const override { return true; }
    
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
    {
        // Use the base class helper for Game Thread execution
        return ExecuteOnGameThread([this, Arguments]() -> FMcpToolCallResult
        {
            // Your editor API calls here
            // This lambda will run on the Game Thread
            return FMcpToolCallResult::CreateTextResult(TEXT("Result"));
        });
    }
};
```

### 4. Build and Test

1. Build the plugin (`.\build.ps1 -All`, see the repo `AGENTS.md`)
2. The tool registers itself when the plugin loads; it is listed in the default mode (and in dynamic discovery once
   its category is enabled)
3. Test with an MCP client or with curl, after the `initialize` in "Calling the Server with curl" below:

```bash
# List available tools
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'

# Call your tool
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "your-tool-name",
      "arguments": {
        "param1": "test",
        "param2": 42
      }
    },
    "id": 2
  }'
```

## Helper Functions

The `FN2CMcpToolBase` class provides several helper functions:

### `BuildInputSchema`
Creates a JSON schema for tool parameters:
```cpp
TMap<FString, FString> Properties;
Properties.Add(TEXT("message"), TEXT("string"));
Properties.Add(TEXT("count"), TEXT("number"));

TArray<FString> Required;
Required.Add(TEXT("message"));

Definition.InputSchema = BuildInputSchema(Properties, Required);
```

### `BuildEmptyObjectSchema`
For tools with no parameters:
```cpp
Definition.InputSchema = BuildEmptyObjectSchema();
```

### `AddReadOnlyAnnotation`
Marks a tool as read-only:
```cpp
AddReadOnlyAnnotation(Definition);
```

### `ExecuteOnGameThread`
Runs code on the Game Thread and waits for it, with a timeout. Called on the Game Thread it runs the logic
directly. From another thread it queues the logic and waits up to `TimeoutSeconds`; the logic and its result live
in shared state, not on the caller's stack. On timeout it returns an error result and abandons the work: if the
queued task has not started, it skips the logic when it finally runs. Abandoned is not cancelled: logic the Game
Thread had already started still runs to completion and its result is dropped, so keep the logic free of
references to the caller's locals (capture by value).
```cpp
return ExecuteOnGameThread([this]() -> FMcpToolCallResult
{
    // Code that must run on Game Thread
    return FMcpToolCallResult::CreateTextResult(TEXT("Done"));
}, 30.0f); // 30 second timeout
```

## Calling the Server with curl

The server speaks MCP Streamable HTTP on one endpoint, `http://localhost:27000/mcp` (Server Port, default 27000; when
that port is taken the server uses the next free one in Port Fallback Range and writes the port it took to
`<Project>/Saved/NodeToCode/mcp-server.json`). Every POST needs `Content-Type: application/json` (else 415), and every
POST after `initialize` needs the `Mcp-Session-Id` that `initialize` returned in its response header (none: 400, an
unknown or ended session: 404). `MCP-Protocol-Version` is optional; when sent it must be a supported version. See
"Sessions and versions" in `Context/mcp-server/AGENTS-mcp-server.md`.

```bash
# Open a session: keep the Mcp-Session-Id response header
SID=$(curl -s -D - -o /dev/null -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" -H "Accept: application/json, text/event-stream" \
  -d '{"jsonrpc": "2.0", "method": "initialize", "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "curl", "version": "1.0"}}, "id": 0}' \
  | tr -d '\r' | sed -n 's/^[Mm]cp-[Ss]ession-[Ii]d: //p')
curl -X POST http://localhost:27000/mcp -H "Content-Type: application/json" -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "notifications/initialized"}'

# See all available tools and their schemas
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: $SID" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'
```

The examples in this file and in the per-folder `AGENTS.md` files under `Implementations/` assume `$SID` is set this
way. End the session when done (later requests with that id get 404):

```bash
curl -X DELETE http://localhost:27000/mcp -H "Mcp-Session-Id: $SID"
```

## Best Practices

1. **Tool Naming**: Use kebab-case for tool names (e.g., `get-blueprint-info`)
2. **Error Handling**: Always validate input parameters and return clear error messages
3. **Logging**: Use `FN2CLogger` for debugging and error reporting
4. **Thread Safety**: If your tool doesn't need Editor APIs, avoid requiring Game Thread execution
5. **Documentation**: Include clear descriptions in your tool definition
6. **Input Validation**: Check all required parameters before processing
7. **Tool Dependencies**: If tools work together (like search and add nodes), clearly document the workflow
8. **Category**: Use an existing category string, so dynamic discovery can enable the tool

## Troubleshooting

### Tool Not Appearing in tools/list
- Check the registration mode: Python script-only mode lists 7 tools (4 without Allow MCP Python Execution) and
  dynamic discovery lists only `assess-needed-tools` until a category is enabled
- `run-python`, `save-python-script` and `delete-python-script` need the per-user Allow MCP Python Execution
- Ensure the `REGISTER_MCP_TOOL` macro is in the .cpp file
- Check that the .cpp file is included in the build
- Look for registration errors in the log
- A settings change applies after an editor restart

### Tool Execution Fails
- Check the log for error messages
- Verify input parameters match the schema
- For Game Thread tools, ensure timeout is sufficient

### Threading Issues
- Tools accessing Editor APIs must override `RequiresGameThread()` to return true
- Use `ExecuteOnGameThread` helper for safe execution
- Adjust timeout if operations take longer than expected
