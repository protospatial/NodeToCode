// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Internationalization/Regex.h"

/**
 * Static checks over every MCP tool compiled into the module (FN2CMcpToolRegistry, whatever the registration mode):
 * names are unique and valid, every inputSchema is a JSON Schema object whose required fields exist in its
 * properties, and the categories match what assess-needed-tools advertises, since dynamic tool discovery enables
 * tools by exact category name. Review ids: imp-dx-docs-release-3 (c), mcp-tools-python-translation-resources-14.
 */
BEGIN_DEFINE_SPEC(FN2CToolRegistrySpec, "NodeToCode.Registry", N2C_TEST_FLAGS)

	TArray<FMcpToolDefinition> Definitions;

	/**
	 * The categories assess-needed-tools offers a client: the enum on categories.items when the schema has one,
	 * otherwise the "- **Name**: ..." lines of its description.
	 */
	TSet<FString> AdvertisedCategories()
	{
		TSet<FString> Categories;
		const FMcpToolDefinition* Assess = Definitions.FindByPredicate(
			[](const FMcpToolDefinition& Def) { return Def.Name == TEXT("assess-needed-tools"); });
		if (!TestNotNull(TEXT("assess-needed-tools is registered"), Assess))
		{
			return Categories;
		}

		const TSharedPtr<FJsonObject>* Properties = nullptr;
		const TSharedPtr<FJsonObject>* CategoriesProp = nullptr;
		const TSharedPtr<FJsonObject>* Items = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Enum = nullptr;
		if (Assess->InputSchema.IsValid() && Assess->InputSchema->TryGetObjectField(TEXT("properties"), Properties)
			&& (*Properties)->TryGetObjectField(TEXT("categories"), CategoriesProp)
			&& (*CategoriesProp)->TryGetObjectField(TEXT("items"), Items)
			&& (*Items)->TryGetArrayField(TEXT("enum"), Enum))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Enum)
			{
				Categories.Add(Value->AsString());
			}
			return Categories;
		}

		const FRegexPattern Bullet(TEXT("^\\s*-\\s*\\*\\*(.+?)\\*\\*\\s*:"));
		TArray<FString> Lines;
		Assess->Description.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(Bullet, Line);
			if (Matcher.FindNext())
			{
				Categories.Add(Matcher.GetCaptureGroup(1).TrimStartAndEnd());
			}
		}
		return Categories;
	}

	/** Checks one (sub)schema of type object: properties is an object and every required name is in it. Recurses. */
	void CheckObjectSchema(const FString& Where, const TSharedPtr<FJsonObject>& Schema)
	{
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		const bool bHasProperties = Schema->HasField(TEXT("properties"));
		if (bHasProperties)
		{
			TestTrue(FString::Printf(TEXT("%s: properties is an object"), *Where), Schema->TryGetObjectField(TEXT("properties"), Properties));
		}

		const TArray<TSharedPtr<FJsonValue>>* Required = nullptr;
		if (Schema->HasField(TEXT("required")))
		{
			if (!TestTrue(FString::Printf(TEXT("%s: required is an array"), *Where), Schema->TryGetArrayField(TEXT("required"), Required)))
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Required)
			{
				FString Name;
				if (!TestTrue(FString::Printf(TEXT("%s: required entries are strings"), *Where), Value.IsValid() && Value->TryGetString(Name)))
				{
					continue;
				}
				TestTrue(FString::Printf(TEXT("%s: required '%s' is in properties"), *Where, *Name),
					Properties != nullptr && (*Properties)->HasField(Name));
			}
		}

		if (Properties != nullptr)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
			{
				const TSharedPtr<FJsonObject>* Property = nullptr;
				const FString PropertyWhere = Where + TEXT(".") + Pair.Key;
				if (!TestTrue(FString::Printf(TEXT("%s is a schema object"), *PropertyWhere), Pair.Value.IsValid() && Pair.Value->TryGetObject(Property)))
				{
					continue;
				}
				FString Type;
				if ((*Property)->TryGetStringField(TEXT("type"), Type) && Type == TEXT("object"))
				{
					CheckObjectSchema(PropertyWhere, *Property);
				}
				const TSharedPtr<FJsonObject>* Items = nullptr;
				FString ItemsType;
				if ((*Property)->TryGetObjectField(TEXT("items"), Items) && (*Items)->TryGetStringField(TEXT("type"), ItemsType) && ItemsType == TEXT("object"))
				{
					CheckObjectSchema(PropertyWhere + TEXT("[]"), *Items);
				}
			}
		}
	}

END_DEFINE_SPEC(FN2CToolRegistrySpec)

void FN2CToolRegistrySpec::Define()
{
	BeforeEach([this]()
	{
		Definitions.Reset();
		for (const TSharedPtr<IN2CMcpTool>& Tool : N2CTest::AllRegisteredTools())
		{
			Definitions.Add(Tool->GetDefinition());
		}
	});

	It(TEXT("registers tools"), [this]()
	{
		TestTrue(FString::Printf(TEXT("%d tools registered"), Definitions.Num()), Definitions.Num() > 1);
	});

	It(TEXT("gives every tool a unique, valid name and a description"), [this]()
	{
		// MCP clients (and the Anthropic and OpenAI tool APIs behind them) accept [A-Za-z0-9_-]{1,64}
		const FRegexPattern ValidName(TEXT("^[A-Za-z0-9_-]{1,64}$"));
		TSet<FString> Seen;
		for (const FMcpToolDefinition& Def : Definitions)
		{
			FRegexMatcher Matcher(ValidName, Def.Name);
			TestTrue(FString::Printf(TEXT("'%s' is a valid tool name"), *Def.Name), Matcher.FindNext());
			bool bDuplicate = false;
			Seen.Add(Def.Name, &bDuplicate);
			TestFalse(FString::Printf(TEXT("'%s' is registered once"), *Def.Name), bDuplicate);
			TestFalse(FString::Printf(TEXT("'%s' has a description"), *Def.Name), Def.Description.TrimStartAndEnd().IsEmpty());
		}
	});

	It(TEXT("gives every tool an inputSchema of type object whose required fields are in its properties"), [this]()
	{
		for (const FMcpToolDefinition& Def : Definitions)
		{
			if (!TestTrue(FString::Printf(TEXT("'%s' has an inputSchema"), *Def.Name), Def.InputSchema.IsValid()))
			{
				continue;
			}
			FString Type;
			TestTrue(FString::Printf(TEXT("'%s' inputSchema type is object"), *Def.Name),
				Def.InputSchema->TryGetStringField(TEXT("type"), Type) && Type == TEXT("object"));
			CheckObjectSchema(Def.Name, Def.InputSchema);

			// tools/list sends the definition's JSON: the schema must survive it
			const TSharedPtr<FJsonObject> Json = Def.ToJson();
			TestTrue(FString::Printf(TEXT("'%s' tools/list entry has inputSchema"), *Def.Name), Json.IsValid() && Json->HasTypedField<EJson::Object>(TEXT("inputSchema")));
		}
	});

	It(TEXT("advertises only categories that have tools"), [this]()
	{
		TSet<FString> Used;
		for (const FMcpToolDefinition& Def : Definitions)
		{
			Used.Add(Def.Category);
		}
		const TSet<FString> Advertised = AdvertisedCategories();
		TestTrue(FString::Printf(TEXT("assess-needed-tools advertises categories (%d)"), Advertised.Num()), Advertised.Num() > 0);
		for (const FString& Category : Advertised)
		{
			TestTrue(FString::Printf(TEXT("advertised category '%s' has at least one tool"), *Category), Used.Contains(Category));
		}
	});

	// Known defect mcp-tools-python-translation-resources-14: assess-needed-tools lists 9 hard-coded categories, so
	// Python, Blueprint Classes, Blueprint Compilation and Blueprint Components can never be enabled in dynamic mode.
	// Enable (xIt -> It) when the list is built from the registry.
	xIt(TEXT("advertises the category of every tool through assess-needed-tools"), [this]()
	{
		const TSet<FString> Advertised = AdvertisedCategories();
		for (const FMcpToolDefinition& Def : Definitions)
		{
			if (!Def.Category.IsEmpty())
			{
				TestTrue(FString::Printf(TEXT("'%s' category '%s' is advertised"), *Def.Name, *Def.Category), Advertised.Contains(Def.Category));
			}
		}
	});

	// Known defect mcp-tools-python-translation-resources-14: find-nodes-in-graph is built with the two-argument
	// constructor and has no category, so no assess-needed-tools request can enable it. Enable (xIt -> It) with the fix.
	xIt(TEXT("gives every tool a category"), [this]()
	{
		for (const FMcpToolDefinition& Def : Definitions)
		{
			TestFalse(FString::Printf(TEXT("'%s' has a category"), *Def.Name), Def.Category.IsEmpty());
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
