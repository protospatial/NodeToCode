// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/Perf/N2CPerfFixtures.h"
#include "Core/N2CCapture.h"
#include "Core/N2CSerializer.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformTime.h"
#include "K2Node_Composite.h"
#include "MCP/Utils/N2CMcpBlueprintExport.h"
#include "Models/N2CJsonContract.h"
#include "Utils/Validators/N2CBlueprintValidator.h"

/**
 * The perf fixtures (card 0359, review later-7 and gap-6 step 1; N2CPerfFixtures.h): BP_N2CPerf_100, _1k and _5k load
 * from the test-only content plugin on every engine, hold 100, 1000 and 5000 K2 nodes over all their graphs (within
 * 5%) in the shape the card asks for, and export to N2C JSON v2 (get-blueprint-n2cjson's capture of every graph) that
 * the validator accepts, before and after a FromJson round trip. The 5k fixture's call chain F_Chain_0..5 captures one
 * more chain graph per Max Translation Depth step from 0 to 5. Capture time, JSON bytes and estimated tokens per node
 * are written to the test log as info (the baseline later-7 asks for; nothing is asserted on time).
 *
 * With N2C_GENERATE_PERF_FIXTURES=1 (UE 5.6 only) the first case regenerates the fixtures before the checks.
 *
 * Known defect translation-core-24 (card 0236): capture logs ERROR 'Missing name' when it meets S_N2CPerf or E_N2CPerf
 * a second time, so each export case expects that error at least once; the fixing card removes the expectation.
 */
BEGIN_DEFINE_SPEC(FN2CPerfFixturesSpec, "NodeToCode.Perf.Fixtures", N2C_TEST_FLAGS)

	/** Loads the fixture, regenerating all of them first when asked (once per session) */
	UBlueprint* LoadFixture(const N2CTest::PerfFixtures::FFixture& Fixture)
	{
		static bool bGenerated = false;
		if (N2CTest::PerfFixtures::ShouldGenerate() && !bGenerated)
		{
			bGenerated = true;
			TArray<FString> Errors;
			TArray<FString> Notes;
			const bool bOk = N2CTest::PerfFixtures::GenerateAll(Errors, Notes);
			for (const FString& Note : Notes)
			{
				AddInfo(TEXT("generate: ") + Note);
			}
			for (const FString& Error : Errors)
			{
				AddError(TEXT("generate: ") + Error);
			}
			if (!bOk)
			{
				return nullptr;
			}
		}
		const FString Path = N2CTest::PerfFixtures::ObjectPath(Fixture.Name);
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
		TestNotNull(FString::Printf(TEXT("%s loads (Tests/NodeToCodeTestContent is junctioned into the host's Plugins folder by build.ps1)"), *Path), Blueprint);
		return Blueprint;
	}

	/** Graphs of Blueprint by kind */
	struct FGraphShape
	{
		int32 EventGraphs = 0;
		int32 Functions = 0;
		int32 Macros = 0;
		int32 Collapsed = 0;
		int32 EventGraphNodes = 0;
	};

	static FGraphShape ShapeOf(const UBlueprint& Blueprint)
	{
		FGraphShape Shape;
		Shape.EventGraphs = Blueprint.UbergraphPages.Num();
		for (const UEdGraph* Graph : Blueprint.FunctionGraphs)
		{
			// UserConstructionScript is not one of the fixture's functions
			Shape.Functions += Graph && Graph->GetFName() != UEdGraphSchema_K2::FN_UserConstructionScript ? 1 : 0;
		}
		Shape.Macros = Blueprint.MacroGraphs.Num();
		TArray<UEdGraph*> All;
		Blueprint.GetAllGraphs(All);
		for (const UEdGraph* Graph : All)
		{
			Shape.Collapsed += Graph && Cast<UK2Node_Composite>(Graph->GetOuter()) ? 1 : 0;
		}
		for (const UEdGraph* Graph : Blueprint.UbergraphPages)
		{
			for (const UEdGraphNode* Node : Graph ? Graph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
			{
				Shape.EventGraphNodes += Node && Node->IsA<UK2Node>() ? 1 : 0;
			}
		}
		return Shape;
	}

	static int32 CapturedNodes(const FN2CBlueprint& Blueprint)
	{
		int32 Count = 0;
		for (const FN2CGraph& Graph : Blueprint.Graphs)
		{
			Count += Graph.Nodes.Num();
		}
		return Count;
	}

END_DEFINE_SPEC(FN2CPerfFixturesSpec)

void FN2CPerfFixturesSpec::Define()
{
	using namespace N2CTest::PerfFixtures;

	for (const FFixture& Fixture : All())
	{
		Describe(Fixture.Name, [this, Fixture]()
		{
			It(FString::Printf(TEXT("loads and holds %d K2 nodes (within 5%%) in the card's shape"), Fixture.TargetNodes), [this, Fixture]()
			{
				UBlueprint* Blueprint = LoadFixture(Fixture);
				if (!Blueprint)
				{
					return;
				}
				const int32 Nodes = CountK2Nodes(*Blueprint);
				AddInfo(FString::Printf(TEXT("%s: %d K2 nodes"), Fixture.Name, Nodes));
				TestTrue(FString::Printf(TEXT("%d K2 nodes is within 5%% of %d"), Nodes, Fixture.TargetNodes),
					FMath::Abs(Nodes - Fixture.TargetNodes) * 20 <= Fixture.TargetNodes);

				const FGraphShape Shape = ShapeOf(*Blueprint);
				TestEqual(TEXT("event graphs"), Shape.EventGraphs, 1);
				TestEqual(TEXT("functions"), Shape.Functions, Fixture.Functions);
				TestEqual(TEXT("macros"), Shape.Macros, Fixture.Macros);
				TestEqual(TEXT("collapsed graphs"), Shape.Collapsed, Fixture.CollapsedGraphs);
				if (Fixture.TargetNodes >= 5000)
				{
					// "an EventGraph of about 2k nodes" (collapsed graph nodes not counted)
					TestTrue(FString::Printf(TEXT("the event graph holds about 2000 K2 nodes (%d)"), Shape.EventGraphNodes),
						Shape.EventGraphNodes >= 1700 && Shape.EventGraphNodes <= 2300);
				}
			});

			It(TEXT("exports every graph to N2C JSON v2 that validates and survives FromJson"), [this, Fixture]()
			{
				UBlueprint* Blueprint = LoadFixture(Fixture);
				if (!Blueprint)
				{
					return;
				}
				// Known defect translation-core-24 (card 0236): a user struct or enum met a second time logs 'Missing name'
				AddExpectedError(TEXT("Missing name"), EAutomationExpectedErrorFlags::Contains, 0);

				const double Start = FPlatformTime::Seconds();
				const TValueOrError<FN2CCapture, FString> Capture = N2CMcpBlueprintExport::CaptureAllGraphs(*Blueprint, 0);
				const double CaptureSeconds = FPlatformTime::Seconds() - Start;
				if (!TestTrue(TEXT("CaptureAllGraphs succeeds") + (Capture.HasError() ? TEXT(": ") + Capture.GetError() : FString()),
					Capture.HasValue()))
				{
					return;
				}
				const FN2CBlueprint& Captured = Capture.GetValue().Blueprint;
				FString Error;
				TestTrue(TEXT("the capture validates") + (Error.IsEmpty() ? FString() : TEXT(": ") + Error),
					FN2CBlueprintValidator().Validate(Captured, Error));

				const double SerializeStart = FPlatformTime::Seconds();
				const FString Json = FN2CSerializer::ToJson(Captured, EN2CJsonStyle::Condensed);
				const double SerializeSeconds = FPlatformTime::Seconds() - SerializeStart;
				const TSharedPtr<FJsonObject> Parsed = N2CTest::ParseJsonObject(Json);
				if (!TestTrue(TEXT("ToJson output parses"), Parsed.IsValid()))
				{
					return;
				}
				const FString Version = Parsed->GetStringField(N2CJsonContract::VersionField);
				TestEqual(TEXT("the document is N2C JSON v2 (") + Version + TEXT(")"), N2CJsonContract::ParseMajor(Version), 2);

				FN2CBlueprint Reread;
				if (TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Json, Reread)))
				{
					FString RereadError;
					TestTrue(TEXT("the document read back validates") + (RereadError.IsEmpty() ? FString() : TEXT(": ") + RereadError),
						FN2CBlueprintValidator().Validate(Reread, RereadError));
					TestEqual(TEXT("graphs read back"), Reread.Graphs.Num(), Captured.Graphs.Num());
					TestEqual(TEXT("nodes read back"), CapturedNodes(Reread), CapturedNodes(Captured));
				}

				const int32 Nodes = FMath::Max(1, CapturedNodes(Captured));
				const int32 Tokens = N2CMcpBlueprintExport::EstimateTokens(Json);
				AddInfo(FString::Printf(TEXT("%s: %d graphs, %d captured nodes; capture %.1f ms, ToJson %.1f ms; %d bytes (%.0f per node), "
					"about %d tokens (%.1f per node, %.2f characters per token)"), Fixture.Name, Captured.Graphs.Num(), Nodes,
					CaptureSeconds * 1000.0, SerializeSeconds * 1000.0, Json.Len(), double(Json.Len()) / Nodes, Tokens,
					double(Tokens) / Nodes, N2CMcpBlueprintExport::JsonCharsPerToken));
			});

			if (Fixture.ChainDepth > 0)
			{
				It(FString::Printf(TEXT("captures one more F_Chain graph per Max Translation Depth step from 0 to %d"), Fixture.ChainDepth),
					[this, Fixture]()
				{
					UBlueprint* Blueprint = LoadFixture(Fixture);
					if (!Blueprint)
					{
						return;
					}
					AddExpectedError(TEXT("Missing name"), EAutomationExpectedErrorFlags::Contains, 0);
					// Each depth stops at the chain's next graph and says so
					AddExpectedMessage(TEXT("maximum translation depth reached"), ELogVerbosity::Warning,
						EAutomationExpectedMessageFlags::Contains, 0);
					const FString RootName = FString(ChainPrefix) + TEXT("0");
					UEdGraph* Root = nullptr;
					for (UEdGraph* Graph : Blueprint->FunctionGraphs)
					{
						Root = Graph && Graph->GetName() == RootName ? Graph : Root;
					}
					if (!TestNotNull(TEXT("F_Chain_0"), Root))
					{
						return;
					}
					for (int32 Depth = 0; Depth <= Fixture.ChainDepth; ++Depth)
					{
						FN2CCaptureOptions Options;
						Options.Depth = Depth;
						const double Start = FPlatformTime::Seconds();
						const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*Root, Options);
						const double Seconds = FPlatformTime::Seconds() - Start;
						if (!TestTrue(FString::Printf(TEXT("depth %d captures"), Depth), Capture.HasValue()))
						{
							continue;
						}
						TArray<FString> ChainGraphs;
						for (const FN2CGraph& Graph : Capture.GetValue().Blueprint.Graphs)
						{
							if (Graph.Name.StartsWith(ChainPrefix))
							{
								ChainGraphs.Add(Graph.Name);
							}
						}
						TestEqual(FString::Printf(TEXT("depth %d captures F_Chain_0..%d (%s)"), Depth, Depth,
							*FString::Join(ChainGraphs, TEXT(", "))), ChainGraphs.Num(), Depth + 1);
						AddInfo(FString::Printf(TEXT("depth %d: %d graphs, %d nodes, %.1f ms"), Depth,
							Capture.GetValue().Blueprint.Graphs.Num(), CapturedNodes(Capture.GetValue().Blueprint), Seconds * 1000.0));
					}
				});
			}
		});
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
