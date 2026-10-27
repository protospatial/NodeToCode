// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "MCP/Utils/N2CMcpArgumentParser.h"
#include "MCP/Utils/N2CMcpVariableUtils.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace N2CArgumentParserSpec
{
	/** Arguments from JSON text */
	TSharedPtr<FJsonObject> Args(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}

	/** Every tool under Implementations/Blueprint/Variables: strict arguments are on for each */
	const TCHAR* const VariableTools[] = {
		TEXT("create-variable"), TEXT("create-local-variable"), TEXT("create-get-member-variable-node"),
		TEXT("create-set-member-variable-node"), TEXT("create-get-local-function-variable-node"),
		TEXT("create-set-local-function-variable-node"), TEXT("get-blueprint-member-variables"),
		TEXT("get-blueprint-function-local-variables"), TEXT("set-member-variable-default-value"),
		TEXT("set-local-function-variable-default-value"), TEXT("search-variable-types"),
	};
}

/**
 * FN2CMcpArgumentParser's input rules (card 0194, imp-mcp-tools-variables-8): required strings take only JSON strings,
 * Blueprint member names follow FKismetNameValidator's rules, enum-like fields (containerType, replicationCondition,
 * search-variable-types' category) match any case and refuse unknown values listing the valid ones, and a strict tool
 * (every variable tool) refuses arguments its schema does not declare, naming them. The tool cases run through
 * FN2CMcpToolManager::ExecuteTool as tools/call does; each refusal comes before any Blueprint is looked up, so they need
 * no editor.
 */
BEGIN_DEFINE_SPEC(FN2CArgumentParserSpec, "NodeToCode.Robustness.ArgumentParser", N2C_TEST_FLAGS)

	/** Runs a tool and returns its text; a test error when it did not fail */
	FString ExpectToolError(const FString& Tool, const FString& Json)
	{
		const FMcpToolCallResult Result = N2CTest::RunTool(Tool, N2CArgumentParserSpec::Args(Json));
		const FString Text = N2CTest::ResultText(Result);
		TestTrue(FString::Printf(TEXT("%s %s is refused (got: %s)"), *Tool, *Json, *Text.Left(300)), Result.bIsError);
		return Text;
	}

END_DEFINE_SPEC(FN2CArgumentParserSpec)

void FN2CArgumentParserSpec::Define()
{
	using namespace N2CArgumentParserSpec;

	Describe(TEXT("a required string"), [this]()
	{
		It(TEXT("refuses a number, a bool, an object, an array and null instead of turning them into text"), [this]()
		{
			for (const TCHAR* Value : { TEXT("5"), TEXT("1.5"), TEXT("true"), TEXT("{\"a\":1}"), TEXT("[\"x\"]"), TEXT("null") })
			{
				FN2CMcpArgumentParser Parser(Args(FString::Printf(TEXT("{\"name\":%s}"), Value)));
				FString Out, Error;
				TestFalse(FString::Printf(TEXT("%s is refused"), Value), Parser.TryGetRequiredString(TEXT("name"), Out, Error));
				TestEqual(FString::Printf(TEXT("%s: the error"), Value), Error, FString(TEXT("Field 'name' must be a string.")));
			}
		});

		It(TEXT("takes a string, and an empty one only when allowed"), [this]()
		{
			FN2CMcpArgumentParser Parser(Args(TEXT("{\"name\":\"Health\",\"empty\":\"\"}")));
			FString Out, Error;
			TestTrue(TEXT("a string"), Parser.TryGetRequiredString(TEXT("name"), Out, Error));
			TestEqual(TEXT("its value"), Out, FString(TEXT("Health")));
			TestFalse(TEXT("an empty string"), Parser.TryGetRequiredString(TEXT("empty"), Out, Error));
			TestTrue(TEXT("an empty string when allowed"), Parser.TryGetRequiredString(TEXT("empty"), Out, Error, /*bAllowEmpty*/ true));
			TestFalse(TEXT("a missing field"), Parser.TryGetRequiredString(TEXT("missing"), Out, Error));
			TestEqual(TEXT("the missing-field error"), Error, FString(TEXT("Missing required field: missing")));
		});
	});

	Describe(TEXT("an identifier"), [this]()
	{
		It(TEXT("takes the names the editor takes for a variable"), [this]()
		{
			for (const TCHAR* Name : { TEXT("Health"), TEXT("My Health"), TEXT("bIsAlive"), TEXT("Größe"), TEXT("_tmp1") })
			{
				FString Error;
				TestTrue(FString::Printf(TEXT("'%s' (%s)"), Name, *Error), FN2CMcpArgumentParser::ValidateIdentifier(TEXT("variableName"), Name, Error));
			}
			const FString Longest = FString::ChrN(FKismetNameValidator::GetMaximumNameLength(), TEXT('a'));
			FString Error;
			TestTrue(TEXT("a name of the maximum length"), FN2CMcpArgumentParser::ValidateIdentifier(TEXT("variableName"), Longest, Error));
		});

		It(TEXT("refuses what FKismetNameValidator refuses: empty, None, too long, a dot"), [this]()
		{
			const FString TooLong = FString::ChrN(FKismetNameValidator::GetMaximumNameLength() + 1, TEXT('a'));
			const TPair<FString, FString> Cases[] = {
				{ TEXT(""), TEXT("empty and 'None' are not names") },
				{ TEXT("None"), TEXT("empty and 'None' are not names") },
				{ TooLong, TEXT("Blueprint names are limited to") },
				{ TEXT("Owner.Health"), TEXT("contains a character Blueprint names cannot hold") },
			};
			for (const TPair<FString, FString>& Case : Cases)
			{
				FString Error;
				TestFalse(FString::Printf(TEXT("'%s' is refused"), *Case.Key.Left(40)), FN2CMcpArgumentParser::ValidateIdentifier(TEXT("variableName"), Case.Key, Error));
				TestTrue(FString::Printf(TEXT("'%s': the error names the rule (got: %s)"), *Case.Key.Left(40), *Error),
					Error.StartsWith(TEXT("INVALID_ARGUMENT: Field 'variableName'")) && Error.Contains(Case.Value));
			}
		});

		It(TEXT("is a required string first"), [this]()
		{
			FN2CMcpArgumentParser Parser(Args(TEXT("{\"variableName\":42}")));
			FString Out, Error;
			TestFalse(TEXT("a number"), Parser.TryGetRequiredIdentifier(TEXT("variableName"), Out, Error));
			TestEqual(TEXT("the error"), Error, FString(TEXT("Field 'variableName' must be a string.")));
		});
	});

	Describe(TEXT("an enum-like field"), [this]()
	{
		const TArray<FString> Allowed = { TEXT("none"), TEXT("array"), TEXT("set"), TEXT("map") };

		It(TEXT("matches any case and gives the value as listed"), [this, Allowed]()
		{
			FN2CMcpArgumentParser Parser(Args(TEXT("{\"containerType\":\"ARRAY\"}")));
			FString Out, Error;
			TestTrue(TEXT("ARRAY"), Parser.TryGetOptionalEnum(TEXT("containerType"), Allowed, TEXT("none"), Out, Error));
			TestEqual(TEXT("the listed value"), Out, FString(TEXT("array")));
		});

		It(TEXT("gives the default when absent or null"), [this, Allowed]()
		{
			for (const TCHAR* Json : { TEXT("{}"), TEXT("{\"containerType\":null}") })
			{
				FN2CMcpArgumentParser Parser(Args(Json));
				FString Out, Error;
				TestTrue(Json, Parser.TryGetOptionalEnum(TEXT("containerType"), Allowed, TEXT("none"), Out, Error));
				TestEqual(FString(Json) + TEXT(": the default"), Out, FString(TEXT("none")));
			}
		});

		It(TEXT("refuses an unknown value or a non-string, listing the valid values"), [this, Allowed]()
		{
			for (const TCHAR* Json : { TEXT("{\"containerType\":\"list\"}"), TEXT("{\"containerType\":\"\"}"), TEXT("{\"containerType\":1}") })
			{
				FN2CMcpArgumentParser Parser(Args(Json));
				FString Out, Error;
				TestFalse(Json, Parser.TryGetOptionalEnum(TEXT("containerType"), Allowed, TEXT("none"), Out, Error));
				TestTrue(FString::Printf(TEXT("%s: the error lists the valid values (got: %s)"), Json, *Error),
					Error.StartsWith(TEXT("INVALID_ARGUMENT: Field 'containerType'")) && Error.Contains(TEXT("none, array, set, map")));
			}
		});
	});

	Describe(TEXT("unknown fields"), [this]()
	{
		It(TEXT("are the arguments the schema's properties do not declare"), [this]()
		{
			const TSharedPtr<FJsonObject> Schema = Args(TEXT("{\"type\":\"object\",\"properties\":{\"variableName\":{},\"isInstanceEditable\":{}}}"));
			TestEqual(TEXT("a typo"), FString::Join(FN2CMcpArgumentParser::FindUnknownFields(
				Args(TEXT("{\"variableName\":\"A\",\"isInstanceEditible\":true}")), Schema), TEXT(",")), FString(TEXT("isInstanceEditible")));
			TestEqual(TEXT("declared fields only"), FN2CMcpArgumentParser::FindUnknownFields(
				Args(TEXT("{\"variableName\":\"A\",\"isInstanceEditable\":true}")), Schema).Num(), 0);
			TestEqual(TEXT("a schema without properties"), FString::Join(FN2CMcpArgumentParser::FindUnknownFields(
				Args(TEXT("{\"x\":1}")), Args(TEXT("{\"type\":\"object\"}"))), TEXT(",")), FString(TEXT("x")));
		});
	});

	Describe(TEXT("the variable tools"), [this]()
	{
		It(TEXT("are strict and publish additionalProperties false"), [this]()
		{
			for (const TCHAR* Name : VariableTools)
			{
				const TSharedPtr<IN2CMcpTool> Tool = N2CTest::FindRegisteredTool(Name);
				if (!TestTrue(FString::Printf(TEXT("%s is registered"), Name), Tool.IsValid()))
				{
					continue;
				}
				const FMcpToolDefinition Definition = Tool->GetDefinition();
				TestTrue(FString::Printf(TEXT("%s is strict"), Name), Definition.bStrictArguments);
				bool bAdditional = true;
				TestTrue(FString::Printf(TEXT("%s publishes additionalProperties false"), Name), Definition.InputSchema.IsValid()
					&& Definition.InputSchema->TryGetBoolField(TEXT("additionalProperties"), bAdditional) && !bAdditional);
			}
		});

		It(TEXT("refuse a misspelled argument, naming it and the valid ones"), [this]()
		{
			const FString Text = ExpectToolError(TEXT("create-variable"),
				TEXT("{\"variableName\":\"N2CStrictProbe\",\"typeIdentifier\":\"float\",\"isInstanceEditible\":true}"));
			TestTrue(FString::Printf(TEXT("the error names the argument (got: %s)"), *Text),
				Text.Contains(TEXT("INVALID_ARGUMENT: Unknown argument for create-variable: isInstanceEditible")));
			TestTrue(TEXT("the error lists isInstanceEditable"), Text.Contains(TEXT("isInstanceEditable")));
		});

		It(TEXT("refuse a variable name that is not a string or not a Blueprint name"), [this]()
		{
			TestTrue(TEXT("a number"), ExpectToolError(TEXT("create-variable"),
				TEXT("{\"variableName\":7,\"typeIdentifier\":\"float\"}")).Contains(TEXT("Field 'variableName' must be a string.")));
			TestTrue(TEXT("a dot"), ExpectToolError(TEXT("create-local-variable"),
				TEXT("{\"variableName\":\"A.B\",\"typeIdentifier\":\"float\"}")).Contains(TEXT("cannot hold")));
			TestTrue(TEXT("None"), ExpectToolError(TEXT("set-member-variable-default-value"),
				TEXT("{\"variableName\":\"None\",\"defaultValue\":\"1\"}")).Contains(TEXT("are not names")));
			TestTrue(TEXT("a number for a node tool"), ExpectToolError(TEXT("create-get-member-variable-node"),
				TEXT("{\"variableName\":true}")).Contains(TEXT("must be a string")));
		});

		It(TEXT("refuse an unknown containerType or replicationCondition, listing the valid values"), [this]()
		{
			TestTrue(TEXT("containerType list"), ExpectToolError(TEXT("create-variable"),
				TEXT("{\"variableName\":\"N2CStrictProbe\",\"typeIdentifier\":\"float\",\"containerType\":\"list\"}"))
				.Contains(TEXT("valid values: none, array, set, map")));
			TestTrue(TEXT("containerType for a local"), ExpectToolError(TEXT("create-local-variable"),
				TEXT("{\"variableName\":\"N2CStrictProbe\",\"typeIdentifier\":\"float\",\"containerType\":\"TArray\"}"))
				.Contains(TEXT("valid values: none, array, set, map")));
			TestTrue(TEXT("replicationCondition"), ExpectToolError(TEXT("create-variable"),
				TEXT("{\"variableName\":\"N2CStrictProbe\",\"typeIdentifier\":\"float\",\"replicationCondition\":\"sometimes\"}"))
				.Contains(TEXT("valid values: none, replicated, repnotify")));
		});

		// search-variable-types looks for the focused Blueprint editor before it reads its arguments
		It(TEXT("search-variable-types refuses an unknown category and takes one in any case"), [this]()
		{
#if !N2C_CAN_CLOSE_HEADLESS_BLUEPRINT_EDITOR
			AddInfo(TEXT("Skipped below UE 5.8: search-variable-types needs a focused Blueprint editor, which a headless test cannot close there"));
#else
			N2CTest::FToolSpecBlueprint Blueprint(TEXT("BP_N2CArgumentParser"));
			if (!Blueprint.OpenInEditor(*this, Blueprint.EventGraph()))
			{
				return;
			}
			TestTrue(TEXT("category structs"), ExpectToolError(TEXT("search-variable-types"),
				TEXT("{\"searchTerm\":\"Vector\",\"category\":\"structs\"}"))
				.Contains(TEXT("valid values: all, primitive, class, struct, enum")));
			const FMcpToolCallResult Result = N2CTest::RunTool(TEXT("search-variable-types"),
				Args(TEXT("{\"searchTerm\":\"Vector\",\"category\":\"STRUCT\",\"maxResults\":5}")));
			TestFalse(FString::Printf(TEXT("STRUCT is a category (got: %s)"), *N2CTest::ResultText(Result).Left(300)), Result.bIsError);
#endif
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
