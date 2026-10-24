// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "Dom/JsonValue.h"
#include "MCP/Prompts/N2CMcpPromptManager.h"
#include "MCP/Prompts/N2CMcpPromptTypes.h"
#include "MCP/Resources/N2CMcpResourceManager.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * Characterization specs for the MCP protocol layer: FN2CMcpProtocol::ProcessMessage (the protocol core) driven with
 * parsed JSON-RPC bodies, no socket. They pin what the server answers today for initialize, ping, tools, resources,
 * prompts, notifications, client responses, errors, ids and batches, so the E08 rewrite (cards 0108-0110) changes
 * behaviour on purpose and not by accident. Transport checks (Origin, Host, sessions, headers, streaming) are not
 * here: they live in NodeToCode.MCP.Transport.* and Tests/mcp_probe.
 *
 * A case whose expected value is a spec violation says so in a comment with the finding id and the rule
 * (MCP 2025-11-25 unless named), and the correct behaviour sits next to it as an xIt naming the card that turns it on.
 * That card deletes the "today" case and enables the xIt. Review ids: imp-mcp-transport-8, imp-dx-docs-release-3 (b).
 */
BEGIN_DEFINE_SPEC(FN2CMcpProtocolSpec, "NodeToCode.MCP.Protocol", N2C_TEST_FLAGS)

	/** What ProcessMessage gave back for one body */
	struct FReply
	{
		bool bProcessed = false;
		int32 Status = 0;
		bool bWasInitialize = false;
		FString Body;
		/** The body parsed as JSON; null when the body is empty or not JSON */
		TSharedPtr<FJsonValue> Json;
	};

	/** A prompt this spec registers so prompts/get has a known name with one required argument */
	const FString TestPromptName = TEXT("n2c-test-protocol-prompt");
	bool bRegisteredTestPrompt = false;

	/** Parses Text the way the server does (ParseRequestBody over its UTF-8 bytes); null when it is not JSON */
	static TSharedPtr<FJsonValue> ParseBody(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		const TArray<uint8> Bytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		TSharedPtr<FJsonValue> Value;
		FN2CMcpProtocol::ParseBody(Bytes, Value);
		return Value;
	}

	static FReply SendValue(const TSharedPtr<FJsonValue>& Value)
	{
		FReply Reply;
		FN2CMcpProtocol Protocol;
		Reply.bProcessed = Protocol.ProcessMessage(Value, Reply.Body, Reply.Status, Reply.bWasInitialize);
		if (!Reply.Body.IsEmpty())
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Reply.Body);
			FJsonSerializer::Deserialize(Reader, Reply.Json);
		}
		return Reply;
	}

	/** Sends one JSON-RPC body (text) through the protocol layer */
	FReply Send(const FString& Text)
	{
		const TSharedPtr<FJsonValue> Value = ParseBody(Text);
		TestTrue(FString::Printf(TEXT("test body parses: %s"), *Text.Left(120)), Value.IsValid());
		return SendValue(Value);
	}

	/** A request body with a numeric id, a method and optional params JSON */
	static FString Request(int32 Id, const FString& Method, const FString& ParamsJson = FString())
	{
		return ParamsJson.IsEmpty()
			? FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\"}"), Id, *Method)
			: FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}"), Id, *Method, *ParamsJson);
	}

	static FString Initialize(int32 Id, const FString& Version)
	{
		return Request(Id, TEXT("initialize"), FString::Printf(
			TEXT("{\"protocolVersion\":\"%s\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-spec\",\"version\":\"1.0\"}}"), *Version));
	}

	/** The single response object of a reply (null, with a test error, when the body is not one) */
	TSharedPtr<FJsonObject> Message(const FReply& Reply)
	{
		if (!TestTrue(FString::Printf(TEXT("reply body is one JSON object: %s"), *Reply.Body.Left(200)),
			Reply.Json.IsValid() && Reply.Json->Type == EJson::Object))
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject> Object = Reply.Json->AsObject();
		FString JsonRpc;
		TestTrue(TEXT("response says jsonrpc 2.0"), Object->TryGetStringField(TEXT("jsonrpc"), JsonRpc) && JsonRpc == TEXT("2.0"));
		return Object;
	}

	/** The result object of a success response */
	TSharedPtr<FJsonObject> ResultOf(const FReply& Reply)
	{
		const TSharedPtr<FJsonObject> Object = Message(Reply);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		if (!Object.IsValid() || !TestTrue(FString::Printf(TEXT("response has a result object: %s"), *Reply.Body.Left(200)),
			Object->TryGetObjectField(TEXT("result"), Result)))
		{
			return nullptr;
		}
		TestFalse(TEXT("a success response has no error"), Object->HasField(TEXT("error")));
		return *Result;
	}

	/** The error object of an error response */
	TSharedPtr<FJsonObject> ErrorOf(const FReply& Reply)
	{
		const TSharedPtr<FJsonObject> Object = Message(Reply);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		if (!Object.IsValid() || !TestTrue(FString::Printf(TEXT("response has an error object: %s"), *Reply.Body.Left(200)),
			Object->TryGetObjectField(TEXT("error"), Error)))
		{
			return nullptr;
		}
		TestFalse(TEXT("an error response has no result"), Object->HasField(TEXT("result")));
		FString ErrorMessage;
		TestTrue(TEXT("error has a message"), (*Error)->TryGetStringField(TEXT("message"), ErrorMessage) && !ErrorMessage.IsEmpty());
		return *Error;
	}

	/** Checks a reply is an error response with this code and this numeric id (or a null id when Id < 0) */
	void TestError(const FString& What, const FReply& Reply, int32 ExpectedCode, int32 Id, int32 ExpectedStatus)
	{
		TestEqual(What + TEXT(": HTTP status"), Reply.Status, ExpectedStatus);
		const TSharedPtr<FJsonObject> Error = ErrorOf(Reply);
		if (!Error.IsValid())
		{
			return;
		}
		double Code = 0.0;
		TestTrue(What + TEXT(": error code is a number"), Error->TryGetNumberField(TEXT("code"), Code));
		TestEqual(What + TEXT(": error code"), static_cast<int32>(Code), ExpectedCode);
		TestId(What, Reply, Id);
	}

	/** Checks the response id is this number, or null when Id < 0 */
	void TestId(const FString& What, const FReply& Reply, int32 Id)
	{
		const TSharedPtr<FJsonObject> Object = Message(Reply);
		if (!Object.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonValue> IdValue = Object->TryGetField(TEXT("id"));
		if (!TestTrue(What + TEXT(": response has an id member"), IdValue.IsValid()))
		{
			return;
		}
		if (Id < 0)
		{
			TestTrue(What + TEXT(": id is null"), IdValue->IsNull());
		}
		else if (TestEqual(What + TEXT(": id is a number"), static_cast<int32>(IdValue->Type), static_cast<int32>(EJson::Number)))
		{
			TestEqual(What + TEXT(": id"), static_cast<int32>(IdValue->AsNumber()), Id);
		}
	}

	/** The protocolVersion an initialize request with this version gets back */
	FString NegotiatedVersion(const FString& Requested)
	{
		const FReply Reply = Send(Initialize(1, Requested));
		TestEqual(FString::Printf(TEXT("initialize %s: HTTP status"), *Requested), Reply.Status, 200);
		TestTrue(FString::Printf(TEXT("initialize %s: flagged as an initialize call"), *Requested), Reply.bWasInitialize);
		FString Version;
		if (const TSharedPtr<FJsonObject> Result = ResultOf(Reply))
		{
			TestTrue(TEXT("result has a protocolVersion string"), Result->TryGetStringField(TEXT("protocolVersion"), Version));
		}
		return Version;
	}

	/** Checks a reply is 202 Accepted with an empty body */
	void TestAccepted(const FString& What, const FReply& Reply)
	{
		TestEqual(What + TEXT(": HTTP status"), Reply.Status, 202);
		TestTrue(What + TEXT(": empty body"), Reply.Body.IsEmpty());
		TestTrue(What + TEXT(": processed"), Reply.bProcessed);
	}

END_DEFINE_SPEC(FN2CMcpProtocolSpec)

void FN2CMcpProtocolSpec::Define()
{
	Describe(TEXT("initialize"), [this]()
	{
		It(TEXT("echoes 2024-11-05 and 2025-03-26, the versions it supports"), [this]()
		{
			TestEqual(TEXT("2024-11-05"), NegotiatedVersion(TEXT("2024-11-05")), FString(TEXT("2024-11-05")));
			TestEqual(TEXT("2025-03-26"), NegotiatedVersion(TEXT("2025-03-26")), FString(TEXT("2025-03-26")));
		});

		// Finding next-1 (imp-mcp-transport-1), fixed by card 0109. Lifecycle, version negotiation: "If the server
		// supports the requested protocol version, it MUST respond with the same version"; otherwise its latest.
		It(TEXT("echoes 2025-06-18 and 2025-11-25 and answers 2026-07-28 with 2025-11-25"), [this]()
		{
			TestEqual(TEXT("2025-06-18"), NegotiatedVersion(TEXT("2025-06-18")), FString(TEXT("2025-06-18")));
			TestEqual(TEXT("2025-11-25"), NegotiatedVersion(TEXT("2025-11-25")), FString(TEXT("2025-11-25")));
			TestEqual(TEXT("2026-07-28"), NegotiatedVersion(TEXT("2026-07-28")), FString(TEXT("2025-11-25")));
		});

		It(TEXT("answers an unknown version with a version it supports"), [this]()
		{
			const FString Version = NegotiatedVersion(TEXT("1999-01-01"));
			TestEqual(TEXT("the newest supported version"), Version, FString(TEXT("2025-11-25")));
		});

		It(TEXT("returns tools, resources and prompts capabilities and a serverInfo"), [this]()
		{
			const TSharedPtr<FJsonObject> Result = ResultOf(Send(Initialize(1, TEXT("2025-03-26"))));
			if (!Result.IsValid())
			{
				return;
			}
			const TSharedPtr<FJsonObject>* Capabilities = nullptr;
			if (TestTrue(TEXT("capabilities object"), Result->TryGetObjectField(TEXT("capabilities"), Capabilities)))
			{
				const TSharedPtr<FJsonObject>* Tools = nullptr;
				bool bListChanged = false;
				TestTrue(TEXT("tools capability"), (*Capabilities)->TryGetObjectField(TEXT("tools"), Tools));
				TestTrue(TEXT("tools.listChanged is true"), Tools && (*Tools)->TryGetBoolField(TEXT("listChanged"), bListChanged) && bListChanged);
				TestTrue(TEXT("resources capability"), (*Capabilities)->HasTypedField<EJson::Object>(TEXT("resources")));
				TestTrue(TEXT("prompts capability"), (*Capabilities)->HasTypedField<EJson::Object>(TEXT("prompts")));
			}
			const TSharedPtr<FJsonObject>* ServerInfo = nullptr;
			if (TestTrue(TEXT("serverInfo object"), Result->TryGetObjectField(TEXT("serverInfo"), ServerInfo)))
			{
				TestEqual(TEXT("serverInfo.name"), (*ServerInfo)->GetStringField(TEXT("name")), FString(TEXT("NodeToCodeMCPServer")));
				TestTrue(TEXT("serverInfo.version is a string"), (*ServerInfo)->HasTypedField<EJson::String>(TEXT("version")));
			}
		});

		// Finding mcp-transport-19, fixed by card 0110: logging was advertised without logging/setLevel. Capability
		// negotiation: both parties "Only use capabilities that were successfully negotiated" (and logging is
		// deprecated in 2026-07-28), so it is no longer advertised and logging/setLevel is an unknown method.
		It(TEXT("does not advertise logging"), [this]()
		{
			const TSharedPtr<FJsonObject> Result = ResultOf(Send(Initialize(1, TEXT("2025-11-25"))));
			const TSharedPtr<FJsonObject>* Capabilities = nullptr;
			if (Result.IsValid() && TestTrue(TEXT("capabilities object"), Result->TryGetObjectField(TEXT("capabilities"), Capabilities)))
			{
				TestFalse(TEXT("logging capability absent"), (*Capabilities)->HasField(TEXT("logging")));
			}
			TestError(TEXT("logging/setLevel"), Send(Request(2, TEXT("logging/setLevel"), TEXT("{\"level\":\"info\"}"))),
				JsonRpcErrorCodes::MethodNotFound, 2, 200);
		});

		// imp-mcp-transport-5 (card 0110): InitializeResult.instructions tells the agent which tools to start with
		It(TEXT("returns instructions that name the tools to start with"), [this]()
		{
			const TSharedPtr<FJsonObject> Result = ResultOf(Send(Initialize(1, TEXT("2025-11-25"))));
			FString Instructions;
			TestTrue(TEXT("instructions is a string"), Result.IsValid() && Result->TryGetStringField(TEXT("instructions"), Instructions));
			TestTrue(TEXT("names get-focused-blueprint"), Instructions.Contains(TEXT("get-focused-blueprint")));
			TestTrue(TEXT("names translate-focused-blueprint"), Instructions.Contains(TEXT("translate-focused-blueprint")));
		});

		It(TEXT("refuses an initialize without protocolVersion with -32602"), [this]()
		{
			const FReply Reply = Send(Request(3, TEXT("initialize"), TEXT("{\"capabilities\":{},\"clientInfo\":{\"name\":\"x\",\"version\":\"1\"}}")));
			TestError(TEXT("no protocolVersion"), Reply, JsonRpcErrorCodes::InvalidParams, 3, 200);
			TestTrue(TEXT("still flagged as an initialize call"), Reply.bWasInitialize);
		});

		It(TEXT("refuses initialize params that are missing or not an object with -32602"), [this]()
		{
			TestError(TEXT("no params"), Send(Request(4, TEXT("initialize"))), JsonRpcErrorCodes::InvalidParams, 4, 200);
			TestError(TEXT("array params"), Send(Request(5, TEXT("initialize"), TEXT("[\"2025-03-26\"]"))), JsonRpcErrorCodes::InvalidParams, 5, 200);
		});

		It(TEXT("does not flag other methods as initialize calls"), [this]()
		{
			TestFalse(TEXT("ping"), Send(Request(6, TEXT("ping"))).bWasInitialize);
			TestFalse(TEXT("notifications/initialized"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}")).bWasInitialize);
		});
	});

	Describe(TEXT("ping"), [this]()
	{
		It(TEXT("answers with an empty result, the request id and 200"), [this]()
		{
			const FReply Reply = Send(Request(11, TEXT("ping")));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			TestTrue(TEXT("processed"), Reply.bProcessed);
			if (const TSharedPtr<FJsonObject> Result = ResultOf(Reply))
			{
				TestEqual(TEXT("result is empty"), Result->Values.Num(), 0);
			}
			TestId(TEXT("ping"), Reply, 11);
		});
	});

	Describe(TEXT("tools/list"), [this]()
	{
		It(TEXT("returns a tools array whose entries have a name, a description and an object inputSchema"), [this]()
		{
			// Without -N2CMcp the session may have no active tools: activate one for the case so the shape is checked
			FN2CMcpToolManager& Tools = FN2CMcpToolManager::Get();
			FString AddedTool;
			if (Tools.GetAllToolDefinitions().Num() == 0)
			{
				for (const TSharedPtr<IN2CMcpTool>& Tool : N2CTest::AllRegisteredTools())
				{
					if (Tools.RegisterTool(Tool))
					{
						AddedTool = Tool->GetDefinition().Name;
						break;
					}
				}
			}

			const FReply Reply = Send(Request(21, TEXT("tools/list"), TEXT("{}")));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			const TSharedPtr<FJsonObject> Result = ResultOf(Reply);
			const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
			if (Result.IsValid() && TestTrue(TEXT("result.tools is an array"), Result->TryGetArrayField(TEXT("tools"), ToolsArray)))
			{
				TestTrue(TEXT("at least one tool"), ToolsArray->Num() > 0);
				for (const TSharedPtr<FJsonValue>& Value : *ToolsArray)
				{
					const TSharedPtr<FJsonObject>* Tool = nullptr;
					if (!TestTrue(TEXT("tool entry is an object"), Value->TryGetObject(Tool)))
					{
						continue;
					}
					FString Name;
					TestTrue(TEXT("tool has a non-empty name"), (*Tool)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty());
					TestTrue(FString::Printf(TEXT("%s: has a description"), *Name), (*Tool)->HasTypedField<EJson::String>(TEXT("description")));
					const TSharedPtr<FJsonObject>* Schema = nullptr;
					FString SchemaType;
					TestTrue(FString::Printf(TEXT("%s: inputSchema is an object"), *Name), (*Tool)->TryGetObjectField(TEXT("inputSchema"), Schema));
					TestTrue(FString::Printf(TEXT("%s: inputSchema.type is object"), *Name),
						Schema && (*Schema)->TryGetStringField(TEXT("type"), SchemaType) && SchemaType == TEXT("object"));
				}
			}
			// No pagination today: every tool in one page and no nextCursor
			TestFalse(TEXT("no nextCursor"), Result.IsValid() && Result->HasField(TEXT("nextCursor")));

			if (!AddedTool.IsEmpty())
			{
				Tools.UnregisterTool(AddedTool);
			}
		});
	});

	Describe(TEXT("tools/call"), [this]()
	{
		// Finding mcp-transport-20, fixed by card 0110: an unknown tool was -32601. Tools, error handling: an unknown
		// tool is a protocol error, -32602 in the 2025-11-25 example ("Unknown tool: invalid_tool_name"); -32601 is for
		// unknown methods. Either that or a CallToolResult with isError true naming the tool is accepted here; the
		// server answers -32602.
		It(TEXT("reports an unknown tool as an isError result or -32602"), [this]()
		{
			const FReply Reply = Send(Request(32, TEXT("tools/call"), TEXT("{\"name\":\"n2c-no-such-tool\",\"arguments\":{}}")));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			const TSharedPtr<FJsonObject> Object = Message(Reply);
			if (!Object.IsValid())
			{
				return;
			}
			const TSharedPtr<FJsonObject>* Result = nullptr;
			const TSharedPtr<FJsonObject>* Error = nullptr;
			if (Object->TryGetObjectField(TEXT("result"), Result))
			{
				bool bIsError = false;
				TestTrue(TEXT("isError is true"), (*Result)->TryGetBoolField(TEXT("isError"), bIsError) && bIsError);
				TestTrue(TEXT("result names the tool"), Reply.Body.Contains(TEXT("n2c-no-such-tool")));
			}
			else if (TestTrue(TEXT("a result or an error"), Object->TryGetObjectField(TEXT("error"), Error)))
			{
				TestEqual(TEXT("error code"), static_cast<int32>((*Error)->GetNumberField(TEXT("code"))), JsonRpcErrorCodes::InvalidParams);
			}
			TestId(TEXT("unknown tool"), Reply, 32);
		});

		It(TEXT("refuses a call without a name, or with arguments that are not an object, with -32602"), [this]()
		{
			TestError(TEXT("no name"), Send(Request(33, TEXT("tools/call"), TEXT("{\"arguments\":{}}"))), JsonRpcErrorCodes::InvalidParams, 33, 200);
			TestError(TEXT("array arguments"), Send(Request(34, TEXT("tools/call"), TEXT("{\"name\":\"n2c-no-such-tool\",\"arguments\":[1]}"))),
				JsonRpcErrorCodes::InvalidParams, 34, 200);
			TestError(TEXT("no params"), Send(Request(35, TEXT("tools/call"))), JsonRpcErrorCodes::InvalidParams, 35, 200);
		});
	});

	Describe(TEXT("resources"), [this]()
	{
		It(TEXT("lists resources in a resources array"), [this]()
		{
			const TSharedPtr<FJsonObject> Result = ResultOf(Send(Request(41, TEXT("resources/list"))));
			TestTrue(TEXT("result.resources is an array"), Result.IsValid() && Result->HasTypedField<EJson::Array>(TEXT("resources")));
		});

		// Findings mcp-transport-20 and mcp-tools-python-translation-resources-23, fixed by card 0110: an unknown
		// resource was -32601. Resources, error handling: "Resource not found: -32002" with the uri in data.
		It(TEXT("refuses an unknown uri with -32002 and the uri in data"), [this]()
		{
			const FReply Reply = Send(Request(43, TEXT("resources/read"), TEXT("{\"uri\":\"n2ctest://no/such/resource\"}")));
			TestError(TEXT("unknown uri"), Reply, -32002, 43, 200);
			if (const TSharedPtr<FJsonObject> Error = ErrorOf(Reply))
			{
				const TSharedPtr<FJsonObject>* Data = nullptr;
				FString Uri;
				TestTrue(TEXT("data.uri is the requested uri"), Error->TryGetObjectField(TEXT("data"), Data)
					&& (*Data)->TryGetStringField(TEXT("uri"), Uri) && Uri == TEXT("n2ctest://no/such/resource"));
			}
		});

		It(TEXT("refuses resources/read without a uri with -32602"), [this]()
		{
			TestError(TEXT("no uri"), Send(Request(44, TEXT("resources/read"), TEXT("{}"))), JsonRpcErrorCodes::InvalidParams, 44, 200);
		});

		// Findings mcp-transport-19 and mcp-tools-python-translation-resources-16, fixed by card 0110:
		// resources/templates/list did not exist and templates rode in a non-standard resourceTemplates field of
		// resources/list. Resources, resource templates: resources/templates/list answers {resourceTemplates: [...]}.
		It(TEXT("answers resources/templates/list with a resourceTemplates array"), [this]()
		{
			const TSharedPtr<FJsonObject> Result = ResultOf(Send(Request(45, TEXT("resources/templates/list"))));
			const TArray<TSharedPtr<FJsonValue>>* Templates = nullptr;
			if (TestTrue(TEXT("result.resourceTemplates is an array"), Result.IsValid() && Result->TryGetArrayField(TEXT("resourceTemplates"), Templates)))
			{
				// Every registered template is listed (nodetocode://blueprint/{name} once the server has registered it)
				const TArray<FMcpResourceTemplate> Registered = FN2CMcpResourceManager::Get().ListResourceTemplates();
				TestEqual(TEXT("one entry per registered template"), Templates->Num(), Registered.Num());
				for (const FMcpResourceTemplate& Template : Registered)
				{
					bool bListed = false;
					for (const TSharedPtr<FJsonValue>& Value : *Templates)
					{
						const TSharedPtr<FJsonObject>* Entry = nullptr;
						FString UriTemplate;
						bListed |= Value->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("uriTemplate"), UriTemplate)
							&& UriTemplate == Template.UriTemplate;
					}
					TestTrue(FString::Printf(TEXT("%s listed"), *Template.UriTemplate), bListed);
				}
			}
			const TSharedPtr<FJsonObject> Listed = ResultOf(Send(Request(46, TEXT("resources/list"))));
			TestFalse(TEXT("resources/list has no resourceTemplates field"), Listed.IsValid() && Listed->HasField(TEXT("resourceTemplates")));
		});
	});

	Describe(TEXT("prompts"), [this]()
	{
		BeforeEach([this]()
		{
			FMcpPromptDefinition Definition;
			Definition.Name = TestPromptName;
			Definition.Description = TEXT("Prompt registered by NodeToCode.MCP.Protocol");
			FMcpPromptArgument Topic;
			Topic.Name = TEXT("topic");
			Topic.Description = TEXT("What to talk about");
			Topic.bRequired = true;
			Definition.Arguments.Add(Topic);

			const FMcpPromptGetDelegate Handler = FMcpPromptGetDelegate::CreateLambda([](const FMcpPromptArguments& Arguments)
			{
				FMcpPromptResult Result;
				Result.Description = TEXT("spec prompt");
				FMcpPromptMessage Message;
				Message.Role = TEXT("user");
				Message.Content.Type = TEXT("text");
				Message.Content.Text = FString::Printf(TEXT("Talk about %s"), *Arguments.FindRef(TEXT("topic")));
				Result.Messages.Add(Message);
				return Result;
			});
			bRegisteredTestPrompt = FN2CMcpPromptManager::Get().RegisterPrompt(Definition, Handler, false);
			TestTrue(TEXT("spec prompt registered"), bRegisteredTestPrompt);
		});

		AfterEach([this]()
		{
			if (bRegisteredTestPrompt)
			{
				FN2CMcpPromptManager::Get().UnregisterPrompt(TestPromptName);
				bRegisteredTestPrompt = false;
			}
		});

		It(TEXT("lists prompts in a prompts array with their arguments"), [this]()
		{
			const TSharedPtr<FJsonObject> Result = ResultOf(Send(Request(51, TEXT("prompts/list"))));
			const TArray<TSharedPtr<FJsonValue>>* Prompts = nullptr;
			if (!Result.IsValid() || !TestTrue(TEXT("result.prompts is an array"), Result->TryGetArrayField(TEXT("prompts"), Prompts)))
			{
				return;
			}
			const TSharedPtr<FJsonObject>* Found = nullptr;
			for (const TSharedPtr<FJsonValue>& Value : *Prompts)
			{
				const TSharedPtr<FJsonObject>* Prompt = nullptr;
				if (Value->TryGetObject(Prompt) && (*Prompt)->GetStringField(TEXT("name")) == TestPromptName)
				{
					Found = Prompt;
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Arguments = nullptr;
			if (TestNotNull(TEXT("spec prompt listed"), Found)
				&& TestTrue(TEXT("arguments array"), (*Found)->TryGetArrayField(TEXT("arguments"), Arguments))
				&& TestEqual(TEXT("one argument"), Arguments->Num(), 1))
			{
				const TSharedPtr<FJsonObject> Argument = (*Arguments)[0]->AsObject();
				TestEqual(TEXT("argument name"), Argument->GetStringField(TEXT("name")), FString(TEXT("topic")));
				bool bRequired = false;
				TestTrue(TEXT("argument required"), Argument->TryGetBoolField(TEXT("required"), bRequired) && bRequired);
			}
		});

		It(TEXT("returns the messages of a registered prompt"), [this]()
		{
			const TSharedPtr<FJsonObject> Result = ResultOf(Send(Request(52, TEXT("prompts/get"),
				FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":{\"topic\":\"pins\"}}"), *TestPromptName))));
			const TArray<TSharedPtr<FJsonValue>>* Messages = nullptr;
			if (Result.IsValid() && TestTrue(TEXT("result.messages is an array"), Result->TryGetArrayField(TEXT("messages"), Messages))
				&& TestEqual(TEXT("one message"), Messages->Num(), 1))
			{
				const TSharedPtr<FJsonObject> First = (*Messages)[0]->AsObject();
				TestEqual(TEXT("role"), First->GetStringField(TEXT("role")), FString(TEXT("user")));
				const TSharedPtr<FJsonObject>* Content = nullptr;
				if (TestTrue(TEXT("content object"), First->TryGetObjectField(TEXT("content"), Content)))
				{
					TestEqual(TEXT("content type"), (*Content)->GetStringField(TEXT("type")), FString(TEXT("text")));
					TestEqual(TEXT("content text uses the argument"), (*Content)->GetStringField(TEXT("text")), FString(TEXT("Talk about pins")));
				}
			}
		});

		// Finding mcp-transport-20, fixed by card 0110: an unknown prompt was -32601. Prompts, error handling:
		// "Invalid prompt name: -32602 (Invalid params)".
		It(TEXT("refuses an unknown prompt with -32602"), [this]()
		{
			TestError(TEXT("unknown prompt"), Send(Request(54, TEXT("prompts/get"), TEXT("{\"name\":\"n2c-no-such-prompt\"}"))),
				JsonRpcErrorCodes::InvalidParams, 54, 200);
		});

		// mcp-transport-2: a null, array or object argument value hit a JSON writer check() and crashed the editor.
		// Prompt arguments are string to string (prompts/get params). The qw-15 guard is on dev-2.0, so this runs; if
		// it is ever removed, this case takes the test editor down instead of failing.
		It(TEXT("refuses null, array, object, number and boolean argument values with -32602"), [this]()
		{
			const TArray<FString> Values = { TEXT("null"), TEXT("[1,2]"), TEXT("{\"a\":1}"), TEXT("42"), TEXT("true") };
			int32 Id = 55;
			for (const FString& Value : Values)
			{
				TestError(FString::Printf(TEXT("argument %s"), *Value), Send(Request(Id, TEXT("prompts/get"),
					FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":{\"topic\":%s}}"), *TestPromptName, *Value))),
					JsonRpcErrorCodes::InvalidParams, Id, 200);
				++Id;
			}
		});

		It(TEXT("refuses a non-string argument of an unknown prompt without crashing"), [this]()
		{
			const FReply Reply = Send(Request(60, TEXT("prompts/get"), TEXT("{\"name\":\"n2c-no-such-prompt\",\"arguments\":{\"a\":null}}")));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			TestNotNull(TEXT("an error response"), ErrorOf(Reply).Get());
		});

		// Finding mcp-tools-python-translation-resources-23, fixed by card 0110: a missing required argument gave a
		// success with an empty messages array. Prompts, error handling: "Missing required arguments: -32602 (Invalid
		// params)".
		It(TEXT("refuses a missing required argument with -32602"), [this]()
		{
			TestError(TEXT("missing topic"), Send(Request(62, TEXT("prompts/get"), FString::Printf(TEXT("{\"name\":\"%s\"}"), *TestPromptName))),
				JsonRpcErrorCodes::InvalidParams, 62, 200);
		});
	});

	// Finding mcp-tools-python-translation-resources-23 (card 0110): a resource or prompt handler that timed out came
	// back as a success with an error text built with Printf (the uri unescaped). Resources and prompts, error
	// handling: "Internal errors: -32603". The managers now mark the result with -32603, which resources/read and
	// prompts/get answer with; the resource text is JSON built with FJsonObject. The handlers need the game thread and
	// are called from a worker while this test holds the game thread, so each wait (5 s) runs out.
	Describe(TEXT("handler timeouts"), [this]()
	{
		It(TEXT("mark a resource or prompt handler that timed out with -32603"), [this]()
		{
			const FString Uri = TEXT("n2ctest://slow/\"quoted\"");
			const FString PromptName = TEXT("n2c-test-protocol-slow-prompt");
			FMcpResourceDefinition Resource;
			Resource.Uri = Uri;
			Resource.Name = TEXT("slow");
			Resource.MimeType = TEXT("text/plain");
			const bool bResource = FN2CMcpResourceManager::Get().RegisterStaticResource(Resource,
				FMcpResourceReadDelegate::CreateLambda([](const FString& InUri)
				{
					FMcpResourceContents Contents;
					Contents.Uri = InUri;
					Contents.Text = TEXT("late");
					return Contents;
				}), /*bRequiresGameThread=*/true);
			FMcpPromptDefinition Prompt;
			Prompt.Name = PromptName;
			Prompt.Description = TEXT("Slow prompt registered by NodeToCode.MCP.Protocol");
			const bool bPrompt = FN2CMcpPromptManager::Get().RegisterPrompt(Prompt,
				FMcpPromptGetDelegate::CreateLambda([](const FMcpPromptArguments&) { return FMcpPromptResult(); }), /*bRequiresGameThread=*/true);
			if (!TestTrue(TEXT("slow resource and prompt registered"), bResource && bPrompt))
			{
				FN2CMcpResourceManager::Get().UnregisterStaticResource(Uri);
				FN2CMcpPromptManager::Get().UnregisterPrompt(PromptName);
				return;
			}

			// The managers log each timeout as an error
			AddExpectedMessage(TEXT("Timeout reading resource on game thread"), EAutomationExpectedMessageFlags::Contains, 1);
			AddExpectedMessage(TEXT("Timeout getting prompt on game thread"), EAutomationExpectedMessageFlags::Contains, 1);
			TFuture<FMcpResourceContents> Read = Async(EAsyncExecution::Thread, [Uri]() { return FN2CMcpResourceManager::Get().ReadResource(Uri); });
			TFuture<FMcpPromptResult> Get = Async(EAsyncExecution::Thread, [PromptName]()
			{
				return FN2CMcpPromptManager::Get().GetPrompt(PromptName, FMcpPromptArguments());
			});
			// Hold the game thread (without pumping its task queue) until both waits have run out
			const double Deadline = FPlatformTime::Seconds() + 15.0;
			while ((!Read.IsReady() || !Get.IsReady()) && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.05f);
			}
			if (TestTrue(TEXT("both calls returned"), Read.IsReady() && Get.IsReady()))
			{
				const FMcpResourceContents Contents = Read.Get();
				TestEqual(TEXT("resource: -32603"), Contents.ErrorCode, static_cast<int32>(JsonRpcErrorCodes::InternalError));
				TSharedPtr<FJsonObject> ErrorText;
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents.Text), ErrorText);
				FString TextUri;
				TestTrue(TEXT("resource: the error text is JSON carrying the uri"), ErrorText.IsValid()
					&& ErrorText->TryGetStringField(TEXT("uri"), TextUri) && TextUri == Uri);
				const FMcpPromptResult PromptResult = Get.Get();
				TestEqual(TEXT("prompt: -32603"), PromptResult.ErrorCode, static_cast<int32>(JsonRpcErrorCodes::InternalError));
				TestEqual(TEXT("prompt: no messages"), PromptResult.Messages.Num(), 0);
			}
			FN2CMcpResourceManager::Get().UnregisterStaticResource(Uri);
			FN2CMcpPromptManager::Get().UnregisterPrompt(PromptName);
		});
	});

	Describe(TEXT("notifications"), [this]()
	{
		It(TEXT("accept notifications/initialized with 202 and no body"), [this]()
		{
			TestAccepted(TEXT("initialized"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}")));
		});

		It(TEXT("accept notifications/cancelled and unknown notifications with 202 and no body"), [this]()
		{
			TestAccepted(TEXT("cancelled"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":99,\"reason\":\"spec\"}}")));
			TestAccepted(TEXT("unknown notification"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/n2c-no-such\"}")));
			// A message without an id is a notification even for a method that does not exist: never answered
			TestAccepted(TEXT("unknown method without id"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"n2c/no-such-method\"}")));
		});
	});

	Describe(TEXT("client responses"), [this]()
	{
		// Finding mcp-transport-19 (client responses part), fixed by card 0109. Transports, sending messages to the
		// server: "If the input is a JSON-RPC response or notification ... the server MUST return HTTP status code 202
		// Accepted with no body".
		It(TEXT("accept a result or error response with 202 and no body"), [this]()
		{
			TestAccepted(TEXT("client result"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":70,\"result\":{}}")));
			TestAccepted(TEXT("client error"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":71,\"error\":{\"code\":-1,\"message\":\"no\"}}")));
		});
	});

	Describe(TEXT("errors"), [this]()
	{
		It(TEXT("answer a body that is not JSON with -32700, a null id and 400"), [this]()
		{
			TestFalse(TEXT("body does not parse"), ParseBody(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,")).IsValid());
			const FReply Reply = SendValue(nullptr);
			TestFalse(TEXT("not processed"), Reply.bProcessed);
			TestError(TEXT("parse error"), Reply, JsonRpcErrorCodes::ParseError, -1, 400);
		});

		// UE's JSON reader accepts only an object or an array as the root, so a scalar body is a parse error on the wire
		It(TEXT("answer a scalar body with -32700, a null id and 400"), [this]()
		{
			TestFalse(TEXT("a number body does not parse"), ParseBody(TEXT("42")).IsValid());
			TestError(TEXT("number body"), SendValue(ParseBody(TEXT("42"))), JsonRpcErrorCodes::ParseError, -1, 400);
			TestError(TEXT("string body"), SendValue(ParseBody(TEXT("\"ping\""))), JsonRpcErrorCodes::ParseError, -1, 400);
		});

		It(TEXT("answer a parsed value that is neither an object nor an array with -32600, a null id and 400"), [this]()
		{
			TestError(TEXT("number"), SendValue(MakeShared<FJsonValueNumber>(42.0)), JsonRpcErrorCodes::InvalidRequest, -1, 400);
			TestError(TEXT("string"), SendValue(MakeShared<FJsonValueString>(TEXT("ping"))), JsonRpcErrorCodes::InvalidRequest, -1, 400);
		});

		It(TEXT("answer a message without jsonrpc 2.0 with -32600 and 400"), [this]()
		{
			TestError(TEXT("no jsonrpc"), Send(TEXT("{\"id\":80,\"method\":\"ping\"}")), JsonRpcErrorCodes::InvalidRequest, -1, 400);
			TestError(TEXT("jsonrpc 1.0"), Send(TEXT("{\"jsonrpc\":\"1.0\",\"id\":81,\"method\":\"ping\"}")), JsonRpcErrorCodes::InvalidRequest, -1, 400);
		});

		It(TEXT("answer a message with an id but no method, result or error with -32600 and 400"), [this]()
		{
			TestError(TEXT("no method"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":83}")), JsonRpcErrorCodes::InvalidRequest, -1, 400);
		});

		// JSON-RPC 2.0, request object: "method - A String containing the name of the method"; anything else is an
		// Invalid Request. FJsonObject::TryGetStringField would turn the number 5 into "5" (dispatched as an unknown
		// method before card 0108), so the core checks the member's type.
		It(TEXT("answer a message whose method is not a string with -32600, a null id and 400"), [this]()
		{
			TestError(TEXT("numeric method"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":82,\"method\":5}")),
				JsonRpcErrorCodes::InvalidRequest, -1, 400);
			TestError(TEXT("boolean method"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":85,\"method\":true}")),
				JsonRpcErrorCodes::InvalidRequest, -1, 400);
			TestError(TEXT("numeric method without id"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":5}")),
				JsonRpcErrorCodes::InvalidRequest, -1, 400);
		});

		It(TEXT("answer an unknown method with -32601, the request id and 200"), [this]()
		{
			const FReply Reply = Send(Request(84, TEXT("n2c/no-such-method")));
			TestTrue(TEXT("processed"), Reply.bProcessed);
			TestError(TEXT("unknown method"), Reply, JsonRpcErrorCodes::MethodNotFound, 84, 200);
		});
	});

	Describe(TEXT("ids"), [this]()
	{
		It(TEXT("echo a string id as a string and a number id as a number"), [this]()
		{
			const TSharedPtr<FJsonObject> StringReply = Message(Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":\"req-7\",\"method\":\"ping\"}")));
			FString StringId;
			TestTrue(TEXT("string id echoed"), StringReply.IsValid() && StringReply->TryGetStringField(TEXT("id"), StringId) && StringId == TEXT("req-7"));
			// A string that looks like a number stays a string
			const TSharedPtr<FJsonObject> NumericString = Message(Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":\"7\",\"method\":\"ping\"}")));
			TestTrue(TEXT("numeric string id stays a string"), NumericString.IsValid() && NumericString->HasTypedField<EJson::String>(TEXT("id")));
			TestId(TEXT("number id"), Send(Request(90, TEXT("ping"))), 90);
			const TSharedPtr<FJsonObject> Large = Message(Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2147483647,\"method\":\"ping\"}")));
			TestTrue(TEXT("large number id echoed"), Large.IsValid() && Large->GetNumberField(TEXT("id")) == 2147483647.0);
		});

		It(TEXT("echo the request id on an error response"), [this]()
		{
			const TSharedPtr<FJsonObject> Reply = Message(Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":\"err-1\",\"method\":\"tools/call\",\"params\":{}}")));
			FString Id;
			TestTrue(TEXT("string id on the error"), Reply.IsValid() && Reply->TryGetStringField(TEXT("id"), Id) && Id == TEXT("err-1"));
		});

		// Basic, requests: "Unlike base JSON-RPC, the ID MUST NOT be null", and JSON-RPC 2.0 allows only a string,
		// a number or null. A null, boolean or object id is an Invalid Request (before card 0108 a null id was answered
		// like any request).
		It(TEXT("refuse a request with a null, boolean or object id with -32600, a null id and 400"), [this]()
		{
			const FReply Reply = Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"ping\"}"));
			TestError(TEXT("null id"), Reply, JsonRpcErrorCodes::InvalidRequest, -1, 400);
			TestFalse(TEXT("null id: not processed"), Reply.bProcessed);
			TestError(TEXT("boolean id"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":true,\"method\":\"ping\"}")),
				JsonRpcErrorCodes::InvalidRequest, -1, 400);
			TestError(TEXT("object id"), Send(TEXT("{\"jsonrpc\":\"2.0\",\"id\":{\"a\":1},\"method\":\"ping\"}")),
				JsonRpcErrorCodes::InvalidRequest, -1, 400);
		});

		It(TEXT("answer a null id inside a batch with -32600 in its place"), [this]()
		{
			const FReply Reply = Send(FString::Printf(TEXT("[{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"ping\"},%s]"), *Request(91, TEXT("ping"))));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			if (TestTrue(TEXT("body is an array"), Reply.Json.IsValid() && Reply.Json->Type == EJson::Array)
				&& TestEqual(TEXT("two responses"), Reply.Json->AsArray().Num(), 2))
			{
				const TSharedPtr<FJsonObject> First = Reply.Json->AsArray()[0]->AsObject();
				const TSharedPtr<FJsonObject>* Error = nullptr;
				TestTrue(TEXT("null-id item is -32600"), First->TryGetObjectField(TEXT("error"), Error)
					&& static_cast<int32>((*Error)->GetNumberField(TEXT("code"))) == JsonRpcErrorCodes::InvalidRequest);
				TestEqual(TEXT("ping answered"), Reply.Json->AsArray()[1]->AsObject()->GetNumberField(TEXT("id")), 91.0);
			}
		});
	});

	// JSON-RPC batching was added in 2025-03-26 and removed in 2025-06-18 (changelog, PR #416). ProcessMessage answers
	// any batch; FN2CMcpProtocol::Handle accepts one only on a session negotiated at 2025-03-26 and refuses it with 400
	// and -32600 otherwise (card 0109). NodeToCode.MCP.Core.batches covers that rule.
	Describe(TEXT("batch"), [this]()
	{
		It(TEXT("answers a batch of two requests with two responses in order and 200"), [this]()
		{
			const FReply Reply = Send(FString::Printf(TEXT("[%s,%s]"), *Request(101, TEXT("ping")), *Request(102, TEXT("n2c/no-such-method"))));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			TestTrue(TEXT("processed"), Reply.bProcessed);
			if (!TestTrue(TEXT("body is an array"), Reply.Json.IsValid() && Reply.Json->Type == EJson::Array))
			{
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>& Items = Reply.Json->AsArray();
			if (TestEqual(TEXT("two responses"), Items.Num(), 2))
			{
				TestEqual(TEXT("first id"), Items[0]->AsObject()->GetNumberField(TEXT("id")), 101.0);
				TestTrue(TEXT("first is a result"), Items[0]->AsObject()->HasField(TEXT("result")));
				TestEqual(TEXT("second id"), Items[1]->AsObject()->GetNumberField(TEXT("id")), 102.0);
				const TSharedPtr<FJsonObject>* Error = nullptr;
				TestTrue(TEXT("second is -32601"), Items[1]->AsObject()->TryGetObjectField(TEXT("error"), Error)
					&& static_cast<int32>((*Error)->GetNumberField(TEXT("code"))) == JsonRpcErrorCodes::MethodNotFound);
			}
		});

		It(TEXT("answers a batch of notifications only with 202 and no body"), [this]()
		{
			TestAccepted(TEXT("notifications only"), Send(TEXT("[{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"},")
				TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":1}}]")));
		});

		It(TEXT("answers only the requests of a batch that mixes requests and notifications"), [this]()
		{
			const FReply Reply = Send(FString::Printf(TEXT("[{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"},%s]"), *Request(103, TEXT("ping"))));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			if (TestTrue(TEXT("body is an array"), Reply.Json.IsValid() && Reply.Json->Type == EJson::Array)
				&& TestEqual(TEXT("one response"), Reply.Json->AsArray().Num(), 1))
			{
				TestEqual(TEXT("its id"), Reply.Json->AsArray()[0]->AsObject()->GetNumberField(TEXT("id")), 103.0);
			}
		});

		It(TEXT("answers an invalid batch item with -32600 in its place and a client response item with nothing"), [this]()
		{
			const FReply Reply = Send(FString::Printf(TEXT("[1,%s,{\"jsonrpc\":\"2.0\",\"id\":5,\"result\":{}}]"), *Request(104, TEXT("ping"))));
			TestEqual(TEXT("HTTP status"), Reply.Status, 200);
			if (!TestTrue(TEXT("body is an array"), Reply.Json.IsValid() && Reply.Json->Type == EJson::Array))
			{
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>& Items = Reply.Json->AsArray();
			// A client response item is accepted and, like a notification, gets no entry (mcp-transport-19, card 0109)
			if (TestEqual(TEXT("two responses"), Items.Num(), 2))
			{
				const TSharedPtr<FJsonObject>* Error = nullptr;
				TestTrue(TEXT("non-object item is -32600"), Items[0]->AsObject()->TryGetObjectField(TEXT("error"), Error)
					&& static_cast<int32>((*Error)->GetNumberField(TEXT("code"))) == JsonRpcErrorCodes::InvalidRequest);
				TestTrue(TEXT("non-object item has a null id"), Items[0]->AsObject()->TryGetField(TEXT("id"))->IsNull());
				TestEqual(TEXT("ping answered"), Items[1]->AsObject()->GetNumberField(TEXT("id")), 104.0);
			}
		});

		It(TEXT("refuses an empty batch with -32600, a null id and 400"), [this]()
		{
			const FReply Reply = Send(TEXT("[]"));
			TestFalse(TEXT("not processed"), Reply.bProcessed);
			TestError(TEXT("empty batch"), Reply, JsonRpcErrorCodes::InvalidRequest, -1, 400);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
