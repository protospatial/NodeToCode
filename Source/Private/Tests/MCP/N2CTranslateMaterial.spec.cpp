// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Core/N2CTargetLanguage.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolsets.h"
#include "MCP/Tools/N2CToolCallContext.h"
#include "MCP/Tools/Implementations/Material/N2CMcpTranslateMaterialTool.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CTranslateMaterialSpecPrivate
{
	const TCHAR* const WorldGrid = TEXT("/Engine/EngineMaterials/WorldGridMaterial");

	/** The request translate-material sent instead of an LLM call: pending until the test completes it */
	struct FFakeRequest
	{
		int32 Starts = 0;
		FString N2CJson;
		FString Provider;
		FString Model;
		FN2CLLMRequestHandlePtr Handle;
		FOnLLMResponseReceived OnComplete;
	};

	struct FObserved
	{
		TArray<FString> Messages;
		int32 Completions = 0;
		bool bIsError = false;
		FString Text;
		FString ErrorCode;
		TSharedPtr<FJsonObject> Structured;
	};

	TSharedRef<FJsonObject> Args(const FString& Path, const FString& Language = FString(), const FString& Provider = FString())
	{
		const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
		if (!Path.IsEmpty()) { Arguments->SetStringField(TEXT("materialPath"), Path); }
		if (!Language.IsEmpty()) { Arguments->SetStringField(TEXT("language"), Language); }
		if (!Provider.IsEmpty()) { Arguments->SetStringField(TEXT("provider"), Provider); }
		return Arguments;
	}
}

/**
 * Card 0455: translate-material captures a material and sends its N2C JSON (asset_kind Material, so the module picks
 * MaterialGen) through its request source, here a fake provider. The request holds the material's document; progress
 * names the material; the result is the explanation plus target, marked untrusted; cancel aborts the request once.
 * A language other than Pseudocode, an unknown provider and a missing material are refused before any request.
 */
BEGIN_DEFINE_SPEC(FN2CTranslateMaterialSpec, "NodeToCode.MCP.TranslateMaterial", N2C_TEST_FLAGS)

	TSharedPtr<N2CTranslateMaterialSpecPrivate::FFakeRequest> Fake;
	TSharedPtr<IN2CMcpTool> Tool;
	TArray<FN2CToolCallContextPtr> Contexts;

	TSharedRef<N2CTranslateMaterialSpecPrivate::FObserved> Invoke(const TSharedRef<FJsonObject>& Arguments)
	{
		using namespace N2CTranslateMaterialSpecPrivate;
		const TSharedRef<FObserved> Observed = MakeShared<FObserved>();
		const FN2CToolCallContextRef Context = FN2CToolCallContext::Create([Observed](float, const FString& Message)
		{
			Observed->Messages.Add(Message);
		});
		Contexts.Add(Context);
		TFuture<FMcpToolCallResult> Future = Tool->InvokeAsync(Context, Arguments);
		Future.Next([Observed](FMcpToolCallResult Result)
		{
			++Observed->Completions;
			Observed->bIsError = Result.bIsError;
			if (Result.Content.Num() > 0 && Result.Content[0].IsValid())
			{
				Result.Content[0]->TryGetStringField(TEXT("text"), Observed->Text);
			}
			Observed->ErrorCode = Result.GetErrorCode();
			Observed->Structured = Result.StructuredContent;
		});
		return Observed;
	}

	bool CompleteFake(const FString& Response)
	{
		return Fake.IsValid() && Fake->Handle.IsValid() && Fake->Handle->Complete(Fake->OnComplete, Response);
	}

END_DEFINE_SPEC(FN2CTranslateMaterialSpec)

void FN2CTranslateMaterialSpec::Define()
{
	using namespace N2CTranslateMaterialSpecPrivate;

	BeforeEach([this]()
	{
		Fake = MakeShared<FFakeRequest>();
		Tool = N2CTest::FindRegisteredTool(TEXT("translate-material"));
		const TSharedPtr<FFakeRequest> Captured = Fake;
		FN2CMcpTranslateMaterialTool::SetRequestStarterForTesting([Captured](const FString& N2CJson, const FString& Provider,
			const FString& Model, const FOnLLMResponseReceived& OnComplete)
		{
			++Captured->Starts;
			Captured->N2CJson = N2CJson;
			Captured->Provider = Provider;
			Captured->Model = Model;
			const FN2CLLMRequestHandleRef Handle = FN2CLLMRequestHandle::Create();
			Captured->Handle = Handle;
			Captured->OnComplete = OnComplete;
			return Handle;
		});
	});

	AfterEach([this]()
	{
		for (const FN2CToolCallContextPtr& Context : Contexts)
		{
			Context->Cancel();
		}
		Contexts.Reset();
		FN2CMcpTranslateMaterialTool::SetRequestStarterForTesting(nullptr);
		Tool.Reset();
		Fake.Reset();
	});

	It(TEXT("is a long-running, open-world MaterialTools tool that returns untrusted content"), [this]()
	{
		if (!TestTrue(TEXT("registered"), Tool.IsValid()))
		{
			return;
		}
		const FMcpToolDefinition Definition = Tool->GetDefinition();
		TestTrue(TEXT("MaterialTools"), Definition.Toolset == &N2CMcpToolsets::Material);
		TestTrue(TEXT("long-running"), Definition.bIsLongRunning);
		TestTrue(TEXT("untrusted"), Definition.bReturnsUntrustedContent);
		TestTrue(TEXT("read-only"), Definition.Annotations.IsReadOnly());
		TestTrue(TEXT("open world"), Definition.Annotations.OpenWorldHint.Get(false));
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		TestTrue(TEXT("outputSchema has target"), Definition.OutputSchema.IsValid()
			&& Definition.OutputSchema->TryGetObjectField(TEXT("properties"), Properties) && (*Properties)->HasField(TEXT("target")));
	});

	It(TEXT("sends the material's N2C JSON, names the material in progress, and returns the explanation with its target"), [this]()
	{
		if (!TestTrue(TEXT("registered"), Tool.IsValid()))
		{
			return;
		}
		const TSharedRef<FObserved> Observed = Invoke(Args(WorldGrid, FString(), TEXT("anthropic")));
		if (!TestEqual(TEXT("one request: ") + Observed->Text, Fake->Starts, 1))
		{
			return;
		}
		TestEqual(TEXT("the provider override is passed"), Fake->Provider, FString(TEXT("anthropic")));
		TestEqual(TEXT("the document is a material, so the module picks MaterialGen"),
			FString(N2C::GetPromptFamilyForDocument(Fake->N2CJson)), FString(N2C::MaterialPromptFamily));
		TestTrue(TEXT("the request holds WorldGridMaterial's graph"), Fake->N2CJson.Contains(TEXT("WorldGridMaterial")));
		TestTrue(TEXT("progress names the material: ") + FString::Join(Observed->Messages, TEXT(" | ")),
			Observed->Messages.ContainsByPredicate([](const FString& Message) { return Message.Contains(TEXT("WorldGridMaterial")); }));

		TestTrue(TEXT("the fake response is delivered"), CompleteFake(TEXT("{\"graphs\":[{\"graph_name\":\"WorldGridMaterial\",\"graph_type\":\"Material\",\"graph_class\":\"\",\"code\":{\"graphDeclaration\":\"\",\"graphImplementation\":\"BaseColor = grid\",\"implementationNotes\":\"\"}}]}")));
		TestEqual(TEXT("one result"), Observed->Completions, 1);
		TestFalse(TEXT("not an error: ") + Observed->Text.Left(300), Observed->bIsError);
		TestTrue(TEXT("marked as untrusted"), Observed->Text.Contains(FN2CMcpUntrustedContent::MarkerField));
		const TSharedPtr<FJsonObject>* Target = nullptr;
		if (TestTrue(TEXT("structuredContent has target"), Observed->Structured.IsValid() && Observed->Structured->TryGetObjectField(TEXT("target"), Target)))
		{
			TestTrue(TEXT("target.assetPath"), (*Target)->GetStringField(TEXT("assetPath")).StartsWith(WorldGrid));
			TestEqual(TEXT("target.resolvedBy"), (*Target)->GetStringField(TEXT("resolvedBy")), FString(TEXT("path")));
			TestFalse(TEXT("target.workingCopy"), (*Target)->GetBoolField(TEXT("workingCopy")));
		}
		TestFalse(TEXT("nothing archived: no run_folder"), Observed->Structured.IsValid() && Observed->Structured->HasField(TEXT("run_folder")));
	});

	It(TEXT("cancels the request once"), [this]()
	{
		if (!TestTrue(TEXT("registered"), Tool.IsValid()))
		{
			return;
		}
		const TSharedRef<FObserved> Observed = Invoke(Args(WorldGrid));
		TestEqual(TEXT("one request"), Fake->Starts, 1);
		TestTrue(TEXT("cancel is accepted"), Contexts.Last()->Cancel());
		TestEqual(TEXT("one result"), Observed->Completions, 1);
		TestTrue(TEXT("cancelled"), Observed->bIsError && Observed->Text.Contains(TEXT("cancelled")));
		TestTrue(TEXT("the request was cancelled"), Fake->Handle.IsValid() && Fake->Handle->IsCancelled());
		TestFalse(TEXT("a late response is refused"), CompleteFake(TEXT("{\"graphs\":[]}")));
		TestEqual(TEXT("still one result"), Observed->Completions, 1);
	});

	It(TEXT("refuses another language, an unknown provider and a missing material before any request"), [this]()
	{
		if (!TestTrue(TEXT("registered"), Tool.IsValid()))
		{
			return;
		}
		const TSharedRef<FObserved> Cpp = Invoke(Args(WorldGrid, TEXT("cpp")));
		TestTrue(TEXT("cpp: TARGET_NOT_AVAILABLE_FOR_MATERIALS: ") + Cpp->Text, Cpp->bIsError
			&& Cpp->Text.StartsWith(N2C::TargetNotAvailableForMaterialsCode) && Cpp->ErrorCode == N2C::TargetNotAvailableForMaterialsCode);
		const TSharedRef<FObserved> Pseudo = Invoke(Args(WorldGrid, TEXT("pseudocode")));
		TestEqual(TEXT("pseudocode is accepted"), Pseudo->Completions, 0);
		Contexts.Last()->Cancel();

		const int32 StartsBefore = Fake->Starts;
		const TSharedRef<FObserved> Provider = Invoke(Args(WorldGrid, FString(), TEXT("openia")));
		TestTrue(TEXT("an unknown provider is refused: ") + Provider->Text, Provider->bIsError && Provider->Text.Contains(TEXT("openia")));
		const TSharedRef<FObserved> Missing = Invoke(Args(TEXT("/Game/N2CNoSuchMaterial")));
		TestTrue(TEXT("ASSET_NOT_FOUND"), Missing->bIsError && Missing->Text.StartsWith(TEXT("ASSET_NOT_FOUND")));
		const TSharedRef<FObserved> Blueprint = Invoke(Args(TEXT("/Engine/EngineSky/BP_Sky_Sphere")));
		TestTrue(TEXT("a Blueprint is UNSUPPORTED_ASSET_TYPE: ") + Blueprint->Text.Left(200), Blueprint->bIsError && Blueprint->Text.StartsWith(TEXT("UNSUPPORTED_ASSET_TYPE")));
		TestEqual(TEXT("no request for any refusal"), Fake->Starts, StartsBefore);
		TestEqual(TEXT("cpp made no request"), StartsBefore, 1);
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
