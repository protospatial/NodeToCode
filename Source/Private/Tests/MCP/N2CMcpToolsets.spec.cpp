// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace N2CMcpToolsetsSpec
{
	const TCHAR* LongToolName = TEXT("n2c-test-toolsets-long");

	/** A long-running tool that reports progress once and finishes at once with the text argument it was given */
	class FLongTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(LongToolName, TEXT("Long-running spec tool registered by NodeToCode.MCP.Toolsets"));
			Definition.InputSchema = MakeShared<FJsonObject>();
			Definition.InputSchema->SetStringField(TEXT("type"), TEXT("object"));
			Definition.bIsLongRunning = true;
			return Definition;
		}

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments) override
		{
			++Invocations;
			FString Text;
			if (Arguments.IsValid())
			{
				Arguments->TryGetStringField(TEXT("text"), Text);
			}
			Context->ReportProgress(0.5f, TEXT("halfway"));
			return MakeFulfilledPromise<FMcpToolCallResult>(FMcpToolCallResult::CreateTextResult(TEXT("long: ") + Text)).GetFuture();
		}

		int32 Invocations = 0;
	};

	FString Request(int32 Id, const FString& Method, const FString& ParamsJson = FString())
	{
		return ParamsJson.IsEmpty()
			? FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\"}"), Id, *Method)
			: FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":%s}"), Id, *Method, *ParamsJson);
	}

	FString ToolCall(int32 Id, const FString& Tool, const FString& ArgumentsJson, const FString& MetaJson = FString())
	{
		return Request(Id, TEXT("tools/call"), FString::Printf(TEXT("{\"name\":\"%s\",\"arguments\":%s%s}"), *Tool, *ArgumentsJson,
			MetaJson.IsEmpty() ? TEXT("") : *(TEXT(",\"_meta\":") + MetaJson)));
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	FString Serialize(const TSharedPtr<FJsonValue>& Value)
	{
		FString Text;
		if (Value.IsValid())
		{
			FJsonSerializer::Serialize(Value, FString(), TJsonWriterFactory<>::Create(&Text));
		}
		return Text;
	}

	/** The text of a CallToolResult's content (empty when there is none) and its isError */
	FString ResultText(const TSharedPtr<FJsonObject>& Result, bool& bOutIsError)
	{
		bOutIsError = false;
		FString Text;
		if (!Result.IsValid())
		{
			return Text;
		}
		Result->TryGetBoolField(TEXT("isError"), bOutIsError);
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (Result->TryGetArrayField(TEXT("content"), Content))
		{
			for (const TSharedPtr<FJsonValue>& Item : *Content)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				FString Part;
				if (Item.IsValid() && Item->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("text"), Part))
				{
					Text += Part;
				}
			}
		}
		return Text;
	}
}

/**
 * Declared toolsets and the tool profile and exposure built on them (card 0165, decision 0005): tools/list is fixed for
 * the whole session in both exposures, the tool-search meta-tools (list_toolsets, describe_toolset, call_tool) reach
 * every served tool and never change tools/list, an unknown toolset is an isError result naming the valid ones,
 * call_tool runs a long-running tool on its own call, and the tool-mode switches saved before card 0165 map onto the
 * new settings. Each case builds its own tool manager and protocol core, so the session's MCP server is untouched.
 * Review ids: next-12, dx-docs-release-6, mcp-transport-4, mcp-tools-python-translation-resources-15.
 */
BEGIN_DEFINE_SPEC(FN2CMcpToolsetsSpec, "NodeToCode.MCP.Toolsets", N2C_TEST_FLAGS)

	TUniquePtr<FN2CMcpToolManager> Tools;
	TUniquePtr<FN2CMcpProtocol> Core;
	FString SessionId;
	int32 NextId = 100;

	/** Configures a fresh manager with the default toolsets and Exposure, and opens a session on a core that uses it */
	void Start(EN2CMcpToolExposure Exposure, const TArray<FString>& Toolsets = FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {}))
	{
		Core.Reset();
		Tools = MakeUnique<FN2CMcpToolManager>();
		FN2CMcpToolConfig Config;
		Config.Toolsets = Toolsets;
		Config.Exposure = Exposure;
		Tools->Configure(Config);

		FN2CMcpCatalogs Catalogs = FN2CMcpCatalogs::Default();
		Catalogs.Tools = Tools.Get();
		Core = MakeUnique<FN2CMcpProtocol>(Catalogs);

		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Init = Send(N2CMcpToolsetsSpec::Request(1, TEXT("initialize"),
			TEXT("{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-toolsets-spec\",\"version\":\"1.0\"}}")), false, FString());
		SessionId = Init->LastReply().SessionId;
		TestFalse(TEXT("initialize: a session"), SessionId.IsEmpty());
		Send(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"));
	}

	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(const FString& Body, bool bCanStream = false, const FString& InSessionId = TEXT("<session>"))
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(bCanStream);
		Core->Handle(N2CTest::MakeMcpInbound(Body, TEXT("application/json, text/event-stream"),
			InSessionId == TEXT("<session>") ? SessionId : InSessionId), Responder);
		return Responder;
	}

	/** The tools/list result's tools array as JSON text, and its names */
	FString ListTools(TArray<FString>* OutNames = nullptr)
	{
		const TSharedPtr<FJsonObject> Reply = N2CMcpToolsetsSpec::ParseObject(Send(N2CMcpToolsetsSpec::Request(NextId++, TEXT("tools/list")))->LastReply().Body);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
		if (!TestTrue(TEXT("tools/list: a result with tools"), Reply.IsValid() && Reply->TryGetObjectField(TEXT("result"), Result)
			&& (*Result)->TryGetArrayField(TEXT("tools"), ToolsArray)))
		{
			return FString();
		}
		if (OutNames)
		{
			for (const TSharedPtr<FJsonValue>& Tool : *ToolsArray)
			{
				OutNames->Add(Tool->AsObject()->GetStringField(TEXT("name")));
			}
		}
		return N2CMcpToolsetsSpec::Serialize(MakeShared<FJsonValueArray>(*ToolsArray));
	}

	/** tools/call Tool with ArgumentsJson: the result's text and isError (a JSON-RPC error fails the test) */
	FString Call(const FString& Tool, const FString& ArgumentsJson, bool& bOutIsError)
	{
		const TSharedPtr<FJsonObject> Reply = N2CMcpToolsetsSpec::ParseObject(Send(N2CMcpToolsetsSpec::ToolCall(NextId++, Tool, ArgumentsJson))->LastReply().Body);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		bOutIsError = true;
		if (!TestTrue(FString::Printf(TEXT("%s %s: a CallToolResult"), *Tool, *ArgumentsJson), Reply.IsValid() && Reply->TryGetObjectField(TEXT("result"), Result)))
		{
			return FString();
		}
		return N2CMcpToolsetsSpec::ResultText(*Result, bOutIsError);
	}

	/** The JSON-RPC error code of tools/call Tool (0 when it answered with a result) */
	int32 CallErrorCode(const FString& Tool, const FString& ArgumentsJson)
	{
		const TSharedPtr<FJsonObject> Reply = N2CMcpToolsetsSpec::ParseObject(Send(N2CMcpToolsetsSpec::ToolCall(NextId++, Tool, ArgumentsJson))->LastReply().Body);
		const TSharedPtr<FJsonObject>* Error = nullptr;
		int32 Code = 0;
		if (Reply.IsValid() && Reply->TryGetObjectField(TEXT("error"), Error))
		{
			(*Error)->TryGetNumberField(TEXT("code"), Code);
		}
		return Code;
	}

	/** The names list_toolsets lists ("- Name: Description" lines) */
	TArray<FString> ListedToolsets()
	{
		bool bIsError = false;
		const FString Text = Call(FN2CMcpToolManager::ListToolsetsName, TEXT("{}"), bIsError);
		TestFalse(TEXT("list_toolsets: not an error"), bIsError);
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		TArray<FString> Names;
		for (const FString& Line : Lines)
		{
			FString Name, Rest;
			if (Line.StartsWith(TEXT("- ")) && Line.RightChop(2).Split(TEXT(": "), &Name, &Rest))
			{
				Names.Add(Name);
			}
		}
		return Names;
	}

	/** Actual equals Expected, element by element (both lists are printed) */
	void ExpectNames(const FString& What, const TArray<FString>& Actual, const TArray<FString>& Expected)
	{
		TestTrue(FString::Printf(TEXT("%s: [%s] is [%s]"), *What, *FString::Join(Actual, TEXT(", ")), *FString::Join(Expected, TEXT(", "))),
			Actual == Expected);
	}

	/** Every registry tool whose toolset is enabled by Toolsets, minus the Python execution tools when they are off */
	TArray<FString> ExpectedServed(const TArray<FString>& Toolsets)
	{
		TArray<FString> Names;
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			const FMcpToolDefinition Def = Tool->GetDefinition();
			if (Def.Toolset && Toolsets.ContainsByPredicate([&Def](const FString& Name) { return Name.Equals(Def.Toolset->Name, ESearchCase::IgnoreCase); })
				&& !(UN2CUserSettings::IsPythonExecutionTool(Def.Name) && !UN2CUserSettings::IsMcpPythonExecutionAllowed()))
			{
				Names.Add(Def.Name);
			}
		}
		Names.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
		return Names;
	}

	/** A temp settings ini holding Lines under the settings section, loaded into GConfig; returns its path */
	FString WriteSettingsIni(const FString& Lines)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
			/ FString::Printf(TEXT("N2CToolsets_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)) / TEXT("DefaultNodeToCode.ini"));
		TestTrue(TEXT("fixture written"), FFileHelper::SaveStringToFile(
			FString::Printf(TEXT("[%s]\n%s"), *UN2CSettings::StaticClass()->GetPathName(), *Lines), *Path));
		GConfig->UnloadFile(Path);
		GConfig->LoadFile(Path);
		return Path;
	}

	/** Settings loaded from Path as the editor loads them, then the tool-mode migration run on Path */
	UN2CSettings* LoadAndMigrate(const FString& Path, TArray<FString>& OutChanges)
	{
		UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
		Settings->LoadConfig(nullptr, *Path);
		Settings->McpToolProfile = EN2CMcpToolProfile::DefaultToolsets;
		Settings->McpToolExposure = EN2CMcpToolExposure::ListAllTools;
		Settings->LoadConfig(nullptr, *Path);
		Settings->MigrateLegacyMcpToolModes(Path, OutChanges);
		return Settings;
	}

	void RemoveIni(const FString& Path)
	{
		GConfig->UnloadFile(Path);
		IFileManager::Get().DeleteDirectory(*FPaths::GetPath(Path), false, true);
	}

END_DEFINE_SPEC(FN2CMcpToolsetsSpec)

void FN2CMcpToolsetsSpec::Define()
{
	AfterEach([this]()
	{
		// The core subscribes to the manager: drop it first
		Core.Reset();
		Tools.Reset();
		SessionId.Reset();
		FN2CToolAsyncTaskManager::Get().SetToolResolverForTesting(nullptr);
	});

	Describe(TEXT("tool profiles"), [this]()
	{
		It(TEXT("enable every default toolset, PythonTools alone, or the custom names as given"), [this]()
		{
			TArray<FString> Defaults;
			for (const FN2CMcpToolset* Toolset : N2CMcpToolsets::All)
			{
				if (Toolset->bDefaultEnabled)
				{
					Defaults.Add(Toolset->Name);
				}
			}
			ExpectNames(TEXT("DefaultToolsets"), FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, { TEXT("Ignored") }), Defaults);
			ExpectNames(TEXT("PythonScripting"), FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::PythonScripting, {}), TArray<FString>{ TEXT("PythonTools") });
			const TArray<FString> Custom = { TEXT("TranslationTools"), TEXT("NoSuchTools") };
			ExpectNames(TEXT("Custom"), FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::Custom, Custom), Custom);
		});

		It(TEXT("serve exactly the tools of the enabled toolsets and report unknown toolset names"), [this]()
		{
			FN2CMcpToolManager Manager;
			FN2CMcpToolConfig Config;
			Config.Toolsets = { TEXT("translationtools"), TEXT("BlueprintCompileTools"), TEXT("NoSuchTools") };
			const TArray<FString> Unknown = Manager.Configure(Config);
			ExpectNames(TEXT("unknown names returned"), Unknown, TArray<FString>{ TEXT("NoSuchTools") });
			TArray<FString> Served;
			for (const FMcpToolDefinition& Def : Manager.GetAllToolDefinitions())
			{
				Served.Add(Def.Name);
			}
			ExpectNames(TEXT("served tools"), Served, ExpectedServed({ TEXT("TranslationTools"), TEXT("BlueprintCompileTools") }));
			TestTrue(TEXT("compile-blueprint served"), Served.Contains(TEXT("compile-blueprint")));
			TestFalse(TEXT("no meta-tool listed"), Served.Contains(FN2CMcpToolManager::CallToolName));
			ExpectNames(TEXT("GetConfig names the declared toolsets"), Manager.GetConfig().Toolsets,
				TArray<FString>{ TEXT("TranslationTools"), TEXT("BlueprintCompileTools") });
		});
	});

	Describe(TEXT("List Every Tool exposure"), [this]()
	{
		It(TEXT("lists every served tool and no meta-tool, and refuses a meta-tool call as an unknown tool"), [this]()
		{
			Start(EN2CMcpToolExposure::ListAllTools);
			TArray<FString> Names;
			const FString Before = ListTools(&Names);
			ExpectNames(TEXT("tools/list = the default toolsets' tools"), Names,
				ExpectedServed(FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {})));
			TestTrue(TEXT("translate-focused-blueprint listed"), Names.Contains(TEXT("translate-focused-blueprint")));
			TestTrue(TEXT("find-nodes-in-graph listed"), Names.Contains(TEXT("find-nodes-in-graph")));
			TestEqual(TEXT("list_toolsets is not a listed tool: -32602"), CallErrorCode(FN2CMcpToolManager::ListToolsetsName, TEXT("{}")), -32602);
			TestEqual(TEXT("tools/list unchanged"), ListTools(), Before);
		});
	});

	Describe(TEXT("Tool Search exposure"), [this]()
	{
		It(TEXT("lists only list_toolsets, describe_toolset and call_tool, kept loaded and with object schemas"), [this]()
		{
			Start(EN2CMcpToolExposure::ToolSearch);
			TArray<FString> Names;
			ListTools(&Names);
			ExpectNames(TEXT("tools/list"), Names, TArray<FString>{ TEXT("call_tool"), TEXT("describe_toolset"), TEXT("list_toolsets") });
			for (const FMcpToolDefinition& Def : Tools->GetAllToolDefinitions())
			{
				TestTrue(FString::Printf(TEXT("%s: alwaysLoad"), *Def.Name), Def.bAlwaysLoad);
				FString Type;
				TestTrue(FString::Printf(TEXT("%s: object schema"), *Def.Name), Def.InputSchema.IsValid()
					&& Def.InputSchema->TryGetStringField(TEXT("type"), Type) && Type == TEXT("object"));
			}
		});

		It(TEXT("keeps tools/list identical after every meta-tool is called"), [this]()
		{
			Start(EN2CMcpToolExposure::ToolSearch);
			const FString Before = ListTools();

			const TArray<FString> Toolsets = ListedToolsets();
			ExpectNames(TEXT("list_toolsets lists every default toolset"), Toolsets,
				FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {}));
			TestEqual(TEXT("after list_toolsets"), ListTools(), Before);

			TArray<FString> Described;
			for (const FString& Toolset : Toolsets)
			{
				bool bIsError = true;
				const TSharedPtr<FJsonObject> Json = N2CMcpToolsetsSpec::ParseObject(
					Call(FN2CMcpToolManager::DescribeToolsetName, FString::Printf(TEXT("{\"toolset_name\":\"%s\"}"), *Toolset), bIsError));
				TestFalse(FString::Printf(TEXT("describe_toolset %s: not an error"), *Toolset), bIsError);
				const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
				if (TestTrue(FString::Printf(TEXT("describe_toolset %s: JSON with tools"), *Toolset), Json.IsValid()
					&& Json->GetStringField(TEXT("name")) == Toolset && Json->TryGetArrayField(TEXT("tools"), ToolsArray) && ToolsArray->Num() > 0))
				{
					for (const TSharedPtr<FJsonValue>& Tool : *ToolsArray)
					{
						Described.Add(Tool->AsObject()->GetStringField(TEXT("name")));
						TestTrue(FString::Printf(TEXT("%s: inputSchema"), *Described.Last()), Tool->AsObject()->HasTypedField<EJson::Object>(TEXT("inputSchema")));
					}
				}
				TestEqual(FString::Printf(TEXT("after describe_toolset %s"), *Toolset), ListTools(), Before);
			}
			Described.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
			ExpectNames(TEXT("the toolsets describe every served tool"), Described,
				ExpectedServed(FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {})));

			bool bIsError = true;
			const FString Targets = Call(FN2CMcpToolManager::CallToolName,
				TEXT("{\"toolset_name\":\"TranslationTools\",\"tool_name\":\"get-available-translation-targets\"}"), bIsError);
			TestFalse(TEXT("call_tool get-available-translation-targets: not an error"), bIsError);
			TestTrue(TEXT("call_tool returns the tool's result"), Targets.Contains(TEXT("C++")) || Targets.Contains(TEXT("cpp")));
			TestEqual(TEXT("after call_tool"), ListTools(), Before);

			// A second client sees the same list
			Send(N2CMcpToolsetsSpec::Request(1, TEXT("initialize"),
				TEXT("{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"second\",\"version\":\"1.0\"}}")), false, FString());
			TestEqual(TEXT("after a second client initialized"), ListTools(), Before);
		});

		It(TEXT("answers an unknown toolset with isError naming the valid toolsets"), [this]()
		{
			Start(EN2CMcpToolExposure::ToolSearch);
			const TArray<FString> Valid = ListedToolsets();
			for (const FString& Tool : { FString(FN2CMcpToolManager::DescribeToolsetName), FString(FN2CMcpToolManager::CallToolName) })
			{
				bool bIsError = false;
				const FString Text = Call(Tool, TEXT("{\"toolset_name\":\"Blueprint Graph Editing\",\"tool_name\":\"connect-pins\"}"), bIsError);
				TestTrue(FString::Printf(TEXT("%s: isError"), *Tool), bIsError);
				TestTrue(FString::Printf(TEXT("%s: names the unknown toolset"), *Tool), Text.Contains(TEXT("'Blueprint Graph Editing'")));
				for (const FString& Name : Valid)
				{
					TestTrue(FString::Printf(TEXT("%s: lists %s"), *Tool, *Name), Text.Contains(Name));
				}
			}
		});

		It(TEXT("answers a toolset the profile does not enable with isError naming the enabled ones"), [this]()
		{
			Start(EN2CMcpToolExposure::ToolSearch, { TEXT("TranslationTools") });
			bool bIsError = false;
			const FString Text = Call(FN2CMcpToolManager::DescribeToolsetName, TEXT("{\"toolset_name\":\"BlueprintGraphTools\"}"), bIsError);
			TestTrue(TEXT("isError"), bIsError);
			TestTrue(TEXT("says it is not enabled"), Text.Contains(TEXT("not enabled")));
			TestTrue(TEXT("names TranslationTools"), Text.Contains(TEXT("TranslationTools")));
			Call(FN2CMcpToolManager::CallToolName, TEXT("{\"tool_name\":\"connect-pins\"}"), bIsError);
			TestTrue(TEXT("call_tool of a tool the profile does not serve: isError"), bIsError);
		});

		It(TEXT("refuses call_tool misuse with isError: a tool outside the toolset, no tool_name, a meta-tool, bad arguments"), [this]()
		{
			Start(EN2CMcpToolExposure::ToolSearch);
			bool bIsError = false;
			FString Text = Call(FN2CMcpToolManager::CallToolName, TEXT("{\"toolset_name\":\"TranslationTools\",\"tool_name\":\"connect-pins\"}"), bIsError);
			TestTrue(TEXT("wrong toolset: isError"), bIsError);
			TestTrue(TEXT("wrong toolset: lists the toolset's tools"), Text.Contains(TEXT("translate-focused-blueprint")));
			Call(FN2CMcpToolManager::CallToolName, TEXT("{}"), bIsError);
			TestTrue(TEXT("no tool_name: isError"), bIsError);
			Call(FN2CMcpToolManager::CallToolName, TEXT("{\"tool_name\":\"call_tool\"}"), bIsError);
			TestTrue(TEXT("call_tool on itself: isError"), bIsError);
			Call(FN2CMcpToolManager::CallToolName, TEXT("{\"tool_name\":\"get-available-translation-targets\",\"arguments\":[1]}"), bIsError);
			TestTrue(TEXT("arguments not an object: isError"), bIsError);
			Text = Call(FN2CMcpToolManager::CallToolName, TEXT("{\"tool_name\":\"get-available-translation-targets\"}"), bIsError);
			TestFalse(TEXT("toolset_name may be omitted"), bIsError);
		});

		It(TEXT("refuses a direct tools/call of a tool that tools/list does not hold"), [this]()
		{
			Start(EN2CMcpToolExposure::ToolSearch);
			TestEqual(TEXT("-32602"), CallErrorCode(TEXT("get-available-translation-targets"), TEXT("{}")), -32602);
		});

		It(TEXT("runs a long-running tool named by call_tool on its own call, with progress, and the inner arguments"), [this]()
		{
			Start(EN2CMcpToolExposure::ToolSearch);
			const TSharedRef<N2CMcpToolsetsSpec::FLongTool> Long = MakeShared<N2CMcpToolsetsSpec::FLongTool>();
			TestTrue(TEXT("long tool served"), Tools->RegisterTool(Long));
			FN2CToolAsyncTaskManager::Get().SetToolResolverForTesting([Long](const FString& Name) -> TSharedPtr<IN2CMcpTool>
			{
				return Name == N2CMcpToolsetsSpec::LongToolName ? TSharedPtr<IN2CMcpTool>(Long) : nullptr;
			});

			const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(N2CMcpToolsetsSpec::ToolCall(NextId++, FN2CMcpToolManager::CallToolName,
				FString::Printf(TEXT("{\"tool_name\":\"%s\",\"arguments\":{\"text\":\"inner\"}}"), N2CMcpToolsetsSpec::LongToolName),
				TEXT("{\"progressToken\":\"tok-ct\"}")), /*bCanStream=*/true);
			TestEqual(TEXT("the long tool ran once"), Long->Invocations, 1);
			if (TestEqual(TEXT("progress and the response were streamed"), Responder->Streamed.Num(), 2))
			{
				const TSharedPtr<FJsonObject> Progress = N2CMcpToolsetsSpec::ParseObject(Responder->Streamed[0]);
				TestTrue(TEXT("notifications/progress with the call's token"), Progress.IsValid()
					&& Progress->GetStringField(TEXT("method")) == TEXT("notifications/progress")
					&& Progress->GetObjectField(TEXT("params"))->GetStringField(TEXT("progressToken")) == TEXT("tok-ct"));
				const TSharedPtr<FJsonObject> Response = N2CMcpToolsetsSpec::ParseObject(Responder->Streamed[1]);
				bool bIsError = true;
				const FString Text = Response.IsValid() ? N2CMcpToolsetsSpec::ResultText(Response->GetObjectField(TEXT("result")), bIsError) : FString();
				TestEqual(TEXT("the inner tool's result, from the inner arguments"), Text, FString(TEXT("long: inner")));
				TestFalse(TEXT("not an error"), bIsError);
			}
			TestEqual(TEXT("closed"), Responder->CloseCount, 1);
		});
	});

	Describe(TEXT("tool-mode settings saved before card 0165"), [this]()
	{
		It(TEXT("map Enable Dynamic Tool Discovery to Tool Search"), [this]()
		{
			const FString Ini = WriteSettingsIni(TEXT("bEnableDynamicToolDiscovery=True\nbEnablePythonScriptOnlyMode=False\n"));
			TArray<FString> Changes;
			const UN2CSettings* Settings = LoadAndMigrate(Ini, Changes);
			TestTrue(TEXT("Tool Search"), Settings->McpToolExposure == EN2CMcpToolExposure::ToolSearch);
			TestTrue(TEXT("Default toolsets"), Settings->McpToolProfile == EN2CMcpToolProfile::DefaultToolsets);
			TestEqual(TEXT("one change reported"), Changes.Num(), 1);
			RemoveIni(Ini);
		});

		It(TEXT("map Enable Python Script-Only Mode to Python Scripting Only, which wins over dynamic discovery"), [this]()
		{
			const FString Ini = WriteSettingsIni(TEXT("bEnableDynamicToolDiscovery=True\nbEnablePythonScriptOnlyMode=True\n"));
			TArray<FString> Changes;
			const UN2CSettings* Settings = LoadAndMigrate(Ini, Changes);
			TestTrue(TEXT("Python Scripting Only"), Settings->McpToolProfile == EN2CMcpToolProfile::PythonScripting);
			TestTrue(TEXT("tools listed directly, as script-only mode did"), Settings->McpToolExposure == EN2CMcpToolExposure::ListAllTools);
			TestEqual(TEXT("one change reported"), Changes.Num(), 1);
			RemoveIni(Ini);
		});

		It(TEXT("leave a saved new setting alone, and change nothing when run again"), [this]()
		{
			const FString Ini = WriteSettingsIni(TEXT("bEnableDynamicToolDiscovery=True\nbEnablePythonScriptOnlyMode=True\nMcpToolExposure=ListAllTools\nMcpToolProfile=Custom\n"));
			TArray<FString> Changes;
			UN2CSettings* Settings = LoadAndMigrate(Ini, Changes);
			TestTrue(TEXT("saved profile kept"), Settings->McpToolProfile == EN2CMcpToolProfile::Custom);
			TestTrue(TEXT("saved exposure kept"), Settings->McpToolExposure == EN2CMcpToolExposure::ListAllTools);
			TestEqual(TEXT("nothing changed"), Changes.Num(), 0);
			RemoveIni(Ini);

			const FString Old = WriteSettingsIni(TEXT("bEnableDynamicToolDiscovery=True\n"));
			TArray<FString> First, Second;
			Settings = LoadAndMigrate(Old, First);
			Settings->MigrateLegacyMcpToolModes(Old, Second);
			TestEqual(TEXT("first run changes the exposure"), First.Num(), 1);
			TestEqual(TEXT("second run changes nothing"), Second.Num(), 0);
			RemoveIni(Old);
		});

		It(TEXT("keep the defaults when neither switch was on"), [this]()
		{
			const FString Ini = WriteSettingsIni(TEXT("bEnableDynamicToolDiscovery=False\nbEnablePythonScriptOnlyMode=False\n"));
			TArray<FString> Changes;
			const UN2CSettings* Settings = LoadAndMigrate(Ini, Changes);
			TestTrue(TEXT("Default toolsets"), Settings->McpToolProfile == EN2CMcpToolProfile::DefaultToolsets);
			TestTrue(TEXT("List every tool"), Settings->McpToolExposure == EN2CMcpToolExposure::ListAllTools);
			TestEqual(TEXT("nothing changed"), Changes.Num(), 0);
			RemoveIni(Ini);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
