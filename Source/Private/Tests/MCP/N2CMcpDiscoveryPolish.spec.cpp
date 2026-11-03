// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/Dependencies/N2CDependencyFixtures.h"
#include "Tests/MCP/N2CMcpMemoryResponder.h"
#include "MCP/Server/N2CMcpProtocol.h"
#include "MCP/Server/N2CMcpCompletion.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Prompts/N2CMcpPromptManager.h"
#include "MCP/Prompts/Implementations/N2CMcpCodeGenerationPrompt.h"
#include "MCP/Resources/Implementations/N2CMcpBlueprintResource.h"
#include "MCP/Resources/Implementations/N2CMcpPythonStubResource.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CMcpDiscoverySpec
{
	const TCHAR* PromptName = TEXT("n2c-test-discovery-prompt");

	TSharedPtr<FJsonObject> ParseObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	FString ModernMeta()
	{
		return TEXT("\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\",")
			TEXT("\"io.modelcontextprotocol/clientInfo\":{\"name\":\"n2c-discovery-spec\",\"version\":\"1\"},")
			TEXT("\"io.modelcontextprotocol/clientCapabilities\":{}}");
	}

	FN2CMcpInbound ModernInbound(const FString& Id, const FString& Method, const FString& Extra = FString())
	{
		FN2CMcpInbound In = N2CTest::MakeMcpInbound(FString::Printf(TEXT("{\"jsonrpc\":\"2.0\",\"id\":%s,\"method\":\"%s\",\"params\":{%s%s}}"),
			*Id, *Method, *ModernMeta(), *Extra));
		In.Headers.Add(TEXT("MCP-Protocol-Version"), { TEXT("2026-07-28") });
		In.Headers.Add(TEXT("Mcp-Method"), { Method });
		return In;
	}

	TArray<FString> Strings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsString());
			}
		}
		return Out;
	}

	/** completion/complete params */
	TSharedPtr<FJsonObject> CompletionParams(const FString& RefType, const FString& RefKey, const FString& RefValue, const FString& Argument,
		const FString& Value)
	{
		const TSharedRef<FJsonObject> Ref = MakeShared<FJsonObject>();
		Ref->SetStringField(TEXT("type"), RefType);
		Ref->SetStringField(RefKey, RefValue);
		const TSharedRef<FJsonObject> Arg = MakeShared<FJsonObject>();
		Arg->SetStringField(TEXT("name"), Argument);
		Arg->SetStringField(TEXT("value"), Value);
		const TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetObjectField(TEXT("ref"), Ref);
		Params->SetObjectField(TEXT("argument"), Arg);
		return Params;
	}
}

/**
 * Card 0214: discovery polish. serverInfo has title, description, websiteUrl and, on initialize and server/discover
 * only, the plugin icon as a data: URI PNG; capabilities advertise completions, and resources.subscribe only to a
 * 2026-07-28 client whose transport can stream. completion/complete offers prompt argument values and Blueprint paths
 * for the nodetocode://blueprint/{+path} template. A subscriptions/listen stream subscribed to a Blueprint's resource
 * gets notifications/resources/updated after the Blueprint compiles. Resources carry audience and priority
 * annotations; the Python API stub is a resource.
 */
BEGIN_DEFINE_SPEC(FN2CMcpDiscoveryPolishSpec, "NodeToCode.MCP.Discovery", N2C_TEST_FLAGS)

	TSharedRef<N2CTest::FN2CMcpMemoryResponder> Send(FN2CMcpProtocol& Core, const FN2CMcpInbound& In, bool bCanStream = false)
	{
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = MakeShared<N2CTest::FN2CMcpMemoryResponder>(bCanStream);
		Core.Handle(In, Responder);
		return Responder;
	}

	TSharedPtr<FJsonObject> Result(const N2CTest::FN2CMcpMemoryResponder& Responder)
	{
		const TSharedPtr<FJsonObject> Message = N2CMcpDiscoverySpec::ParseObject(Responder.LastReply().Body);
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Message.IsValid() && Message->TryGetObjectField(TEXT("result"), Out) ? *Out : nullptr;
	}

	void ExpectFullServerInfo(const FString& What, const TSharedPtr<FJsonObject>& ServerInfo)
	{
		if (!TestTrue(What + TEXT(": serverInfo"), ServerInfo.IsValid()))
		{
			return;
		}
		TestEqual(What + TEXT(": name unchanged"), ServerInfo->GetStringField(TEXT("name")), FString(TEXT("NodeToCodeMCPServer")));
		TestEqual(What + TEXT(": title"), ServerInfo->GetStringField(TEXT("title")), FString(TEXT("NodeToCode")));
		TestFalse(What + TEXT(": description"), ServerInfo->GetStringField(TEXT("description")).IsEmpty());
		TestTrue(What + TEXT(": websiteUrl"), ServerInfo->GetStringField(TEXT("websiteUrl")).StartsWith(TEXT("https://")));
		const TArray<TSharedPtr<FJsonValue>>* Icons = nullptr;
		if (TestTrue(What + TEXT(": one icon"), ServerInfo->TryGetArrayField(TEXT("icons"), Icons) && Icons->Num() == 1))
		{
			const TSharedPtr<FJsonObject> Icon = (*Icons)[0]->AsObject();
			const FString Src = Icon->GetStringField(TEXT("src"));
			const FString Prefix = TEXT("data:image/png;base64,");
			TestTrue(What + TEXT(": a data: URI PNG"), Src.StartsWith(Prefix));
			TestEqual(What + TEXT(": mimeType"), Icon->GetStringField(TEXT("mimeType")), FString(TEXT("image/png")));
			TArray<uint8> Png;
			TestTrue(What + TEXT(": the data decodes to a PNG"), FBase64::Decode(Src.Mid(Prefix.Len()), Png) && Png.Num() > 8
				&& Png[0] == 0x89 && Png[1] == 'P' && Png[2] == 'N' && Png[3] == 'G');
		}
	}

END_DEFINE_SPEC(FN2CMcpDiscoveryPolishSpec)

void FN2CMcpDiscoveryPolishSpec::Define()
{
	using namespace N2CMcpDiscoverySpec;

	It(TEXT("initialize: serverInfo with title, description and icon; completions advertised, no resource subscriptions"), [this]()
	{
		FN2CMcpProtocol Core;
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Responder = Send(Core, N2CTest::MakeMcpInbound(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},")
			TEXT("\"clientInfo\":{\"name\":\"n2c-discovery-spec\",\"version\":\"1\"}}}")));
		const TSharedPtr<FJsonObject> Init = Result(*Responder);
		if (!TestTrue(TEXT("initialize result"), Init.IsValid()))
		{
			return;
		}
		ExpectFullServerInfo(TEXT("initialize"), Init->GetObjectField(TEXT("serverInfo")));
		const TSharedPtr<FJsonObject> Capabilities = Init->GetObjectField(TEXT("capabilities"));
		TestTrue(TEXT("completions advertised"), Capabilities->HasTypedField<EJson::Object>(TEXT("completions")));
		TestFalse(TEXT("a session has no channel for resource updates"), Capabilities->GetObjectField(TEXT("resources"))->GetBoolField(TEXT("subscribe")));
	});

	It(TEXT("2026-07-28: server/discover carries the icon and resources.subscribe on a stream; other results repeat serverInfo without it"), [this]()
	{
		FN2CMcpProtocol Core;
		const TSharedPtr<FJsonObject> Discover = Result(*Send(Core, ModernInbound(TEXT("1"), TEXT("server/discover")), /*bCanStream=*/true));
		if (!TestTrue(TEXT("discover result"), Discover.IsValid()))
		{
			return;
		}
		ExpectFullServerInfo(TEXT("discover"), Discover->GetObjectField(TEXT("_meta"))->GetObjectField(TEXT("io.modelcontextprotocol/serverInfo")));
		TestTrue(TEXT("subscribe on a stream-capable transport"),
			Discover->GetObjectField(TEXT("capabilities"))->GetObjectField(TEXT("resources"))->GetBoolField(TEXT("subscribe")));

		const TSharedPtr<FJsonObject> NoStream = Result(*Send(Core, ModernInbound(TEXT("2"), TEXT("server/discover")), /*bCanStream=*/false));
		TestFalse(TEXT("no subscribe where the transport cannot stream (UE 5.6/5.7)"), NoStream.IsValid()
			&& NoStream->GetObjectField(TEXT("capabilities"))->GetObjectField(TEXT("resources"))->GetBoolField(TEXT("subscribe")));

		const TSharedPtr<FJsonObject> Prompts = Result(*Send(Core, ModernInbound(TEXT("3"), TEXT("prompts/list"))));
		const TSharedPtr<FJsonObject> Repeated = Prompts.IsValid()
			? Prompts->GetObjectField(TEXT("_meta"))->GetObjectField(TEXT("io.modelcontextprotocol/serverInfo")) : nullptr;
		TestTrue(TEXT("every result names the server"), Repeated.IsValid() && Repeated->GetStringField(TEXT("title")) == TEXT("NodeToCode"));
		TestFalse(TEXT("but without the icon"), Repeated.IsValid() && Repeated->HasField(TEXT("icons")));
	});

	It(TEXT("completes prompt argument values by prefix, case-insensitively, and refuses unknown refs"), [this]()
	{
		// The shipped prompts list their values
		const FMcpPromptDefinition Generate = FN2CMcpCodeGenerationPrompt().GetDefinition();
		const FMcpPromptArgument* Language = Generate.Arguments.FindByPredicate([](const FMcpPromptArgument& A) { return A.Name == TEXT("language"); });
		TestTrue(TEXT("generate-code's language offers cpp and python"), Language && Language->CompletionValues.Contains(TEXT("cpp"))
			&& Language->CompletionValues.Contains(TEXT("python")));

		FMcpPromptDefinition Definition;
		Definition.Name = PromptName;
		FMcpPromptArgument Argument;
		Argument.Name = TEXT("detail");
		Argument.CompletionValues = { TEXT("summary"), TEXT("detailed"), TEXT("comprehensive"), TEXT("Deep") };
		Definition.Arguments.Add(Argument);
		FN2CMcpPromptManager& Prompts = FN2CMcpPromptManager::Get();
		Prompts.RegisterPrompt(Definition, FMcpPromptGetDelegate::CreateLambda([](const FMcpPromptArguments&) { return FMcpPromptResult(); }));
		ON_SCOPE_EXIT { FN2CMcpPromptManager::Get().UnregisterPrompt(N2CMcpDiscoverySpec::PromptName); };

		FString Error;
		const TSharedPtr<FJsonObject> D = N2CMcpCompletion::Complete(CompletionParams(TEXT("ref/prompt"), TEXT("name"), PromptName, TEXT("detail"), TEXT("d")), Prompts, Error);
		if (TestTrue(TEXT("completed: ") + Error, D.IsValid()))
		{
			const TSharedPtr<FJsonObject> Completion = D->GetObjectField(TEXT("completion"));
			TestEqual(TEXT("values starting with d, any case"), Strings(Completion, TEXT("values")), TArray<FString>({ TEXT("detailed"), TEXT("Deep") }));
			TestEqual(TEXT("total"), static_cast<int32>(Completion->GetNumberField(TEXT("total"))), 2);
			TestFalse(TEXT("hasMore"), Completion->GetBoolField(TEXT("hasMore")));
		}
		const TSharedPtr<FJsonObject> All = N2CMcpCompletion::Complete(CompletionParams(TEXT("ref/prompt"), TEXT("name"), PromptName, TEXT("detail"), FString()), Prompts, Error);
		TestEqual(TEXT("an empty value offers every value"), All.IsValid() ? Strings(All->GetObjectField(TEXT("completion")), TEXT("values")).Num() : 0, 4);
		const TSharedPtr<FJsonObject> Other = N2CMcpCompletion::Complete(CompletionParams(TEXT("ref/prompt"), TEXT("name"), PromptName, TEXT("nope"), TEXT("x")), Prompts, Error);
		TestEqual(TEXT("an unknown argument offers nothing"), Other.IsValid() ? Strings(Other->GetObjectField(TEXT("completion")), TEXT("values")).Num() : -1, 0);
		TestFalse(TEXT("an unknown prompt is refused"), N2CMcpCompletion::Complete(
			CompletionParams(TEXT("ref/prompt"), TEXT("name"), TEXT("n2c-no-such-prompt"), TEXT("detail"), TEXT("")), Prompts, Error).IsValid());
		TestFalse(TEXT("an unknown ref type is refused"), N2CMcpCompletion::Complete(
			CompletionParams(TEXT("ref/tool"), TEXT("name"), PromptName, TEXT("detail"), TEXT("")), Prompts, Error).IsValid());

		// Through the protocol, on a session: the method is dispatched and a bad ref is -32602
		FN2CMcpProtocol Core;
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Init = Send(Core, N2CTest::MakeMcpInbound(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},")
			TEXT("\"clientInfo\":{\"name\":\"n2c-discovery-spec\",\"version\":\"1\"}}}")));
		const FString Session = Init->LastReply().SessionId;
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Call = Send(Core, N2CTest::MakeMcpInbound(FString::Printf(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"completion/complete\",\"params\":{\"ref\":{\"type\":\"ref/prompt\",\"name\":\"%s\"},")
			TEXT("\"argument\":{\"name\":\"detail\",\"value\":\"su\"}}}"), PromptName), TEXT("application/json, text/event-stream"), Session));
		const TSharedPtr<FJsonObject> Completed = Result(*Call);
		TestEqual(TEXT("protocol: summary"), Completed.IsValid() ? Strings(Completed->GetObjectField(TEXT("completion")), TEXT("values")) : TArray<FString>(),
			TArray<FString>({ TEXT("summary") }));
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Bad = Send(Core, N2CTest::MakeMcpInbound(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"completion/complete\",\"params\":{\"argument\":{\"name\":\"x\",\"value\":\"\"}}}"),
			TEXT("application/json, text/event-stream"), Session));
		const TSharedPtr<FJsonObject> BadMessage = ParseObject(Bad->LastReply().Body);
		TestTrue(TEXT("protocol: a missing ref is -32602"), BadMessage.IsValid() && BadMessage->HasField(TEXT("error"))
			&& static_cast<int32>(BadMessage->GetObjectField(TEXT("error"))->GetNumberField(TEXT("code"))) == JsonRpcErrorCodes::InvalidParams);

		// 2026-07-28 knows the method too
		const TSharedPtr<FJsonObject> Modern = Result(*Send(Core, ModernInbound(TEXT("4"), TEXT("completion/complete"),
			FString::Printf(TEXT(",\"ref\":{\"type\":\"ref/prompt\",\"name\":\"%s\"},\"argument\":{\"name\":\"detail\",\"value\":\"com\"}"), PromptName))));
		TestEqual(TEXT("modern: comprehensive"), Modern.IsValid() ? Strings(Modern->GetObjectField(TEXT("completion")), TEXT("values")) : TArray<FString>(),
			TArray<FString>({ TEXT("comprehensive") }));
	});

	It(TEXT("completes the Blueprint path of nodetocode://blueprint/{+path} from the AssetRegistry"), [this]()
	{
		using namespace N2CTest::DependencyFixtures;
		FString Error;
		const TSharedPtr<FJsonObject> Paths = N2CMcpCompletion::Complete(CompletionParams(TEXT("ref/resource"), TEXT("uri"),
			FN2CMcpBlueprintByNameResource::UriTemplate, TEXT("path"), TEXT("nodetocodetestcontent/dependencies/bp_n2cdep_")),
			FN2CMcpPromptManager::Get(), Error);
		if (TestTrue(TEXT("completed: ") + Error, Paths.IsValid()))
		{
			TestEqual(TEXT("the four Blueprints named BP_N2CDep_, case-insensitive, sorted (Tests/NodeToCodeTestContent is linked into the host by build.ps1)"),
				Strings(Paths->GetObjectField(TEXT("completion")), TEXT("values")), TArray<FString>({
					TEXT("NodeToCodeTestContent/Dependencies/BP_N2CDep_A"), TEXT("NodeToCodeTestContent/Dependencies/BP_N2CDep_B"),
					TEXT("NodeToCodeTestContent/Dependencies/BP_N2CDep_C"), TEXT("NodeToCodeTestContent/Dependencies/BP_N2CDep_Child") }));
		}
		TestTrue(TEXT("no engine content"), !N2CMcpCompletion::BlueprintPaths(TEXT("Engine/")).Num());
		TestFalse(TEXT("another resource URI is refused"), N2CMcpCompletion::Complete(CompletionParams(TEXT("ref/resource"), TEXT("uri"),
			TEXT("nodetocode://other/{x}"), TEXT("x"), TEXT("")), FN2CMcpPromptManager::Get(), Error).IsValid());
	});

	It(TEXT("sends notifications/resources/updated to a listen stream subscribed to a Blueprint once it compiles"), [this]()
	{
		const TUniquePtr<N2CTest::FTestBlueprint> BP = MakeUnique<N2CTest::FTestBlueprint>(TEXT("BP_N2CResourceUpdate"));
		if (!TestTrue(TEXT("scratch Blueprint"), BP->GetBlueprint() != nullptr))
		{
			return;
		}
		const FString Uri = FN2CMcpBlueprintByNameResource::UriFor(*BP->GetBlueprint());
		FN2CMcpProtocol Core;
		const TSharedRef<N2CTest::FN2CMcpMemoryResponder> Stream = Send(Core, ModernInbound(TEXT("\"sub-r\""), TEXT("subscriptions/listen"),
			FString::Printf(TEXT(",\"notifications\":{\"resourceSubscriptions\":[\"%s\",\"%s\",\"nodetocode://python/unreal-stub\"]}"),
				*Uri, TEXT("nodetocode://blueprint/current"))), /*bCanStream=*/true);
		const TSharedPtr<FJsonObject> Ack = Stream->Streamed.Num() > 0 ? ParseObject(Stream->Streamed[0]) : nullptr;
		if (!TestTrue(TEXT("acknowledged"), Ack.IsValid() && Ack->GetStringField(TEXT("method")) == TEXT("notifications/subscriptions/acknowledged")))
		{
			return;
		}
		TestEqual(TEXT("the Blueprint resources are accepted, the stub is not"),
			Strings(Ack->GetObjectField(TEXT("params"))->GetObjectField(TEXT("notifications")), TEXT("resourceSubscriptions")),
			TArray<FString>({ Uri, TEXT("nodetocode://blueprint/current") }));

		Core.TickStreamsForTesting();
		const int32 Before = Stream->Streamed.Num();
		FKismetEditorUtilities::CompileBlueprint(BP->GetBlueprint());
		Core.TickStreamsForTesting();
		TArray<TSharedPtr<FJsonObject>> Updates;
		for (int32 Index = Before; Index < Stream->Streamed.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Message = ParseObject(Stream->Streamed[Index]);
			if (Message.IsValid() && Message->GetStringField(TEXT("method")) == TEXT("notifications/resources/updated"))
			{
				Updates.Add(Message);
			}
		}
		if (TestEqual(TEXT("one update for the compiled Blueprint"), Updates.Num(), 1))
		{
			const TSharedPtr<FJsonObject> Params = Updates[0]->GetObjectField(TEXT("params"));
			TestEqual(TEXT("its uri"), Params->GetStringField(TEXT("uri")), Uri);
			TestEqual(TEXT("tagged with the subscription"), Params->GetObjectField(TEXT("_meta"))->GetStringField(TEXT("io.modelcontextprotocol/subscriptionId")),
				FString(TEXT("sub-r")));
		}
		const int32 After = Stream->Streamed.Num();
		Core.TickStreamsForTesting();
		TestEqual(TEXT("nothing more without another change"), Stream->Streamed.Num(), After);
		Core.CloseSubscriptions();
	});

	It(TEXT("annotates resources with audience and priority, and serves the Python API stub"), [this]()
	{
		const FMcpResourceDefinition Current = FN2CMcpCurrentBlueprintResource().GetDefinition();
		TestTrue(TEXT("current: priority"), Current.Annotations.IsValid() && Current.Annotations->GetNumberField(TEXT("priority")) > 0.0);
		TestTrue(TEXT("current: audience assistant"), Current.Annotations.IsValid() && Strings(Current.Annotations, TEXT("audience")).Contains(TEXT("assistant")));
		TestFalse(TEXT("current: no readOnly (not an MCP annotation)"), Current.Annotations.IsValid() && Current.Annotations->HasField(TEXT("readOnly")));
		const FMcpResourceDefinition All = FN2CMcpAllBlueprintsResource().GetDefinition();
		TestFalse(TEXT("all: no readOnly"), All.Annotations.IsValid() && All.Annotations->HasField(TEXT("readOnly")));
		const FMcpResourceTemplate Template = FN2CMcpBlueprintByNameResource::GetResourceTemplate();
		TestTrue(TEXT("template: priority"), Template.Annotations.IsValid() && Template.Annotations->HasField(TEXT("priority")));

		const FMcpResourceDefinition Stub = FN2CMcpPythonStubResource::GetDefinition();
		TestEqual(TEXT("stub uri"), Stub.Uri, FString(TEXT("nodetocode://python/unreal-stub")));
		const FMcpResourceContents Read = FN2CMcpPythonStubResource::Read(Stub.Uri);
		TestEqual(TEXT("stub mime"), Read.MimeType, FString(TEXT("text/x-python")));
		FString OnDisk;
		if (FFileHelper::LoadFileToString(OnDisk, *FN2CMcpPythonStubResource::StubPath()))
		{
			TestEqual(TEXT("the stub as on disk"), Read.Text.Len(), OnDisk.Len());
		}
		else
		{
			TestTrue(TEXT("without a stub, how to generate it"), Read.Text.Contains(TEXT("Developer Mode")));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
