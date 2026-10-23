### Plugin Settings → Provider Configuration
***

## Provider Selection & Setup

Choose and configure your [preferred LLM provider](Choosing-an-LLM-Provider) through the [Plugin Settings](Initial-Configuration#accessing-plugin-settings):

### [Anthropic](LLM-Provider-Documentation-&-Resources#anthropic-claude)
- **API Key**: Enter your Claude API key
- **Model**:
  - **Claude Sonnet 5.5** (default, recommended)
  - **Claude Opus 5.5** (most capable)
  - **Claude Haiku 5.5** (lowest cost)
  - **Claude Opus 4.5**
  - **Claude Sonnet 4.5 (retires 2026-11-30)**

### [OpenAI](LLM-Provider-Documentation-&-Resources#openai)
- **API Key**: Enter your OpenAI API key
- **Model**:
  - **GPT-6.1 Sol** (default, recommended)
  - **GPT-6 Astra** (most capable)
  - **GPT-6 Luna** (lowest cost)
  - **GPT-5.2**, **GPT-5.2 Pro**
  - **GPT-5**, **GPT-5 Mini**, **GPT-5 Nano** and **o3** (shut down 2026-12-11)
  - **GPT-4.1**, **GPT-4o** and **GPT-4o Mini** (shut down 2027-01-14)

### [Google's Gemini](LLM-Provider-Documentation-&-Resources#gemini)
- **API Key**: Enter your Gemini API key
- **Model**:
  - **Gemini 3.8 Flash** (default, recommended)
  - **Gemini 4 Argon** (most capable)
  - **Gemini 3.5 Flash-Lite** (lowest cost)
  - **Gemini 3 Flash Preview**

A model whose name ends in a date still works until that date, then the provider turns it off. Switch to one of
the recommended models before then.

### Retired models

Models the provider has shut down are no longer offered in the **Model** list. If your project had one selected
(for example Claude 3.7 Sonnet, o4-mini or Gemini 2.5 Flash Preview), Node to Code uses that provider's default
model instead when the editor starts and writes a warning to the Output Log. Your project's config file is not
changed until you choose a **Model** in **Project Settings → Plugins → Node to Code**, so the warning repeats on
each start until you do.

### [DeepSeek](LLM-Provider-Documentation-&-Resources#deepseek)
- **API Key**: Enter your DeepSeek API key
- **Model**:
  - **DeepSeek Flash, thinking (high effort)** (default, recommended): `deepseek-flash` with thinking on
  - **DeepSeek Flash, no thinking (JSON output)**: `deepseek-flash` with thinking off and JSON output, faster and cheaper
  - **DeepSeek V4 Pro, thinking (high effort)** (most capable): `deepseek-v4-pro` with thinking on

DeepSeek discontinued `deepseek-reasoner` and `deepseek-chat` on 2026-07-24. A project that saved DeepSeek R1 or
DeepSeek V3 keeps working without changes: R1 now sends `deepseek-flash` with thinking on, and V3 sends
`deepseek-flash` with thinking off. A model chosen per request through the MCP `translate-focused-blueprint` tool uses
that model's own thinking mode (for `deepseek-flash` that is thinking on, unless it is also the model selected
here).

### [Ollama (Local Processing)](LLM-Provider-Documentation-&-Resources#ollama-local)
- **Endpoint**: Default http://localhost:11434/
- **No API Key Required**
- **Advanced Settings**:
  - Model name (e.g., codellama:code)
  - Use System Prompts (on by default; turn it off only for a model whose template drops system messages)
  - Think: **Model Default** sends nothing, **On** and **Off** switch thinking on or off, and **Level** sends a
    level name for models that take one (for example `low`, `medium` or `high` for gpt-oss; `ollama show <model>`
    lists them). This replaces typing `/no_think` into Prepended Model Command.
  - Context Window (0 = the server's context length). The system prompt alone is about 4-5K tokens, so 32K-64K is
    recommended (see [Translation Depth](Translation-Depth) for guidance). Ollama cuts a prompt that is longer than
    the window without an error; NodeToCode logs a warning when its estimate of the prompt does not fit.
  - Max Tokens (0 = no limit, the server default)
  - Keep Alive Duration (seconds; 0 unloads the model after each request, -1 keeps it loaded)
  - Temperature, Top P, Top K, Min P, Repeat Penalty, Mirostat Mode, Mirostat Eta, Mirostat Tau and Random Seed:
    each is sent only when its checkbox is ticked. Unticked, the model's own defaults apply, which are usually
    tuned for that model.

Settings saved by an earlier version are updated once when the editor loads them: values equal to the old built-in
defaults (Context Window 8192, Max Tokens 8192, Temperature 0, Top P 0.5, Top K 40, Min P 0.05, Repeat Penalty 1.1,
Use System Prompts off) are cleared so the server and model defaults apply, and any other value you had set is kept
with its checkbox ticked.

## Subscription sign-in removed in 2.0

Every cloud provider (Anthropic, OpenAI, Gemini, DeepSeek) needs its **API Key**; Ollama and LM Studio need
none. Sign-in with a Claude Pro/Max subscription or a Google account was removed; a project that had it selected
now uses the API key. To use a Claude, ChatGPT or Google AI subscription, connect Claude Code, Antigravity CLI or
Codex CLI to the NodeToCode MCP server: see [Using a Claude or Gemini Subscription](Using-a-Claude-or-Gemini-Subscription),
which also covers the old setting and saved tokens.

## Sampling Settings for Cloud Models

For cloud providers, NodeToCode sends a temperature only to models that accept one, and otherwise leaves the provider's default in place. Current Claude models (Opus 4.7 and later), OpenAI reasoning models (the o-series and GPT-5 family) and Gemini 3 models get no temperature, top P or top K, because they reject those settings or work worse with them. A model name NodeToCode does not list, such as a newer ID passed by an MCP client, is treated the same way. Ollama uses the **Advanced Settings** above, and LM Studio uses the sampling settings in the LM Studio app.

## Best Practices

1. **API Key Security**
   - Never commit API keys to version control
   - Regularly rotate keys for security
   - Configure through [Plugin Settings](Initial-Configuration#accessing-plugin-settings)

2. **Model Selection**
   - Start with recommended models for your use case
   - Consider [cost vs capability](Choosing-an-LLM-Provider) tradeoffs
   - Monitor token usage and adjust as needed

3. **Local Processing**
   - Ensure sufficient system resources for Ollama
   - Monitor GPU/CPU usage
   - Adjust context window based on translation needs

## More Provider Resources

**Not sure which model will work best for your needs?**

- Check out [Choosing-an-LLM-Provider](Choosing-an-LLM-Provider)

**Want to understand more specific details about the LLM service providers?**

- Check out [LLM Provider Documentation & Resources](LLM-Provider-Documentation-&-Resources) for a quick reference to documentation and references from each supported provider.
