// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Resources/Implementations/N2CMcpBlueprintResource.h"
#include "MCP/Resources/N2CMcpResourceManager.h"
#include "MCP/Resources/N2CMcpUriTemplate.h"
#include "MCP/Server/N2CMcpJsonRpcTypes.h"
#include "MCP/Utils/N2CMcpBlueprintUtils.h"
#include "MCP/Utils/N2CMcpUntrustedContent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "BlueprintEditor.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

namespace N2CMcpResourceUriSpec
{
	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) ? Object : nullptr;
	}

	/** The template's match of Uri, or "<no match>" */
	FString MatchOf(const FString& Template, const FString& Uri, const FString& Variable)
	{
		TMap<FString, FString> Params;
		return FN2CMcpUriTemplate(Template).Match(Uri, Params) ? Params.FindRef(Variable) : FString(TEXT("<no match>"));
	}
}

// Findings mcp-tools-python-translation-resources-17 and -24, imp-mcp-tools-python-translation-resources-8 (card
// 0208): blueprints/all wrote BP_Player's URI with spaces as '_' and the by-name read turned every '_' back into a
// space, so UE-convention names never resolved and a match returned a stub; MatchesTemplate was a prefix check.
BEGIN_DEFINE_SPEC(FN2CMcpResourceUriSpec, "NodeToCode.MCP.ResourceUri", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CMcpResourceUriSpec)

void FN2CMcpResourceUriSpec::Define()
{
	using namespace N2CMcpResourceUriSpec;

	Describe(TEXT("a {var} template"), [this]()
	{
		const FString Template = TEXT("nodetocode://blueprint/{name}");

		It(TEXT("matches one segment and returns it"), [this, Template]()
		{
			TestEqual(TEXT("BP_Player"), MatchOf(Template, TEXT("nodetocode://blueprint/BP_Player"), TEXT("name")), TEXT("BP_Player"));
		});

		It(TEXT("rejects an empty value, a second segment, a query and a fragment"), [this, Template]()
		{
			for (const TCHAR* Uri : { TEXT("nodetocode://blueprint/"), TEXT("nodetocode://blueprint/a/b"), TEXT("nodetocode://blueprint/a/b?x"),
				TEXT("nodetocode://blueprint/a?x"), TEXT("nodetocode://blueprint/a#f"), TEXT("nodetocode://blueprint/a/"), TEXT("nodetocode://blueprint") })
			{
				TestEqual(Uri, MatchOf(Template, Uri, TEXT("name")), TEXT("<no match>"));
			}
		});

		It(TEXT("percent-decodes the value and rejects a malformed escape or an encoded slash"), [this, Template]()
		{
			TestEqual(TEXT("%20"), MatchOf(Template, TEXT("nodetocode://blueprint/BP%20Player"), TEXT("name")), TEXT("BP Player"));
			TestEqual(TEXT("UTF-8"), MatchOf(Template, TEXT("nodetocode://blueprint/BP_%C3%9Cber"), TEXT("name")), FString(TEXT("BP_\u00DCber")));
			for (const TCHAR* Uri : { TEXT("nodetocode://blueprint/BP%zz"), TEXT("nodetocode://blueprint/BP%2"), TEXT("nodetocode://blueprint/a%2Fb") })
			{
				TestEqual(Uri, MatchOf(Template, Uri, TEXT("name")), TEXT("<no match>"));
			}
		});

		It(TEXT("matches the literal text around the variable exactly"), [this]()
		{
			TestEqual(TEXT("suffix"), MatchOf(TEXT("x://a/{name}.json"), TEXT("x://a/b.c.json"), TEXT("name")), TEXT("b.c"));
			TestEqual(TEXT("other prefix"), MatchOf(TEXT("nodetocode://blueprint/{name}"), TEXT("nodetocode://blueprints/all"), TEXT("name")), TEXT("<no match>"));
			TestEqual(TEXT("missing suffix"), MatchOf(TEXT("x://a/{name}.json"), TEXT("x://a/b"), TEXT("name")), TEXT("<no match>"));
		});
	});

	Describe(TEXT("a {+var} template"), [this]()
	{
		const FString Template = TEXT("nodetocode://blueprint/{+path}");

		It(TEXT("matches the rest of the path and decodes each segment"), [this, Template]()
		{
			TestEqual(TEXT("one segment"), MatchOf(Template, TEXT("nodetocode://blueprint/BP_Player"), TEXT("path")), TEXT("BP_Player"));
			TestEqual(TEXT("asset path"), MatchOf(Template, TEXT("nodetocode://blueprint/Game/Blueprints/BP_Player"), TEXT("path")), TEXT("Game/Blueprints/BP_Player"));
			TestEqual(TEXT("encoded"), MatchOf(Template, TEXT("nodetocode://blueprint/Game/My%20Folder/BP_Player"), TEXT("path")), TEXT("Game/My Folder/BP_Player"));
		});

		It(TEXT("rejects an empty value, an empty segment, a query, a fragment and an encoded slash"), [this, Template]()
		{
			for (const TCHAR* Uri : { TEXT("nodetocode://blueprint/"), TEXT("nodetocode://blueprint/Game//BP"), TEXT("nodetocode://blueprint/Game/BP/"),
				TEXT("nodetocode://blueprint//Game/BP"), TEXT("nodetocode://blueprint/Game/BP?x"), TEXT("nodetocode://blueprint/Game/BP#f"),
				TEXT("nodetocode://blueprint/Game/A%2FB"), TEXT("nodetocode://blueprint/Game/BP%G0") })
			{
				TestEqual(Uri, MatchOf(Template, Uri, TEXT("path")), TEXT("<no match>"));
			}
		});
	});

	Describe(TEXT("a template"), [this]()
	{
		It(TEXT("with an unsupported or unterminated expression is invalid and matches nothing"), [this]()
		{
			for (const TCHAR* Template : { TEXT("x://a/{?q}"), TEXT("x://a/{#f}"), TEXT("x://a/{a,b}"), TEXT("x://a/{name"), TEXT("x://a/}b"), TEXT("x://a/{}") })
			{
				TMap<FString, FString> Params;
				const FN2CMcpUriTemplate Compiled(Template);
				TestFalse(FString(Template) + TEXT(" is invalid"), Compiled.IsValid());
				TestFalse(FString(Template) + TEXT(" matches nothing"), Compiled.Match(TEXT("x://a/b"), Params));
			}
		});

		It(TEXT("is refused by the resource manager when invalid"), [this]()
		{
			FMcpResourceTemplate Template;
			Template.UriTemplate = TEXT("nodetocode://n2c-resource-uri-spec/{?q}");
			Template.Name = TEXT("invalid");
			AddExpectedMessage(TEXT("unsupported URI template"), EAutomationExpectedMessageFlags::Contains, 1);
			TestFalse(TEXT("not registered"), FN2CMcpResourceManager::Get().RegisterDynamicResource(Template,
				FMcpResourceTemplateHandler::CreateLambda([](const FString& Uri) { return FMcpResourceContents(); })));
		});
	});

	Describe(TEXT("percent-encoding"), [this]()
	{
		It(TEXT("keeps unreserved characters, underscores included, and round-trips the rest"), [this]()
		{
			TestEqual(TEXT("BP_Player"), FN2CMcpUriTemplate::EncodeSegment(TEXT("BP_Player")), TEXT("BP_Player"));
			TestEqual(TEXT("space"), FN2CMcpUriTemplate::EncodeSegment(TEXT("BP Player")), TEXT("BP%20Player"));
			TestEqual(TEXT("slash in a segment"), FN2CMcpUriTemplate::EncodeSegment(TEXT("a/b")), TEXT("a%2Fb"));
			TestEqual(TEXT("UTF-8"), FN2CMcpUriTemplate::EncodeSegment(FString(TEXT("\u00DC"))), TEXT("%C3%9C"));
			TestEqual(TEXT("path keeps its slashes"), FN2CMcpUriTemplate::EncodePath(TEXT("Game/My Folder/BP_Player")), TEXT("Game/My%20Folder/BP_Player"));
			for (const FString& Value : { FString(TEXT("BP_Player")), FString(TEXT("a b%c?d#e")), FString(TEXT("\u00DCber \u65E5\u672C")) })
			{
				FString Decoded;
				TestTrue(Value + TEXT(" decodes"), FN2CMcpUriTemplate::Decode(FN2CMcpUriTemplate::EncodeSegment(Value), Decoded));
				TestEqual(Value + TEXT(" round-trips"), Decoded, Value);
			}
		});
	});

	Describe(TEXT("the Blueprint resources"), [this]()
	{
		It(TEXT("round-trip BP_Player from its blueprints/all entry to a read that returns its N2C JSON"), [this]()
		{
			N2CTest::FTestBlueprint Test(TEXT("BP_Player"));
			if (!TestNotNull(TEXT("Blueprint"), Test.GetBlueprint())
				|| !TestNotNull(TEXT("a node"), Test.AddCall(Test.EventGraph(), UKismetSystemLibrary::StaticClass(), TEXT("PrintString"))))
			{
				return;
			}

			const TSharedRef<FJsonObject> Entry = FN2CMcpAllBlueprintsResource::DescribeBlueprint(*Test.GetBlueprint());
			const FString Uri = Entry->GetStringField(TEXT("resourceUri"));
			// The factory's Blueprint is /Temp/N2CTest_<guid>.BP_Player (not its package's same-named asset): the object path
			TestEqual(TEXT("the URI is the asset path, the name unchanged"), Uri, TEXT("nodetocode://blueprint/") + Test.GetBlueprint()->GetPathName().RightChop(1));
			TestTrue(TEXT("BP_Player keeps its underscore"), Uri.EndsWith(TEXT(".BP_Player")));

			const FMcpResourceContents Contents = FN2CMcpBlueprintByNameResource().Read(Uri);
			if (!TestFalse(TEXT("the read succeeds: ") + Contents.ErrorMessage, Contents.IsError()))
			{
				return;
			}
			const TSharedPtr<FJsonObject> Document = Parse(Contents.Text);
			const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
			if (TestTrue(TEXT("the text is N2C JSON with graphs"), Document.IsValid() && Document->TryGetArrayField(TEXT("graphs"), Graphs) && Graphs->Num() > 0))
			{
				TestFalse(TEXT("no stub message"), Document->HasField(TEXT("message")));
				TestTrue(TEXT("marked as untrusted data"), Document->HasField(FN2CMcpUntrustedContent::MarkerField));
				TestTrue(TEXT("the captured node is in it"), Contents.Text.Contains(TEXT("PrintString")) || Contents.Text.Contains(TEXT("Print String")));
			}
			TestEqual(TEXT("contents uri"), Contents.Uri, Uri);
			TestEqual(TEXT("mime type"), Contents.MimeType, TEXT("application/json"));

			// The same read through the resource manager, as resources/read does it
			FN2CMcpResourceManager& Manager = FN2CMcpResourceManager::Get();
			bool bRegisteredHere = false;
			if (!Manager.ListResourceTemplates().ContainsByPredicate([](const FMcpResourceTemplate& Template)
				{
					return Template.UriTemplate == FN2CMcpBlueprintByNameResource::UriTemplate;
				}))
			{
				bRegisteredHere = Manager.RegisterDynamicResource(FN2CMcpBlueprintByNameResource::GetResourceTemplate(),
					FMcpResourceTemplateHandler::CreateLambda([](const FString& InUri) { return FN2CMcpBlueprintByNameResource().Read(InUri); }), true);
			}
			ON_SCOPE_EXIT
			{
				if (bRegisteredHere)
				{
					FN2CMcpResourceManager::Get().UnregisterDynamicResource(FN2CMcpBlueprintByNameResource::UriTemplate);
				}
			};
			TestTrue(TEXT("the URI is a registered resource"), Manager.IsResourceRegistered(Uri));
			const FMcpResourceContents ViaManager = Manager.ReadResource(Uri);
			TestFalse(TEXT("the manager's read succeeds: ") + ViaManager.ErrorMessage, ViaManager.IsError());
			TestEqual(TEXT("the manager returns the same document"), ViaManager.Text, Contents.Text);

			// Empty and malformed values are not resources (resources/read answers -32002)
			for (const TCHAR* Bad : { TEXT("nodetocode://blueprint/"), TEXT("nodetocode://blueprint/a/b?x"), TEXT("nodetocode://blueprint/Game//BP_Player"),
				TEXT("nodetocode://blueprint/BP%zz") })
			{
				TestFalse(FString(Bad) + TEXT(" is not registered"), Manager.IsResourceRegistered(Bad));
				TestEqual(FString(Bad) + TEXT(": -32002"), Manager.ReadResource(Bad).ErrorCode, static_cast<int32>(JsonRpcErrorCodes::ResourceNotFound));
			}
		});

		It(TEXT("answer an unknown Blueprint with -32002 instead of a success"), [this]()
		{
			for (const TCHAR* Uri : { TEXT("nodetocode://blueprint/Game/__N2CTestResourceUri/BP_Missing"), TEXT("nodetocode://blueprint/BP_N2CResourceUriMissing"),
				TEXT("nodetocode://blueprint/") })
			{
				const FMcpResourceContents Contents = FN2CMcpBlueprintByNameResource().Read(Uri);
				TestEqual(FString(Uri) + TEXT(": -32002"), Contents.ErrorCode, static_cast<int32>(JsonRpcErrorCodes::ResourceNotFound));
				const TSharedPtr<FJsonObject> Error = Parse(Contents.Text);
				TestTrue(FString(Uri) + TEXT(": the text is the error with the uri"), Error.IsValid() && Error->GetStringField(TEXT("uri")) == Uri);
			}
		});

		It(TEXT("still read an open Blueprint by its name alone and list its by-path URI"), [this]()
		{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
			// UE 5.6 and 5.7 under -nullrhi cannot close a Blueprint editor window (see NodeToCode.Editor.Focus)
			AddInfo(TEXT("Skipped below UE 5.8: a headless Blueprint editor window cannot be closed there"));
#else
			N2CTest::FTestBlueprint Test(TEXT("BP_N2CResourceUriOpen"));
			UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (!TestNotNull(TEXT("Blueprint"), Test.GetBlueprint()) || !TestNotNull(TEXT("asset editor subsystem"), AssetEditors)
				|| !TestNotNull(TEXT("a node"), Test.AddCall(Test.EventGraph(), UKismetSystemLibrary::StaticClass(), TEXT("PrintString"))))
			{
				return;
			}
			ON_SCOPE_EXIT
			{
				Test.GetPackage()->SetDirtyFlag(false);
				AssetEditors->CloseAllEditorsForAsset(Test.GetBlueprint());
			};
			TSharedPtr<IBlueprintEditor> Opened;
			FString OpenError;
			if (!TestTrue(TEXT("opens: ") + OpenError, FN2CMcpBlueprintUtils::OpenBlueprintEditor(Test.GetBlueprint(), Opened, OpenError)))
			{
				return;
			}

			const FMcpResourceContents List = FN2CMcpAllBlueprintsResource().Read(TEXT("nodetocode://blueprints/all"));
			const TSharedPtr<FJsonObject> Root = Parse(List.Text);
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			FString ListedUri;
			if (Root.IsValid() && Root->TryGetArrayField(TEXT("blueprints"), Entries))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Entries)
				{
					const TSharedPtr<FJsonObject> Entry = Value->AsObject();
					if (Entry.IsValid() && Entry->GetStringField(TEXT("path")) == Test.GetBlueprint()->GetPathName())
					{
						ListedUri = Entry->GetStringField(TEXT("resourceUri"));
					}
				}
			}
			TestEqual(TEXT("blueprints/all lists the by-path URI"), ListedUri, FN2CMcpBlueprintByNameResource::UriFor(*Test.GetBlueprint()));
			const FMcpResourceContents ByListedUri = FN2CMcpBlueprintByNameResource().Read(ListedUri);
			TestFalse(TEXT("the listed URI reads: ") + ByListedUri.ErrorMessage, ByListedUri.IsError());

			const FMcpResourceContents ByName = FN2CMcpBlueprintByNameResource().Read(TEXT("nodetocode://blueprint/BP_N2CResourceUriOpen"));
			TestFalse(TEXT("the old name form reads: ") + ByName.ErrorMessage, ByName.IsError());
			TestEqual(TEXT("the same document"), ByName.Text, ByListedUri.Text);
#endif
		});
	});
}

#endif
