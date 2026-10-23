// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CSettings.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "LLM/N2CLLMModule.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/Providers/N2COllamaService.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

namespace N2CLLMRequestTests
{
    /** Nothing listens on the discard port, so a request there fails without ever reaching a model */
    static const TCHAR* UnroutableEndpoint = TEXT("http://127.0.0.1:9");

    /** Ticks the HTTP manager on the game thread, as the editor does, until Seconds pass or Until returns true */
    static void PumpHttp(double Seconds, TFunctionRef<bool()> Until)
    {
        FHttpManager& Manager = FHttpModule::Get().GetHttpManager();
        const double End = FPlatformTime::Seconds() + Seconds;
        double Last = FPlatformTime::Seconds();
        while (FPlatformTime::Seconds() < End && !Until())
        {
            FPlatformProcess::Sleep(0.01f);
            const double Now = FPlatformTime::Seconds();
            Manager.Tick(static_cast<float>(Now - Last));
            Last = Now;
        }
    }

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

    static FN2CLLMConfig MakeOllamaConfig()
    {
        FN2CLLMConfig Config;
        Config.Provider = EN2CLLMProvider::Ollama;
        Config.Model = TEXT("n2c-cancel-test");
        return Config;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CLLMRequestCancelTest, "NodeToCode.Robustness.LLMRequestCancel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CLLMRequestCancelTest::RunTest(const FString& Parameters)
{
    using namespace N2CLLMRequestTests;
    FScopedOllamaEndpoint ScopedEndpoint;

    TStrongObjectPtr<UN2COllamaService> Service(NewObject<UN2COllamaService>());
    if (!TestTrue(TEXT("Ollama service initializes"), Service->Initialize(MakeOllamaConfig())))
    {
        return false;
    }

    // 1. Cancelled at once: the delegate never runs, even after the HTTP module has had time to fail the request
    {
        const TSharedRef<int32, ESPMode::ThreadSafe> Calls = MakeShared<int32, ESPMode::ThreadSafe>(0);
        FN2CLLMRequestHandleRef Handle = Service->SendRequest(TEXT("{}"), TEXT("system"),
            FOnLLMResponseReceived::CreateLambda([Calls](const FString&) { ++(*Calls); }));
        TestTrue(TEXT("request is pending after SendRequest"), Handle->GetState() == FN2CLLMRequestHandle::EState::Pending);

        TestTrue(TEXT("Cancel() wins against a pending request"), Handle->Cancel());
        TestTrue(TEXT("handle reports cancelled"), Handle->IsCancelled());

        PumpHttp(2.0, [] { return false; });

        TestEqual(TEXT("delegate never ran after Cancel()"), *Calls, 0);
        TestTrue(TEXT("handle stays cancelled"), Handle->IsCancelled());
        TestTrue(TEXT("a second Cancel() reports the request as cancelled"), Handle->Cancel());
    }

    // 2. Control: the same request without Cancel() fails and runs the delegate exactly once, so the pump above
    //    really delivers completions
    {
        AddExpectedError(TEXT("HTTP request failed"), EAutomationExpectedErrorFlags::Contains, 1);
        const TSharedRef<int32, ESPMode::ThreadSafe> Calls = MakeShared<int32, ESPMode::ThreadSafe>(0);
        FN2CLLMRequestHandleRef Handle = Service->SendRequest(TEXT("{}"), TEXT("system"),
            FOnLLMResponseReceived::CreateLambda([Calls](const FString&) { ++(*Calls); }));

        PumpHttp(15.0, [&Handle] { return Handle->IsFinished(); });

        TestEqual(TEXT("uncancelled request runs its delegate once"), *Calls, 1);
        TestTrue(TEXT("handle reports completed"), Handle->IsCompleted());
        TestFalse(TEXT("Cancel() after completion does nothing"), Handle->Cancel());
        TestFalse(TEXT("handle is not cancelled"), Handle->IsCancelled());
        PumpHttp(0.5, [] { return false; });
        TestEqual(TEXT("delegate still ran only once"), *Calls, 1);
    }

    // 3. The module returns the handle too, and cancelling it puts the module back to Idle
    {
        UN2CLLMModule* Module = UN2CLLMModule::Get();
        // Ollama needs no API key, so the module (and its provider registry) initializes in any host
        UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
        const EN2CLLMProvider SavedProvider = Settings->Provider;
        Settings->Provider = EN2CLLMProvider::Ollama;
        const bool bInitialized = Module->Initialize();
        Settings->Provider = SavedProvider;
        if (!TestTrue(TEXT("module initializes with Ollama"), bInitialized))
        {
            return false;
        }
        const TSharedRef<int32, ESPMode::ThreadSafe> Calls = MakeShared<int32, ESPMode::ThreadSafe>(0);
        FN2CLLMRequestHandleRef Handle = Module->ProcessN2CJsonWithOverrides(TEXT("{}"), MakeOllamaConfig(),
            EN2CCodeLanguage::Cpp, FOnLLMResponseReceived::CreateLambda([Calls](const FString&) { ++(*Calls); }));
        TestTrue(TEXT("module is processing"), Module->GetSystemStatus() == EN2CSystemStatus::Processing);
        TestTrue(TEXT("module request cancels"), Handle->Cancel());
        TestTrue(TEXT("module returns to Idle on cancel"), Module->GetSystemStatus() == EN2CSystemStatus::Idle);

        PumpHttp(2.0, [] { return false; });
        TestEqual(TEXT("module delegate never ran after Cancel()"), *Calls, 0);
    }

    // 4. Errors found before sending complete inside the call, and the handle says so
    {
        TStrongObjectPtr<UN2COllamaService> Uninitialized(NewObject<UN2COllamaService>());
        AddExpectedError(TEXT("Service not initialized"), EAutomationExpectedErrorFlags::Contains, 1);
        int32 Calls = 0;
        FN2CLLMRequestHandleRef Handle = Uninitialized->SendRequest(TEXT("{}"), TEXT("system"),
            FOnLLMResponseReceived::CreateLambda([&Calls](const FString&) { ++Calls; }));
        TestEqual(TEXT("early error completes synchronously"), Calls, 1);
        TestTrue(TEXT("early-error handle is completed"), Handle->IsCompleted());
        TestFalse(TEXT("early-error handle cannot be cancelled"), Handle->Cancel());
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
