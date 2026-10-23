// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LLM/N2CLLMModule.h"
#include "LLM/IN2CLLMService.h"
#include "Core/N2CSettings.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CLLMServiceSurvivesGCTest, "NodeToCode.Robustness.LLMServiceSurvivesGC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CLLMServiceSurvivesGCTest::RunTest(const FString& Parameters)
{
	UN2CLLMModule* Module = UN2CLLMModule::Get();
	if (!TestNotNull(TEXT("LLM module"), Module))
	{
		return false;
	}

	// Ollama needs no API key, so the module can create a service in any host
	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	const EN2CLLMProvider SavedProvider = Settings->Provider;
	Settings->Provider = EN2CLLMProvider::Ollama;
	const bool bInitialized = Module->Initialize();
	Settings->Provider = SavedProvider;
	TestTrue(TEXT("module initializes with Ollama"), bInitialized);

	TScriptInterface<IN2CLLMService> Before = Module->GetActiveService();
	if (!TestNotNull(TEXT("active service after Initialize"), Before.GetObject()))
	{
		return false;
	}
	TWeakObjectPtr<UObject> Weak(Before.GetObject());
	Before = nullptr;

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);

	TestTrue(TEXT("active service survives a full garbage collection"), Weak.IsValid());
	TestTrue(TEXT("module still reports the same service"), Module->GetActiveService().GetObject() == Weak.Get());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
