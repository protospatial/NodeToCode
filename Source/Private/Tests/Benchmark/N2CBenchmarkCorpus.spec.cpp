// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CCapture.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/Benchmark/N2CBenchmarkCorpus.h"
#include "Tests/N2CTestHelpers.h"
#include "Tests/Perf/N2CPerfFixtures.h"

/**
 * The benchmark corpus (card 0364; N2CBenchmarkCorpus.h): every corpus Blueprint loads from
 * /NodeToCodeTestContent/Benchmark, captures to N2C JSON v2 with no error, and has the shape its
 * Benchmarks/corpus/shapes/<name>.json records; Benchmarks/corpus/manifest.json lists every corpus and perf graph as
 * the assets have them. With N2C_GENERATE_BENCHMARK_CORPUS=1 (UE 5.6 only) the first case regenerates the corpus.
 */
BEGIN_DEFINE_SPEC(FN2CBenchmarkCorpusSpec, "NodeToCode.Benchmark.Corpus", N2C_TEST_FLAGS)

	bool GenerateIfAsked()
	{
		static bool bGenerated = false;
		if (!N2CTest::BenchmarkCorpus::ShouldGenerate() || bGenerated)
		{
			return true;
		}
		bGenerated = true;
		TArray<FString> Errors;
		TArray<FString> Notes;
		const bool bOk = N2CTest::BenchmarkCorpus::GenerateAll(Errors, Notes);
		for (const FString& Note : Notes)
		{
			AddInfo(TEXT("generate: ") + Note);
		}
		for (const FString& Error : Errors)
		{
			AddError(TEXT("generate: ") + Error);
		}
		return bOk;
	}

	static TSharedPtr<FJsonObject> ReadJson(const FString& File)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *File) ? N2CTest::ParseJsonObject(Text) : nullptr;
	}

	/** The graphs a translation of the whole Blueprint would capture: every top-level graph with a node */
	static TArray<UEdGraph*> TopLevelGraphs(UBlueprint& Blueprint)
	{
		TArray<UEdGraph*> Candidates;
		Candidates.Append(Blueprint.UbergraphPages);
		Candidates.Append(Blueprint.FunctionGraphs);
		Candidates.Append(Blueprint.MacroGraphs);
		for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
		{
			Candidates.Append(Interface.Graphs);
		}
		TArray<UEdGraph*> Out;
		for (UEdGraph* Graph : Candidates)
		{
			if (Graph && Graph->Nodes.ContainsByPredicate([](const UEdGraphNode* Node) { return Node && !Node->IsAutomaticallyPlacedGhostNode(); }))
			{
				Out.Add(Graph);
			}
		}
		return Out;
	}

END_DEFINE_SPEC(FN2CBenchmarkCorpusSpec)

void FN2CBenchmarkCorpusSpec::Define()
{
	It(TEXT("finds the checkout's Benchmarks/corpus with a manifest"), [this]()
	{
		if (!GenerateIfAsked())
		{
			return;
		}
		const FString Corpus = N2CTest::BenchmarkCorpus::CorpusDir();
		TestFalse(TEXT("the checkout root is found (through the host's Source junction, or N2C_REPO_ROOT)"), Corpus.IsEmpty());
		TestTrue(TEXT("manifest.json exists in ") + Corpus, FPaths::FileExists(Corpus / TEXT("manifest.json")));
	});

	for (const N2CTest::BenchmarkCorpus::FFixture& Fixture : N2CTest::BenchmarkCorpus::All())
	{
		const FString Name = Fixture.Name;
		It(FString::Printf(TEXT("%s loads, captures to N2C JSON with no error and has its recorded shape"), *Name), [this, Name]()
		{
			if (!GenerateIfAsked())
			{
				return;
			}
			UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *N2CTest::BenchmarkCorpus::ObjectPath(*Name));
			if (!TestNotNull(Name + TEXT(" loads (Tests/NodeToCodeTestContent is junctioned into the host by build.ps1)"), Blueprint))
			{
				return;
			}
			TestTrue(Name + TEXT(" is up to date"), Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings || Blueprint->Status == BS_Unknown);

			const TArray<UEdGraph*> Graphs = TopLevelGraphs(*Blueprint);
			const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraphs(Graphs);
			if (TestTrue(Name + TEXT(" captures (") + (Capture.HasError() ? Capture.GetError() : FString()) + TEXT(")"), Capture.HasValue()))
			{
				TestFalse(Name + TEXT(": no error diagnostic"), Capture.GetValue().HasErrors());
				TestTrue(Name + TEXT(": the document validates"), Capture.GetValue().Blueprint.IsValid());
				TestTrue(Name + TEXT(": every graph with a node is captured"), Capture.GetValue().Blueprint.Graphs.Num() >= Graphs.Num());
			}

			const FString ShapeFile = N2CTest::BenchmarkCorpus::CorpusDir() / TEXT("shapes") / Name + TEXT(".json");
			const TSharedPtr<FJsonObject> Recorded = ReadJson(ShapeFile);
			if (!TestTrue(TEXT("the shape file reads: ") + ShapeFile, Recorded.IsValid()))
			{
				return;
			}
			TestEqual(Name + TEXT(": the Blueprint has the recorded shape (class, parent, interfaces, components, members, functions, events, calls)"),
				N2CTest::BenchmarkCorpus::StableJson(N2CTest::BenchmarkCorpus::ComputeShape(*Blueprint)),
				N2CTest::BenchmarkCorpus::StableJson(Recorded.ToSharedRef()));
		});
	}

	It(TEXT("has a manifest that lists every corpus and perf graph as the assets have them"), [this]()
	{
		if (!GenerateIfAsked())
		{
			return;
		}
		const TSharedPtr<FJsonObject> Recorded = ReadJson(N2CTest::BenchmarkCorpus::CorpusDir() / TEXT("manifest.json"));
		if (!TestTrue(TEXT("manifest.json reads"), Recorded.IsValid()))
		{
			return;
		}
		TArray<FString> Errors;
		const TSharedRef<FJsonObject> Current = N2CTest::BenchmarkCorpus::BuildManifest(Errors);
		TestEqual(TEXT("every manifest asset loads"), FString::Join(Errors, TEXT("; ")), FString());
		TestEqual(TEXT("the manifest is what the assets give (regenerate the corpus if this differs)"),
			N2CTest::BenchmarkCorpus::StableJson(Current), N2CTest::BenchmarkCorpus::StableJson(Recorded.ToSharedRef()));
		TestEqual(TEXT("one entry per corpus Blueprint and perf fixture"), Current->GetArrayField(TEXT("entries")).Num(),
			N2CTest::BenchmarkCorpus::All().Num() + N2CTest::PerfFixtures::All().Num());
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
