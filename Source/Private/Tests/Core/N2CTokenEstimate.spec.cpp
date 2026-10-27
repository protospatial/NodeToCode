// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Core/Services/N2CTokenEstimationService.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "Core/N2CSettings.h"
#include "BlueprintLibraries/N2CTagBlueprintLibrary.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

/**
 * The overlay's token estimate (card 0237, suggested card 0429). Before, the service ran its own depth-first walker on
 * top of a capture that already followed nested graphs, so nested graphs were counted twice and a graph reachable along
 * several paths could be cut at the wrong depth; every Blueprint compile emptied the whole cache, so every live overlay
 * re-captured its graph; a graph edit without a compile left the estimate stale; and a Translation Depth change did not
 * reach the token count.
 */
// The Blueprints live under /Game: capture follows nested graphs only into the project's own content
BEGIN_DEFINE_SPEC(FN2CTokenEstimateSpec, "NodeToCode.TokenEstimate", N2C_TEST_FLAGS)

	static FN2CTagInfo TagFor(const UEdGraph* Graph, const UBlueprint* Blueprint)
	{
		FN2CTagInfo Info;
		Info.GraphGuid = Graph->GraphGuid.ToString();
		Info.GraphName = Graph->GetName();
		Info.BlueprintPath = Blueprint->GetPathName();
		return Info;
	}

	/** Each caller runs its calls in order from its entry */
	static void Calls(N2CTest::FTestBlueprint& BP, TMap<FName, UEdGraph*>& Graphs, const TCHAR* Caller, const TArray<FName>& Callees)
	{
		UEdGraph* Graph = Graphs.FindRef(Caller);
		UEdGraphNode* Previous = BP.FunctionEntry(Graph);
		for (const FName Callee : Callees)
		{
			UK2Node_CallFunction* Call = BP.AddSelfCall(Graph, Callee);
			BP.Link(Previous, UEdGraphSchema_K2::PN_Then, Call, UEdGraphSchema_K2::PN_Execute);
			Previous = Call;
		}
	}

	/**
	 * Root calls Near, then Far. Far -> Mid -> Shared; Near -> Shared; Shared -> Leaf -> Beyond. At depth 3 the
	 * translator takes Near, Far, Mid, Shared and Leaf (Shared at depth 2 through Near), not Beyond.
	 */
	static UEdGraph* BuildShortestPath(N2CTest::FTestBlueprint& BP)
	{
		TMap<FName, UEdGraph*> Graphs;
		for (const TCHAR* Name : { TEXT("Root"), TEXT("Near"), TEXT("Far"), TEXT("Mid"), TEXT("Shared"), TEXT("Leaf"), TEXT("Beyond") })
		{
			Graphs.Add(Name, BP.AddFunction(Name));
		}
		Calls(BP, Graphs, TEXT("Root"), { TEXT("Near"), TEXT("Far") });
		Calls(BP, Graphs, TEXT("Near"), { TEXT("Shared") });
		Calls(BP, Graphs, TEXT("Far"), { TEXT("Mid") });
		Calls(BP, Graphs, TEXT("Mid"), { TEXT("Shared") });
		Calls(BP, Graphs, TEXT("Shared"), { TEXT("Leaf") });
		Calls(BP, Graphs, TEXT("Leaf"), { TEXT("Beyond") });
		return Graphs.FindRef(TEXT("Root"));
	}

	/** A function graph with one PrintString call, so it has something to capture */
	static UEdGraph* BuildSimple(N2CTest::FTestBlueprint& BP, const FName Name)
	{
		UEdGraph* Graph = BP.AddFunction(Name);
		UK2Node_CallFunction* Print = BP.AddCall(Graph, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		BP.Link(BP.FunctionEntry(Graph), UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
		return Graph;
	}

	int32 SavedDepth = 0;

END_DEFINE_SPEC(FN2CTokenEstimateSpec)

void FN2CTokenEstimateSpec::Define()
{
	BeforeEach([this]()
	{
		SavedDepth = GetMutableDefault<UN2CSettings>()->TranslationDepth;
	});

	AfterEach([this]()
	{
		GetMutableDefault<UN2CSettings>()->TranslationDepth = SavedDepth;
		FN2CTokenEstimationService::Get().InvalidateAllCache();
	});

	It(TEXT("equals ceil(len(JSON)/4) of the capture Translate sends, and counts each nested graph once at its shortest depth"), [this]()
	{
		N2CTest::FTestBlueprint BP(TEXT("BP_N2CTokenEstimateShortest"), TEXT("/Game"));
		UEdGraph* Root = BuildShortestPath(BP);
		if (!TestNotNull(TEXT("fixture built"), Root) || !TestEqual(TEXT("fixture errors"), BP.Errors().Num(), 0))
		{
			return;
		}
		GetMutableDefault<UN2CSettings>()->TranslationDepth = 3;

		// What the Translate button sends: one capture at the configured depth, condensed JSON
		const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*Root);
		if (!TestTrue(TEXT("the capture succeeds"), Capture.HasValue()))
		{
			return;
		}
		const FString Json = FN2CSerializer::ToJson(Capture.GetValue().Blueprint, EN2CJsonStyle::Condensed);
		const int32 Expected = FMath::CeilToInt(Json.Len() / 4.0f);

		FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
		const FN2CTagInfo Info = TagFor(Root, BP.GetBlueprint());
		const FN2CTokenEstimate Estimate = Service.GetEstimate(Info);
		TestTrue(TEXT("valid"), Estimate.bValid);
		TestEqual(TEXT("tokens = ceil(len(JSON) / 4) of that capture"), Estimate.Tokens, Expected);
		TestEqual(TEXT("nested graphs: Near, Far, Mid, Shared, Leaf (Beyond is past depth 3)"), Estimate.NestedGraphCount, 5);
		TestEqual(TEXT("the same count as the capture"), Estimate.NestedGraphCount, Capture.GetValue().GraphGuids.Num() - 1);
		int32 Nested = -1;
		TestEqual(TEXT("GetTokenEstimateWithNesting agrees"), Service.GetTokenEstimateWithNesting(Info, Nested), Expected);
		TestEqual(TEXT("and its nested count"), Nested, 5);
	});

	It(TEXT("captures once, then re-captures only the Blueprint that was edited or compiled"), [this]()
	{
		N2CTest::FTestBlueprint A(TEXT("BP_N2CTokenEstimateA"), TEXT("/Game"));
		N2CTest::FTestBlueprint B(TEXT("BP_N2CTokenEstimateB"), TEXT("/Game"));
		UEdGraph* GraphA = BuildSimple(A, TEXT("WorkA"));
		UEdGraph* GraphB = BuildSimple(B, TEXT("WorkB"));
		if (!TestNotNull(TEXT("A built"), GraphA) || !TestNotNull(TEXT("B built"), GraphB))
		{
			return;
		}
		GetMutableDefault<UN2CSettings>()->TranslationDepth = 0;
		FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
		const FN2CTagInfo InfoA = TagFor(GraphA, A.GetBlueprint());
		const FN2CTagInfo InfoB = TagFor(GraphB, B.GetBlueprint());

		const int32 Start = Service.GetCaptureCount();
		const int32 TokensA = Service.GetTokenEstimate(InfoA);
		Service.GetTokenEstimate(InfoB);
		TestEqual(TEXT("one capture per graph"), Service.GetCaptureCount() - Start, 2);
		Service.GetTokenEstimate(InfoA);
		Service.GetTokenEstimate(InfoB);
		TestEqual(TEXT("asking again captures nothing"), Service.GetCaptureCount() - Start, 2);

		// Compile A: only A's estimate goes stale
		TArray<FSoftObjectPath> StaleBlueprints;
		const FDelegateHandle Handle = Service.OnEstimatesStale.AddLambda([&StaleBlueprints](const FSoftObjectPath& Path) { StaleBlueprints.Add(Path); });
		ON_SCOPE_EXIT { Service.OnEstimatesStale.Remove(Handle); };
		FKismetEditorUtilities::CompileBlueprint(A.GetBlueprint());
		TestFalse(TEXT("A is stale after its compile"), Service.IsEstimateCurrent(InfoA));
		TestTrue(TEXT("B is still current"), Service.IsEstimateCurrent(InfoB));
		TestTrue(TEXT("the stale notice names A"), StaleBlueprints.Contains(FSoftObjectPath(A.GetBlueprint())));
		TestFalse(TEXT("and not B"), StaleBlueprints.Contains(FSoftObjectPath(B.GetBlueprint())));
		Service.GetTokenEstimate(InfoA);
		Service.GetTokenEstimate(InfoB);
		TestEqual(TEXT("one capture after compiling A"), Service.GetCaptureCount() - Start, 3);

		// Edit A's graph without compiling: its estimate follows the edit
		UK2Node_CallFunction* Extra = A.AddCall(GraphA, UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
		TestNotNull(TEXT("a node added"), Extra);
		GraphA->NotifyGraphChanged();
		TestFalse(TEXT("A is stale after an edit"), Service.IsEstimateCurrent(InfoA));
		TestTrue(TEXT("B is still current"), Service.IsEstimateCurrent(InfoB));
		const int32 EditedTokensA = Service.GetTokenEstimate(InfoA);
		Service.GetTokenEstimate(InfoB);
		TestEqual(TEXT("one capture after editing A"), Service.GetCaptureCount() - Start, 4);
		TestTrue(FString::Printf(TEXT("the edited graph estimates larger (%d > %d)"), EditedTokensA, TokensA), EditedTokensA > TokensA);
	});

	It(TEXT("takes the estimate again at a new Translation Depth and tells the overlays"), [this]()
	{
		N2CTest::FTestBlueprint BP(TEXT("BP_N2CTokenEstimateDepth"), TEXT("/Game"));
		UEdGraph* Root = BuildShortestPath(BP);
		if (!TestNotNull(TEXT("fixture built"), Root))
		{
			return;
		}
		UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
		FN2CTokenEstimationService& Service = FN2CTokenEstimationService::Get();
		const FN2CTagInfo Info = TagFor(Root, BP.GetBlueprint());

		Settings->TranslationDepth = 0;
		const FN2CTokenEstimate Shallow = Service.GetEstimate(Info);
		TestEqual(TEXT("depth 0: no nested graphs"), Shallow.NestedGraphCount, 0);

		int32 Invalidations = 0;
		const FDelegateHandle Handle = Service.OnCacheInvalidated.AddLambda([&Invalidations]() { ++Invalidations; });
		ON_SCOPE_EXIT { Service.OnCacheInvalidated.Remove(Handle); };
		Settings->TranslationDepth = 1;
		FProperty* DepthProperty = UN2CSettings::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UN2CSettings, TranslationDepth));
		FPropertyChangedEvent Event(DepthProperty);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Settings, Event);
		TestEqual(TEXT("a Translation Depth edit tells the listeners"), Invalidations, 1);
		TestFalse(TEXT("the depth-0 estimate is no longer current"), Service.IsEstimateCurrent(Info));

		const FN2CTokenEstimate Deeper = Service.GetEstimate(Info);
		TestEqual(TEXT("depth 1: Near and Far"), Deeper.NestedGraphCount, 2);
		TestTrue(TEXT("and more tokens"), Deeper.Tokens > Shallow.Tokens);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
