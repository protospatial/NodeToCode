What Node to Code sends, to whom, what each provider says it keeps, and what stays on your machine. Use it to decide
which provider may see your Blueprints, for example before you translate game logic under an NDA.

**Node to Code sends nothing to Protospatial.** It has no telemetry, analytics, crash upload or update check. The only
data that leaves the editor is what you send to the LLM provider you chose when you translate, and the results of the
MCP tools your own agent calls.

## What a translation sends

Each translation sends one request to the provider selected in **Project Settings → Plugins → Node to Code**:
- the system prompt for your target language,
- the selected graphs as Node to Code JSON: node and pin names, comments, variable names, default values and string
  literals,
- the full text of every file in **Reference Source Files**.

The request carries only that provider's key, in a header and never in the URL. It goes straight from the editor to
the provider; there is no relay. A batch sends one request per graph. When an agent translates through the MCP
server (`translate-focused-blueprint`, `translate-batch`), the same request goes to the same provider. Anything else
an agent reads over MCP goes to the agent and from there to *its* model, under the agent's own terms
([Security Model](Security-Model)).

## Providers

Policies as read on 2026-11-02 from each provider's own pages. They change: check the linked page before you rely on
a row, and read your own contract if you have one.

| Provider | Endpoint (default) | Key sent as | What the provider says it keeps | Used for training | Source (read 2026-11-02) |
|---|---|---|---|---|---|
| Anthropic | `https://api.anthropic.com/v1/messages` | `x-api-key` header | Inputs and outputs deleted within 30 days. Longer when flagged by trust and safety (up to 2 years), for usage-policy enforcement, or by law. Zero data retention by agreement | No, by default (except feedback you submit) | [Retention](https://privacy.claude.com/en/articles/7996866-how-long-do-you-store-my-organization-s-data) (page dated 2026-07-01), [training](https://privacy.claude.com/en/articles/7996868-is-my-data-used-for-model-training) (page dated 2026-08-18); [covered models](https://privacy.claude.com/en/articles/15425996-data-retention-practices-for-covered-models) need 30-day retention in zero-retention workspaces |
| OpenAI | `https://api.openai.com/v1/chat/completions` | `Authorization: Bearer` | Abuse-monitoring logs up to 30 days; no application state for Chat Completions (Node to Code does not ask OpenAI to store responses). Zero data retention only with OpenAI's approval | No (API data since 2023-03-01, unless you opt in) | [Your data](https://developers.openai.com/api/docs/guides/your-data) (no page date) |
| Google Gemini | `https://generativelanguage.googleapis.com/v1beta/models/` | `x-goog-api-key` header | Paid tier: prompts and responses logged for 55 days for abuse monitoring. Free tier: no retention period stated; human reviewers may read prompts and responses after they are disconnected from your account | Paid tier: no. Free tier: yes, used to improve Google products and models (in the EEA, UK and Switzerland the paid-tier terms apply to all use) | [Gemini API terms](https://aistudio.google.com/docs/terms) (effective 2026-03-23), [abuse monitoring](https://aistudio.google.com/docs/usage-policies) (no page date) |
| DeepSeek | `https://api.deepseek.com/chat/completions` | `Authorization: Bearer` | Account and input data kept as long as the account exists; data stored and processed in the People's Republic of China. The Open Platform terms set no retention period for API data | The privacy policy lists training as a use of collected data, de-identified, with a right to opt out; it does not say whether API inputs are included | [Privacy policy](https://cdn.deepseek.com/policies/en-US/deepseek-privacy-policy.html) (updated 2026-02-10), [Open Platform terms](https://cdn.deepseek.com/policies/en-US/deepseek-open-platform-terms-of-service.html) (effective 2026-04-29), [model training disclosure](https://cdn.deepseek.com/policies/en-US/model-algorithm-disclosure.html) (no page date) |
| Ollama | `http://localhost:11434/api/chat` (your **Ollama Endpoint**) | none | Nothing leaves your machine while the endpoint is local and the model is a local one. Another machine as the endpoint, or an Ollama cloud model, sends the request there | Ollama says cloud prompts are not used for training | [Ollama cloud](https://docs.ollama.com/cloud) (no page date) |
| LM Studio | `http://localhost:1234/v1/chat/completions` (your **Server Endpoint**) | `Authorization: Bearer` only if you set a key | Nothing leaves your machine while the endpoint is local | Not applicable | (local server) |
| Azure OpenAI / Microsoft Foundry, Amazon Bedrock, Google Vertex AI | not supported | | Node to Code cannot send to these endpoints yet | | |

Studios that need enterprise data terms (a data processing agreement, regional processing, zero retention) can get
them today through a direct contract with Anthropic, OpenAI or Google, or run a model locally with Ollama or LM Studio.

## What stays on your machine

| Where (in your project) | What | How to delete it |
|---|---|---|
| `Saved/NodeToCode/Translations/` (or your custom output directory) | Every translation: the Blueprint JSON that was sent, the provider's answer and the generated code ([Utilizing Saved Translations](Utilizing-Saved-Translations)) | Delete the run folders |
| `Saved/NodeToCode/Exports/` | Large Blueprint JSON that MCP tools hand to an agent as a file instead of inline | Delete the folder |
| `Saved/NodeToCode/BlueprintGraphState.json` | Graph tags and which graphs have translations | Delete the file (the tags go with it) |
| `Saved/Logs/` | The editor log. Node to Code logs provider, model, status and sizes, never the Blueprint, prompts or code, unless you turn on `n2c.LogLLMPayloads` ([Logging Configuration](Logging-Configuration)) | Delete old logs |
| Windows Credential Manager, `NodeToCode/Anthropic` and so on | Your API keys | the provider's **Remove Saved ... Key** button in the settings (for example **Remove Saved OpenAI Key**), or `cmdkey /delete:NodeToCode/OpenAI` ([Initial Configuration](Initial-Configuration)) |

Nothing is uploaded from these folders. Unreal's usual source-control setup ignores `Saved/`; keep it that way if
your translations are confidential.
