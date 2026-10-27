### Plugin Settings → Logging Configuration
***

## Severity Levels

Set minimum log level for plugin operations:

- **Debug**: Most verbose, all details
- **Info**: Standard operations
- **Warning**: Issues that need attention
- **Error**: Serious problems
- **Fatal**: Critical failures

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_Logging.png" alt="Node to Code logging levels" width="500">
    </p>

## What the Log Never Holds

Even at **Debug**, the log does not show the Blueprint you send, the prompts, or the code the model writes back. It
shows the provider, the model, the HTTP status and the size of each request and response instead, for example
`HTTP request sent to Anthropic (claude-sonnet-5-5), 18234 characters` and `LLM request body: 18234 characters (not
logged; n2c.LogLLMPayloads 1 logs it)`. Your API key never appears in the log, in an error message or in a saved
translation, and it is never part of a request URL. That keeps a log safe to attach to a bug report.

To see the full request and response text while you investigate a problem yourself:

1. Set **Min Severity** to **Debug**.
2. In the editor's console (the **Cmd** box at the bottom of the Output Log), enter `n2c.LogLLMPayloads 1`. The
   log shows a warning that payload logging is on.
3. Translate again. The log now has `LLM Request Payload:`, the prompts and the model's answer.
4. Enter `n2c.LogLLMPayloads 0` when you are done.

The setting lasts until you turn it off or restart the editor; it is never saved. Turn it off before you share a
log: the payloads contain your Blueprint logic and the generated code.

## MCP Requests in the Log

At **Info** and below, every request an MCP client sends gets one line when it is answered, for example
`MCP request: method=tools/call tool=translate-focused-blueprint status=200 isError=false duration_ms=8412.6`. It
names the method, the tool for `tools/call` (`call_tool:<tool>` when `call_tool` runs another tool), the HTTP status,
whether the answer was an error (a refused request, a JSON-RPC error, or a tool result with `isError`) and the time
from receiving the request to answering it. A long-running call is logged when it finishes. When the client sends a
W3C `traceparent` in the request's `params._meta`, the line ends with it, so you can match the line to the client's
own trace; it is only logged. Arguments and results are never in the line.

## Graph Overlays in the Log

Opening a Blueprint or switching between its graphs logs no warnings. Each graph tab gets its NodeToCode overlay (the
tags, Copy JSON and Translate bar at the bottom left of the graph) without a line above **Debug**. At **Debug** you see
one line when an overlay is built, and one when a graph cannot get one yet, for example while a Widget Blueprint is in
the Designer. A check every 2 seconds adds a missing overlay to a graph you reached inside an open tab, such as a
collapsed graph or the Back button. It retries a graph that cannot get one with a growing delay and stops after a
few tries, with one **Info** line; bringing that graph's tab to the front tries again. Enter `n2c.OverlayPoll 0` in
the console to turn the check off for the session.

## Debug Logging and Speed

Debug lines are built only while **Min Severity** is **Debug**. At **Info** or higher, capturing a large Blueprint
spends no time on the per-node and per-pin detail Debug would show, so leave Debug on only while you investigate.

## Log Categories

Different aspects of plugin operation have specific logging:

- Node collection
- Translation process
- LLM communication
- Code generation
- Editor integration