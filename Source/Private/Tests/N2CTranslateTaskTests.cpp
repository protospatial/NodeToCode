// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CEditorIntegration.h"
#include "Core/N2CSettings.h"
#include "Dom/JsonObject.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "LLM/N2CBaseLLMService.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/N2CSystemPromptManager.h"
#include "MCP/Tools/Implementations/Translation/N2CTranslateBlueprintAsyncTask.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace N2CTranslateTaskTests
{
    /** Nothing listens on the discard port, so a request there fails without ever reaching a model */
    static const TCHAR* UnroutableEndpoint = TEXT("http://127.0.0.1:9");

    /** Points the Ollama provider at the unroutable endpoint for the life of the scope */
    struct FScopedOllamaEndpoint
    {
        FScopedOllamaEndpoint()
        {
            UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
            Saved = Settings->OllamaConfig.OllamaEndpoint;
            Settings->OllamaConfig.OllamaEndpoint = UnroutableEndpoint;
        }
        ~FScopedOllamaEndpoint()
        {
            GetMutableDefault<UN2CSettings>()->OllamaConfig.OllamaEndpoint = Saved;
        }
        FString Saved;
    };

    /** What the task reported through the delegates the task manager normally binds */
    struct FTaskObserver
    {
        int32 Completions = 0;
        int32 ProgressReports = 0;
        bool bLastIsError = false;
        FString LastText;
    };

    using FTaskRef = TSharedRef<FN2CTranslateBlueprintAsyncTask, ESPMode::ThreadSafe>;

    /**
     * A translate task as the task manager creates it, except for the request: a focused Blueprint editor cannot exist
     * headless, so the request sends fixed N2C JSON through the same TranslateJsonAsync/ProcessN2CJsonWithOverrides
     * path, to Ollama at the unroutable endpoint.
     */
    static FTaskRef MakeTask(const TSharedRef<FTaskObserver>& Observer)
    {
        TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("provider"), TEXT("Ollama"));
        Arguments->SetStringField(TEXT("model"), TEXT("n2c-cancel-test"));
        Arguments->SetStringField(TEXT("language"), TEXT("Cpp"));

        FTaskRef Task = MakeShared<FN2CTranslateBlueprintAsyncTask>(FGuid::NewGuid(), FGuid::NewGuid().ToString(), Arguments);
        Task->SetRequestStarter([](const FString& ProviderId, const FString& ModelId, const FString& LanguageId,
            const FOnLLMResponseReceived& OnComplete)
        {
            return FN2CEditorIntegration::Get().TranslateJsonAsync(TEXT("{}"), ProviderId, ModelId, LanguageId, OnComplete);
        });
        Task->SetProgressDelegate(FN2CAsyncTaskProgressDelegate::CreateLambda([Observer](float, const FString&)
        {
            ++Observer->ProgressReports;
        }));
        Task->SetCompleteDelegate(FN2CAsyncTaskCompleteDelegate::CreateLambda([Observer](const FMcpToolCallResult& Result)
        {
            ++Observer->Completions;
            Observer->bLastIsError = Result.bIsError;
            FString Text;
            if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
            {
                Result.Content[0]->TryGetStringField(TEXT("text"), Text);
            }
            Observer->LastText = Text;
        }));
        return Task;
    }

    /**
     * Ticks the HTTP manager (as the editor does each frame) and the task's own progress step (as its FTSTicker does)
     * until Seconds pass or Until returns true. The core ticker itself is not ticked: the test already runs inside it.
     */
    static void Pump(double Seconds, const TArray<FTaskRef>& Tasks, TFunctionRef<bool()> Until)
    {
        FHttpManager& Manager = FHttpModule::Get().GetHttpManager();
        const double End = FPlatformTime::Seconds() + Seconds;
        double Last = FPlatformTime::Seconds();
        double SinceTaskTick = 0.0;
        while (FPlatformTime::Seconds() < End && !Until())
        {
            FPlatformProcess::Sleep(0.01f);
            const double Now = FPlatformTime::Seconds();
            const float Delta = static_cast<float>(Now - Last);
            Last = Now;
            Manager.Tick(Delta);
            SinceTaskTick += Delta;
            if (SinceTaskTick >= FN2CTranslateBlueprintAsyncTask::ProgressIntervalSeconds)
            {
                for (const FTaskRef& Task : Tasks)
                {
                    Task->Tick(static_cast<float>(SinceTaskTick));
                }
                SinceTaskTick = 0.0;
            }
        }
    }

    /** Live LLM services and system prompt managers (what a leaked translation request leaves behind) */
    static int32 CountLLMObjects()
    {
        int32 Count = 0;
        for (TObjectIterator<UN2CBaseLLMService> It; It; ++It)
        {
            if (!It->HasAnyFlags(RF_ClassDefaultObject))
            {
                ++Count;
            }
        }
        for (TObjectIterator<UN2CSystemPromptManager> It; It; ++It)
        {
            if (!It->HasAnyFlags(RF_ClassDefaultObject))
            {
                ++Count;
            }
        }
        return Count;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CTranslateTaskCancelTest, "NodeToCode.Robustness.TranslateTaskCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CTranslateTaskCancelTest::RunTest(const FString& Parameters)
{
    using namespace N2CTranslateTaskTests;
    FScopedOllamaEndpoint ScopedEndpoint;

    // Ollama needs no API key, so the module (and its provider registry) initializes in any host
    UN2CLLMModule* Module = UN2CLLMModule::Get();
    {
        UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
        const EN2CLLMProvider SavedProvider = Settings->Provider;
        Settings->Provider = EN2CLLMProvider::Ollama;
        const bool bInitialized = Module->Initialize();
        Settings->Provider = SavedProvider;
        if (!TestTrue(TEXT("module initializes with Ollama"), bInitialized))
        {
            return false;
        }
    }

    // 1. Start, then cancel: the task reports cancelled once, the request is really cancelled and no callback runs
    {
        const TSharedRef<FTaskObserver> Observer = MakeShared<FTaskObserver>();
        FTaskRef Task = MakeTask(Observer);
        Task->Execute();

        FN2CLLMRequestHandlePtr Handle = Task->GetRequestHandle();
        if (!TestTrue(TEXT("Execute() sent a request and kept its handle"), Handle.IsValid()))
        {
            return false;
        }
        TestTrue(TEXT("Execute() returned while the request is in flight"), Handle->GetState() == FN2CLLMRequestHandle::EState::Pending);
        TestFalse(TEXT("task is running after Execute()"), Task->IsComplete());
        TestEqual(TEXT("nothing completed yet"), Observer->Completions, 0);
        TestTrue(TEXT("Execute() reported progress"), Observer->ProgressReports > 0);

        Task->RequestCancel();
        TestEqual(TEXT("cancel reports the task once"), Observer->Completions, 1);
        TestTrue(TEXT("cancelled result is an error"), Observer->bLastIsError);
        TestTrue(TEXT("cancelled result says so"), Observer->LastText.Contains(TEXT("cancelled")));
        TestTrue(TEXT("cancel cancelled the LLM request"), Handle->IsCancelled());
        TestTrue(TEXT("task is complete after cancel"), Task->IsComplete());

        Pump(2.0, { Task }, [] { return false; });

        TestEqual(TEXT("no second report after the HTTP layer settled"), Observer->Completions, 1);
        TestTrue(TEXT("request stayed cancelled (its callback never ran)"), Handle->IsCancelled());

        // A second cancel is harmless
        Task->RequestCancel();
        TestEqual(TEXT("second cancel reports nothing"), Observer->Completions, 1);
    }

    // 2. Control: without a cancel the same request fails and the task reports that once, so the pump above really
    //    delivers completions
    {
        AddExpectedError(TEXT("HTTP request failed"), EAutomationExpectedErrorFlags::Contains, 0);
        AddExpectedError(TEXT("Failed to parse LLM response"), EAutomationExpectedErrorFlags::Contains, 0);
        AddExpectedError(TEXT("Ollama API error"), EAutomationExpectedErrorFlags::Contains, 0);
        const TSharedRef<FTaskObserver> Observer = MakeShared<FTaskObserver>();
        FTaskRef Task = MakeTask(Observer);
        Task->Execute();
        Pump(15.0, { Task }, [&Observer] { return Observer->Completions > 0; });

        TestEqual(TEXT("uncancelled request reports once"), Observer->Completions, 1);
        TestTrue(TEXT("the unroutable request reports an error"), Observer->bLastIsError);
        TestTrue(TEXT("its handle completed (the callback ran)"), Task->GetRequestHandle().IsValid() && Task->GetRequestHandle()->IsCompleted());
        Task->RequestCancel();
        TestEqual(TEXT("cancel after completion reports nothing"), Observer->Completions, 1);
    }

    // 3. Timeout: the ticker step cancels the request and reports the timeout once
    {
        const TSharedRef<FTaskObserver> Observer = MakeShared<FTaskObserver>();
        FTaskRef Task = MakeTask(Observer);
        Task->SetTimeoutSeconds(0.0);
        Task->Execute();
        FN2CLLMRequestHandlePtr Handle = Task->GetRequestHandle();
        TestFalse(TEXT("first tick after the timeout stops the ticker"), Task->Tick(0.5f));
        TestEqual(TEXT("timeout reports once"), Observer->Completions, 1);
        TestTrue(TEXT("timeout result says so"), Observer->LastText.Contains(TEXT("timed out")));
        TestTrue(TEXT("timeout cancelled the LLM request"), Handle.IsValid() && Handle->IsCancelled());

        Pump(2.0, { Task }, [] { return false; });
        TestEqual(TEXT("no report after the timeout"), Observer->Completions, 1);
    }

    // 4. Twenty start/cancel cycles leave no LLM objects behind once the HTTP layer has let go and GC has run
    {
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
        const int32 Before = CountLLMObjects();

        TArray<FTaskRef> Tasks;
        for (int32 Run = 0; Run < 20; ++Run)
        {
            const TSharedRef<FTaskObserver> Observer = MakeShared<FTaskObserver>();
            FTaskRef Task = MakeTask(Observer);
            Task->Execute();
            Task->RequestCancel();
            TestEqual(FString::Printf(TEXT("run %d reports once"), Run), Observer->Completions, 1);
            Tasks.Add(Task);
        }
        TestTrue(TEXT("each run created objects"), CountLLMObjects() > Before);

        Pump(2.0, Tasks, [] { return false; });
        Tasks.Reset();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
        const int32 After = CountLLMObjects();
        TestEqual(TEXT("live LLM services and prompt managers back to the starting count"), After, Before);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
