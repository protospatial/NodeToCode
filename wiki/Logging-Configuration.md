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

## Log Categories

Different aspects of plugin operation have specific logging:

- Node collection
- Translation process
- LLM communication
- Code generation
- Editor integration