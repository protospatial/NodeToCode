// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Core/N2CSerializer.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CJsonContract.h"
#include "Utils/Validators/N2CBlueprintValidator.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

/**
 * Card 0445: the N2C JSON reader policy (N2CJsonContract.h, decision 0003 amendment). A 2.0 build reads a later 2.x
 * document, here a synthetic material capture, without rejecting it or dropping anything: unknown blueprint, graph,
 * node and pin kinds become Unknown with their text kept, unknown fields are kept, each with one structured warning,
 * and ToJson writes the document back byte for byte. Documents this build knows (the 1.x fixtures and the capture
 * goldens) give no warning; text that is not N2C JSON is still rejected.
 */
BEGIN_DEFINE_SPEC(FN2CSerializerForwardCompatSpec, "NodeToCode.Serializer.ForwardCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	FString FixturePath() const
	{
		return N2CTest::FixturesDir(TEXT("Capture/ForwardCompat")) / TEXT("UnknownKinds_2.1.json");
	}

	bool LoadFixture(FString& OutText)
	{
		if (!TestTrue(TEXT("the forward-compat fixture loads"), FFileHelper::LoadFileToString(OutText, *FixturePath())))
		{
			return false;
		}
		OutText.TrimStartAndEndInline();
		return true;
	}

	/** The reader's warnings (one per unknown thing) are expected here, and with bValidator the validator's too */
	void ExpectReadWarnings(bool bValidator = false)
	{
		AddExpectedMessage(TEXT("N2C JSON "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false);
		if (bValidator)
		{
			AddExpectedMessage(TEXT("this build does not know"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false);
		}
	}

	static FString WarningLines(const TArray<FN2CJsonReadWarning>& Warnings)
	{
		TArray<FString> Lines;
		for (const FN2CJsonReadWarning& Warning : Warnings)
		{
			Lines.Add(FString::Printf(TEXT("%s %s %s"), *Warning.Code, *Warning.Path, *Warning.Value));
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	static FString Condensed(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Text;
	}

END_DEFINE_SPEC(FN2CSerializerForwardCompatSpec)

void FN2CSerializerForwardCompatSpec::Define()
{
	Describe(TEXT("a later 2.x document with unknown kinds and fields"), [this]()
	{
		It(TEXT("is read with one structured warning per unknown kind and field"), [this]()
		{
			FString Text;
			if (!LoadFixture(Text))
			{
				return;
			}
			ExpectReadWarnings();
			FN2CBlueprint Blueprint;
			TArray<FN2CJsonReadWarning> Warnings;
			if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint, Warnings)))
			{
				return;
			}
			const FString Expected = FString::Join(TArray<FString>{
				TEXT("NEWER_VERSION version 2.1.0"),
				TEXT("UNKNOWN_BLUEPRINT_TYPE metadata.blueprint_type Material"),
				TEXT("UNKNOWN_FIELD metadata.asset_kind asset_kind"),
				TEXT("UNKNOWN_GRAPH_TYPE graphs[0].graph_type MaterialGraph"),
				TEXT("UNKNOWN_NODE_TYPE graphs[0].nodes[0].type MaterialExpression"),
				TEXT("UNKNOWN_PIN_TYPE graphs[0].nodes[0].input_pins[0].type Float2"),
				TEXT("UNKNOWN_PIN_TYPE graphs[0].nodes[0].output_pins[0].type Float3"),
				TEXT("UNKNOWN_FIELD graphs[0].nodes[0].output_pins[0].channel_mask channel_mask"),
				TEXT("UNKNOWN_FIELD graphs[0].nodes[0].expression_guid expression_guid"),
				TEXT("UNKNOWN_NODE_TYPE graphs[0].nodes[1].type MaterialRoot"),
				TEXT("UNKNOWN_PIN_TYPE graphs[0].nodes[1].input_pins[0].type Float3"),
				TEXT("UNKNOWN_FIELD graphs[0].nodes[1].input_pins[0].material_property material_property"),
				TEXT("UNKNOWN_FIELD graphs[0].domain domain"),
				TEXT("UNKNOWN_NODE_TYPE graphs[1].nodes[1].type FutureLatentAction"),
				TEXT("UNKNOWN_PIN_TYPE graphs[1].nodes[1].output_pins[1].type FutureHandle"),
				TEXT("UNKNOWN_FIELD extensions extensions") }, TEXT("\n"));
			TestEqual(TEXT("the warnings, in read order"), WarningLines(Warnings), Expected);
			for (const FN2CJsonReadWarning& Warning : Warnings)
			{
				TestFalse(Warning.Path + TEXT(": the warning has a message"), Warning.Message.IsEmpty());
			}
		});

		It(TEXT("keeps every graph, node, pin and flow, with the unknown kinds' text"), [this]()
		{
			FString Text;
			if (!LoadFixture(Text))
			{
				return;
			}
			ExpectReadWarnings();
			FN2CBlueprint Blueprint;
			if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint))
				|| !TestEqual(TEXT("graphs"), Blueprint.Graphs.Num(), 2))
			{
				return;
			}
			TestEqual(TEXT("the version stays 2.1.0"), Blueprint.Version.Value, FString(TEXT("2.1.0")));
			TestTrue(TEXT("blueprint_type is Unknown"), Blueprint.Metadata.BlueprintType == EN2CBlueprintType::Unknown);
			TestEqual(TEXT("blueprint_type text"), Blueprint.Metadata.UnknownBlueprintType, FString(TEXT("Material")));
			TestEqual(TEXT("metadata.asset_kind kept"), Blueprint.Metadata.UnknownFields.FindRef(TEXT("asset_kind")), FString(TEXT("\"Material\"")));
			TestTrue(TEXT("the top-level extensions object kept"), Blueprint.UnknownFields.Contains(TEXT("extensions")));

			const FN2CGraph& Material = Blueprint.Graphs[0];
			TestTrue(TEXT("graph_type is Unknown"), Material.GraphType == EN2CGraphType::Unknown);
			TestEqual(TEXT("graph_type text"), Material.UnknownGraphType, FString(TEXT("MaterialGraph")));
			TestEqual(TEXT("graph domain kept"), Material.UnknownFields.FindRef(TEXT("domain")), FString(TEXT("\"Surface\"")));
			if (TestEqual(TEXT("material nodes"), Material.Nodes.Num(), 2))
			{
				const FN2CNodeDefinition& Sample = Material.Nodes[0];
				TestTrue(TEXT("node type is Unknown"), Sample.NodeType == EN2CNodeType::Unknown);
				TestEqual(TEXT("node type text"), Sample.UnknownType, FString(TEXT("MaterialExpression")));
				TestEqual(TEXT("node class"), Sample.NodeClass, FString(TEXT("MaterialExpressionTextureSample")));
				TestEqual(TEXT("expression_guid kept"), Sample.UnknownFields.FindRef(TEXT("expression_guid")),
					FString(TEXT("\"0A1B2C3D-0000-0000-0000-000000000001\"")));
				if (TestEqual(TEXT("sample pins"), Sample.InputPins.Num() + Sample.OutputPins.Num(), 2))
				{
					TestTrue(TEXT("pin type is Unknown"), Sample.OutputPins[0].Type == EN2CPinType::Unknown);
					TestEqual(TEXT("pin type text"), Sample.OutputPins[0].UnknownType, FString(TEXT("Float3")));
					TestEqual(TEXT("channel_mask kept"), Sample.OutputPins[0].UnknownFields.FindRef(TEXT("channel_mask")),
						FString(TEXT("[true,true,true,false]")));
				}
				TestEqual(TEXT("root pins"), Material.Nodes[1].InputPins.Num(), 1);
			}
			const FN2CDataFlowTargets* Edge = Material.Flows.Data.Find(TEXT("N1.P2"));
			if (TestNotNull(TEXT("the material data edge"), Edge))
			{
				TestEqual(TEXT("the material data edge"), FString::Join(Edge->Targets, TEXT(",")), FString(TEXT("N2.P1")));
			}

			const FN2CGraph& Function = Blueprint.Graphs[1];
			TestTrue(TEXT("a known graph_type stays known"), Function.GraphType == EN2CGraphType::Function && Function.UnknownGraphType.IsEmpty());
			if (TestEqual(TEXT("function nodes"), Function.Nodes.Num(), 2))
			{
				TestTrue(TEXT("a known node type stays known"), Function.Nodes[0].NodeType == EN2CNodeType::FunctionEntry);
				TestTrue(TEXT("the latent node is Unknown"), Function.Nodes[1].NodeType == EN2CNodeType::Unknown);
				TestEqual(TEXT("the latent node keeps its pins"), Function.Nodes[1].InputPins.Num() + Function.Nodes[1].OutputPins.Num(), 4);
				TestTrue(TEXT("its exec pins are Exec"), Function.Nodes[1].InputPins[0].Type == EN2CPinType::Exec);
			}
			TestEqual(TEXT("the exec flow into the unknown node"), FString::Join(Function.Flows.Execution, TEXT(",")), FString(TEXT("N1.P1->N2.P1")));
		});

		It(TEXT("round-trips byte for byte through FromJson and ToJson"), [this]()
		{
			FString Text;
			if (!LoadFixture(Text))
			{
				return;
			}
			ExpectReadWarnings();
			FN2CBlueprint Blueprint;
			if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)))
			{
				return;
			}
			TestEqual(TEXT("condensed ToJson gives the fixture back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Text);

			const FString Pretty = FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Pretty);
			FN2CBlueprint Again;
			if (TestTrue(TEXT("FromJson reads the pretty form"), FN2CSerializer::FromJson(Pretty, Again)))
			{
				TestEqual(TEXT("pretty ToJson -> FromJson -> ToJson is stable"), FN2CSerializer::ToJson(Again, EN2CJsonStyle::Pretty), Pretty);
				TestEqual(TEXT("and condensed still gives the fixture"), FN2CSerializer::ToJson(Again, EN2CJsonStyle::Condensed), Text);
			}
		});

		It(TEXT("passes the validator, with warnings"), [this]()
		{
			FString Text;
			if (!LoadFixture(Text))
			{
				return;
			}
			ExpectReadWarnings(true);
			FN2CBlueprint Blueprint;
			if (!TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Text, Blueprint)))
			{
				return;
			}
			FN2CBlueprintValidator Validator;
			FString Error;
			TestTrue(TEXT("the validator accepts it"), Validator.Validate(Blueprint, Error));
			TestEqual(TEXT("no validation error"), Error, FString());
			TestTrue(TEXT("IsValid"), Blueprint.IsValid());
		});

		It(TEXT("reads the literal kind \"Unknown\" as Unknown without a warning and writes it back"), [this]()
		{
			FString Text;
			if (!LoadFixture(Text))
			{
				return;
			}
			ExpectReadWarnings();
			const FString Literal = Text.Replace(TEXT("\"type\":\"MaterialRoot\""), TEXT("\"type\":\"Unknown\""));
			FN2CBlueprint Blueprint;
			TArray<FN2CJsonReadWarning> Warnings;
			if (TestTrue(TEXT("FromJson reads it"), FN2CSerializer::FromJson(Literal, Blueprint, Warnings)))
			{
				TestFalse(TEXT("no warning names MaterialRoot"), WarningLines(Warnings).Contains(TEXT("MaterialRoot")));
				TestFalse(TEXT("no node-type warning for graphs[0].nodes[1]"), WarningLines(Warnings).Contains(TEXT("graphs[0].nodes[1].type")));
				TestEqual(TEXT("written back"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Literal);
			}
		});
	});

	Describe(TEXT("documents this build knows"), [this]()
	{
		It(TEXT("give no warning: the 1.x fixtures and every capture golden"), [this]()
		{
			TArray<FString> Files;
			const FString V1Dir = N2CTest::FixturesDir(TEXT("Capture/V1"));
			IFileManager::Get().FindFiles(Files, *(V1Dir / TEXT("*.json")), true, false);
			TestTrue(TEXT("1.x fixtures found"), Files.Num() >= 4);
			for (const FString& File : Files)
			{
				FString Text;
				FN2CBlueprint Blueprint;
				TArray<FN2CJsonReadWarning> Warnings;
				if (TestTrue(File + TEXT(" loads"), FFileHelper::LoadFileToString(Text, *(V1Dir / File)))
					&& TestTrue(File + TEXT(" is read"), FN2CSerializer::FromJson(Text, Blueprint, Warnings)))
				{
					TestEqual(File + TEXT(": warnings"), WarningLines(Warnings), FString());
					TestEqual(File + TEXT(": read into the current version"), Blueprint.Version.Value, FString(N2CJsonContract::Version));
				}
			}

			TArray<FString> Goldens;
			const FString GoldenDir = N2CTest::GoldensDir(TEXT("Capture"));
			IFileManager::Get().FindFiles(Goldens, *(GoldenDir / TEXT("*.json")), true, false);
			TestTrue(TEXT("capture goldens found"), Goldens.Num() >= 20);
			for (const FString& File : Goldens)
			{
				FString Text;
				if (!TestTrue(File + TEXT(" loads"), FFileHelper::LoadFileToString(Text, *(GoldenDir / File))))
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Golden = N2CTest::ParseJsonObject(Text);
				const TSharedPtr<FJsonObject>* Capture = nullptr;
				if (!TestTrue(File + TEXT(" has a capture"), Golden.IsValid() && Golden->TryGetObjectField(TEXT("capture"), Capture)))
				{
					continue;
				}
				const FString Document = Condensed(*Capture);
				FN2CBlueprint Blueprint;
				TArray<FN2CJsonReadWarning> Warnings;
				if (TestTrue(File + TEXT(" is read"), FN2CSerializer::FromJson(Document, Blueprint, Warnings)))
				{
					TestEqual(File + TEXT(": warnings"), WarningLines(Warnings), FString());
					TestEqual(File + TEXT(": written back unchanged"), FN2CSerializer::ToJson(Blueprint, EN2CJsonStyle::Condensed), Document);
				}
			}
		});
	});

	Describe(TEXT("negative controls"), [this]()
	{
		It(TEXT("still rejects text that is not JSON, and JSON that is not N2C JSON"), [this]()
		{
			FN2CBlueprint Blueprint;
			AddExpectedError(TEXT("Failed to parse JSON string"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("not JSON"), FN2CSerializer::FromJson(TEXT("{\"version\":\"2.1.0\",\"metadata\":"), Blueprint));
			AddExpectedError(TEXT("Missing metadata object in JSON"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("no metadata"), FN2CSerializer::FromJson(TEXT("{\"version\":\"2.1.0\",\"graphs\":[]}"), Blueprint));
			AddExpectedError(TEXT("Missing version field in JSON"), EAutomationExpectedErrorFlags::Contains, 1);
			TestFalse(TEXT("no version"), FN2CSerializer::FromJson(TEXT("{\"name\":\"x\"}"), Blueprint));
		});

		It(TEXT("names a node it has to skip instead of dropping it silently"), [this]()
		{
			FString Text;
			if (!LoadFixture(Text))
			{
				return;
			}
			ExpectReadWarnings();
			AddExpectedError(TEXT("Missing required node fields in JSON"), EAutomationExpectedErrorFlags::Contains, 1);
			// The material root without its "id": malformed, not unknown
			const FString Malformed = Text.Replace(TEXT("{\"id\":\"N2\",\"type\":\"MaterialRoot\","), TEXT("{\"type\":\"MaterialRoot\","));
			if (!TestNotEqual(TEXT("the fixture was changed"), Malformed, Text))
			{
				return;
			}
			FN2CBlueprint Blueprint;
			TArray<FN2CJsonReadWarning> Warnings;
			if (TestTrue(TEXT("FromJson reads the rest"), FN2CSerializer::FromJson(Malformed, Blueprint, Warnings)))
			{
				TestEqual(TEXT("material nodes"), Blueprint.Graphs[0].Nodes.Num(), 1);
				TestTrue(TEXT("SKIPPED_NODE names it"), WarningLines(Warnings).Contains(TEXT("SKIPPED_NODE graphs[0].nodes[1]")));
			}
		});

		It(TEXT("names a graph it has to skip, and fails when no graph is left"), [this]()
		{
			AddExpectedMessage(TEXT("N2C JSON "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0, false);
			AddExpectedError(TEXT("Missing nodes array in JSON"), EAutomationExpectedErrorFlags::Contains, 1);
			const FString Text = TEXT("{\"version\":\"2.0.0\",\"metadata\":{\"name\":\"BP\",\"blueprint_type\":\"Normal\",\"blueprint_class\":\"BP\"},")
				TEXT("\"graphs\":[{\"name\":\"G\",\"graph_type\":\"Function\",\"flows\":{\"execution\":[],\"data\":{}}}],\"structs\":[],\"enums\":[]}");
			FN2CBlueprint Blueprint;
			TArray<FN2CJsonReadWarning> Warnings;
			TestFalse(TEXT("no graph could be read"), FN2CSerializer::FromJson(Text, Blueprint, Warnings));
			TestEqual(TEXT("the warning"), WarningLines(Warnings), FString(TEXT("SKIPPED_GRAPH graphs[0] ")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
