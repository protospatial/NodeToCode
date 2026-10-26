// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Async/N2CToolAsyncTaskManager.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "MCP/Utils/N2CMcpPlaySessionGuard.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformTime.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "PlayInEditorDataTypes.h"
#include "Runtime/Launch/Resources/Version.h"

/** Whether a headless (-nullrhi) test can start and end a PIE session; same engine limit as a Blueprint editor */
#define N2C_CAN_END_HEADLESS_PIE N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR

namespace N2CPlaySessionGuardSpec
{
	/**
	 * A long-running probe tool that counts its calls: not read-only (no annotations), and allowed during PIE only when
	 * bAllowed. Never registered; the spec launches it through FN2CToolAsyncTaskManager as tools/call would.
	 */
	class FProbeTool : public IN2CMcpTool
	{
	public:
		FProbeTool(const TCHAR* InName, bool bInAllowed) : Name(InName), bAllowed(bInAllowed) {}

		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(Name, TEXT("PIE guard probe"));
			Definition.bIsLongRunning = true;
			Definition.bAllowedDuringPlaySession = bAllowed;
			return Definition;
		}

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef&, const TSharedPtr<FJsonObject>&) override
		{
			++Calls;
			return MakeFulfilledPromise<FMcpToolCallResult>(FMcpToolCallResult::CreateTextResult(TEXT("probe ran"))).GetFuture();
		}

		FString Name;
		bool bAllowed = false;
		int32 Calls = 0;
	};

	/** Launches the probe as a long-running tools/call and returns the JSON-RPC response it wrote (empty when none) */
	FString LaunchProbe(const TSharedRef<FProbeTool>& Probe)
	{
		const TSharedRef<FString> Response = MakeShared<FString>();
		FN2CAsyncTaskResponder Responder;
		Responder.SendResponse = [Response](const FString& Json) { *Response = Json; };
		FN2CToolAsyncTaskManager::Get().LaunchToolCall(Probe, FGuid::NewGuid(), Probe->Name, MakeShared<FJsonObject>(),
			FGuid::NewGuid().ToString(), TEXT("n2c-pie-spec"), MakeShared<FJsonValueNumber>(1), Responder);
		return *Response;
	}

	/** Calls Poll every core tick until it returns true or TimeoutSeconds pass, then Then(whether Poll became true) */
	void WaitUntil(TFunction<bool()> Poll, double TimeoutSeconds, TFunction<void(bool)> Then)
	{
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Poll = MoveTemp(Poll), Then = MoveTemp(Then), Deadline](float)
		{
			if (Poll())
			{
				Then(true);
				return false;
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				Then(false);
				return false;
			}
			return true;
		}));
	}

	/** Asks the editor for a Play In Editor session, as the toolbar's Play button does (it starts on a later tick) */
	void RequestPlayInEditor()
	{
		FRequestPlaySessionParams Params;
		Params.WorldType = EPlaySessionWorldType::PlayInEditor;
		GEditor->RequestPlaySession(Params);
	}

	TSharedPtr<FJsonObject> FunctionArgs(const UBlueprint* Blueprint, const TCHAR* FunctionName)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
		Args->SetStringField(TEXT("functionName"), FunctionName);
		return Args;
	}

	TSharedPtr<FJsonObject> VariableArgs(const TCHAR* VariableName)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("variableName"), VariableName);
		Args->SetStringField(TEXT("typeIdentifier"), TEXT("bool"));
		return Args;
	}

	/**
	 * call_tool on Inner with Arguments, on a manager configured like a server in tool-search exposure with the default
	 * toolsets, through FN2CMcpToolManager::ExecuteTool as tools/call runs it
	 */
	FMcpToolCallResult CallThroughCallTool(const TCHAR* Inner, const TSharedPtr<FJsonObject>& Arguments)
	{
		FN2CMcpToolManager Manager;
		FN2CMcpToolConfig Config;
		Config.Toolsets = FN2CMcpToolConfig::ToolsetsForProfile(EN2CMcpToolProfile::DefaultToolsets, {});
		Config.Exposure = EN2CMcpToolExposure::ToolSearch;
		Manager.Configure(Config);
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("tool_name"), Inner);
		Args->SetObjectField(TEXT("arguments"), Arguments);
		return Manager.ExecuteTool(FN2CMcpToolManager::CallToolName, Args);
	}

	bool IsPieRefusal(const FMcpToolCallResult& Result)
	{
		return Result.bIsError && N2CTest::ResultText(Result).StartsWith(TEXT("PIE_ACTIVE:"));
	}

	bool HasFunction(const UBlueprint* Blueprint, const TCHAR* Name)
	{
		return Blueprint->FunctionGraphs.ContainsByPredicate([Name](const UEdGraph* Graph) { return Graph && Graph->GetName() == Name; });
	}

	bool HasVariable(const UBlueprint* Blueprint, const TCHAR* Name)
	{
		return FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(Name)) != INDEX_NONE;
	}
}

/**
 * While a Play In Editor session runs, every MCP tool that is not read-only (and does not set
 * bAllowedDuringPlaySession) answers PIE_ACTIVE instead of editing a Blueprint or an asset, and read-only tools keep
 * working (card 0170, review gap-1 step 7), called directly or through call_tool.
 *
 * The live case starts a real PIE session in the headless editor (UEditorEngine::RequestPlaySession) and ends it
 * (RequestEndPlayMap). PIE opens a game window, and on UE 5.6 and 5.7 under -nullrhi the next Slate layout pass after
 * that window closes reaches FGenericWindow::GetRestoredDimensions, which is Fatal (as for a Blueprint editor, see
 * N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR), so it is compiled on UE 5.8 only. On every engine a second case sets
 * GEditor->PlayWorld for the length of its calls, which is what the guard reads during PIE; Tests/mcp_probe P12 runs
 * real PIE on 5.6 and 5.7 (with -RenderOffScreen).
 */
BEGIN_DEFINE_SPEC(FN2CPlaySessionGuardSpec, "NodeToCode.MCP.PlaySession", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FToolSpecBlueprint> Test;
END_DEFINE_SPEC(FN2CPlaySessionGuardSpec)

void FN2CPlaySessionGuardSpec::Define()
{
	using namespace N2CPlaySessionGuardSpec;

	Describe(TEXT("the rule"), [this]()
	{
		It(TEXT("refuses exactly the tools that are not read-only and not allowed during PIE, and says so in their descriptions"), [this]()
		{
			// Tools that change a Blueprint, an asset or a folder, which the card names
			const TSet<FString> MustRefuse = {
				TEXT("create-variable"), TEXT("compile-blueprint"), TEXT("save-blueprint"), TEXT("create-blueprint-class"),
				TEXT("add-component-class-to-actor"), TEXT("delete-component-in-actor"), TEXT("move-asset"), TEXT("rename-asset"),
				TEXT("copy-asset"), TEXT("create-folder"), TEXT("move-folder"), TEXT("rename-folder"), TEXT("connect-pins"),
				TEXT("delete-blueprint-node"), TEXT("tag-blueprint-graph") };
			// Not read-only, but they leave Blueprints, assets and levels alone
			const TSet<FString> MustAllow = {
				TEXT("translate-batch"), TEXT("translate-focused-blueprint"), TEXT("open-blueprint-asset"),
				TEXT("open-blueprint-function"), TEXT("open-content-browser-path"), TEXT("save-python-script"),
				TEXT("delete-python-script"), TEXT("run-python") };

			TSet<FString> Seen;
			for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
			{
				if (!Tool.IsValid())
				{
					continue;
				}
				FMcpToolDefinition Definition = Tool->GetDefinition();
				const FString& Name = Definition.Name;
				Seen.Add(Name);
				const bool bRefused = FN2CMcpPlaySessionGuard::IsRefusedDuringPlaySession(Definition);
				TestEqual(FString::Printf(TEXT("%s: refused during PIE exactly when it is neither read-only nor allowed"), *Name),
					bRefused, !Definition.Annotations.IsReadOnly() && !Definition.bAllowedDuringPlaySession);
				if (MustRefuse.Contains(Name))
				{
					TestTrue(FString::Printf(TEXT("%s is refused during PIE"), *Name), bRefused);
				}
				if (MustAllow.Contains(Name) || Definition.Annotations.IsReadOnly())
				{
					TestFalse(FString::Printf(TEXT("%s keeps working during PIE"), *Name), bRefused);
				}

				FN2CMcpPlaySessionGuard::AddRuleToDescription(Definition);
				TestEqual(FString::Printf(TEXT("%s: the description mentions PIE_ACTIVE exactly when it is refused"), *Name),
					Definition.Description.Contains(FN2CMcpPlaySessionGuard::DescriptionNote), bRefused);
				FN2CMcpPlaySessionGuard::AddRuleToDescription(Definition);
				TestEqual(FString::Printf(TEXT("%s: the note is added once"), *Name),
					Definition.Description.Contains(FString(FN2CMcpPlaySessionGuard::DescriptionNote) + TEXT(" ") + FN2CMcpPlaySessionGuard::DescriptionNote), false);
			}
			for (const FString& Name : MustRefuse.Union(MustAllow))
			{
				TestTrue(FString::Printf(TEXT("%s is in the registry"), *Name), Seen.Contains(Name));
			}
		});

		It(TEXT("puts the rule in the description a registered tool serves"), [this]()
		{
			FN2CMcpToolManager& Manager = FN2CMcpToolManager::Get();
			const bool bWasRegistered = Manager.IsToolRegistered(TEXT("create-variable"));
			if (!bWasRegistered)
			{
				Manager.RegisterTool(N2CTest::FindRegisteredTool(TEXT("create-variable")));
			}
			const TSharedPtr<const FMcpToolDefinition> Definition = Manager.GetToolDefinition(TEXT("create-variable"));
			if (TestTrue(TEXT("create-variable has a definition"), Definition.IsValid()))
			{
				TestTrue(TEXT("its description names PIE_ACTIVE"), Definition->Description.Contains(TEXT("PIE_ACTIVE")));
			}
			if (!bWasRegistered)
			{
				Manager.UnregisterTool(TEXT("create-variable"));
			}
		});

		It(TEXT("lets every tool run when no play session is running"), [this]()
		{
			if (!TestFalse(TEXT("no PIE session at the start"), FN2CMcpPlaySessionGuard::IsPlaySessionActive()))
			{
				return;
			}
			FMcpToolDefinition Mutating(TEXT("n2c-pie-mutating"));
			TestFalse(TEXT("a mutating tool may run outside PIE"), FN2CMcpPlaySessionGuard::CheckCall(Mutating).IsSet());
		});
	});

	Describe(TEXT("while GEditor->PlayWorld is set"), [this]()
	{
		AfterEach([this]()
		{
			Test.Reset();
		});

		It(TEXT("refuses mutating tools and call_tool on them with PIE_ACTIVE, keeps read-only tools working, and allows them again after"), [this]()
		{
			Test = MakeUnique<N2CTest::FToolSpecBlueprint>(TEXT("BP_N2CPieGuardWorld"));
			UBlueprint* Blueprint = Test->GetBlueprint();
			if (!TestNotNull(TEXT("test Blueprint"), Blueprint) || !TestNotNull(TEXT("GEditor"), GEditor)
				|| !TestFalse(TEXT("no PIE session at the start"), FN2CMcpPlaySessionGuard::IsPlaySessionActive()))
			{
				return;
			}
			Test->Settle();
			const TSharedPtr<FJsonObject> PathArgs = MakeShared<FJsonObject>();
			PathArgs->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());

			{
				// The world a PIE session would play in; nothing ticks while it is set
				UWorld* const Saved = GEditor->PlayWorld;
				GEditor->PlayWorld = GEditor->GetEditorWorldContext().World();
				ON_SCOPE_EXIT { GEditor->PlayWorld = Saved; };
				TestTrue(TEXT("the guard sees a play world"), FN2CMcpPlaySessionGuard::IsPlaySessionActive());

				TestTrue(TEXT("create-variable answers PIE_ACTIVE"), IsPieRefusal(N2CTest::RunTool(TEXT("create-variable"), VariableArgs(TEXT("PieVar")))));
				TestTrue(TEXT("create-blueprint-function answers PIE_ACTIVE"),
					IsPieRefusal(N2CTest::RunTool(TEXT("create-blueprint-function"), FunctionArgs(Blueprint, TEXT("PieFunction")))));
				{
					N2CTest::FCompileCounter Compiles(Blueprint);
					TestTrue(TEXT("compile-blueprint answers PIE_ACTIVE"), IsPieRefusal(N2CTest::RunTool(TEXT("compile-blueprint"), PathArgs)));
					TestEqual(TEXT("no compile ran"), Compiles.Count, 0);
				}
				TestFalse(TEXT("no function was created"), HasFunction(Blueprint, TEXT("PieFunction")));

				const FMcpToolCallResult ViaCallTool = CallThroughCallTool(TEXT("create-blueprint-function"), FunctionArgs(Blueprint, TEXT("PieFunction")));
				TestTrue(TEXT("call_tool create-blueprint-function answers PIE_ACTIVE: ") + N2CTest::ResultText(ViaCallTool).Left(200), IsPieRefusal(ViaCallTool));
				TestFalse(TEXT("no function was created through call_tool"), HasFunction(Blueprint, TEXT("PieFunction")));

				const FMcpToolCallResult List = N2CTest::RunTool(TEXT("list-blueprint-functions"), PathArgs);
				TestFalse(TEXT("list-blueprint-functions works: ") + N2CTest::ResultText(List).Left(200), List.bIsError);
				const FMcpToolCallResult ListViaCallTool = CallThroughCallTool(TEXT("list-blueprint-functions"), PathArgs);
				TestFalse(TEXT("call_tool list-blueprint-functions works: ") + N2CTest::ResultText(ListViaCallTool).Left(200), ListViaCallTool.bIsError);

				const TSharedRef<FProbeTool> Refused = MakeShared<FProbeTool>(TEXT("n2c-pie-probe-refused"), false);
				const FString RefusedResponse = LaunchProbe(Refused);
				TestEqual(TEXT("a long-running mutating tool does not start"), Refused->Calls, 0);
				TestTrue(TEXT("its response is PIE_ACTIVE"), RefusedResponse.Contains(TEXT("PIE_ACTIVE:")));
				const TSharedRef<FProbeTool> Allowed = MakeShared<FProbeTool>(TEXT("n2c-pie-probe-allowed"), true);
				LaunchProbe(Allowed);
				TestEqual(TEXT("a long-running tool allowed during PIE runs"), Allowed->Calls, 1);
			}

			TestFalse(TEXT("no play world after"), FN2CMcpPlaySessionGuard::IsPlaySessionActive());
			// compile-blueprint, not create-blueprint-function: that one opens a Blueprint editor, which cannot be closed
			// headless below UE 5.8
			N2CTest::FCompileCounter Compiles(Blueprint);
			const FMcpToolCallResult After = CallThroughCallTool(TEXT("compile-blueprint"), PathArgs);
			TestFalse(TEXT("call_tool compile-blueprint works after: ") + N2CTest::ResultText(After).Left(200), After.bIsError);
			TestTrue(TEXT("the Blueprint was compiled after"), Compiles.Count > 0);
		});
	});

#if N2C_CAN_END_HEADLESS_PIE
	Describe(TEXT("during a Play In Editor session"), [this]()
	{
		AfterEach([this]()
		{
			// A case that failed half-way must not leave PIE running for the next spec
			if (GEditor && GEditor->PlayWorld)
			{
				GEditor->EndPlayMap();
			}
			Test.Reset();
		});

		LatentIt(TEXT("refuses create-variable and the other mutating tools with PIE_ACTIVE, keeps read-only tools working, and allows them again after PIE ends"),
			FTimespan::FromSeconds(120), [this](const FDoneDelegate& Done)
		{
			Test = MakeUnique<N2CTest::FToolSpecBlueprint>(TEXT("BP_N2CPieGuard"));
			UBlueprint* Blueprint = Test->GetBlueprint();
			if (!TestNotNull(TEXT("test Blueprint"), Blueprint) || !TestNotNull(TEXT("GEditor"), GEditor)
				|| !TestFalse(TEXT("no PIE session at the start"), FN2CMcpPlaySessionGuard::IsPlaySessionActive()))
			{
				Done.Execute();
				return;
			}
			Test->Settle();
			// create-variable edits the focused Blueprint: focus it (UE 5.8; elsewhere OpenInEditor adds why it cannot)
			const bool bFocused = Test->OpenInEditor(*this, FBlueprintEditorUtils::FindEventGraph(Blueprint));

			RequestPlayInEditor();
			WaitUntil([]() { return GEditor->PlayWorld != nullptr; }, 60.0, [this, Blueprint, bFocused, Done](bool bStarted)
			{
				if (!TestTrue(TEXT("the PIE session started"), bStarted))
				{
					Done.Execute();
					return;
				}
				TestTrue(TEXT("the guard sees the PIE session"), FN2CMcpPlaySessionGuard::IsPlaySessionActive());

				// A mutating tool on the focused Blueprint, as the card asks
				const FMcpToolCallResult Variable = N2CTest::RunTool(TEXT("create-variable"), VariableArgs(TEXT("PieVar")));
				const FString VariableText = N2CTest::ResultText(Variable);
				TestTrue(TEXT("create-variable is an error during PIE"), Variable.bIsError);
				TestTrue(TEXT("create-variable answers PIE_ACTIVE: ") + VariableText.Left(200), VariableText.StartsWith(TEXT("PIE_ACTIVE:")));
				TestTrue(TEXT("the refusal names Play In Editor"), VariableText.Contains(TEXT("Play In Editor (PIE)")));
				TestFalse(TEXT("no variable was created"), HasVariable(Blueprint, TEXT("PieVar")));

				// The same through blueprintPath, and the content tools
				const FMcpToolCallResult Function = N2CTest::RunTool(TEXT("create-blueprint-function"), FunctionArgs(Blueprint, TEXT("PieFunction")));
				TestTrue(TEXT("create-blueprint-function answers PIE_ACTIVE"), Function.bIsError && N2CTest::ResultText(Function).StartsWith(TEXT("PIE_ACTIVE:")));
				TestFalse(TEXT("no function was created"), HasFunction(Blueprint, TEXT("PieFunction")));

				const TSharedPtr<FJsonObject> FolderArgs = MakeShared<FJsonObject>();
				FolderArgs->SetStringField(TEXT("folderPath"), TEXT("/Game/N2CTestPieGuard"));
				const FMcpToolCallResult Folder = N2CTest::RunTool(TEXT("create-folder"), FolderArgs);
				TestTrue(TEXT("create-folder answers PIE_ACTIVE"), Folder.bIsError && N2CTest::ResultText(Folder).StartsWith(TEXT("PIE_ACTIVE:")));

				const TSharedPtr<FJsonObject> CompileArgs = MakeShared<FJsonObject>();
				CompileArgs->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
				const FMcpToolCallResult Compile = N2CTest::RunTool(TEXT("compile-blueprint"), CompileArgs);
				TestTrue(TEXT("compile-blueprint answers PIE_ACTIVE"), Compile.bIsError && N2CTest::ResultText(Compile).StartsWith(TEXT("PIE_ACTIVE:")));

				const FMcpToolCallResult ViaCallTool = CallThroughCallTool(TEXT("create-variable"), VariableArgs(TEXT("PieVar")));
				TestTrue(TEXT("call_tool create-variable answers PIE_ACTIVE: ") + N2CTest::ResultText(ViaCallTool).Left(200), IsPieRefusal(ViaCallTool));
				TestFalse(TEXT("no variable was created through call_tool"), HasVariable(Blueprint, TEXT("PieVar")));

				// A read-only tool still works
				const TSharedPtr<FJsonObject> ListArgs = MakeShared<FJsonObject>();
				ListArgs->SetStringField(TEXT("blueprintPath"), Blueprint->GetPathName());
				const FMcpToolCallResult List = N2CTest::RunTool(TEXT("list-blueprint-functions"), ListArgs);
				TestFalse(TEXT("list-blueprint-functions works during PIE: ") + N2CTest::ResultText(List).Left(200), List.bIsError);

				// Long-running calls take the same rule
				const TSharedRef<FProbeTool> Refused = MakeShared<FProbeTool>(TEXT("n2c-pie-probe-refused"), false);
				const FString RefusedResponse = LaunchProbe(Refused);
				TestEqual(TEXT("a long-running mutating tool does not start during PIE"), Refused->Calls, 0);
				TestTrue(TEXT("its response is PIE_ACTIVE: ") + RefusedResponse.Left(200), RefusedResponse.Contains(TEXT("PIE_ACTIVE:")));
				const TSharedRef<FProbeTool> Allowed = MakeShared<FProbeTool>(TEXT("n2c-pie-probe-allowed"), true);
				const FString AllowedResponse = LaunchProbe(Allowed);
				TestEqual(TEXT("a long-running tool allowed during PIE runs"), Allowed->Calls, 1);
				TestFalse(TEXT("its response is no refusal"), AllowedResponse.Contains(TEXT("PIE_ACTIVE")));

				GEditor->RequestEndPlayMap();
				WaitUntil([]() { return !FN2CMcpPlaySessionGuard::IsPlaySessionActive(); }, 60.0, [this, Blueprint, bFocused, Done](bool bEnded)
				{
					if (!TestTrue(TEXT("the PIE session ended"), bEnded))
					{
						Done.Execute();
						return;
					}

					const FMcpToolCallResult After = N2CTest::RunTool(TEXT("create-blueprint-function"), FunctionArgs(Blueprint, TEXT("PieFunction")));
					TestFalse(TEXT("create-blueprint-function works after PIE: ") + N2CTest::ResultText(After).Left(200), After.bIsError);
					TestTrue(TEXT("the function exists after PIE"), HasFunction(Blueprint, TEXT("PieFunction")));

					if (bFocused)
					{
						const FMcpToolCallResult Variable = N2CTest::RunTool(TEXT("create-variable"), VariableArgs(TEXT("PieVar")));
						TestFalse(TEXT("create-variable works after PIE: ") + N2CTest::ResultText(Variable).Left(200), Variable.bIsError);
						TestTrue(TEXT("the variable exists after PIE"), HasVariable(Blueprint, TEXT("PieVar")));
					}
					Done.Execute();
				});
			});
		});
	});
#else
	It(TEXT("starts a real PIE session (UE 5.8 only: closing the PIE window headless is fatal on 5.6 and 5.7)"), [this]()
	{
		AddInfo(TEXT("Skipped below UE 5.8: the PIE window cannot be closed under -nullrhi (FGenericWindow::GetRestoredDimensions)"));
	});
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
