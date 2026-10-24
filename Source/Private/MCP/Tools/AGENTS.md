# NodeToCode MCP Tools

This directory contains the MCP (Model Context Protocol) tools framework and implementations for the NodeToCode plugin. The system uses an interface-based architecture with automatic registration, making it easy to add new tools without modifying existing code.

## Directory Structure

```
MCP/Tools/
├── Framework Files (in this directory)
│   ├── IN2CMcpTool.h           - Tool interface definition
│   ├── N2CMcpToolBase.h/.cpp   - Base class for all tools
│   ├── N2CMcpToolRegistry.h/.cpp - Auto-registration system
│   ├── N2CMcpToolManager.h/.cpp  - Tool lifecycle management
│   └── N2CMcpToolTypes.h/.cpp    - Common type definitions
│
├── Implementations/            - Tool implementations by category
│   ├── Blueprint/              - Blueprint manipulation tools
│   ├── Translation/            - Code translation tools
│   ├── FileSystem/             - File and directory operations
│   ├── ContentBrowser/         - Content browser navigation
│   ├── Python/                 - Python script management
│   └── ToolManagement/         - Dynamic tool management
│
└── README.md                   - This documentation
```

## Framework Overview

### Core Components

1. **`IN2CMcpTool`** - The interface that all MCP tools must implement
   - Defines the contract for tool definition and execution
   - Supports both synchronous and asynchronous operations

2. **`FN2CMcpToolBase`** - Base class providing common functionality
   - Implements helper methods for schema building
   - Provides Game Thread execution utilities
   - Handles common error scenarios

3. **`FN2CMcpToolRegistry`** - Static registry with automatic registration
   - Uses C++ static initialization for tool discovery
   - No manual registration required
   - Thread-safe tool lookup

4. **`FN2CMcpToolManager`** - Runtime tool management
   - Handles tool execution lifecycle
   - Manages Game Thread dispatch
   - Supports progress tracking for long-running tools

### Registration System

The `REGISTER_MCP_TOOL` macro enables automatic tool registration:

```cpp
// In your tool's .cpp file
REGISTER_MCP_TOOL(FN2CMcpYourToolName)
```

This macro:
- Creates a static initializer that runs before main()
- Registers the tool class with the global registry
- Ensures the tool is available when the MCP server starts

### Tool Categories

Tools are organized into logical categories:
- **[ToolManagement/](Implementations/ToolManagement/)** - Tools for managing the available toolset
- **[Blueprint/](Implementations/Blueprint/)** - Comprehensive Blueprint manipulation
  - Analysis - Inspection and metadata extraction
  - Functions - Function creation and management
  - Variables - Variable creation and type discovery
  - Graph - Node and connection manipulation
  - Organization - Tagging and tracking system
  - Discovery - Searching for nodes, functions, and variable types
  - Compilation - Save and compile Blueprints
  - Classes - Blueprint class search and creation
  - Components - Actor component management

- **[Translation/](Implementations/Translation/)** - Code generation tools
  - LLM provider management
  - Language target selection
  - Async translation execution

- **[FileSystem/](Implementations/FileSystem/)** - Secure file operations
  - Directory browsing with path jail
  - Text file reading with size limits
  - Project-relative path enforcement

- **[ContentBrowser/](Implementations/ContentBrowser/)** - Asset management
  - Path navigation and folder creation
  - Asset discovery and filtering
  - Blueprint opening and focusing

- **[Python/](Implementations/Python/)** - Python script management
  - Script execution in UE environment
  - Script library CRUD operations
  - Token-efficient function discovery via AST

Each category has its own README with detailed documentation.

## Long-Running Tools and Async Execution

NodeToCode runs long-running tools (LLM translation) as async tasks on the game thread and answers the client's
`tools/call` on that same POST, with an ordinary CallToolResult, once the task finishes.

### How Async Tools Work

1. **Tool Declaration**: Mark a tool as long-running by setting `bIsLongRunning = true` in its definition
2. **Same-POST response**: `FN2CMcpHttpServerManager::HandleMcpRequest` starts the task and does not answer at once.
   On UE 5.8, when the request's `Accept` lists `text/event-stream` and `params._meta.progressToken` is set, the
   response is a `text/event-stream`: `notifications/progress` events (the token echoed with its JSON type, a number
   stays a number), then the JSON-RPC response with the CallToolResult, then the connection closes. Otherwise (no
   token, JSON-only Accept, or UE 5.6/5.7) the request is held and answered with one `application/json` response
   when the task finishes; no progress is sent. The 5.6/5.7 choice follows the open decision card 0085 and lives in
   `FN2CMcpHttpServerManager::CanStreamToolProgress()`. A long-running tool inside a JSON-RPC batch is refused.
3. **Progress Reporting**: Tools report progress from the game thread; the translation reports real stages
   (collecting the Blueprint, request sent, waiting with elapsed seconds, parsing) at least once per second, and each
   value sent is larger than the last, as MCP requires
4. **Cancellation**: Clients can cancel running tasks using the `nodetocode/cancelTask` method with the call's
   progress token; the call's own response then carries the cancelled error result

### Creating a Long-Running Tool

To create a long-running tool, you need to:

1. Mark the tool as long-running in its definition
2. Create an async task class that implements `IN2CToolAsyncTask`
3. Register the async task in the tool manager

Here's an example:

```cpp
// In your tool's GetDefinition() method
FMcpToolDefinition FN2CMcpLongRunningTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("long-running-tool"),
        TEXT("A tool that takes time to complete")
    );
    
    // Mark as long-running
    Definition.bIsLongRunning = true;
    
    // Define input schema as usual
    Definition.InputSchema = BuildInputSchema(Properties, Required);
    
    return Definition;
}
```

### Creating an Async Task

Create a task class that inherits from `FN2CToolAsyncTaskBase`. The task manager calls `Execute()` once, on the
game thread, and there is no worker thread: `Execute()` must start the work (for example an async HTTP request) and
return at once, never block or sleep. Report progress from an `FTSTicker` and the result from the work's completion
callback, both on the game thread. Keep anything the callback touches in a `TSharedPtr` captured by value (plus a weak
pointer to the task), and override `RequestCancel()` to cancel the in-flight work so no callback arrives after the
task has reported. `FN2CTranslateBlueprintAsyncTask` is the reference implementation: it keeps the
`FN2CLLMRequestHandle` that `ProcessN2CJsonWithOverrides` returns and calls `Cancel()` on it on cancel and on
timeout.

```cpp
// N2CMyAsyncTask.h
#pragma once

#include "MCP/Async/N2CToolAsyncTaskBase.h"
#include "LLM/N2CLLMRequestHandle.h"

class FN2CMyAsyncTask : public FN2CToolAsyncTaskBase // the base is already TSharedFromThis: use SharedThis(this)
{
public:
    FN2CMyAsyncTask(const FGuid& TaskId, const FString& ProgressToken,
        const TSharedPtr<FJsonObject>& Arguments)
        : FN2CToolAsyncTaskBase(TaskId, ProgressToken, TEXT("long-running-tool"), Arguments)
    {
    }

    virtual void Execute() override;
    virtual void RequestCancel() override;

private:
    FN2CLLMRequestHandlePtr Request;
};

// N2CMyAsyncTask.cpp
void FN2CMyAsyncTask::Execute()
{
    check(IsInGameThread());
    if (CheckCancellationAndReport())
    {
        return;
    }
    ReportProgress(0.1f, TEXT("Sending request..."));

    TWeakPtr<FN2CMyAsyncTask, ESPMode::ThreadSafe> WeakTask = SharedThis(this);
    Request = UN2CLLMModule::Get()->ProcessN2CJson(Json, FOnLLMResponseReceived::CreateLambda(
        [WeakTask](const FString& Response)
        {
            // Runs on the game thread, at most once, and never after Request->Cancel() succeeded
            if (TSharedPtr<FN2CMyAsyncTask, ESPMode::ThreadSafe> Task = WeakTask.Pin())
            {
                Task->ReportComplete(FMcpToolCallResult::CreateTextResult(Response));
            }
        }));
    // Returns now; add an FTSTicker here for progress and a timeout
}

void FN2CMyAsyncTask::RequestCancel()
{
    FN2CToolAsyncTaskBase::RequestCancel();
    if (Request.IsValid() && Request->Cancel())
    {
        ReportComplete(FMcpToolCallResult::CreateErrorResult(TEXT("Task was cancelled")));
    }
}
```

### Registering the Async Task

Update `FN2CToolAsyncTaskManager::CreateAsyncTask` to create your task:

```cpp
if (ToolName == TEXT("long-running-tool"))
{
    return MakeShared<FN2CMyAsyncTask>(TaskId, ProgressToken, Arguments);
}
```

### Client Usage with Progress

When calling a long-running tool, clients should provide a progress token:

```bash
# Call with progress token
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "tools/call",
    "params": {
      "name": "long-running-tool",
      "arguments": {...},
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
# data: {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"Processing complete!"}]}}
#
# Without it (or on UE 5.6/5.7) the call waits and returns one JSON response:
# {"jsonrpc":"2.0","id":1,"result":{"content":[{"type":"text","text":"Processing complete!"}]}}
```

### Cancelling a Task

To cancel a running task:

```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "nodetocode/cancelTask",
    "params": {
      "progressToken": "unique-progress-token-123"
    },
    "id": 2
  }'

# Response:
# {"jsonrpc":"2.0","id":2,"result":{"status":"cancellation_initiated"}}
```

## Dynamic Tool Management

The NodeToCode MCP server can be configured to use dynamic tool management to prevent overwhelming LLM clients with a large number of tools. This behavior is controlled by the "Enable Dynamic Tool Discovery" setting in the plugin settings.

### How It Works

1. **Initial State**: 
   - **Dynamic Discovery Enabled**: Only the `assess-needed-tools` tool is registered when the server starts.
   - **Dynamic Discovery Disabled (Default)**: All tools except `assess-needed-tools` are registered when the server starts.

2. **Tool Assessment**: The LLM client calls `assess-needed-tools` with a list of required tool categories:
   ```json
   {
     "categories": ["Blueprint Discovery", "Blueprint Graph Editing"]
   }
   ```

3. **Dynamic Registration**: The server activates only the tools in the requested categories.

4. **Notification**: The server sends a `notifications/tools/list_changed` notification to inform the client that the tool list has changed.

5. **Client Refresh**: The client should call `tools/list` again to get the updated tool set.

### Configuration

To enable dynamic tool discovery:
1. Open Project Settings → Plugins → Node to Code
2. Under "MCP Server", check "Enable Dynamic Tool Discovery"
3. Restart the editor for the change to take effect

**Note**: Dynamic tool discovery is only supported by MCP clients that implement the discovery protocol (e.g., VSCode, Cline). Other clients may not work properly with this feature enabled.

### Available Tool Categories

When using dynamic tool discovery with `assess-needed-tools`, these categories are available:

- **Tool Management**: Tools for managing the available toolset
- **Blueprint Discovery**: Tools for searching and listing Blueprints, functions, variables, and nodes
- **Blueprint Graph Editing**: Tools for adding, connecting, and deleting nodes in a Blueprint graph
- **Blueprint Function Management**: Tools for creating, deleting, and opening Blueprint functions
- **Blueprint Variable Management**: Tools for creating member and local variables in Blueprints
- **Blueprint Organization**: Tools for applying and managing tags on Blueprint graphs
- **Blueprint Compilation**: Tools for saving and compiling Blueprints
- **Blueprint Class Management**: Tools for searching and creating Blueprint classes
- **Blueprint Component Management**: Tools for managing components in Actor Blueprints
- **Content Browser**: Tools for interacting with the Unreal Engine Content Browser
- **File System**: Tools for reading files and directories from the project's file system
- **Translation**: Tools for translating Blueprints to code and managing LLM providers
- **Python Scripting**: Tools for executing and managing Python scripts

### Example Workflow

When dynamic tool discovery is enabled:

```bash
# 1. Initial connection - only assess-needed-tools is available
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'

# 2. Assess needed tools for a task
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
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

# 3. Server sends notification about tool list change
# Client receives: {"jsonrpc":"2.0","method":"notifications/tools/list_changed","params":{}}

# 4. Get updated tool list
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
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
        TEXT("Your Category") // Choose from available categories
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

1. Build the plugin in your IDE
2. The tool will automatically register when the plugin loads
3. Test using curl or an MCP client:

```bash
# List available tools
curl -X POST http://localhost:10000/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'

# Call your tool
curl -X POST http://localhost:10000/mcp \
  -H "Content-Type: application/json" \
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

## Tool Discovery

To see all available tools and their schemas:

```bash
curl -X POST http://localhost:27000/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc": "2.0", "method": "tools/list", "id": 1}'
```

Each tool category has comprehensive documentation in its respective README file.

## Best Practices

1. **Tool Naming**: Use kebab-case for tool names (e.g., `get-blueprint-info`)
2. **Error Handling**: Always validate input parameters and return clear error messages
3. **Logging**: Use `FN2CLogger` for debugging and error reporting
4. **Thread Safety**: If your tool doesn't need Editor APIs, avoid requiring Game Thread execution
5. **Documentation**: Include clear descriptions in your tool definition
6. **Input Validation**: Check all required parameters before processing
7. **Tool Dependencies**: If tools work together (like search and add nodes), clearly document the workflow

## Troubleshooting

### Tool Not Appearing in tools/list
- Ensure the `REGISTER_MCP_TOOL` macro is in the .cpp file
- Check that the .cpp file is included in the build
- Look for registration errors in the log

### Tool Execution Fails
- Check the log for error messages
- Verify input parameters match the schema
- For Game Thread tools, ensure timeout is sufficient

### Threading Issues
- Tools accessing Editor APIs must override `RequiresGameThread()` to return true
- Use `ExecuteOnGameThread` helper for safe execution
- Adjust timeout if operations take longer than expected
