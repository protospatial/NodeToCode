// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LLM/Providers/N2CAnthropicResponseParser.h"
#include "LLM/Providers/N2CDeepSeekResponseParser.h"
#include "LLM/Providers/N2CGeminiResponseParser.h"
#include "LLM/Providers/N2CLMStudioResponseParser.h"
#include "LLM/Providers/N2COllamaResponseParser.h"
#include "LLM/Providers/N2COpenAIResponseParser.h"
#include "Models/N2CTranslation.h"
#include "UObject/Package.h"

/**
 * Each provider parser reads the stop or finish reason before the content. Canned bodies follow each provider's
 * documented response shape (Anthropic stop_reason and stop_details, OpenAI Chat Completions finish_reason and
 * message.refusal, the Responses API status and incomplete_details, Gemini finishReason and
 * promptFeedback.blockReason, DeepSeek finish_reason, Ollama done_reason, LM Studio's OpenAI-compatible body):
 * an answer cut at the output limit is Truncated with a message naming the provider's setting, a refusal is
 * Refused and a filter stop is Blocked, all with the provider's reason. A valid answer whose generated code holds an
 * unbalanced brace inside a string parses, and reasoning text is never used as the answer. Card 0185: the answer is
 * found in a ``` fence with any tag or none and with prose around it (Fences), optional and string-valued fields are
 * read without LogJson warnings (Fields), and cache read and write and thinking counts are recorded (Usage).
 */
BEGIN_DEFINE_SPEC(FN2CResponseParserSpec, "NodeToCode.LLM.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	/** A JSON string literal (quotes included) holding Text */
	static FString Quote(const FString& Text)
	{
		FString Out = TEXT("\"");
		for (const TCHAR Char : Text)
		{
			switch (Char)
			{
			case TEXT('"'): Out += TEXT("\\\""); break;
			case TEXT('\\'): Out += TEXT("\\\\"); break;
			case TEXT('\n'): Out += TEXT("\\n"); break;
			case TEXT('\r'): Out += TEXT("\\r"); break;
			case TEXT('\t'): Out += TEXT("\\t"); break;
			default: Out.AppendChar(Char); break;
			}
		}
		return Out + TEXT("\"");
	}

	/**
	 * A complete NodeToCode answer whose generated code holds braces that do not balance: an opening brace in a C++
	 * string literal, a C# "{{" and a note ending in "{". Valid JSON; the old raw brace count rejected it.
	 */
	static FString UnbalancedBraceAnswer()
	{
		return TEXT("{\"graphs\":[{\"graph_name\":\"EventGraph\",\"graph_type\":\"EventGraph\",\"graph_class\":\"BP_Braces\",")
			TEXT("\"code\":{\"graphDeclaration\":\"void BuildLabel(const FString& Name);\",")
			TEXT("\"graphImplementation\":\"FString Label = FString(TEXT(\\\"{\\\")) + Name;\\nconst char* Cs = \\\"$\\\\\\\"{{\\\\\\\"\\\";\",")
			TEXT("\"implementationNotes\":\"Builds a label that opens with {\"}}]}");
	}

	/** The first characters of a translation, as an answer cut off at the output limit looks */
	static FString CutAnswer()
	{
		return UnbalancedBraceAnswer().Left(70);
	}

	// ---- Anthropic Messages API (platform.claude.com/docs: handling-stop-reasons, refusals-and-fallback) ----
	static FString AnthropicBody(const FString& StopReason, const FString& ContentBlocks, const FString& StopDetails = TEXT("null"))
	{
		return FString::Printf(TEXT("{\"id\":\"msg_01N2CParseSpec\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-haiku-5-5\",")
			TEXT("\"content\":[%s],\"stop_reason\":\"%s\",\"stop_sequence\":null,\"stop_details\":%s,")
			TEXT("\"usage\":{\"input_tokens\":5120,\"output_tokens\":64}}"), *ContentBlocks, *StopReason, *StopDetails);
	}
	static FString AnthropicThinking()
	{
		return TEXT("{\"type\":\"thinking\",\"thinking\":\"\",\"signature\":\"EqQBCkYIBxgCKkDn2cParseSpecSignature\"}");
	}
	static FString AnthropicText(const FString& Text)
	{
		return FString::Printf(TEXT("{\"type\":\"text\",\"text\":%s}"), *Quote(Text));
	}

	// ---- OpenAI-compatible Chat Completions (OpenAI, DeepSeek, LM Studio) ----
	static FString ChatBody(const FString& Model, const FString& FinishReason, const FString& MessageFields, const FString& Extra = FString())
	{
		return FString::Printf(TEXT("{\"id\":\"chatcmpl-n2c\",\"object\":\"chat.completion\",\"created\":1761264000,\"model\":\"%s\",")
			TEXT("\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\",%s},\"logprobs\":null,\"finish_reason\":%s}]%s}"),
			*Model, *MessageFields, *FinishReason, *Extra);
	}
	static FString ChatUsage()
	{
		return TEXT(",\"usage\":{\"prompt_tokens\":5120,\"completion_tokens\":64,\"total_tokens\":5184}");
	}

	// ---- OpenAI Responses API ----
	static FString ResponsesBody(const FString& Status, const FString& IncompleteDetails, const FString& Output)
	{
		return FString::Printf(TEXT("{\"id\":\"resp_n2c\",\"object\":\"response\",\"created_at\":1761264000,\"status\":\"%s\",")
			TEXT("\"error\":null,\"incomplete_details\":%s,\"model\":\"gpt-6.1-sol\",\"output\":[%s],")
			TEXT("\"usage\":{\"input_tokens\":5120,\"output_tokens\":64,\"total_tokens\":5184}}"), *Status, *IncompleteDetails, *Output);
	}

	// ---- Gemini generateContent (ai.google.dev/api/generate-content) ----
	static FString GeminiBody(const FString& Parts, const FString& FinishReason)
	{
		return FString::Printf(TEXT("{\"candidates\":[{\"content\":{\"parts\":[%s],\"role\":\"model\"},\"finishReason\":\"%s\",\"index\":0}],")
			TEXT("\"usageMetadata\":{\"promptTokenCount\":5120,\"candidatesTokenCount\":64,\"totalTokenCount\":5184},")
			TEXT("\"modelVersion\":\"gemini-3.5-flash-lite\"}"), *Parts, *FinishReason);
	}

	// ---- Ollama /api/chat (non-streaming) ----
	static FString OllamaBody(const FString& Content, const FString& DoneReason, const FString& Thinking = FString())
	{
		const FString ThinkingField = Thinking.IsEmpty() ? FString() : FString::Printf(TEXT(",\"thinking\":%s"), *Quote(Thinking));
		return FString::Printf(TEXT("{\"model\":\"qwen3:32b\",\"created_at\":\"2026-10-24T00:00:00Z\",")
			TEXT("\"message\":{\"role\":\"assistant\",\"content\":%s%s},\"done_reason\":\"%s\",\"done\":true,")
			TEXT("\"total_duration\":1000,\"prompt_eval_count\":5120,\"eval_count\":64}"), *Quote(Content), *ThinkingField, *DoneReason);
	}

	struct FCase
	{
		FString Name;
		UN2CResponseParserBase* Parser = nullptr;
		FString Body;
		EN2CTranslationErrorKind Kind = EN2CTranslationErrorKind::None;
		/** Expected provider_error_type */
		FString Reason;
		/** Texts the message must contain */
		TArray<FString> MessageHas;
	};

	template <typename T>
	static UN2CResponseParserBase* Make()
	{
		return NewObject<T>(GetTransientPackage());
	}

	void ExpectFailure(const FCase& Case)
	{
		FN2CTranslationResponse Response;
		TestFalse(Case.Name + TEXT(": parse reports failure"), Case.Parser->ParseLLMResponse(Case.Body, Response));
		TestEqual(Case.Name + TEXT(": kind"), FN2CTranslationError::KindToString(Response.Error.Kind), FN2CTranslationError::KindToString(Case.Kind));
		if (!Case.Reason.IsEmpty())
		{
			TestEqual(Case.Name + TEXT(": provider reason"), Response.Error.ProviderErrorType, Case.Reason);
		}
		for (const FString& Text : Case.MessageHas)
		{
			TestTrue(FString::Printf(TEXT("%s: message has '%s' (was: %s)"), *Case.Name, *Text, *Response.Error.Message),
				Response.Error.Message.Contains(Text));
		}
		TestEqual(Case.Name + TEXT(": retryable"), Response.Error.bRetryable, FN2CTranslationError::IsRetryableKind(Case.Kind));
		TestEqual(Case.Name + TEXT(": no graphs"), Response.Graphs.Num(), 0);
		TestTrue(Case.Name + TEXT(": the error JSON names the kind"),
			Response.Error.ToJsonString().Contains(FString::Printf(TEXT("\"error_kind\":\"%s\""), *FN2CTranslationError::KindToString(Case.Kind))));
	}

	void ExpectParsed(const FString& Name, UN2CResponseParserBase* Parser, const FString& Body)
	{
		FN2CTranslationResponse Response;
		const bool bParsed = Parser->ParseLLMResponse(Body, Response);
		TestTrue(Name + FString::Printf(TEXT(": parses (error: %s)"), *Response.Error.Message), bParsed);
		TestFalse(Name + TEXT(": no error"), Response.Error.IsError());
		if (TestEqual(Name + TEXT(": one graph"), Response.Graphs.Num(), 1))
		{
			TestTrue(Name + TEXT(": the brace in the string survived"),
				Response.Graphs[0].Code.GraphImplementation.Contains(TEXT("FString(TEXT(\"{\"))")));
			TestTrue(Name + TEXT(": the C# braces survived"), Response.Graphs[0].Code.GraphImplementation.Contains(TEXT("{{")));
			TestEqual(Name + TEXT(": graph class"), Response.Graphs[0].GraphClass, FString(TEXT("BP_Braces")));
		}
		TestEqual(Name + TEXT(": output tokens read"), Response.Usage.OutputTokens, 64);
	}

	/** One provider's way of carrying answer text: its parser and a successful body holding Text */
	struct FProviderShape
	{
		FString Name;
		TFunction<UN2CResponseParserBase*()> MakeParser;
		TFunction<FString(const FString& Text)> Body;
	};

	/** Every provider and API shape, each with a normal stop */
	static TArray<FProviderShape> ProviderShapes()
	{
		return {
			{ TEXT("Anthropic"), [] { return Make<UN2CAnthropicResponseParser>(); },
				[](const FString& Text) { return AnthropicBody(TEXT("end_turn"), AnthropicThinking() + TEXT(",") + AnthropicText(Text)); } },
			{ TEXT("OpenAI chat"), [] { return Make<UN2COpenAIResponseParser>(); },
				[](const FString& Text) { return ChatBody(TEXT("gpt-6.1-sol"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":%s,\"refusal\":null"), *Quote(Text)), ChatUsage()); } },
			{ TEXT("OpenAI Responses"), [] { return Make<UN2COpenAIResponseParser>(); },
				[](const FString& Text) { return ResponsesBody(TEXT("completed"), TEXT("null"), FString::Printf(
					TEXT("{\"id\":\"msg_n2c\",\"type\":\"message\",\"status\":\"completed\",\"role\":\"assistant\",")
					TEXT("\"content\":[{\"type\":\"output_text\",\"text\":%s,\"annotations\":[]}]}"), *Quote(Text))); } },
			{ TEXT("Gemini"), [] { return Make<UN2CGeminiResponseParser>(); },
				[](const FString& Text) { return GeminiBody(FString::Printf(TEXT("{\"text\":%s}"), *Quote(Text)), TEXT("STOP")); } },
			{ TEXT("DeepSeek"), [] { return Make<UN2CDeepSeekResponseParser>(); },
				[](const FString& Text) { return ChatBody(TEXT("deepseek-flash"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":%s"), *Quote(Text)), ChatUsage()); } },
			{ TEXT("LM Studio"), [] { return Make<UN2CLMStudioResponseParser>(); },
				[](const FString& Text) { return ChatBody(TEXT("qwen3-32b"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":%s"), *Quote(Text)), ChatUsage()); } },
			{ TEXT("Ollama"), [] { return Make<UN2COllamaResponseParser>(); },
				[](const FString& Text) { return OllamaBody(Text, TEXT("stop")); } },
		};
	}

	/** A complete answer whose implementationNotes hold a Markdown ``` fence of their own */
	static FString AnswerWithFenceInNotes()
	{
		return TEXT("{\"graphs\":[{\"graph_name\":\"EventGraph\",\"graph_type\":\"EventGraph\",\"graph_class\":\"BP_Braces\",")
			TEXT("\"code\":{\"graphDeclaration\":\"\",\"graphImplementation\":\"FString(TEXT(\\\"{\\\")) {{\",")
			TEXT("\"implementationNotes\":\"Call it like this:\\n```cpp\\nBuildLabel(Name);\\n```\\nDone.\"}}]}");
	}

	/** LogJson lines said through GLog while it lives (GetField and AsObject warn there for a missing or mistyped field) */
	struct FLogJsonCapture : public FOutputDevice
	{
		TArray<FString> Lines;
		FLogJsonCapture() { GLog->AddOutputDevice(this); }
		virtual ~FLogJsonCapture() override { GLog->RemoveOutputDevice(this); }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (Category == TEXT("LogJson"))
			{
				Lines.Add(V);
			}
		}
		TArray<FString> Flushed() const
		{
			GLog->Flush();
			return Lines;
		}
	};

	void ExpectNoLogJson(const FString& What, const FLogJsonCapture& Capture)
	{
		const TArray<FString> Lines = Capture.Flushed();
		TestEqual(FString::Printf(TEXT("%s: no LogJson warning (first: %s)"), *What, Lines.Num() > 0 ? *Lines[0] : TEXT("none")), Lines.Num(), 0);
	}

END_DEFINE_SPEC(FN2CResponseParserSpec)

void FN2CResponseParserSpec::Define()
{
	Describe(TEXT("Truncated"), [this]()
	{
		It(TEXT("reports an answer cut at the output limit as Truncated in every provider, naming its setting"), [this]()
		{
			AddExpectedError(TEXT("(Truncated)"), EAutomationExpectedErrorFlags::Contains, 0, /*IsRegex=*/false);
			AddExpectedError(TEXT("Failed to extract message content"), EAutomationExpectedErrorFlags::Contains, 0, false);

			const TArray<FCase> Cases = {
				{ TEXT("Anthropic max_tokens"), Make<UN2CAnthropicResponseParser>(),
					AnthropicBody(TEXT("max_tokens"), AnthropicThinking() + TEXT(",") + AnthropicText(CutAnswer())),
					EN2CTranslationErrorKind::Truncated, TEXT("max_tokens"),
					{ TEXT("stop_reason \"max_tokens\""), TEXT("Anthropic > Max Output Tokens") } },
				// Card 0417: max_tokens reached while thinking leaves only an (empty, display omitted) thinking block
				{ TEXT("Anthropic max_tokens with only a thinking block"), Make<UN2CAnthropicResponseParser>(),
					AnthropicBody(TEXT("max_tokens"), AnthropicThinking()),
					EN2CTranslationErrorKind::Truncated, TEXT("max_tokens"), { TEXT("Anthropic > Max Output Tokens") } },
				{ TEXT("Anthropic model_context_window_exceeded"), Make<UN2CAnthropicResponseParser>(),
					AnthropicBody(TEXT("model_context_window_exceeded"), AnthropicText(CutAnswer())),
					EN2CTranslationErrorKind::Truncated, TEXT("model_context_window_exceeded"), { TEXT("context window") } },
				{ TEXT("OpenAI finish_reason length"), Make<UN2COpenAIResponseParser>(),
					ChatBody(TEXT("gpt-6.1-sol"), TEXT("\"length\""), FString::Printf(TEXT("\"content\":%s,\"refusal\":null"), *Quote(CutAnswer())), ChatUsage()),
					EN2CTranslationErrorKind::Truncated, TEXT("length"),
					{ TEXT("finish_reason \"length\""), TEXT("OpenAI > Max Output Tokens") } },
				{ TEXT("OpenAI Responses incomplete max_output_tokens"), Make<UN2COpenAIResponseParser>(),
					ResponsesBody(TEXT("incomplete"), TEXT("{\"reason\":\"max_output_tokens\"}"),
						TEXT("{\"id\":\"rs_n2c\",\"type\":\"reasoning\",\"summary\":[]}")),
					EN2CTranslationErrorKind::Truncated, TEXT("max_output_tokens"),
					{ TEXT("status \"incomplete\", reason \"max_output_tokens\""), TEXT("OpenAI > Max Output Tokens") } },
				{ TEXT("Gemini MAX_TOKENS"), Make<UN2CGeminiResponseParser>(),
					GeminiBody(FString::Printf(TEXT("{\"text\":%s}"), *Quote(CutAnswer())), TEXT("MAX_TOKENS")),
					EN2CTranslationErrorKind::Truncated, TEXT("MAX_TOKENS"),
					{ TEXT("finishReason \"MAX_TOKENS\""), TEXT("Gemini > Max Output Tokens") } },
				// Thinking used the whole budget: the candidate has no content at all
				{ TEXT("Gemini MAX_TOKENS with no content"), Make<UN2CGeminiResponseParser>(),
					TEXT("{\"candidates\":[{\"finishReason\":\"MAX_TOKENS\",\"index\":0}],\"usageMetadata\":{\"promptTokenCount\":5120,\"thoughtsTokenCount\":256,\"totalTokenCount\":5376}}"),
					EN2CTranslationErrorKind::Truncated, TEXT("MAX_TOKENS"), { TEXT("Gemini > Max Output Tokens") } },
				{ TEXT("DeepSeek finish_reason length while reasoning"), Make<UN2CDeepSeekResponseParser>(),
					ChatBody(TEXT("deepseek-flash"), TEXT("\"length\""), TEXT("\"content\":\"\",\"reasoning_content\":\"Let me map each node to C++ first...\""), ChatUsage()),
					EN2CTranslationErrorKind::Truncated, TEXT("length"), { TEXT("DeepSeek > Max Output Tokens") } },
				// LM Studio: content null next to reasoning_content and finish_reason length (PR #20's case)
				{ TEXT("LM Studio content null, finish_reason length"), Make<UN2CLMStudioResponseParser>(),
					ChatBody(TEXT("qwen3-32b"), TEXT("\"length\""), TEXT("\"content\":null,\"reasoning_content\":\"Thinking about the graph...\"")),
					EN2CTranslationErrorKind::Truncated, TEXT("length"), { TEXT("LM Studio > Max Output Tokens") } },
				{ TEXT("Ollama done_reason length"), Make<UN2COllamaResponseParser>(),
					OllamaBody(CutAnswer(), TEXT("length")),
					EN2CTranslationErrorKind::Truncated, TEXT("length"), { TEXT("done_reason \"length\""), TEXT("Max Tokens (num_predict") } },
			};
			for (const FCase& Case : Cases)
			{
				ExpectFailure(Case);
			}
		});
	});

	Describe(TEXT("RefusedAndBlocked"), [this]()
	{
		It(TEXT("reports refusals as Refused and filter stops as Blocked with the provider's reason"), [this]()
		{
			AddExpectedError(TEXT("(Refused)"), EAutomationExpectedErrorFlags::Contains, 0, false);
			AddExpectedError(TEXT("(Blocked)"), EAutomationExpectedErrorFlags::Contains, 0, false);
			AddExpectedError(TEXT("(Overloaded, retryable)"), EAutomationExpectedErrorFlags::Contains, 0, false);
			AddExpectedError(TEXT("Failed to extract message content"), EAutomationExpectedErrorFlags::Contains, 0, false);

			const TArray<FCase> Cases = {
				// The documented refusal: HTTP 200, empty content, stop_details with category and explanation
				{ TEXT("Anthropic refusal"), Make<UN2CAnthropicResponseParser>(),
					AnthropicBody(TEXT("refusal"), FString(),
						TEXT("{\"type\":\"refusal\",\"category\":\"cyber\",\"explanation\":\"This request was declined because it could enable cyber harm.\"}")),
					EN2CTranslationErrorKind::Refused, TEXT("refusal"),
					{ TEXT("category \"cyber\""), TEXT("could enable cyber harm") } },
				{ TEXT("Anthropic refusal without a category"), Make<UN2CAnthropicResponseParser>(),
					AnthropicBody(TEXT("refusal"), FString(), TEXT("{\"type\":\"refusal\",\"category\":null,\"explanation\":null}")),
					EN2CTranslationErrorKind::Refused, TEXT("refusal"), { TEXT("stop_reason \"refusal\"") } },
				{ TEXT("OpenAI message.refusal"), Make<UN2COpenAIResponseParser>(),
					ChatBody(TEXT("gpt-6.1-sol"), TEXT("\"stop\""), TEXT("\"content\":null,\"refusal\":\"I'm sorry, I can't help with that.\""), ChatUsage()),
					EN2CTranslationErrorKind::Refused, TEXT("refusal"), { TEXT("I'm sorry, I can't help with that.") } },
				{ TEXT("OpenAI finish_reason content_filter"), Make<UN2COpenAIResponseParser>(),
					ChatBody(TEXT("gpt-6.1-sol"), TEXT("\"content_filter\""), TEXT("\"content\":null,\"refusal\":null"), ChatUsage()),
					EN2CTranslationErrorKind::Blocked, TEXT("content_filter"), { TEXT("finish_reason \"content_filter\"") } },
				{ TEXT("OpenAI Responses refusal part"), Make<UN2COpenAIResponseParser>(),
					ResponsesBody(TEXT("completed"), TEXT("null"),
						TEXT("{\"id\":\"msg_n2c\",\"type\":\"message\",\"status\":\"completed\",\"role\":\"assistant\",")
						TEXT("\"content\":[{\"type\":\"refusal\",\"refusal\":\"I can't help with that request.\"}]}")),
					EN2CTranslationErrorKind::Refused, TEXT("refusal"), { TEXT("I can't help with that request.") } },
				{ TEXT("OpenAI Responses incomplete content_filter"), Make<UN2COpenAIResponseParser>(),
					ResponsesBody(TEXT("incomplete"), TEXT("{\"reason\":\"content_filter\"}"), FString()),
					EN2CTranslationErrorKind::Blocked, TEXT("content_filter"), { TEXT("content_filter") } },
				// A blocked prompt: no candidates, promptFeedback.blockReason
				{ TEXT("Gemini promptFeedback.blockReason"), Make<UN2CGeminiResponseParser>(),
					TEXT("{\"promptFeedback\":{\"blockReason\":\"PROHIBITED_CONTENT\",\"safetyRatings\":[]},")
					TEXT("\"usageMetadata\":{\"promptTokenCount\":5120,\"totalTokenCount\":5120},\"modelVersion\":\"gemini-3.5-flash-lite\"}"),
					EN2CTranslationErrorKind::Blocked, TEXT("PROHIBITED_CONTENT"), { TEXT("blockReason \"PROHIBITED_CONTENT\"") } },
				{ TEXT("Gemini finishReason SAFETY"), Make<UN2CGeminiResponseParser>(),
					TEXT("{\"candidates\":[{\"finishReason\":\"SAFETY\",\"index\":0,\"safetyRatings\":[{\"category\":\"HARM_CATEGORY_DANGEROUS_CONTENT\",\"probability\":\"HIGH\",\"blocked\":true}]}]}"),
					EN2CTranslationErrorKind::Blocked, TEXT("SAFETY"), { TEXT("finishReason \"SAFETY\"") } },
				{ TEXT("Gemini finishReason RECITATION"), Make<UN2CGeminiResponseParser>(),
					GeminiBody(FString::Printf(TEXT("{\"text\":%s}"), *Quote(CutAnswer())), TEXT("RECITATION")),
					EN2CTranslationErrorKind::Blocked, TEXT("RECITATION"), { TEXT("finishReason \"RECITATION\"") } },
				{ TEXT("DeepSeek content_filter"), Make<UN2CDeepSeekResponseParser>(),
					ChatBody(TEXT("deepseek-flash"), TEXT("\"content_filter\""), TEXT("\"content\":\"\""), ChatUsage()),
					EN2CTranslationErrorKind::Blocked, TEXT("content_filter"), { TEXT("content_filter") } },
				{ TEXT("DeepSeek insufficient_system_resource"), Make<UN2CDeepSeekResponseParser>(),
					ChatBody(TEXT("deepseek-flash"), TEXT("\"insufficient_system_resource\""), FString::Printf(TEXT("\"content\":%s"), *Quote(CutAnswer())), ChatUsage()),
					EN2CTranslationErrorKind::Overloaded, TEXT("insufficient_system_resource"), { TEXT("insufficient_system_resource") } },
				{ TEXT("LM Studio content_filter"), Make<UN2CLMStudioResponseParser>(),
					ChatBody(TEXT("qwen3-32b"), TEXT("\"content_filter\""), TEXT("\"content\":null")),
					EN2CTranslationErrorKind::Blocked, TEXT("content_filter"), { TEXT("content_filter") } },
			};
			for (const FCase& Case : Cases)
			{
				ExpectFailure(Case);
			}
		});
	});

	Describe(TEXT("Answers"), [this]()
	{
		It(TEXT("parses a valid answer with an unbalanced brace inside a string in every provider"), [this]()
		{
			const FString Answer = UnbalancedBraceAnswer();
			ExpectParsed(TEXT("Anthropic thinking + text"), Make<UN2CAnthropicResponseParser>(),
				AnthropicBody(TEXT("end_turn"), AnthropicThinking() + TEXT(",") + AnthropicText(Answer)));
			ExpectParsed(TEXT("Anthropic text split in two blocks"), Make<UN2CAnthropicResponseParser>(),
				AnthropicBody(TEXT("end_turn"), AnthropicText(Answer.Left(40)) + TEXT(",") + AnthropicText(Answer.Mid(40))));
			ExpectParsed(TEXT("OpenAI chat"), Make<UN2COpenAIResponseParser>(),
				ChatBody(TEXT("gpt-6.1-sol"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":%s,\"refusal\":null"), *Quote(Answer)), ChatUsage()));
			ExpectParsed(TEXT("OpenAI Responses"), Make<UN2COpenAIResponseParser>(),
				ResponsesBody(TEXT("completed"), TEXT("null"),
					FString::Printf(TEXT("{\"id\":\"rs_n2c\",\"type\":\"reasoning\",\"summary\":[]},{\"id\":\"msg_n2c\",\"type\":\"message\",\"status\":\"completed\",")
						TEXT("\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":%s,\"annotations\":[]}]}"), *Quote(Answer))));
			// The answer split over two text parts after a thought summary part
			ExpectParsed(TEXT("Gemini thought + two text parts"), Make<UN2CGeminiResponseParser>(),
				GeminiBody(FString::Printf(TEXT("{\"text\":\"Planning the translation.\",\"thought\":true},{\"text\":%s},{\"text\":%s}"),
					*Quote(Answer.Left(50)), *Quote(Answer.Mid(50))), TEXT("STOP")));
			ExpectParsed(TEXT("DeepSeek with reasoning_content"), Make<UN2CDeepSeekResponseParser>(),
				ChatBody(TEXT("deepseek-flash"), TEXT("\"stop\""),
					FString::Printf(TEXT("\"content\":%s,\"reasoning_content\":\"Map nodes to calls {\""), *Quote(Answer)), ChatUsage()));
			ExpectParsed(TEXT("LM Studio"), Make<UN2CLMStudioResponseParser>(),
				ChatBody(TEXT("qwen3-32b"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":%s"), *Quote(Answer)),
					ChatUsage() + TEXT(",\"stats\":{\"tokens_per_second\":42.5,\"time_to_first_token\":0.4,\"generation_time\":1.5}")));
			ExpectParsed(TEXT("Ollama with thinking"), Make<UN2COllamaResponseParser>(),
				OllamaBody(TEXT("```json\n") + Answer + TEXT("\n```"), TEXT("stop"), TEXT("Thinking about braces {")));
		});

		It(TEXT("reports a missing answer as a Parse error that names the stop reason and never parses reasoning"), [this]()
		{
			AddExpectedError(TEXT("no text answer|no answer text|returned no answer text"), EAutomationExpectedErrorFlags::Contains, 0, true);
			AddExpectedError(TEXT("Failed to extract message content"), EAutomationExpectedErrorFlags::Contains, 0, false);

			const FString Answer = UnbalancedBraceAnswer();
			const TArray<FCase> Cases = {
				// Card 0417 shape with a normal stop: thinking only, no text block
				{ TEXT("Anthropic end_turn with only a thinking block"), Make<UN2CAnthropicResponseParser>(),
					AnthropicBody(TEXT("end_turn"), AnthropicThinking()),
					EN2CTranslationErrorKind::Parse, FString(), { TEXT("stop_reason \"end_turn\""), TEXT("content blocks: thinking") } },
				// A valid N2C answer in reasoning_content with no content must not be taken as the answer
				{ TEXT("LM Studio content null with the answer only in reasoning_content"), Make<UN2CLMStudioResponseParser>(),
					ChatBody(TEXT("qwen3-32b"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":null,\"reasoning_content\":%s"), *Quote(Answer))),
					EN2CTranslationErrorKind::Parse, FString(), { TEXT("finish_reason \"stop\""), TEXT("only reasoning text") } },
				{ TEXT("LM Studio content missing"), Make<UN2CLMStudioResponseParser>(),
					ChatBody(TEXT("qwen3-32b"), TEXT("null"), TEXT("\"reasoning\":\"vLLM-style reasoning\"")),
					EN2CTranslationErrorKind::Parse, FString(), { TEXT("finish_reason \"none\"") } },
				{ TEXT("Gemini STOP with only a thought part"), Make<UN2CGeminiResponseParser>(),
					GeminiBody(TEXT("{\"text\":\"Thinking.\",\"thought\":true}"), TEXT("STOP")),
					EN2CTranslationErrorKind::Parse, FString(), { TEXT("finishReason \"STOP\"") } },
				{ TEXT("Ollama empty content with thinking"), Make<UN2COllamaResponseParser>(),
					OllamaBody(FString(), TEXT("stop"), Answer),
					EN2CTranslationErrorKind::Parse, FString(), { TEXT("done_reason \"stop\"") } },
			};
			for (const FCase& Case : Cases)
			{
				ExpectFailure(Case);
			}
		});

		It(TEXT("reports cut-off JSON with a normal stop as a Parse error from the JSON reader"), [this]()
		{
			AddExpectedError(TEXT("Failed to parse JSON response"), EAutomationExpectedErrorFlags::Contains, 0, false);
			FCase Case;
			Case.Name = TEXT("OpenAI stop with cut-off JSON");
			Case.Parser = Make<UN2COpenAIResponseParser>();
			Case.Body = ChatBody(TEXT("gpt-6.1-sol"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":%s"), *Quote(CutAnswer())), ChatUsage());
			Case.Kind = EN2CTranslationErrorKind::Parse;
			Case.MessageHas = { TEXT("not valid JSON"), TEXT("looks cut off") };
			ExpectFailure(Case);

			TestFalse(TEXT("a brace inside a string does not look cut off"), UN2CResponseParserBase::LooksCutOff(UnbalancedBraceAnswer()));
			TestTrue(TEXT("an open string looks cut off"), UN2CResponseParserBase::LooksCutOff(TEXT("{\"a\":\"b")));
			TestTrue(TEXT("an open object looks cut off"), UN2CResponseParserBase::LooksCutOff(TEXT("{\"a\":{\"b\":1}")));
		});
	});

	Describe(TEXT("Fences"), [this]()
	{
		It(TEXT("takes the object out of a fence with any tag or none, trimmed, with prose before or after"), [this]()
		{
			const FString A = UnbalancedBraceAnswer();
			const FString Notes = AnswerWithFenceInNotes();
			const FString Cut = CutAnswer().TrimStartAndEnd();
			struct FFenceCase { const TCHAR* Name; FString In; FString Out; bool bStripped; };
			const TArray<FFenceCase> Cases = {
				{ TEXT("bare object"), A, A, false },
				{ TEXT("object with surrounding whitespace"), TEXT("  \n") + A + TEXT("\n\n"), A, false },
				{ TEXT("```json fence"), TEXT("```json\n") + A + TEXT("\n```"), A, true },
				{ TEXT("```json fence and a trailing newline"), TEXT("```json\n") + A + TEXT("\n```\n"), A, true },
				{ TEXT("leading whitespace before the fence"), TEXT("\n  ```json\n") + A + TEXT("\n```"), A, true },
				{ TEXT("```JSON fence"), TEXT("```JSON\n") + A + TEXT("\n```"), A, true },
				{ TEXT("```Json fence on one line"), TEXT("```Json ") + A + TEXT("```"), A, true },
				{ TEXT("bare ``` fence"), TEXT("```\n") + A + TEXT("\n```"), A, true },
				{ TEXT("a sentence before the fence"), TEXT("Here is the translation:\n```json\n") + A + TEXT("\n```"), A, true },
				{ TEXT("a sentence after the fence"), TEXT("```json\n") + A + TEXT("\n```\nThe braces in strings are kept."), A, true },
				{ TEXT("sentences around a bare fence"), TEXT("Sure.\n```\n") + A + TEXT("\n```\nDone."), A, true },
				{ TEXT("a sentence before an unfenced object"), TEXT("Here is the translation:\n") + A, A, true },
				{ TEXT("a sentence after an unfenced object"), A + TEXT("\n\nLet me know if you need changes."), A, true },
				{ TEXT("a fence inside a string of the fenced object"), TEXT("```json\n") + Notes + TEXT("\n```"), Notes, true },
				{ TEXT("a fence inside a string of a bare object"), Notes, Notes, false },
				{ TEXT("a cut-off fenced answer keeps its text from the brace"), TEXT("```json\n") + CutAnswer(), Cut, true },
				{ TEXT("a cut-off answer loses only the closing fence"), TEXT("```json\n") + CutAnswer() + TEXT("\n```"), Cut, true },
				{ TEXT("text without an object is left to the JSON reader"), TEXT("  I could not translate this graph.\n"), TEXT("I could not translate this graph."), false },
			};
			for (const FFenceCase& Case : Cases)
			{
				FString Content = Case.In;
				const bool bStripped = UN2CResponseParserBase::ProcessJsonContentWithMarkers(Content);
				TestEqual(FString::Printf(TEXT("%s: content"), Case.Name), Content, Case.Out);
				TestEqual(FString::Printf(TEXT("%s: reports stripping"), Case.Name), bStripped, Case.bStripped);
			}
		});

		It(TEXT("parses a fenced answer, with or without a tag and with prose around it, in every provider"), [this]()
		{
			const FString A = UnbalancedBraceAnswer();
			const TArray<TPair<FString, FString>> Variants = {
				{ TEXT("```json fence with a trailing newline"), TEXT("```json\n") + A + TEXT("\n```\n") },
				{ TEXT("```JSON fence"), TEXT("```JSON\n") + A + TEXT("\n```") },
				{ TEXT("bare ``` fence"), TEXT("```\n") + A + TEXT("\n```") },
				{ TEXT("a sentence before the fence"), TEXT("Here is the translation:\n\n```json\n") + A + TEXT("\n```") },
				{ TEXT("a sentence after the fence"), TEXT("```json\n") + A + TEXT("\n```\n\nLet me know if you need changes.") },
			};
			for (const FProviderShape& Shape : ProviderShapes())
			{
				for (const TPair<FString, FString>& Variant : Variants)
				{
					ExpectParsed(Shape.Name + TEXT(", ") + Variant.Key, Shape.MakeParser(), Shape.Body(Variant.Value));
				}
			}

			// A Markdown fence inside the generated notes is part of the answer, not its end
			FN2CTranslationResponse Response;
			const bool bParsed = Make<UN2CAnthropicResponseParser>()->ParseLLMResponse(
				AnthropicBody(TEXT("end_turn"), AnthropicText(TEXT("```json\n") + AnswerWithFenceInNotes() + TEXT("\n```"))), Response);
			if (TestTrue(TEXT("an answer with a fence in its notes parses"), bParsed) && TestEqual(TEXT("one graph"), Response.Graphs.Num(), 1))
			{
				TestTrue(TEXT("the notes keep their fence"), Response.Graphs[0].Code.ImplementationNotes.Contains(TEXT("```cpp\nBuildLabel(Name);\n```")));
			}
		});
	});

	Describe(TEXT("Fields"), [this]()
	{
		It(TEXT("reads an answer without optional fields with no LogJson warning"), [this]()
		{
			// graph_name missing, graph_type a number, graph_class null, code without implementationNotes; a second graph
			// that is not an object; no usage, stats or model_info in the provider body
			const FString Sparse = TEXT("{\"graphs\":[{\"graph_type\":7,\"graph_class\":null,")
				TEXT("\"code\":{\"graphDeclaration\":\"void F();\",\"graphImplementation\":\"void F() {}\"}},\"not a graph\"]}");
			const FString NoCode = TEXT("{\"graphs\":[{\"graph_name\":\"EventGraph\",\"graph_type\":\"EventGraph\",\"graph_class\":\"BP_X\"}]}");

			FLogJsonCapture Capture;
			const TArray<TPair<FString, UN2CResponseParserBase*>> Parsers = {
				{ TEXT("LM Studio without usage, stats or model_info"), Make<UN2CLMStudioResponseParser>() },
				{ TEXT("DeepSeek without usage"), Make<UN2CDeepSeekResponseParser>() },
				{ TEXT("OpenAI without usage"), Make<UN2COpenAIResponseParser>() },
			};
			for (const TPair<FString, UN2CResponseParserBase*>& Parser : Parsers)
			{
				FN2CTranslationResponse Response;
				const bool bParsed = Parser.Value->ParseLLMResponse(
					ChatBody(TEXT("qwen3-32b"), TEXT("\"stop\""), FString::Printf(TEXT("\"content\":%s"), *Quote(Sparse))), Response);
				if (TestTrue(Parser.Key + TEXT(": parses"), bParsed) && TestEqual(Parser.Key + TEXT(": one graph"), Response.Graphs.Num(), 1))
				{
					const FN2CGraphTranslation& Graph = Response.Graphs[0];
					TestEqual(Parser.Key + TEXT(": missing graph_name is empty"), Graph.GraphName, FString());
					TestEqual(Parser.Key + TEXT(": numeric graph_type is read as text"), Graph.GraphType, FString(TEXT("7")));
					TestEqual(Parser.Key + TEXT(": null graph_class is empty"), Graph.GraphClass, FString());
					TestEqual(Parser.Key + TEXT(": implementation read"), Graph.Code.GraphImplementation, FString(TEXT("void F() {}")));
					TestEqual(Parser.Key + TEXT(": missing notes are empty"), Graph.Code.ImplementationNotes, FString());
				}
				TestEqual(Parser.Key + TEXT(": no usage reported"), Response.Usage.InputTokens, 0);
			}

			FN2CTranslationResponse NoCodeResponse;
			TestTrue(TEXT("a graph without code parses"), Make<UN2CGeminiResponseParser>()->ParseLLMResponse(
				TEXT("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":") + Quote(NoCode) + TEXT("}],\"role\":\"model\"},\"finishReason\":\"STOP\"}]}"),
				NoCodeResponse));
			FN2CTranslationResponse NoUsage;
			TestTrue(TEXT("Anthropic without usage parses"), Make<UN2CAnthropicResponseParser>()->ParseLLMResponse(
				FString::Printf(TEXT("{\"type\":\"message\",\"content\":[%s],\"stop_reason\":\"end_turn\"}"), *AnthropicText(UnbalancedBraceAnswer())), NoUsage));

			ExpectNoLogJson(TEXT("sparse answers"), Capture);
		});

		It(TEXT("returns the text of a string-valued error from every provider parser, with no LogJson warning"), [this]()
		{
			AddExpectedError(TEXT("Model not loaded"), EAutomationExpectedErrorFlags::Contains, 0, false);
			FLogJsonCapture Capture;
			const TArray<TPair<FString, UN2CResponseParserBase*>> Parsers = {
				{ TEXT("Anthropic"), Make<UN2CAnthropicResponseParser>() },
				{ TEXT("OpenAI"), Make<UN2COpenAIResponseParser>() },
				{ TEXT("Gemini"), Make<UN2CGeminiResponseParser>() },
				{ TEXT("DeepSeek"), Make<UN2CDeepSeekResponseParser>() },
				{ TEXT("LM Studio"), Make<UN2CLMStudioResponseParser>() },
				{ TEXT("Ollama"), Make<UN2COllamaResponseParser>() },
			};
			for (const TPair<FString, UN2CResponseParserBase*>& Parser : Parsers)
			{
				FN2CTranslationResponse Response;
				TestFalse(Parser.Key + TEXT(": reports failure"), Parser.Value->ParseLLMResponse(TEXT("{\"error\":\"Model not loaded\"}"), Response));
				TestEqual(Parser.Key + TEXT(": the provider's text is the message"), Response.Error.Message, FString(TEXT("Model not loaded")));
				TestFalse(Parser.Key + TEXT(": not the empty 'API error:  - ' text"), Response.Error.ToDisplayString().Contains(TEXT("API error:  - ")));
			}
			ExpectNoLogJson(TEXT("string errors"), Capture);
		});
	});

	Describe(TEXT("Usage"), [this]()
	{
		It(TEXT("records cache read and write, and thinking, in every provider's usage"), [this]()
		{
			const FString Answer = UnbalancedBraceAnswer();
			struct FUsageCase { const TCHAR* Name; UN2CResponseParserBase* Parser; FString Body; int32 In; int32 Out; int32 CacheRead; int32 CacheWrite; int32 Reasoning; };
			const FString ChatContent = FString::Printf(TEXT("\"content\":%s"), *Quote(Answer));
			const TArray<FUsageCase> Cases = {
				// input_tokens is only the input after the last cache breakpoint: the total adds both cache counts
				{ TEXT("Anthropic cache read and creation"), Make<UN2CAnthropicResponseParser>(),
					FString::Printf(TEXT("{\"type\":\"message\",\"content\":[%s],\"stop_reason\":\"end_turn\",")
						TEXT("\"usage\":{\"input_tokens\":120,\"cache_creation_input_tokens\":2000,\"cache_read_input_tokens\":3000,\"output_tokens\":64}}"),
						*AnthropicText(Answer)),
					5120, 64, 3000, 2000, 0 },
				// prompt_tokens and completion_tokens already include the cached and reasoning parts
				{ TEXT("OpenAI chat cached and reasoning tokens"), Make<UN2COpenAIResponseParser>(),
					ChatBody(TEXT("gpt-6.1-sol"), TEXT("\"stop\""), ChatContent,
						TEXT(",\"usage\":{\"prompt_tokens\":5120,\"completion_tokens\":964,\"total_tokens\":6084,")
						TEXT("\"prompt_tokens_details\":{\"cached_tokens\":4096,\"audio_tokens\":0},")
						TEXT("\"completion_tokens_details\":{\"reasoning_tokens\":900,\"audio_tokens\":0}}")),
					5120, 964, 4096, 0, 900 },
				{ TEXT("OpenAI Responses cached and reasoning tokens"), Make<UN2COpenAIResponseParser>(),
					FString::Printf(TEXT("{\"id\":\"resp_n2c\",\"object\":\"response\",\"status\":\"completed\",\"error\":null,\"incomplete_details\":null,")
						TEXT("\"output\":[{\"type\":\"message\",\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":%s}]}],")
						TEXT("\"usage\":{\"input_tokens\":5120,\"input_tokens_details\":{\"cached_tokens\":2048},")
						TEXT("\"output_tokens\":964,\"output_tokens_details\":{\"reasoning_tokens\":900},\"total_tokens\":6084}}"), *Quote(Answer)),
					5120, 964, 2048, 0, 900 },
				// DeepSeek: prompt_tokens = prompt_cache_hit_tokens + prompt_cache_miss_tokens
				{ TEXT("DeepSeek cache hits and reasoning"), Make<UN2CDeepSeekResponseParser>(),
					ChatBody(TEXT("deepseek-flash"), TEXT("\"stop\""), ChatContent,
						TEXT(",\"usage\":{\"prompt_tokens\":5120,\"completion_tokens\":964,\"total_tokens\":6084,")
						TEXT("\"prompt_cache_hit_tokens\":4864,\"prompt_cache_miss_tokens\":256,")
						TEXT("\"completion_tokens_details\":{\"reasoning_tokens\":900}}")),
					5120, 964, 4864, 0, 900 },
				// Gemini: thinking is billed as output but counted apart from the candidates
				{ TEXT("Gemini thoughts and cached content"), Make<UN2CGeminiResponseParser>(),
					FString::Printf(TEXT("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":%s}],\"role\":\"model\"},\"finishReason\":\"STOP\"}],")
						TEXT("\"usageMetadata\":{\"promptTokenCount\":5120,\"cachedContentTokenCount\":4096,\"candidatesTokenCount\":64,")
						TEXT("\"thoughtsTokenCount\":900,\"totalTokenCount\":6084}}"), *Quote(Answer)),
					5120, 964, 4096, 0, 900 },
				{ TEXT("LM Studio cached and reasoning tokens"), Make<UN2CLMStudioResponseParser>(),
					ChatBody(TEXT("qwen3-32b"), TEXT("\"stop\""), ChatContent,
						TEXT(",\"usage\":{\"prompt_tokens\":5120,\"completion_tokens\":964,\"total_tokens\":6084,")
						TEXT("\"prompt_tokens_details\":{\"cached_tokens\":1024},\"completion_tokens_details\":{\"reasoning_tokens\":900}}")),
					5120, 964, 1024, 0, 900 },
				// Details present but null, as some OpenAI-compatible servers send them
				{ TEXT("LM Studio with null details"), Make<UN2CLMStudioResponseParser>(),
					ChatBody(TEXT("qwen3-32b"), TEXT("\"stop\""), ChatContent,
						TEXT(",\"usage\":{\"prompt_tokens\":5120,\"completion_tokens\":64,\"prompt_tokens_details\":null,\"completion_tokens_details\":null}")),
					5120, 64, 0, 0, 0 },
				{ TEXT("Ollama eval counts"), Make<UN2COllamaResponseParser>(), OllamaBody(Answer, TEXT("stop")), 5120, 64, 0, 0, 0 },
			};
			FLogJsonCapture Capture;
			for (const FUsageCase& Case : Cases)
			{
				FN2CTranslationResponse Response;
				TestTrue(FString::Printf(TEXT("%s: parses (error: %s)"), Case.Name, *Response.Error.Message), Case.Parser->ParseLLMResponse(Case.Body, Response));
				TestEqual(FString::Printf(TEXT("%s: input tokens"), Case.Name), Response.Usage.InputTokens, Case.In);
				TestEqual(FString::Printf(TEXT("%s: output tokens"), Case.Name), Response.Usage.OutputTokens, Case.Out);
				TestEqual(FString::Printf(TEXT("%s: cache read tokens"), Case.Name), Response.Usage.CacheReadInputTokens, Case.CacheRead);
				TestEqual(FString::Printf(TEXT("%s: cache write tokens"), Case.Name), Response.Usage.CacheWriteInputTokens, Case.CacheWrite);
				TestEqual(FString::Printf(TEXT("%s: reasoning tokens"), Case.Name), Response.Usage.ReasoningTokens, Case.Reasoning);
			}
			ExpectNoLogJson(TEXT("usage objects"), Capture);
		});

		It(TEXT("keeps the usage of an answer cut off while thinking"), [this]()
		{
			AddExpectedError(TEXT("(Truncated)"), EAutomationExpectedErrorFlags::Contains, 0, false);
			FN2CTranslationResponse Response;
			TestFalse(TEXT("Gemini MAX_TOKENS with no content fails"), Make<UN2CGeminiResponseParser>()->ParseLLMResponse(
				TEXT("{\"candidates\":[{\"finishReason\":\"MAX_TOKENS\",\"index\":0}],\"usageMetadata\":{\"promptTokenCount\":5120,\"thoughtsTokenCount\":256,\"totalTokenCount\":5376}}"),
				Response));
			TestEqual(TEXT("the thinking tokens are billed output"), Response.Usage.OutputTokens, 256);
			TestEqual(TEXT("and reported as reasoning"), Response.Usage.ReasoningTokens, 256);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
