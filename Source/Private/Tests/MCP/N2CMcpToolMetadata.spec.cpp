// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpToolMetadataSpecPrivate
{
	/** A tool that counts how often its definition is built; reaches the protected FN2CMcpToolBase helpers */
	class FProbeTool : public FN2CMcpToolBase
	{
	public:
		FProbeTool(const FString& InName, bool bInLongRunning = false)
			: Name(InName)
			, bLongRunning(bInLongRunning)
		{
		}

		virtual FMcpToolDefinition GetDefinition() const override
		{
			++DefinitionBuilds;
			FMcpToolDefinition Definition(Name, TEXT("Spec probe tool"));
			Definition.Title = TEXT("Probe ") + Name;
			Definition.InputSchema = BuildEmptyObjectSchema();
			SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
			Definition.bIsLongRunning = bLongRunning;
			return Definition;
		}

		virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override
		{
			return FMcpToolCallResult::CreateTextResult(TEXT("probe ran"));
		}

		using FN2CMcpToolBase::BuildInputSchema;
		using FN2CMcpToolBase::BuildEmptyObjectSchema;
		using FN2CMcpToolBase::SetAnnotations;

		FString Name;
		bool bLongRunning = false;
		mutable int32 DefinitionBuilds = 0;
	};

	/** The fields MCP 2025-11-25 defines on Tool; tools/list sends nothing else */
	const TSet<FString>& ToolFields()
	{
		static const TSet<FString> Fields = { TEXT("name"), TEXT("title"), TEXT("description"), TEXT("inputSchema"),
			TEXT("outputSchema"), TEXT("annotations"), TEXT("icons"), TEXT("execution"), TEXT("_meta") };
		return Fields;
	}
}

/**
 * The MCP tool metadata model (card 0164): FMcpToolDefinition serializes title, the four ToolAnnotations hints,
 * outputSchema and _meta (toolset and the anthropic/* entries) as MCP 2025-11-25 defines them; SetAnnotations and
 * BuildInputSchema fill it; FN2CMcpToolManager builds each definition once, at registration, finds it by name and
 * lists the tools sorted by name. Review ids: rec-mcp-spec-clients-7, imp-mcp-transport-5.
 */
BEGIN_DEFINE_SPEC(FN2CMcpToolMetadataSpec, "NodeToCode.MCP.ToolMetadata", N2C_TEST_FLAGS)

	/** Probe tools this spec put in the active set; removed after each case */
	TArray<FString> AddedTools;

	/** Registers a probe tool in the active set and remembers to remove it */
	TSharedRef<N2CMcpToolMetadataSpecPrivate::FProbeTool> AddProbe(const FString& Name, bool bLongRunning = false)
	{
		const TSharedRef<N2CMcpToolMetadataSpecPrivate::FProbeTool> Probe = MakeShared<N2CMcpToolMetadataSpecPrivate::FProbeTool>(Name, bLongRunning);
		if (TestTrue(FString::Printf(TEXT("%s registers"), *Name), FN2CMcpToolManager::Get().RegisterTool(Probe)))
		{
			AddedTools.Add(Name);
		}
		return Probe;
	}

	/** The names of a tools/list answered by the protocol core */
	TArray<FString> ListedNames(TMap<FString, TSharedPtr<FJsonObject>>& OutTools)
	{
		TArray<FString> Names;
		FN2CMcpProtocol Protocol;
		FString Body;
		int32 Status = 0;
		bool bWasInitialize = false;
		TSharedPtr<FJsonValue> Request;
		const TSharedRef<TJsonReader<>> RequestReader = TJsonReaderFactory<>::Create(TEXT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/list\",\"params\":{}}"));
		FJsonSerializer::Deserialize(RequestReader, Request);
		Protocol.ProcessMessage(Request, Body, Status, bWasInitialize);
		TestEqual(TEXT("tools/list HTTP status"), Status, 200);

		TSharedPtr<FJsonObject> Response;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		const TSharedPtr<FJsonObject>* Result = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
		if (!TestTrue(TEXT("tools/list answers a tools array"), FJsonSerializer::Deserialize(Reader, Response) && Response.IsValid()
			&& Response->TryGetObjectField(TEXT("result"), Result) && (*Result)->TryGetArrayField(TEXT("tools"), Tools)))
		{
			return Names;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Tools)
		{
			const TSharedPtr<FJsonObject>* Tool = nullptr;
			FString Name;
			if (Value->TryGetObject(Tool) && (*Tool)->TryGetStringField(TEXT("name"), Name))
			{
				Names.Add(Name);
				OutTools.Add(Name, *Tool);
			}
		}
		return Names;
	}

	/** Checks Names is in ordinal order */
	void TestSorted(const FString& What, const TArray<FString>& Names)
	{
		for (int32 Index = 1; Index < Names.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("%s: '%s' comes before '%s'"), *What, *Names[Index - 1], *Names[Index]),
				Names[Index - 1].Compare(Names[Index], ESearchCase::CaseSensitive) < 0);
		}
	}

END_DEFINE_SPEC(FN2CMcpToolMetadataSpec)

void FN2CMcpToolMetadataSpec::Define()
{
	using namespace N2CMcpToolMetadataSpecPrivate;

	AfterEach([this]()
	{
		for (const FString& Name : AddedTools)
		{
			FN2CMcpToolManager::Get().UnregisterTool(Name);
		}
		AddedTools.Reset();
	});

	Describe(TEXT("a tool definition"), [this]()
	{
		It(TEXT("serializes title, annotations, outputSchema and _meta as the 2025-11-25 Tool schema expects"), [this]()
		{
			FMcpToolDefinition Definition(TEXT("n2c-full"), TEXT("Every field"), N2CMcpToolsets::BlueprintDiscovery);
			Definition.Title = TEXT("Full Tool");
			Definition.InputSchema = FProbeTool::BuildEmptyObjectSchema();
			Definition.OutputSchema = MakeShared<FJsonObject>();
			Definition.OutputSchema->SetStringField(TEXT("type"), TEXT("object"));
			FProbeTool::SetAnnotations(Definition, false, true, false, true);
			Definition.MaxResultSizeChars = 200000;
			Definition.bAlwaysLoad = true;
			Definition.Meta = MakeShared<FJsonObject>();
			Definition.Meta->SetStringField(TEXT("example.com/extra"), TEXT("kept"));
			Definition.bIsLongRunning = true;
			Definition.bReturnsUntrustedContent = true;

			const TSharedPtr<FJsonObject> Json = Definition.ToJson();
			if (!TestTrue(TEXT("ToJson gives an object"), Json.IsValid()))
			{
				return;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Json->Values)
			{
				TestTrue(FString::Printf(TEXT("'%s' is a Tool field (no top-level toolset, no server-only flags)"), *Field.Key),
					ToolFields().Contains(Field.Key));
			}
			TestEqual(TEXT("name"), Json->GetStringField(TEXT("name")), FString(TEXT("n2c-full")));
			TestEqual(TEXT("title"), Json->GetStringField(TEXT("title")), FString(TEXT("Full Tool")));
			TestEqual(TEXT("description"), Json->GetStringField(TEXT("description")), FString(TEXT("Every field")));
			TestTrue(TEXT("inputSchema is an object"), Json->HasTypedField<EJson::Object>(TEXT("inputSchema")));
			TestTrue(TEXT("outputSchema is an object"), Json->HasTypedField<EJson::Object>(TEXT("outputSchema")));

			const TSharedPtr<FJsonObject>* Annotations = nullptr;
			if (TestTrue(TEXT("annotations is an object"), Json->TryGetObjectField(TEXT("annotations"), Annotations)))
			{
				TestEqual(TEXT("annotations has the four hints"), (*Annotations)->Values.Num(), 4);
				bool bValue = true;
				TestTrue(TEXT("readOnlyHint false"), (*Annotations)->TryGetBoolField(TEXT("readOnlyHint"), bValue) && !bValue);
				TestTrue(TEXT("destructiveHint true"), (*Annotations)->TryGetBoolField(TEXT("destructiveHint"), bValue) && bValue);
				TestTrue(TEXT("idempotentHint false"), (*Annotations)->TryGetBoolField(TEXT("idempotentHint"), bValue) && !bValue);
				TestTrue(TEXT("openWorldHint true"), (*Annotations)->TryGetBoolField(TEXT("openWorldHint"), bValue) && bValue);
			}

			const TSharedPtr<FJsonObject>* Meta = nullptr;
			if (TestTrue(TEXT("_meta is an object"), Json->TryGetObjectField(TEXT("_meta"), Meta)))
			{
				FString Toolset;
				TestTrue(TEXT("_meta nodetocode/toolset"), (*Meta)->TryGetStringField(TEXT("nodetocode/toolset"), Toolset) && Toolset == TEXT("BlueprintDiscoveryTools"));
				int32 MaxChars = 0;
				TestTrue(TEXT("_meta anthropic/maxResultSizeChars is the number 200000"),
					(*Meta)->HasTypedField<EJson::Number>(TEXT("anthropic/maxResultSizeChars"))
					&& (*Meta)->TryGetNumberField(TEXT("anthropic/maxResultSizeChars"), MaxChars) && MaxChars == 200000);
				bool bAlwaysLoad = false;
				TestTrue(TEXT("_meta anthropic/alwaysLoad true"), (*Meta)->TryGetBoolField(TEXT("anthropic/alwaysLoad"), bAlwaysLoad) && bAlwaysLoad);
				FString Extra;
				TestTrue(TEXT("other _meta entries are kept"), (*Meta)->TryGetStringField(TEXT("example.com/extra"), Extra) && Extra == TEXT("kept"));
				TestEqual(TEXT("_meta has nothing else"), (*Meta)->Values.Num(), 4);
			}
		});

		It(TEXT("sends no title, annotations or _meta when none is set"), [this]()
		{
			FMcpToolDefinition Definition(TEXT("n2c-plain"), TEXT("Nothing optional"));
			Definition.InputSchema = FProbeTool::BuildEmptyObjectSchema();
			const TSharedPtr<FJsonObject> Json = Definition.ToJson();
			TestFalse(TEXT("no title"), Json->HasField(TEXT("title")));
			TestFalse(TEXT("no annotations"), Json->HasField(TEXT("annotations")));
			TestFalse(TEXT("no _meta"), Json->HasField(TEXT("_meta")));
			TestFalse(TEXT("no outputSchema"), Json->HasField(TEXT("outputSchema")));
			TestFalse(TEXT("no toolset"), Json->HasField(TEXT("toolset")));
		});

		It(TEXT("sends only the hints that are set"), [this]()
		{
			FMcpToolDefinition Definition(TEXT("n2c-one-hint"));
			Definition.Annotations.OpenWorldHint = false;
			const TSharedPtr<FJsonObject> Json = Definition.ToJson();
			const TSharedPtr<FJsonObject>* Annotations = nullptr;
			if (TestTrue(TEXT("annotations is an object"), Json->TryGetObjectField(TEXT("annotations"), Annotations)))
			{
				TestEqual(TEXT("one hint"), (*Annotations)->Values.Num(), 1);
				TestTrue(TEXT("openWorldHint is sent"), (*Annotations)->HasTypedField<EJson::Boolean>(TEXT("openWorldHint")));
			}
			TestFalse(TEXT("an unset readOnlyHint is not read-only"), Definition.Annotations.IsReadOnly());
		});

		It(TEXT("reads its own JSON back, resolving the toolset name to the declared toolset"), [this]()
		{
			FMcpToolDefinition Definition(TEXT("n2c-round-trip"), TEXT("Round trip"), N2CMcpToolsets::ContentBrowser);
			Definition.Title = TEXT("Round Trip");
			FProbeTool::SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
			Definition.MaxResultSizeChars = 1234;
			Definition.bAlwaysLoad = true;

			const FMcpToolDefinition Read = FMcpToolDefinition::FromJson(Definition.ToJson());
			TestEqual(TEXT("name"), Read.Name, Definition.Name);
			TestEqual(TEXT("title"), Read.Title, Definition.Title);
			TestTrue(TEXT("toolset from _meta"), Read.Toolset == &N2CMcpToolsets::ContentBrowser);
			TestTrue(TEXT("readOnlyHint"), Read.Annotations.ReadOnlyHint == TOptional<bool>(true));
			TestTrue(TEXT("destructiveHint"), Read.Annotations.DestructiveHint == TOptional<bool>(false));
			TestTrue(TEXT("idempotentHint"), Read.Annotations.IdempotentHint == TOptional<bool>(true));
			TestTrue(TEXT("openWorldHint"), Read.Annotations.OpenWorldHint == TOptional<bool>(false));
			TestTrue(TEXT("maxResultSizeChars"), Read.MaxResultSizeChars == TOptional<int32>(1234));
			TestTrue(TEXT("alwaysLoad"), Read.bAlwaysLoad);
			TestFalse(TEXT("known _meta keys are not repeated in Meta"), Read.Meta.IsValid());

			// A toolset this build does not declare: no toolset, and the entry stays in Meta as sent
			const TSharedRef<FJsonObject> Other = MakeShared<FJsonObject>();
			Other->SetStringField(TEXT("name"), TEXT("n2c-other"));
			const TSharedRef<FJsonObject> OtherMeta = MakeShared<FJsonObject>();
			OtherMeta->SetStringField(TEXT("nodetocode/toolset"), TEXT("NoSuchTools"));
			Other->SetObjectField(TEXT("_meta"), OtherMeta);
			const FMcpToolDefinition ReadOther = FMcpToolDefinition::FromJson(Other);
			TestTrue(TEXT("unknown toolset: none"), ReadOther.Toolset == nullptr);
			TestTrue(TEXT("unknown toolset: kept in Meta"), ReadOther.Meta.IsValid() && ReadOther.Meta->HasField(TEXT("nodetocode/toolset")));
		});
	});

	Describe(TEXT("SetAnnotations"), [this]()
	{
		It(TEXT("sets all four hints and replaces the ones set before"), [this]()
		{
			FMcpToolDefinition Definition(TEXT("n2c-hints"));
			FProbeTool::SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());
			TestTrue(TEXT("ReadOnly preset is read-only"), Definition.Annotations.IsReadOnly());
			TestTrue(TEXT("ReadOnly preset is closed world"), Definition.Annotations.OpenWorldHint == TOptional<bool>(false));

			FProbeTool::SetAnnotations(Definition, false, true, true, true);
			TestTrue(TEXT("readOnlyHint replaced"), Definition.Annotations.ReadOnlyHint == TOptional<bool>(false));
			TestTrue(TEXT("destructiveHint"), Definition.Annotations.DestructiveHint == TOptional<bool>(true));
			TestTrue(TEXT("idempotentHint"), Definition.Annotations.IdempotentHint == TOptional<bool>(true));
			TestTrue(TEXT("openWorldHint"), Definition.Annotations.OpenWorldHint == TOptional<bool>(true));
		});
	});

	Describe(TEXT("BuildInputSchema"), [this]()
	{
		It(TEXT("writes per-property descriptions"), [this]()
		{
			TMap<FString, FString> Types;
			Types.Add(TEXT("path"), TEXT("string"));
			Types.Add(TEXT("depth"), TEXT("integer"));
			TMap<FString, FString> Descriptions;
			Descriptions.Add(TEXT("path"), TEXT("Asset path"));
			const TSharedPtr<FJsonObject> Schema = FProbeTool::BuildInputSchema(Types, { TEXT("path") }, Descriptions);
			const TSharedPtr<FJsonObject> Properties = Schema->GetObjectField(TEXT("properties"));
			TestEqual(TEXT("path description"), Properties->GetObjectField(TEXT("path"))->GetStringField(TEXT("description")), FString(TEXT("Asset path")));
			TestEqual(TEXT("path type"), Properties->GetObjectField(TEXT("path"))->GetStringField(TEXT("type")), FString(TEXT("string")));
			TestFalse(TEXT("no description without one"), Properties->GetObjectField(TEXT("depth"))->HasField(TEXT("description")));
			TestFalse(TEXT("a tool with arguments does not forbid additional properties"), Schema->HasField(TEXT("additionalProperties")));
		});

		It(TEXT("sets additionalProperties false for a tool with no arguments"), [this]()
		{
			bool bAdditional = true;
			TestTrue(TEXT("BuildInputSchema with no properties"),
				FProbeTool::BuildInputSchema(TMap<FString, FString>())->TryGetBoolField(TEXT("additionalProperties"), bAdditional) && !bAdditional);
			bAdditional = true;
			TestTrue(TEXT("BuildEmptyObjectSchema"),
				FProbeTool::BuildEmptyObjectSchema()->TryGetBoolField(TEXT("additionalProperties"), bAdditional) && !bAdditional);
		});
	});

	Describe(TEXT("the tool manager"), [this]()
	{
		It(TEXT("builds a tool's definition once, when it is registered"), [this]()
		{
			const TSharedRef<FProbeTool> Probe = AddProbe(TEXT("n2c-meta-probe-once"));
			const TSharedRef<FProbeTool> LongProbe = AddProbe(TEXT("n2c-meta-probe-long"), true);
			TestEqual(TEXT("built at registration"), Probe->DefinitionBuilds, 1);

			FN2CMcpToolManager& Tools = FN2CMcpToolManager::Get();
			Tools.GetAllToolDefinitions();
			Tools.GetToolDefinition(Probe->Name);
			TMap<FString, TSharedPtr<FJsonObject>> Listed;
			ListedNames(Listed);
			const FMcpToolCallResult Result = Tools.ExecuteTool(Probe->Name, MakeShared<FJsonObject>());
			TestFalse(TEXT("the probe ran"), Result.bIsError);
			const FMcpToolCallResult LongResult = Tools.ExecuteTool(LongProbe->Name, MakeShared<FJsonObject>());
			TestTrue(TEXT("a long-running tool is refused on the synchronous path (cached bIsLongRunning)"), LongResult.bIsError);

			TestEqual(TEXT("tools/list, lookups and calls do not rebuild it"), Probe->DefinitionBuilds, 1);
			TestEqual(TEXT("the long-running check does not rebuild it"), LongProbe->DefinitionBuilds, 1);
		});

		It(TEXT("finds a cached definition by name, and null for an unknown name"), [this]()
		{
			const TSharedRef<FProbeTool> Probe = AddProbe(TEXT("n2c-meta-probe-find"), true);
			const TSharedPtr<const FMcpToolDefinition> Definition = FN2CMcpToolManager::Get().GetToolDefinition(Probe->Name);
			if (TestTrue(TEXT("found"), Definition.IsValid()))
			{
				TestEqual(TEXT("its name"), Definition->Name, Probe->Name);
				TestTrue(TEXT("its long-running flag"), Definition->bIsLongRunning);
				TestEqual(TEXT("its title"), Definition->Title, TEXT("Probe ") + Probe->Name);
			}
			TestFalse(TEXT("unknown name"), FN2CMcpToolManager::Get().GetToolDefinition(TEXT("n2c-meta-probe-missing")).IsValid());
			FN2CMcpToolManager::Get().UnregisterTool(Probe->Name);
			TestFalse(TEXT("gone after unregistering"), FN2CMcpToolManager::Get().GetToolDefinition(Probe->Name).IsValid());
		});

		It(TEXT("lists tools sorted by name in tools/list"), [this]()
		{
			// Registered out of order, and with whatever tools the session already has active
			AddProbe(TEXT("n2c-meta-probe-zz"));
			AddProbe(TEXT("n2c-meta-probe-aa"));
			AddProbe(TEXT("n2c-meta-probe-mm"));

			TArray<FString> ManagerNames;
			for (const FMcpToolDefinition& Definition : FN2CMcpToolManager::Get().GetAllToolDefinitions())
			{
				ManagerNames.Add(Definition.Name);
			}
			TestSorted(TEXT("GetAllToolDefinitions"), ManagerNames);

			TMap<FString, TSharedPtr<FJsonObject>> Listed;
			const TArray<FString> Names = ListedNames(Listed);
			TestTrue(TEXT("tools/list has the probes"), Names.Contains(TEXT("n2c-meta-probe-aa")) && Names.Contains(TEXT("n2c-meta-probe-mm"))
				&& Names.Contains(TEXT("n2c-meta-probe-zz")));
			TestSorted(TEXT("tools/list"), Names);

			if (const TSharedPtr<FJsonObject>* Probe = Listed.Find(TEXT("n2c-meta-probe-aa")))
			{
				TestEqual(TEXT("tools/list sends the title"), (*Probe)->GetStringField(TEXT("title")), FString(TEXT("Probe n2c-meta-probe-aa")));
				TestTrue(TEXT("tools/list sends the annotations"), (*Probe)->HasTypedField<EJson::Object>(TEXT("annotations")));
				TestFalse(TEXT("tools/list sends no top-level toolset"), (*Probe)->HasField(TEXT("toolset")));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
