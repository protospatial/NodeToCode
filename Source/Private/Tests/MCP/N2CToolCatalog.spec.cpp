// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolsets.h"
#include "MCP/Tools/N2CToolCatalog.h"
#include "Core/N2CUserSettings.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace N2CToolCatalogSpec
{
	const TCHAR* ProbeName = TEXT("n2c-test-catalog-probe");
	const TCHAR* LongName = TEXT("n2c-test-catalog-long");
	const TCHAR* LockProbeName = TEXT("n2c-test-catalog-lock");

	FString ToJsonText(const TSharedPtr<FJsonObject>& Json)
	{
		FString Text;
		if (Json.IsValid())
		{
			FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
		}
		return Text;
	}

	/** {type: object, properties: {<name>: {type: string}}, required: [Required]} */
	TSharedPtr<FJsonObject> StringSchema(const TArray<FString>& Properties, const TArray<FString>& Required)
	{
		const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		const TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		for (const FString& Property : Properties)
		{
			const TSharedRef<FJsonObject> Prop = MakeShared<FJsonObject>();
			Prop->SetStringField(TEXT("type"), TEXT("string"));
			Props->SetObjectField(Property, Prop);
		}
		Schema->SetObjectField(TEXT("properties"), Props);
		TArray<TSharedPtr<FJsonValue>> RequiredValues;
		for (const FString& Name : Required)
		{
			RequiredValues.Add(MakeShared<FJsonValueString>(Name));
		}
		Schema->SetArrayField(TEXT("required"), RequiredValues);
		return Schema;
	}

	/**
	 * Reads Manager from a worker thread (GetConfig takes its lock) and waits up to two seconds for it. False when the
	 * read could not finish because this thread holds the lock; Pending then finishes once the lock is released.
	 */
	bool ReadsFromAnotherThread(FN2CMcpToolManager& Manager, TFuture<void>& Pending)
	{
		FN2CMcpToolManager* Target = &Manager;
		Pending = Async(EAsyncExecution::Thread, [Target]() { Target->GetConfig(); });
		return Pending.WaitFor(FTimespan::FromSeconds(2.0));
	}

	/** A quick tool with one required argument: echoes it as JSON, or fails when "fail" is given */
	class FProbeTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(ProbeName, TEXT("Spec tool of NodeToCode.Tools.Catalog"), N2CMcpToolsets::FileSystem);
			Definition.InputSchema = StringSchema({ TEXT("needed"), TEXT("fail") }, { TEXT("needed") });
			Definition.Annotations = FMcpToolAnnotations::ReadOnly();
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			++Runs;
			if (Arguments.IsValid() && Arguments->HasField(TEXT("fail")))
			{
				return FMcpToolCallResult::CreateErrorResult(TEXT("probe failed"));
			}
			FString Needed;
			Arguments->TryGetStringField(TEXT("needed"), Needed);
			return FMcpToolCallResult::CreateTextResult(FString::Printf(TEXT("{\"echo\":\"%s\"}"), *Needed));
		}

		int32 Runs = 0;
	};

	/** A long-running tool the spec finishes by hand: progress once at the start, a cancelled result on cancel */
	class FLongTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			FMcpToolDefinition Definition(LongName, TEXT("Long-running spec tool of NodeToCode.Tools.Catalog"), N2CMcpToolsets::FileSystem);
			Definition.InputSchema = StringSchema({}, {});
			Definition.Annotations = FMcpToolAnnotations::ReadOnly();
			Definition.bIsLongRunning = true;
			return Definition;
		}

		virtual TFuture<FMcpToolCallResult> InvokeAsync(const FN2CToolCallContextRef& Context, const TSharedPtr<FJsonObject>& Arguments) override
		{
			++Starts;
			Promise = MakeUnique<TPromise<FMcpToolCallResult>>();
			TFuture<FMcpToolCallResult> Future = Promise->GetFuture();
			Context->ReportProgress(0.25f, TEXT("started"));
			const TWeakPtr<FN2CToolCallContext, ESPMode::ThreadSafe> WeakContext = Context;
			Context->OnCancelled([this, WeakContext]()
			{
				const FN2CToolCallContextPtr Pinned = WeakContext.Pin();
				Finish(FMcpToolCallResult::CreateErrorResult(Pinned.IsValid() ? Pinned->GetCancelledMessage() : FString(TEXT("cancelled"))));
			});
			return Future;
		}

		void Finish(const FMcpToolCallResult& Result)
		{
			if (Promise.IsValid())
			{
				Promise->SetValue(Result);
				Promise.Reset();
			}
		}

		TUniquePtr<TPromise<FMcpToolCallResult>> Promise;
		int32 Starts = 0;
	};

	/**
	 * A tool that checks the manager's lock: armed, its GetDefinition (run by Configure) and its Execute read the
	 * manager from another thread; Execute also changes the active set (Configure with no toolsets) while it runs.
	 */
	class FLockProbeTool : public IN2CMcpTool
	{
	public:
		virtual FMcpToolDefinition GetDefinition() const override
		{
			if (bArmed && Manager)
			{
				bReadDuringConfigure = ReadsFromAnotherThread(*Manager, PendingRead);
			}
			FMcpToolDefinition Definition(LockProbeName, TEXT("Lock spec tool of NodeToCode.Tools.Catalog"), N2CMcpToolsets::FileSystem);
			Definition.InputSchema = StringSchema({}, {});
			Definition.Annotations = FMcpToolAnnotations::ReadOnly();
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			check(Manager);
			FN2CMcpToolConfig Nothing;
			Manager->Configure(Nothing);
			bReadDuringExecute = ReadsFromAnotherThread(*Manager, PendingRead);
			return FMcpToolCallResult::CreateTextResult(TEXT("ran"));
		}

		/** Waits for the worker read, whatever happened (so it never outlives the manager) */
		void Settle() const
		{
			if (PendingRead.IsValid())
			{
				PendingRead.Wait();
			}
		}

		FN2CMcpToolManager* Manager = nullptr;
		bool bArmed = false;
		mutable bool bReadDuringConfigure = false;
		bool bReadDuringExecute = false;
		mutable TFuture<void> PendingRead;
	};

	/** Configures every declared toolset, every tool listed */
	FN2CMcpToolConfig AllToolsets(EN2CMcpToolExposure Exposure = EN2CMcpToolExposure::ListAllTools)
	{
		FN2CMcpToolConfig Config;
		Config.Toolsets = N2CMcpToolsets::AllNames();
		Config.Exposure = Exposure;
		return Config;
	}

	FN2CMcpToolConfig FileSystemOnly()
	{
		FN2CMcpToolConfig Config;
		Config.Toolsets = { N2CMcpToolsets::FileSystem.Name };
		return Config;
	}

	/** Arguments for a read-only tool: its required ones (Folder for "path", the project root for "relativePath"), plus blueprintPath */
	TSharedPtr<FJsonObject> ArgumentsFor(const FN2CToolDescriptor& Descriptor, const FString& BlueprintPath, const FString& Folder)
	{
		const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!Descriptor.InputSchema.IsValid() || !Descriptor.InputSchema->TryGetObjectField(TEXT("properties"), Properties) || !Properties)
		{
			return Args;
		}
		const TArray<FString> Required = Descriptor.GetRequiredArguments();
		for (const auto& Pair : (*Properties)->Values)
		{
			const FString Name(*Pair.Key);
			if (Name == TEXT("blueprintPath"))
			{
				Args->SetStringField(Name, BlueprintPath);
				continue;
			}
			if (Name == TEXT("sync_browser"))
			{
				Args->SetBoolField(Name, false);
				continue;
			}
			if (!Required.Contains(Name))
			{
				continue;
			}
			FString Type;
			const TSharedPtr<FJsonObject> Property = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
			if (Property.IsValid())
			{
				Property->TryGetStringField(TEXT("type"), Type);
			}
			if (Type == TEXT("array"))
			{
				TArray<TSharedPtr<FJsonValue>> Items;
				Items.Add(MakeShared<FJsonValueString>(TEXT("N2CCatalogProbe")));
				Args->SetArrayField(Name, Items);
			}
			else if (Type == TEXT("object"))
			{
				Args->SetObjectField(Name, MakeShared<FJsonObject>());
			}
			else if (Type == TEXT("number") || Type == TEXT("integer"))
			{
				Args->SetNumberField(Name, 1);
			}
			else if (Type == TEXT("boolean"))
			{
				Args->SetBoolField(Name, false);
			}
			else
			{
				Args->SetStringField(Name, Name == TEXT("path") ? Folder
					: Name == TEXT("relativePath") ? FString(TEXT(".")) : FString(TEXT("N2CCatalogProbe")));
			}
		}
		return Args;
	}
}

BEGIN_DEFINE_SPEC(FN2CToolCatalogSpec, "NodeToCode.Tools.Catalog", N2C_TEST_FLAGS)
	TSharedPtr<N2CToolCatalogSpec::FProbeTool> Probe;
	TSharedPtr<N2CToolCatalogSpec::FLongTool> Long;
	TUniquePtr<FN2CMcpToolManager> Manager;

	/** A manager whose Configure picks from the probe tools only */
	void MakeProbeManager()
	{
		Probe = MakeShared<N2CToolCatalogSpec::FProbeTool>();
		Long = MakeShared<N2CToolCatalogSpec::FLongTool>();
		const TArray<TSharedPtr<IN2CMcpTool>> Tools = { Probe, Long };
		Manager = MakeUnique<FN2CMcpToolManager>([Tools]() { return Tools; });
		Manager->Configure(N2CToolCatalogSpec::FileSystemOnly());
	}

	TSharedPtr<FJsonObject> Args(const TCHAR* Key, const FString& Value)
	{
		const TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(Key, Value);
		return Json;
	}
END_DEFINE_SPEC(FN2CToolCatalogSpec)

void FN2CToolCatalogSpec::Define()
{
	using namespace N2CToolCatalogSpec;

	AfterEach([this]()
	{
		if (Long.IsValid())
		{
			Long->Finish(FMcpToolCallResult::CreateTextResult(TEXT("spec ended")));
		}
		Manager.Reset();
		Probe.Reset();
		Long.Reset();
	});

	Describe(TEXT("Describe"), [this]()
	{
		It(TEXT("describes every registered tool once, with an object input schema, a toolset and its execution policy"), [this]()
		{
			FN2CMcpToolManager Tools;
			Tools.Configure(AllToolsets());
			const TArray<FN2CToolDescriptor> Descriptors = FN2CToolCatalog(Tools).Describe(FN2CToolFilter::Served());
			TestTrue(TEXT("the catalog describes the NodeToCode tools"), Descriptors.Num() > 50);

			TSet<FString> Names;
			for (const FN2CToolDescriptor& Descriptor : Descriptors)
			{
				bool bDuplicate = false;
				Names.Add(Descriptor.Name, &bDuplicate);
				TestFalse(FString::Printf(TEXT("%s is described once"), *Descriptor.Name), bDuplicate);
				FString Type;
				TestTrue(FString::Printf(TEXT("%s has an object input schema"), *Descriptor.Name),
					Descriptor.InputSchema.IsValid() && Descriptor.InputSchema->TryGetStringField(TEXT("type"), Type) && Type == TEXT("object"));
				TestNotNull(FString::Printf(TEXT("%s has a toolset"), *Descriptor.Name), Descriptor.Toolset);
				if (TestTrue(FString::Printf(TEXT("%s keeps its definition"), *Descriptor.Name), Descriptor.Source.IsValid()))
				{
					TestTrue(FString::Printf(TEXT("%s: execution policy from bIsLongRunning"), *Descriptor.Name),
						(Descriptor.ExecutionPolicy == EN2CToolExecutionPolicy::LongRunning) == Descriptor.Source->bIsLongRunning);
					TestEqual(FString::Printf(TEXT("%s: hints from the annotations"), *Descriptor.Name),
						ToJsonText(Descriptor.Hints.ToJson()), ToJsonText(Descriptor.Source->Annotations.ToJson()));
				}
			}

			// Every registered tool is reached through the legacy shim, with no per-tool change
			for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
			{
				const FMcpToolDefinition Definition = Tool->GetDefinition();
				const bool bSkipped = UN2CUserSettings::IsPythonExecutionTool(Definition.Name) && !UN2CUserSettings::IsMcpPythonExecutionAllowed();
				if (Definition.Toolset && !bSkipped)
				{
					TestTrue(FString::Printf(TEXT("%s is in the catalog"), *Definition.Name), Names.Contains(Definition.Name));
				}
			}
		});

		It(TEXT("serves each descriptor as exactly the MCP entry its cached definition is, in the same order"), [this]()
		{
			FN2CMcpToolManager Tools;
			Tools.Configure(AllToolsets());
			const TArray<FN2CToolDescriptor> Descriptors = FN2CToolCatalog(Tools).Describe();
			const TArray<FMcpToolDefinition> Definitions = Tools.GetAllToolDefinitions();
			if (!TestEqual(TEXT("as many entries as tools/list had"), Descriptors.Num(), Definitions.Num()))
			{
				return;
			}
			for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
			{
				TestEqual(FString::Printf(TEXT("entry %d is %s"), Index, *Definitions[Index].Name),
					ToJsonText(Descriptors[Index].ToMcpDefinition().ToJson()), ToJsonText(Definitions[Index].ToJson()));
			}
		});

		It(TEXT("lists only the three meta-tools in tool search, and every tool as served"), [this]()
		{
			FN2CMcpToolManager Tools;
			Tools.Configure(AllToolsets(EN2CMcpToolExposure::ToolSearch));
			const FN2CToolCatalog Catalog(Tools);
			TArray<FString> Listed;
			for (const FN2CToolDescriptor& Descriptor : Catalog.Describe(FN2CToolFilter::Listed()))
			{
				Listed.Add(Descriptor.Name);
			}
			TestTrue(TEXT("tools/list holds the meta-tools"), Listed
				== TArray<FString>{ FN2CMcpToolManager::CallToolName, FN2CMcpToolManager::DescribeToolsetName, FN2CMcpToolManager::ListToolsetsName });
			TestTrue(TEXT("every tool is still served"), Catalog.Describe(FN2CToolFilter::Served()).Num() > 50);
			TestTrue(TEXT("call_tool is listed"), Catalog.IsListed(FN2CMcpToolManager::CallToolName));
			TestFalse(TEXT("a served tool is not listed"), Catalog.IsListed(TEXT("read-file")));
			TestTrue(TEXT("a served tool is found"), Catalog.Find(TEXT("read-file")).IsSet());
		});

		It(TEXT("filters by toolset and by the read-only hint"), [this]()
		{
			FN2CMcpToolManager Tools;
			Tools.Configure(AllToolsets());
			const FN2CToolCatalog Catalog(Tools);
			FN2CToolFilter Filter = FN2CToolFilter::Served();
			Filter.Toolset = &N2CMcpToolsets::FileSystem;
			TArray<FString> Names;
			for (const FN2CToolDescriptor& Descriptor : Catalog.Describe(Filter))
			{
				Names.Add(Descriptor.Name);
			}
			TestTrue(TEXT("FileSystemTools"), Names == TArray<FString>{ TEXT("read-file"), TEXT("read-path") });

			FN2CToolFilter ReadOnly = FN2CToolFilter::Served();
			ReadOnly.bReadOnlyOnly = true;
			const TArray<FN2CToolDescriptor> ReadOnlyTools = Catalog.Describe(ReadOnly);
			TestTrue(TEXT("some tools are read-only"), ReadOnlyTools.Num() > 10);
			for (const FN2CToolDescriptor& Descriptor : ReadOnlyTools)
			{
				TestTrue(FString::Printf(TEXT("%s is read-only"), *Descriptor.Name), Descriptor.IsReadOnly());
			}
			TestFalse(TEXT("compile-blueprint is not read-only"), ReadOnlyTools.ContainsByPredicate(
				[](const FN2CToolDescriptor& Descriptor) { return Descriptor.Name == TEXT("compile-blueprint"); }));
		});
	});

	Describe(TEXT("Invoke"), [this]()
	{
		BeforeEach([this]()
		{
			MakeProbeManager();
		});

		It(TEXT("returns a tool's JSON text as a value, and its MCP result unchanged"), [this]()
		{
			TFuture<FN2CToolOutcome> Future = FN2CToolCatalog(*Manager).Invoke(ProbeName, Args(TEXT("needed"), TEXT("x")), FN2CToolCallContext::MakeDetached());
			if (!TestTrue(TEXT("an immediate tool's outcome is set when Invoke returns"), Future.IsReady()))
			{
				return;
			}
			const FN2CToolOutcome Outcome = Future.Get();
			TestFalse(TEXT("no error"), Outcome.IsError());
			const TSharedPtr<FJsonObject> Value = Outcome.Value.IsValid() && Outcome.Value->Type == EJson::Object ? Outcome.Value->AsObject() : nullptr;
			TestTrue(TEXT("the value is the JSON object"), Value.IsValid() && Value->GetStringField(TEXT("echo")) == TEXT("x"));
			TestEqual(TEXT("as a plain value string"), Outcome.ToValueString(), FString(TEXT("{\"echo\":\"x\"}")));
			const FMcpToolCallResult Result = Outcome.ToCallToolResult();
			TestEqual(TEXT("the MCP adapter sends the tool's own result"), N2CTest::ResultText(Result), FString(TEXT("{\"echo\":\"x\"}")));
			TestFalse(TEXT("not isError"), Result.bIsError);
			TestEqual(TEXT("the tool ran once"), Probe->Runs, 1);
		});

		It(TEXT("refuses a missing required argument with a typed error naming it, without running the tool"), [this]()
		{
			const FN2CToolCatalog Catalog(*Manager);
			const TSharedPtr<FJsonObject> NullNeeded = MakeShared<FJsonObject>();
			NullNeeded->SetField(TEXT("needed"), MakeShared<FJsonValueNull>());
			TArray<TSharedPtr<FJsonObject>> Calls;
			Calls.Add(MakeShared<FJsonObject>());
			Calls.Add(NullNeeded);
			Calls.Add(nullptr);
			for (const TSharedPtr<FJsonObject>& Arguments : Calls)
			{
				const FN2CToolOutcome Outcome = Catalog.InvokeNow(ProbeName, Arguments);
				if (TestTrue(TEXT("an error"), Outcome.IsError()))
				{
					TestTrue(TEXT("INVALID_ARGUMENT"), Outcome.Error->Code == EN2CToolErrorCode::InvalidArgument);
					TestEqual(TEXT("names the field"), Outcome.Error->Field, FString(TEXT("needed")));
					TestEqualSensitive(TEXT("message"), Outcome.Error->Message, FString(TEXT("INVALID_ARGUMENT: Missing required parameter: needed")));
				}
			}
			const FMcpToolCallResult Result = Manager->ExecuteTool(ProbeName, MakeShared<FJsonObject>());
			TestTrue(TEXT("tools/call answers isError"), Result.bIsError);
			TestEqualSensitive(TEXT("tools/call text"), N2CTest::ResultText(Result), FString(TEXT("INVALID_ARGUMENT: Missing required parameter: needed")));
			TestEqual(TEXT("the tool never ran"), Probe->Runs, 0);
		});

		It(TEXT("reports a tool's failure as TOOL_FAILED with its text, and an unknown tool as UNKNOWN_TOOL"), [this]()
		{
			const FN2CToolCatalog Catalog(*Manager);
			const TSharedPtr<FJsonObject> Failing = Args(TEXT("needed"), TEXT("x"));
			Failing->SetStringField(TEXT("fail"), TEXT("yes"));
			const FN2CToolOutcome Failed = Catalog.InvokeNow(ProbeName, Failing);
			TestTrue(TEXT("failed"), Failed.IsError() && Failed.Error->Code == EN2CToolErrorCode::ToolFailed && Failed.Error->Message == TEXT("probe failed"));
			TestEqual(TEXT("an error's value string is its message"), Failed.ToValueString(), FString(TEXT("probe failed")));

			const FN2CToolOutcome Unknown = Catalog.InvokeNow(TEXT("n2c-test-no-such-tool"), MakeShared<FJsonObject>());
			TestTrue(TEXT("unknown"), Unknown.IsError() && Unknown.Error->Code == EN2CToolErrorCode::UnknownTool);
			TestEqual(TEXT("unknown text"), N2CTest::ResultText(Unknown.ToCallToolResult()), FString(TEXT("Tool 'n2c-test-no-such-tool' not found")));
			TestFalse(TEXT("unknown is not found"), Catalog.Find(TEXT("n2c-test-no-such-tool")).IsSet());
		});

		It(TEXT("starts a long-running tool, reports its progress and sets the outcome when it finishes"), [this]()
		{
			TArray<FString> Progress;
			const FN2CToolCallContextRef Context = FN2CToolCallContext::Create([&Progress](float Value, const FString& Message)
			{
				Progress.Add(Message);
			});
			const TOptional<FN2CToolDescriptor> Descriptor = FN2CToolCatalog(*Manager).Find(LongName);
			TestTrue(TEXT("described as long-running"), Descriptor.IsSet() && Descriptor->ExecutionPolicy == EN2CToolExecutionPolicy::LongRunning);

			TFuture<FN2CToolOutcome> Future = FN2CToolCatalog(*Manager).Invoke(LongName, MakeShared<FJsonObject>(), Context);
			TestEqual(TEXT("started"), Long->Starts, 1);
			TestFalse(TEXT("not finished yet"), Future.IsReady());
			TestTrue(TEXT("progress went to the context's sink"), Progress == TArray<FString>{ TEXT("started") });
			Long->Finish(FMcpToolCallResult::CreateTextResult(TEXT("done")));
			if (TestTrue(TEXT("finished"), Future.IsReady()))
			{
				const FN2CToolOutcome Outcome = Future.Get();
				TestEqual(TEXT("its value"), Outcome.ToValueString(), FString(TEXT("done")));
			}
		});

		It(TEXT("ends a cancelled long-running call with CANCELLED"), [this]()
		{
			const FN2CToolCallContextRef Context = FN2CToolCallContext::Create();
			TFuture<FN2CToolOutcome> Future = FN2CToolCatalog(*Manager).Invoke(LongName, MakeShared<FJsonObject>(), Context);
			Context->Cancel(TEXT("stop"));
			if (TestTrue(TEXT("finished"), Future.IsReady()))
			{
				const FN2CToolOutcome Outcome = Future.Get();
				TestTrue(TEXT("cancelled"), Outcome.IsError() && Outcome.Error->Code == EN2CToolErrorCode::Cancelled);
				TestEqual(TEXT("the reason"), Outcome.ToValueString(), FString(TEXT("stop")));
			}
		});

		It(TEXT("refuses a long-running tool where the outcome is needed at once (a JSON-RPC batch)"), [this]()
		{
			const FN2CToolOutcome Outcome = FN2CToolCatalog(*Manager).InvokeNow(LongName, MakeShared<FJsonObject>());
			TestTrue(TEXT("refused"), Outcome.IsError() && Outcome.Error->Code == EN2CToolErrorCode::NotRunnableHere);
			TestEqual(TEXT("the batch text"), Outcome.ToValueString(),
				FString::Printf(TEXT("%s cannot run inside a JSON-RPC batch. Send it as a single tools/call request."), LongName));
			TestEqual(TEXT("never started"), Long->Starts, 0);
		});

		It(TEXT("turns a native outcome into MCP content: text, with structuredContent for an object"), [this]()
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("count"), 2);
			const FMcpToolCallResult ObjectResult = FN2CToolOutcome::MakeValue(MakeShared<FJsonValueObject>(Object)).ToCallToolResult();
			TestEqual(TEXT("object text"), N2CTest::ResultText(ObjectResult), FString(TEXT("{\"count\":2}")));
			TestTrue(TEXT("structuredContent"), ObjectResult.StructuredContent.IsValid());

			const FMcpToolCallResult StringResult = FN2CToolOutcome::MakeValue(MakeShared<FJsonValueString>(TEXT("plain"))).ToCallToolResult();
			TestEqual(TEXT("string text"), N2CTest::ResultText(StringResult), FString(TEXT("plain")));
			TestFalse(TEXT("no structuredContent for a string"), StringResult.StructuredContent.IsValid());

			const FMcpToolCallResult ErrorResult = FN2CToolOutcome::MakeError(EN2CToolErrorCode::Internal, TEXT("broke")).ToCallToolResult();
			TestTrue(TEXT("isError"), ErrorResult.bIsError);
			TestEqual(TEXT("error text"), N2CTest::ResultText(ErrorResult), FString(TEXT("broke")));
		});
	});

	Describe(TEXT("the manager's lock"), [this]()
	{
		It(TEXT("runs no tool code while Configure holds it"), [this]()
		{
			const TSharedRef<FLockProbeTool> LockProbe = MakeShared<FLockProbeTool>();
			const TArray<TSharedPtr<IN2CMcpTool>> Tools = { LockProbe };
			FN2CMcpToolManager Local([Tools]() { return Tools; });
			LockProbe->Manager = &Local;
			LockProbe->bArmed = true;
			Local.Configure(FileSystemOnly());
			LockProbe->bArmed = false;
			LockProbe->Settle();
			TestTrue(TEXT("another thread read the manager while Configure asked the tool for its definition"), LockProbe->bReadDuringConfigure);
			TestTrue(TEXT("the tool is served"), Local.IsToolRegistered(LockProbeName));
		});

		It(TEXT("lets a tool change the active set during its own Invoke"), [this]()
		{
			const TSharedRef<FLockProbeTool> LockProbe = MakeShared<FLockProbeTool>();
			const TArray<TSharedPtr<IN2CMcpTool>> Tools = { LockProbe };
			FN2CMcpToolManager Local([Tools]() { return Tools; });
			LockProbe->Manager = &Local;
			Local.Configure(FileSystemOnly());
			bool bListChanged = false;
			Local.OnToolListChanged().AddLambda([&bListChanged]() { bListChanged = true; });

			const FN2CToolOutcome Outcome = FN2CToolCatalog(Local).InvokeNow(LockProbeName, MakeShared<FJsonObject>());
			LockProbe->Settle();
			TestEqual(TEXT("the call finished with the tool's value"), Outcome.ToValueString(), FString(TEXT("ran")));
			TestTrue(TEXT("another thread read the manager while the tool ran"), LockProbe->bReadDuringExecute);
			TestFalse(TEXT("the tool's Configure took effect"), Local.IsToolRegistered(LockProbeName));
			TestTrue(TEXT("the change went out through OnToolListChanged"), bListChanged);
		});
	});

	Describe(TEXT("read-only tools through the in-memory adapter"), [this]()
	{
		It(TEXT("answers each read-only tool against a transient Blueprint under /Temp"), [this]()
		{
			N2CTest::FTestBlueprint Test(TEXT("BP_N2CCatalog"));
			const FString BlueprintPath = Test.GetBlueprint()->GetPathName();
			const FString Folder = TEXT("/Game");

			FN2CMcpToolManager Tools;
			Tools.Configure(AllToolsets());
			const FN2CToolCatalog Catalog(Tools);
			FN2CToolFilter Filter = FN2CToolFilter::Served();
			Filter.bReadOnlyOnly = true;

			// These need nothing but the Blueprint, a folder or nothing at all, so they must succeed; the others need a
			// focused editor, a saved script or a real file and answer with their own error (TOOL_FAILED)
			const TSet<FString> MustSucceed = {
				TEXT("get-blueprint-n2cjson"), TEXT("list-blueprint-graphs"), TEXT("list-blueprint-functions"),
				TEXT("list-overridable-functions"), TEXT("list-components-in-actor"), TEXT("find-nodes-in-graph"),
				TEXT("search-blueprint-nodes"), TEXT("list-blueprint-tags"), TEXT("get-available-llm-providers"),
				TEXT("get-available-translation-targets"), TEXT("get-translation-output-directory"),
				TEXT("get-open-blueprint-editors"), TEXT("get-editor-context"), TEXT("list-python-scripts"), TEXT("search-python-scripts"),
				TEXT("search-content-browser"), TEXT("read-content-browser-path"), TEXT("read-path") };
			int32 Succeeded = 0;
			int32 Invoked = 0;
			for (const FN2CToolDescriptor& Descriptor : Catalog.Describe(Filter))
			{
				if (Descriptor.ExecutionPolicy != EN2CToolExecutionPolicy::Immediate)
				{
					continue;
				}
				++Invoked;
				TFuture<FN2CToolOutcome> Future = Catalog.Invoke(Descriptor.Name, ArgumentsFor(Descriptor, BlueprintPath, Folder), FN2CToolCallContext::MakeDetached());
				if (!TestTrue(FString::Printf(TEXT("%s answers at once"), *Descriptor.Name), Future.IsReady()))
				{
					continue;
				}
				const FN2CToolOutcome Outcome = Future.Get();
				AddInfo(FString::Printf(TEXT("%s: %s %s"), *Descriptor.Name,
					Outcome.IsError() ? FN2CToolError::CodeName(Outcome.Error->Code) : TEXT("value"), *Outcome.ToValueString().Left(160)));
				TestFalse(FString::Printf(TEXT("%s: the catalog found the tool and its arguments"), *Descriptor.Name),
					Outcome.IsError() && (Outcome.Error->Code == EN2CToolErrorCode::UnknownTool
						|| Outcome.Error->Code == EN2CToolErrorCode::InvalidArgument || Outcome.Error->Code == EN2CToolErrorCode::Internal));
				TestFalse(FString::Printf(TEXT("%s: the outcome has a value string"), *Descriptor.Name), Outcome.ToValueString().IsEmpty());
				Succeeded += Outcome.IsError() ? 0 : 1;
				if (MustSucceed.Contains(Descriptor.Name))
				{
					TestFalse(FString::Printf(TEXT("%s succeeds (%s)"), *Descriptor.Name, *Outcome.ToValueString().Left(300)), Outcome.IsError());
				}
			}
			TestTrue(TEXT("read-only tools were invoked"), Invoked > 10);
			TestTrue(TEXT("each tool that must succeed was invoked and did"), Succeeded >= MustSucceed.Num());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
