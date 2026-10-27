// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpEpicOverlap.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Core/N2CSettings.h"
#include "Core/N2CUserSettings.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace N2CMcpEpicOverlapSpec
{
	/** The review's overlap matrix, verdict "deprecate" (review.json, lane ue58-toolsets-overlap), minus assess-needed-tools (removed by card 0165) */
	const TArray<FString> MatrixDeprecated = {
		TEXT("list-blueprint-functions"), TEXT("list-overridable-functions"), TEXT("search-blueprint-nodes"),
		TEXT("add-bp-node-to-active-graph"), TEXT("set-input-pin-value"), TEXT("create-blueprint-function"),
		TEXT("add-function-input-pin"), TEXT("add-function-return-pin"), TEXT("remove-function-entry-pin"),
		TEXT("remove-function-return-pin"), TEXT("create-get-member-variable-node"), TEXT("create-set-member-variable-node"),
		TEXT("create-get-local-function-variable-node"), TEXT("create-set-local-function-variable-node"),
		TEXT("get-blueprint-member-variables"), TEXT("get-blueprint-function-local-variables"),
		TEXT("set-member-variable-default-value"), TEXT("compile-blueprint"), TEXT("save-blueprint"),
		TEXT("create-blueprint-class"), TEXT("search-blueprint-classes"), TEXT("add-component-class-to-actor"),
		TEXT("delete-component-in-actor"), TEXT("list-components-in-actor"), TEXT("copy-asset"), TEXT("create-folder"),
		TEXT("move-asset"), TEXT("move-folder"), TEXT("rename-asset"), TEXT("rename-folder"), TEXT("open-blueprint-asset"),
		TEXT("open-content-browser-path"), TEXT("read-content-browser-path"), TEXT("search-content-browser") };

	/** Tools only NodeToCode has, or does better (keep-unique / keep-better / differentiate): never left to Unreal MCP */
	const TArray<FString> Kept = {
		TEXT("get-focused-blueprint"), TEXT("get-blueprint-n2cjson"), TEXT("export-n2cjson-batch"), TEXT("get-open-blueprint-editors"), TEXT("get-editor-context"),
		TEXT("list-blueprint-graphs"), TEXT("connect-pins"), TEXT("create-comment-node"), TEXT("delete-blueprint-node"),
		TEXT("find-nodes-in-graph"), TEXT("delete-blueprint-function"), TEXT("open-blueprint-function"), TEXT("create-variable"),
		TEXT("create-local-variable"), TEXT("search-variable-types"), TEXT("set-local-function-variable-default-value"),
		TEXT("tag-blueprint-graph"), TEXT("list-blueprint-tags"), TEXT("remove-tag-from-graph"), TEXT("read-file"), TEXT("read-path"),
		TEXT("translate-focused-blueprint"), TEXT("translate-batch"), TEXT("get-available-llm-providers"),
		TEXT("get-available-translation-targets"), TEXT("get-translation-output-directory"), TEXT("list-python-scripts") };

	/** The toolsets whose every tool overlaps: list_toolsets drops them when the overlapping tools are left out */
	const TArray<FString> WhollyOverlapping = {
		TEXT("BlueprintCompileTools"), TEXT("BlueprintClassTools"), TEXT("BlueprintComponentTools"), TEXT("ContentBrowserTools") };

	N2CMcpEpicOverlap::FEnvironment Env(bool bEngine, bool bMcp, bool bEditorToolset)
	{
		N2CMcpEpicOverlap::FEnvironment Environment;
		Environment.bEngineHasUnrealMcp = bEngine;
		Environment.bUnrealMcpEnabled = bMcp;
		Environment.bEditorToolsetEnabled = bEditorToolset;
		return Environment;
	}

	const TCHAR* SettingName(EN2CMcpOverlappingTools Setting)
	{
		switch (Setting)
		{
		case EN2CMcpOverlappingTools::LeaveToUnrealMcp: return TEXT("LeaveToUnrealMcp");
		case EN2CMcpOverlappingTools::AlwaysServe: return TEXT("AlwaysServe");
		default: return TEXT("Automatic");
		}
	}

	const TCHAR* ProfileName(EN2CMcpToolProfile Profile)
	{
		switch (Profile)
		{
		case EN2CMcpToolProfile::PythonScripting: return TEXT("PythonScripting");
		case EN2CMcpToolProfile::Custom: return TEXT("Custom");
		default: return TEXT("DefaultToolsets");
		}
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	TArray<FString> Sorted(TArray<FString> Names)
	{
		Names.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
		return Names;
	}

	TArray<FString> ServedNames(const FN2CMcpToolManager& Manager)
	{
		TArray<FString> Names;
		for (const FMcpToolDefinition& Def : Manager.GetAllToolDefinitions())
		{
			Names.Add(Def.Name);
		}
		return Names;
	}

	/** The names list_toolsets lists ("- Name: Description" lines) */
	TArray<FString> ListedToolsets(const FN2CMcpToolManager& Manager)
	{
		TArray<FString> Lines;
		Manager.ListToolsets().ParseIntoArrayLines(Lines);
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
}

/**
 * The tools Epic's Unreal MCP also has on UE 5.8 (card 0174, decision 0089 applied provisionally as option b): which
 * they are, when the MCP server leaves them to Unreal MCP (engine, plugins, Overlapping Tools, tool profile, and
 * NodeToCode's default), what is served then in both exposures, what a client that calls one is told, and that a
 * saved Overlapping Tools value loads as saved. Each case builds its own tool manager, so the session's MCP server is
 * untouched. Review ids: rec-ue58-toolsets-overlap-6, imp-mcp-transport-2.
 */
BEGIN_DEFINE_SPEC(FN2CMcpEpicOverlapSpec, "NodeToCode.MCP.EpicOverlap", N2C_TEST_FLAGS)

	/** Actual equals Expected, element by element (both lists are printed) */
	void ExpectNames(const FString& What, const TArray<FString>& Actual, const TArray<FString>& Expected)
	{
		TestTrue(FString::Printf(TEXT("%s: [%s] is [%s]"), *What, *FString::Join(Actual, TEXT(", ")), *FString::Join(Expected, TEXT(", "))),
			Actual == Expected);
	}

	/** Every registry tool of the default toolsets, minus the Python execution tools when they are off */
	TArray<FString> DefaultServed()
	{
		const TArray<FString> Toolsets = FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {});
		TArray<FString> Names;
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			const FMcpToolDefinition Def = Tool->GetDefinition();
			if (Def.Toolset && Toolsets.Contains(FString(Def.Toolset->Name))
				&& !(UN2CUserSettings::IsPythonExecutionTool(Def.Name) && !UN2CUserSettings::IsMcpPythonExecutionAllowed()))
			{
				Names.Add(Def.Name);
			}
		}
		return N2CMcpEpicOverlapSpec::Sorted(Names);
	}

	void Configure(FN2CMcpToolManager& Manager, bool bLeave, EN2CMcpToolExposure Exposure = EN2CMcpToolExposure::ListAllTools)
	{
		FN2CMcpToolConfig Config;
		Config.Toolsets = FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {});
		Config.Exposure = Exposure;
		Config.bLeaveOverlappingToUnrealMcp = bLeave;
		Manager.Configure(Config);
	}

END_DEFINE_SPEC(FN2CMcpEpicOverlapSpec)

void FN2CMcpEpicOverlapSpec::Define()
{
	using namespace N2CMcpEpicOverlapSpec;

	Describe(TEXT("the overlapping tools"), [this]()
	{
		It(TEXT("are the 34 tools the review's overlap matrix deprecates, each a registered tool with an Epic counterpart"), [this]()
		{
			TArray<FString> Listed;
			for (const N2CMcpEpicOverlap::FOverlap& Overlap : N2CMcpEpicOverlap::Tools)
			{
				TestFalse(FString::Printf(TEXT("%s listed once"), Overlap.Tool), Listed.Contains(FString(Overlap.Tool)));
				Listed.Add(Overlap.Tool);
				TestTrue(FString::Printf(TEXT("%s: an Epic counterpart"), Overlap.Tool), FCString::Strlen(Overlap.EpicCounterpart) > 0);
				const bool bRegistered = FN2CMcpToolRegistry::Get().GetTools().ContainsByPredicate([&Overlap](const TSharedPtr<IN2CMcpTool>& Tool)
				{
					return Tool.IsValid() && Tool->GetDefinition().Name == Overlap.Tool;
				});
				TestTrue(FString::Printf(TEXT("%s is a registered tool"), Overlap.Tool), bRegistered);
			}
			ExpectNames(TEXT("overlapping tools"), Sorted(Listed), Sorted(MatrixDeprecated));
			TestEqual(TEXT("34 tools"), Listed.Num(), 34);
			for (const FString& Name : Kept)
			{
				TestNull(FString::Printf(TEXT("%s is NodeToCode's own"), *Name), N2CMcpEpicOverlap::FindEpicCounterpart(Name));
			}
			TestEqual(TEXT("FindEpicCounterpart(copy-asset)"), FString(N2CMcpEpicOverlap::FindEpicCounterpart(TEXT("copy-asset"))), FString(TEXT("AssetTools.duplicate")));
		});
	});

	Describe(TEXT("the decision"), [this]()
	{
		It(TEXT("leaves them to Unreal MCP only on UE 5.8 with ModelContextProtocol and EditorToolset enabled"), [this]()
		{
			const EN2CMcpOverlappingTools Settings[] = { EN2CMcpOverlappingTools::Automatic, EN2CMcpOverlappingTools::LeaveToUnrealMcp, EN2CMcpOverlappingTools::AlwaysServe };
			const EN2CMcpToolProfile Profiles[] = { EN2CMcpToolProfile::DefaultToolsets, EN2CMcpToolProfile::PythonScripting, EN2CMcpToolProfile::Custom };
			int32 Cases = 0;
			for (int32 Bits = 0; Bits < 8; ++Bits)
			{
				const bool bEngine = (Bits & 1) != 0, bMcp = (Bits & 2) != 0, bEditorToolset = (Bits & 4) != 0;
				for (const EN2CMcpOverlappingTools Setting : Settings)
				{
					for (const EN2CMcpToolProfile Profile : Profiles)
					{
						for (const bool bDefault : { true, false })
						{
							const N2CMcpEpicOverlap::FDecision Decision = N2CMcpEpicOverlap::Decide(Setting, Profile, Env(bEngine, bMcp, bEditorToolset), bDefault);
							bool bExpected = false;
							if (bEngine && bMcp && bEditorToolset)
							{
								bExpected = Setting == EN2CMcpOverlappingTools::LeaveToUnrealMcp
									|| (Setting == EN2CMcpOverlappingTools::Automatic && bDefault && Profile == EN2CMcpToolProfile::DefaultToolsets);
							}
							TestEqual(FString::Printf(TEXT("engine %d, ModelContextProtocol %d, EditorToolset %d, %s, %s, default %d (%s)"),
								bEngine, bMcp, bEditorToolset, SettingName(Setting), ProfileName(Profile), bDefault, *Decision.Reason),
								Decision.bLeaveToUnrealMcp, bExpected);
							TestFalse(TEXT("a reason for the log"), Decision.Reason.IsEmpty());
							++Cases;
						}
					}
				}
			}
			TestEqual(TEXT("every combination"), Cases, 8 * 3 * 3 * 2);
		});

		It(TEXT("names what is missing and the user's choice in its reason"), [this]()
		{
			TestTrue(TEXT("5.6/5.7"), N2CMcpEpicOverlap::Decide(EN2CMcpOverlappingTools::LeaveToUnrealMcp, EN2CMcpToolProfile::DefaultToolsets, Env(false, true, true)).Reason.Contains(TEXT("no Unreal MCP")));
			TestTrue(TEXT("plugins off"), N2CMcpEpicOverlap::Decide(EN2CMcpOverlappingTools::Automatic, EN2CMcpToolProfile::DefaultToolsets, Env(true, false, false)).Reason.Contains(TEXT("ModelContextProtocol and EditorToolset are not enabled")));
			TestTrue(TEXT("EditorToolset off"), N2CMcpEpicOverlap::Decide(EN2CMcpOverlappingTools::Automatic, EN2CMcpToolProfile::DefaultToolsets, Env(true, true, false)).Reason.Contains(TEXT("EditorToolset is not enabled")));
			TestTrue(TEXT("Always serve"), N2CMcpEpicOverlap::Decide(EN2CMcpOverlappingTools::AlwaysServe, EN2CMcpToolProfile::DefaultToolsets, Env(true, true, true)).Reason.Contains(TEXT("Always serve")));
			TestTrue(TEXT("Custom profile"), N2CMcpEpicOverlap::Decide(EN2CMcpOverlappingTools::Automatic, EN2CMcpToolProfile::Custom, Env(true, true, true)).Reason.Contains(TEXT("Tool Profile")));
		});

		It(TEXT("is on by default (decision 0089 applied provisionally as option b)"), [this]()
		{
			TestTrue(TEXT("bLeaveToUnrealMcpByDefault"), N2CMcpEpicOverlap::bLeaveToUnrealMcpByDefault);
			TestTrue(TEXT("UE 5.8, both plugins, defaults: left to Unreal MCP"), N2CMcpEpicOverlap::Decide(
				EN2CMcpOverlappingTools::Automatic, EN2CMcpToolProfile::DefaultToolsets, Env(true, true, true)).bLeaveToUnrealMcp);
		});

		It(TEXT("reads this editor: UE 5.8 or later, and IPluginManager's enabled plugins"), [this]()
		{
			const N2CMcpEpicOverlap::FEnvironment Current = N2CMcpEpicOverlap::FEnvironment::Current();
			TestEqual(TEXT("engine"), Current.bEngineHasUnrealMcp, !UE_VERSION_OLDER_THAN(5, 8, 0));
			TestEqual(TEXT("ModelContextProtocol"), Current.bUnrealMcpEnabled, IPluginManager::Get().FindEnabledPlugin(TEXT("ModelContextProtocol")).IsValid());
			TestEqual(TEXT("EditorToolset"), Current.bEditorToolsetEnabled, IPluginManager::Get().FindEnabledPlugin(TEXT("EditorToolset")).IsValid());
#if UE_VERSION_OLDER_THAN(5, 8, 0)
			TestFalse(TEXT("UE 5.6/5.7: never left out, whatever the setting"), N2CMcpEpicOverlap::Decide(
				EN2CMcpOverlappingTools::LeaveToUnrealMcp, EN2CMcpToolProfile::DefaultToolsets, Current).bLeaveToUnrealMcp);
#endif
		});
	});

	Describe(TEXT("the served tools"), [this]()
	{
		It(TEXT("are every default tool but the 34 when left to Unreal MCP, and every default tool otherwise"), [this]()
		{
			FN2CMcpToolManager Serving;
			Configure(Serving, false);
			ExpectNames(TEXT("not left out: every default tool"), ServedNames(Serving), DefaultServed());
			TestEqual(TEXT("not left out: nothing left"), Serving.GetToolsLeftToUnrealMcp().Num(), 0);

			FN2CMcpToolManager Leaving;
			Configure(Leaving, true);
			TArray<FString> Expected = DefaultServed();
			Expected.RemoveAll([](const FString& Name) { return N2CMcpEpicOverlap::FindEpicCounterpart(Name) != nullptr; });
			ExpectNames(TEXT("left out: the default tools minus the overlapping ones"), ServedNames(Leaving), Expected);
			ExpectNames(TEXT("GetToolsLeftToUnrealMcp"), Leaving.GetToolsLeftToUnrealMcp(), Sorted(MatrixDeprecated));
			TestEqual(TEXT("34 fewer tools"), ServedNames(Serving).Num() - ServedNames(Leaving).Num(), 34);
			for (const FString& Name : Kept)
			{
				TestTrue(FString::Printf(TEXT("%s still served"), *Name), Leaving.IsToolRegistered(Name));
			}
			TestTrue(TEXT("GetConfig says so"), Leaving.GetConfig().bLeaveOverlappingToUnrealMcp);
		});

		It(TEXT("drop the wholly overlapping toolsets from list_toolsets and explain them in describe_toolset and call_tool"), [this]()
		{
			FN2CMcpToolManager Manager;
			Configure(Manager, true, EN2CMcpToolExposure::ToolSearch);
			TArray<FString> Expected = FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {});
			Expected.RemoveAll([](const FString& Name) { return WhollyOverlapping.Contains(Name); });
			ExpectNames(TEXT("list_toolsets"), ListedToolsets(Manager), Expected);

			const TValueOrError<FString, FString> Described = Manager.DescribeToolset(TEXT("ContentBrowserTools"));
			if (TestTrue(TEXT("describe_toolset ContentBrowserTools: an error"), Described.HasError()))
			{
				TestTrue(TEXT("... saying it is left to Unreal MCP"), Described.GetError().Contains(TEXT("left to Unreal MCP")));
				TestTrue(TEXT("... with each tool's counterpart"), Described.GetError().Contains(TEXT("copy-asset (AssetTools.duplicate)")));
				TestTrue(TEXT("... and the setting"), Described.GetError().Contains(TEXT("Overlapping Tools")));
			}

			const TValueOrError<FString, FString> Graph = Manager.DescribeToolset(TEXT("BlueprintGraphTools"));
			if (TestTrue(TEXT("describe_toolset BlueprintGraphTools: its own tools"), Graph.HasValue()))
			{
				TestTrue(TEXT("connect-pins described"), Graph.GetValue().Contains(TEXT("\"connect-pins\"")));
				TestFalse(TEXT("set-input-pin-value not described"), Graph.GetValue().Contains(TEXT("\"set-input-pin-value\"")));
			}

			FString ToolName, Error;
			TSharedPtr<FJsonObject> Arguments;
			TestFalse(TEXT("call_tool copy-asset: refused"), Manager.ResolveCallTool(ParseObject(TEXT("{\"tool_name\":\"copy-asset\"}")), ToolName, Arguments, Error));
			TestTrue(FString::Printf(TEXT("call_tool copy-asset names AssetTools.duplicate: %s"), *Error), Error.Contains(TEXT("AssetTools.duplicate")));
			TestTrue(TEXT("call_tool no-such-tool: the plain unknown-tool text"), !Manager.ResolveCallTool(ParseObject(TEXT("{\"tool_name\":\"no-such-tool\"}")), ToolName, Arguments, Error)
				&& Error.StartsWith(TEXT("Unknown tool")));
		});

		It(TEXT("answer tools/call of a left-out tool with -32602 naming the Epic tool"), [this]()
		{
			FN2CMcpToolManager Manager;
			Configure(Manager, true);
			TestTrue(TEXT("hint for copy-asset"), Manager.LeftToUnrealMcpHint(TEXT("copy-asset")).Contains(TEXT("AssetTools.duplicate")));
			TestTrue(TEXT("no hint for a served tool"), Manager.LeftToUnrealMcpHint(TEXT("connect-pins")).IsEmpty());

			FN2CMcpCatalogs Catalogs = FN2CMcpCatalogs::Default();
			Catalogs.Tools = &Manager;
			{
				FN2CMcpProtocol Core(Catalogs);
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Init = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
				Core.Handle(N2CTest::MakeMcpInbound(TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-epic-overlap-spec\",\"version\":\"1.0\"}}}"),
					TEXT("application/json, text/event-stream"), FString()), Init);
				const FString Session = Init->LastReply().SessionId;
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Initialized = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
				Core.Handle(N2CTest::MakeMcpInbound(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"), TEXT("application/json, text/event-stream"), Session), Initialized);
				const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Call = MakeShared<N2CTest::FN2CMcpMemoryResponder>(false);
				Core.Handle(N2CTest::MakeMcpInbound(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"copy-asset\",\"arguments\":{}}}"),
					TEXT("application/json, text/event-stream"), Session), Call);
				const TSharedPtr<FJsonObject> Reply = ParseObject(Call->LastReply().Body);
				const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
				if (TestTrue(TEXT("tools/call copy-asset: a JSON-RPC error"), Reply.IsValid() && Reply->TryGetObjectField(TEXT("error"), ErrorObject)))
				{
					TestEqual(TEXT("code"), static_cast<int32>((*ErrorObject)->GetNumberField(TEXT("code"))), -32602);
					const FString Message = (*ErrorObject)->GetStringField(TEXT("message"));
					TestTrue(FString::Printf(TEXT("message names the Epic tool: %s"), *Message), Message.StartsWith(TEXT("Unknown tool: copy-asset")) && Message.Contains(TEXT("AssetTools.duplicate")));
				}
			}
		});
	});

	Describe(TEXT("the saved setting"), [this]()
	{
		It(TEXT("loads as saved, so an explicit Always serve or Leave wins over Automatic"), [this]()
		{
			for (const EN2CMcpOverlappingTools Saved : { EN2CMcpOverlappingTools::AlwaysServe, EN2CMcpOverlappingTools::LeaveToUnrealMcp })
			{
				const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
					/ FString::Printf(TEXT("N2CEpicOverlap_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)) / TEXT("DefaultNodeToCode.ini"));
				TestTrue(TEXT("fixture written"), FFileHelper::SaveStringToFile(FString::Printf(TEXT("[%s]\nMcpOverlappingTools=%s\n"),
					*UN2CSettings::StaticClass()->GetPathName(), SettingName(Saved)), *Path));
				GConfig->UnloadFile(Path);
				GConfig->LoadFile(Path);
				UN2CSettings* Settings = NewObject<UN2CSettings>(GetTransientPackage());
				Settings->McpOverlappingTools = EN2CMcpOverlappingTools::Automatic;
				Settings->LoadConfig(nullptr, *Path);
				TestEqual(FString::Printf(TEXT("%s loads"), SettingName(Saved)), Settings->McpOverlappingTools, Saved);
				const bool bLeft = N2CMcpEpicOverlap::Decide(Settings->McpOverlappingTools, EN2CMcpToolProfile::DefaultToolsets, Env(true, true, true)).bLeaveToUnrealMcp;
				TestEqual(FString::Printf(TEXT("%s decides"), SettingName(Saved)), bLeft, Saved == EN2CMcpOverlappingTools::LeaveToUnrealMcp);
				GConfig->UnloadFile(Path);
				IFileManager::Get().DeleteDirectory(*FPaths::GetPath(Path), false, true);
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
