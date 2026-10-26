// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Internationalization/Regex.h"

/**
 * Static checks over every MCP tool compiled into the module (FN2CMcpToolRegistry, whatever the tool profile): names
 * are unique and valid, every inputSchema is a JSON Schema object whose required fields exist in its properties, and
 * every tool belongs to a declared toolset (N2CMcpToolsets), since the tool profile serves tools by toolset and tool
 * search reaches them through it. Review ids: imp-dx-docs-release-3 (c), mcp-tools-python-translation-resources-14,
 * dx-docs-release-6 (card 0165).
 */
BEGIN_DEFINE_SPEC(FN2CToolRegistrySpec, "NodeToCode.Registry", N2C_TEST_FLAGS)

	TArray<FMcpToolDefinition> Definitions;

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

	It(TEXT("gives every tool a declared toolset"), [this]()
	{
		for (const FMcpToolDefinition& Def : Definitions)
		{
			TestTrue(FString::Printf(TEXT("'%s' has a toolset"), *Def.Name), Def.Toolset != nullptr);
			if (Def.Toolset)
			{
				TestTrue(FString::Printf(TEXT("'%s' toolset '%s' is declared"), *Def.Name, Def.Toolset->Name),
					N2CMcpToolsets::Find(Def.Toolset->Name) == Def.Toolset);
			}
		}
	});

	It(TEXT("puts find-nodes-in-graph in the graph-editing toolset"), [this]()
	{
		const FMcpToolDefinition* FindNodes = Definitions.FindByPredicate(
			[](const FMcpToolDefinition& Def) { return Def.Name == TEXT("find-nodes-in-graph"); });
		if (TestNotNull(TEXT("find-nodes-in-graph is registered"), FindNodes))
		{
			TestTrue(TEXT("find-nodes-in-graph is in BlueprintGraphTools"), FindNodes->Toolset == &N2CMcpToolsets::BlueprintGraph);
		}
	});

	It(TEXT("declares each toolset once, with a name, a description and at least one tool"), [this]()
	{
		const FRegexPattern ValidName(TEXT("^[A-Z][A-Za-z0-9]*Tools$"));
		TSet<FString> Seen;
		for (const FN2CMcpToolset* Toolset : N2CMcpToolsets::All)
		{
			const FString Name = Toolset->Name;
			FRegexMatcher Matcher(ValidName, Name);
			TestTrue(FString::Printf(TEXT("'%s' is PascalCase ending in Tools"), *Name), Matcher.FindNext());
			bool bDuplicate = false;
			Seen.Add(Name.ToLower(), &bDuplicate);
			TestFalse(FString::Printf(TEXT("'%s' is declared once"), *Name), bDuplicate);
			TestFalse(FString::Printf(TEXT("'%s' has a description"), *Name), FString(Toolset->Description).TrimStartAndEnd().IsEmpty());
			TestTrue(FString::Printf(TEXT("'%s' has a tool"), *Name), Definitions.ContainsByPredicate(
				[Toolset](const FMcpToolDefinition& Def) { return Def.Toolset == Toolset; }));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
