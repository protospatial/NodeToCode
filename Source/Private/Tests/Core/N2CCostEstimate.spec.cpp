// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Core/Widgets/SN2CContextWindowVisualizer.h"
#include "Core/N2CSettings.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LLM/N2CLLMPricing.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

// The visualizer's priced selection and its model picker
N2C_TEST_PRIVATE_MEMBER(FN2CCostVisualizerTotalCost, float SN2CContextWindowVisualizer::*, &SN2CContextWindowVisualizer::TotalCost)
N2C_TEST_PRIVATE_MEMBER(FN2CCostVisualizerProvider, EN2CLLMProvider SN2CContextWindowVisualizer::*, &SN2CContextWindowVisualizer::CachedProvider)
N2C_TEST_PRIVATE_MEMBER(FN2CCostVisualizerLocalBox, TSharedPtr<SEditableTextBox> SN2CContextWindowVisualizer::*,
	&SN2CContextWindowVisualizer::LocalModelTextBox)

/**
 * Cost and context estimates (card 0220). Before, the price was input tokens of the graph JSON alone at the input
 * price: the ~4K-token system prompt and the reference files every request sends were left out, the reply was not
 * priced, the context bar compared the graph alone with the window, and the batch visualizer kept the old total
 * after a price or model change and did not rebuild its model picker when the provider changed in Project Settings.
 * Checks: a request's overhead is the system prompt plus the reference files; a cloud estimate is input (graph plus
 * overhead) times the input price plus output times the output price; a 6K-token graph with the prompt does not fit
 * Ollama's Context Window of 8192 although the graph alone would; the visualizer reprices on a model change and
 * rebuilds its picker on a provider change. (Nested graphs counted once: NodeToCode.TokenEstimate.)
 */
BEGIN_DEFINE_SPEC(FN2CCostEstimateSpec, "NodeToCode.TokenEstimate.Cost", N2C_TEST_FLAGS)

	/** Restores the settings the cases change, and the service's model info */
	struct FScopedSettings
	{
		EN2CLLMProvider Provider;
		int32 NumCtx;
		TMap<FString, FN2CModelPricing> Pricing;
		FScopedSettings()
		{
			const UN2CSettings* Settings = GetDefault<UN2CSettings>();
			Provider = Settings->Provider;
			NumCtx = Settings->OllamaConfig.NumCtx;
			Pricing = Settings->ModelPricing;
		}
		~FScopedSettings()
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Settings->Provider = Provider;
			Settings->OllamaConfig.NumCtx = NumCtx;
			Settings->ModelPricing = Pricing;
			FN2CTokenEstimationService::Get().RefreshModelInfo();
			FN2CTokenEstimationService::Get().InvalidateAllCache();
		}
	};

	/** Tells the service (and its listeners) a setting changed, as a Project Settings edit does */
	static void Changed(FName Property)
	{
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		FProperty* Edited = UN2CSettings::StaticClass()->FindPropertyByName(Property);
		FPropertyChangedEvent Event(Edited);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Settings, Event);
	}

END_DEFINE_SPEC(FN2CCostEstimateSpec)

void FN2CCostEstimateSpec::Define()
{
	It(TEXT("prices input with the system prompt and reference files, and the expected output at the output price"), [this]()
	{
		FScopedSettings Scoped;
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		Settings->Provider = EN2CLLMProvider::Anthropic;
		Settings->ModelPricing.Add(Settings->GetActiveModelForProvider(EN2CLLMProvider::Anthropic), FN2CModelPricing(3.0f, 15.0f));
		FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
		Service.RefreshModelInfo();

		const int32 Overhead = Service.GetRequestOverheadTokens();
		TestTrue(FString::Printf(TEXT("the system prompt is part of every request (%d tokens)"), Overhead), Overhead > 1000);
		TestTrue(TEXT("and covers the reference files"), Overhead >= Settings->GetReferenceFilesTokenEstimate());

		const int32 Graph = 6000;
		const FN2CCostEstimate One = Service.EstimateCost(Graph);
		TestEqual(TEXT("input = graph + overhead"), One.InputTokens, Graph + Overhead);
		TestEqual(TEXT("output = the graph's size"), One.OutputTokens, Graph);
		TestTrue(TEXT("input cost at the input price"), FMath::IsNearlyEqual(One.InputCost, (Graph + Overhead) / 1000000.0f * 3.0f, 1e-6f));
		TestTrue(TEXT("output cost at the output price"), FMath::IsNearlyEqual(One.OutputCost, Graph / 1000000.0f * 15.0f, 1e-6f));
		TestTrue(TEXT("CalculateCost is their sum"), FMath::IsNearlyEqual(Service.CalculateCost(Graph), One.GetTotal(), 1e-6f));

		const FN2CCostEstimate Batch = Service.EstimateCost(Graph, 3);
		TestEqual(TEXT("a batch of three requests sends the overhead three times"), Batch.InputTokens, Graph + 3 * Overhead);
	});

	It(TEXT("reports a 6K-token graph over an Ollama Context Window of 8192 once the prompt is counted"), [this]()
	{
		FScopedSettings Scoped;
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		Settings->Provider = EN2CLLMProvider::Ollama;
		Settings->OllamaConfig.NumCtx = 8192;
		FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
		Service.RefreshModelInfo();

		TestEqual(TEXT("the window is the Context Window setting"), Service.GetFullContextWindow(), 8192);
		TestTrue(TEXT("the graph alone would fit"), 6000 < Service.GetFullContextWindow());
		TestTrue(FString::Printf(TEXT("graph + %d prompt tokens is over capacity"), Service.GetRequestOverheadTokens()), Service.IsOverContext(6000));
		TestTrue(TEXT("and fills more than the usable window"), Service.GetContextUsage(6000) > 1.0f);
		TestEqual(TEXT("a local model is free"), Service.CalculateCost(6000), 0.0f);
	});

	It(TEXT("reprices the batch total on a price change and rebuilds the model picker on a provider change"), [this]()
	{
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("Slate is not initialized; skipping"));
			return;
		}
		FScopedSettings Scoped;
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		Settings->Provider = EN2CLLMProvider::Anthropic;
		const FString Model = Settings->GetActiveModelForProvider(EN2CLLMProvider::Anthropic);
		Settings->ModelPricing.Add(Model, FN2CModelPricing(3.0f, 15.0f));
		FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
		Service.RefreshModelInfo();

		N2CTest::FTestBlueprint BP(TEXT("BP_N2CCostEstimate"), TEXT("/Game"));
		UEdGraph* Graph = BP.AddFunction(TEXT("Priced"));
		UK2Node_CallFunction* Print = BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
		FN2CTagInfo Info;
		Info.GraphGuid = Graph->GraphGuid.ToString();
		Info.GraphName = Graph->GetName();
		Info.BlueprintPath = BP.GetBlueprint()->GetPathName();

		const TSharedRef<SN2CContextWindowVisualizer> Visualizer = SNew(SN2CContextWindowVisualizer);
		Visualizer->UpdateForSelection({ Info });
		const float Before = (*Visualizer).*N2CTest::GetPrivateMember(N2CTest::FN2CCostVisualizerTotalCost());
		TestTrue(FString::Printf(TEXT("the selection is priced (%f)"), Before), Before > 0.0f);

		// A price edit in Project Settings: the total follows without a new selection
		Settings->ModelPricing.Add(Model, FN2CModelPricing(6.0f, 30.0f));
		Changed(GET_MEMBER_NAME_CHECKED(UN2CSettings, ModelPricing));
		const float After = (*Visualizer).*N2CTest::GetPrivateMember(N2CTest::FN2CCostVisualizerTotalCost());
		TestTrue(FString::Printf(TEXT("doubled prices double the total (%f -> %f)"), Before, After), FMath::IsNearlyEqual(After, 2.0f * Before, 1e-5f));

		// The provider changed in Project Settings, not through the picker: the picker becomes Ollama's model box
		TestFalse(TEXT("a cloud provider has the model list, not the local model box"),
			((*Visualizer).*N2CTest::GetPrivateMember(N2CTest::FN2CCostVisualizerLocalBox())).IsValid());
		Settings->Provider = EN2CLLMProvider::Ollama;
		Changed(GET_MEMBER_NAME_CHECKED(UN2CSettings, Provider));
		TestTrue(TEXT("the visualizer follows the provider"),
			(*Visualizer).*N2CTest::GetPrivateMember(N2CTest::FN2CCostVisualizerProvider()) == EN2CLLMProvider::Ollama);
		TestTrue(TEXT("the picker is rebuilt as the local model box"),
			((*Visualizer).*N2CTest::GetPrivateMember(N2CTest::FN2CCostVisualizerLocalBox())).IsValid());
		TestEqual(TEXT("a local model is free"), (*Visualizer).*N2CTest::GetPrivateMember(N2CTest::FN2CCostVisualizerTotalCost()), 0.0f);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
