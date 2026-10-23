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
  - DeepSeek_R1 **(Recommended)**
  - DeepSeek_V3

### [Ollama (Local Processing)](LLM-Provider-Documentation-&-Resources#ollama-local)
- **Endpoint**: Default http://localhost:11434/
- **No API Key Required**
- **Advanced Settings**:
  - Model name (e.g., codellama:code)
  - Temperature (0.0 - 2.0)
  - Context Window size (see [Translation Depth](Translation-Depth) for guidance)
  - Max Output Tokens
  - Keep Alive Duration
  - Top P
  - Top K
  - Min P
  - Repeat Penalty
  - Mirostat Mode
  - Mirostat Eta
  - Mirostat Tau
  - Random Seed

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
