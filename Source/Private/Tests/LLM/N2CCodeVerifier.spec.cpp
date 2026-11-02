// Copyright (c) 2025 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "LLM/Verify/N2CCodeVerifiers.h"
#include "Tests/N2CTestHelpers.h"

/**
 * Card 0312: the code verifier seam and its two adapters. FN2CPythonSyntaxVerifier parses Python with the editor's own
 * Python and reports a syntax error's line and column (in the original, indented text too); valid code gives nothing.
 * FN2CCaptureCoverageVerifier names a function call of the capture that the generated code leaves out, matches names
 * without case, underscores or the K2_/Receive prefixes, and expects nothing of nodes Blueprint never runs.
 */
BEGIN_DEFINE_SPEC(FN2CCodeVerifierSpec, "NodeToCode.LLM.Verify", N2C_TEST_FLAGS)

	static FN2CTranslationResponse OneGraph(const FString& GraphName, const FString& Implementation)
	{
		FN2CTranslationResponse Translation;
		FN2CGraphTranslation& Graph = Translation.Graphs.AddDefaulted_GetRef();
		Graph.GraphName = GraphName;
		Graph.GraphType = TEXT("Function");
		Graph.Code.GraphImplementation = Implementation;
		return Translation;
	}

	static FN2CNodeDefinition Node(const TCHAR* ID, EN2CNodeType Type, const TCHAR* Parent, const TCHAR* Member)
	{
		FN2CNodeDefinition Definition;
		Definition.ID = ID;
		Definition.NodeType = Type;
		Definition.MemberParent = Parent;
		Definition.MemberName = Member;
		return Definition;
	}

	/** A capture of graph DoThings: BeginPlay, PrintString, K2_GetActorLocation, variable Health and a disabled CollectGarbage */
	static FN2CBlueprint Capture()
	{
		FN2CBlueprint Blueprint;
		Blueprint.Metadata.Name = TEXT("BP_Verify");
		Blueprint.Metadata.BlueprintClass = TEXT("BP_Verify");
		FN2CGraph& Graph = Blueprint.Graphs.AddDefaulted_GetRef();
		Graph.Name = TEXT("DoThings");
		Graph.Nodes.Add(Node(TEXT("N1"), EN2CNodeType::Event, TEXT("Actor"), TEXT("ReceiveBeginPlay")));
		Graph.Nodes.Add(Node(TEXT("N2"), EN2CNodeType::CallFunction, TEXT("KismetSystemLibrary"), TEXT("PrintString")));
		Graph.Nodes.Add(Node(TEXT("N3"), EN2CNodeType::CallFunction, TEXT("Actor"), TEXT("K2_GetActorLocation")));
		Graph.Nodes.Add(Node(TEXT("N4"), EN2CNodeType::VariableGet, TEXT("BP_Verify"), TEXT("Health")));
		FN2CNodeDefinition Disabled = Node(TEXT("N5"), EN2CNodeType::CallFunction, TEXT("KismetSystemLibrary"), TEXT("CollectGarbage"));
		Disabled.bDisabled = true;
		Graph.Nodes.Add(Disabled);
		return Blueprint;
	}

END_DEFINE_SPEC(FN2CCodeVerifierSpec)

void FN2CCodeVerifierSpec::Define()
{
	Describe(TEXT("FN2CPythonSyntaxVerifier"), [this]()
	{
		It(TEXT("reports a syntax error with its graph, line and column"), [this]()
		{
			const FN2CVerifyReport Report = FN2CPythonSyntaxVerifier().Verify(
				OneGraph(TEXT("DoThings"), TEXT("x = 1\ny = = 2\n")), FN2CBlueprint(), EN2CCodeLanguage::Python);
			TestTrue(TEXT("it ran"), Report.bRan);
			if (TestEqual(TEXT("one diagnostic"), Report.Diagnostics.Num(), 1))
			{
				const FN2CVerifyDiagnostic& Diagnostic = Report.Diagnostics[0];
				TestEqual(TEXT("code"), Diagnostic.Code, FString(N2CVerify::SyntaxError));
				TestTrue(TEXT("an error"), Diagnostic.Severity == EN2CDiagnosticSeverity::Error);
				TestEqual(TEXT("graph"), Diagnostic.Graph, FString(TEXT("DoThings")));
				TestEqual(TEXT("file"), Diagnostic.File, FString(TEXT("implementation")));
				TestEqual(TEXT("line"), Diagnostic.Line, 2);
				TestEqual(TEXT("column"), Diagnostic.Column, 5);
				TestTrue(TEXT("Python's message: ") + Diagnostic.Message, Diagnostic.Message.Contains(TEXT("invalid syntax")));
			}
			TestTrue(TEXT("HasErrors"), Report.HasErrors());
		});

		It(TEXT("gives the column in the original text when the code is indented"), [this]()
		{
			const FN2CVerifyReport Report = FN2CPythonSyntaxVerifier().Verify(
				OneGraph(TEXT("Indented"), TEXT("    x = 1\n    y = = 2\n")), FN2CBlueprint(), EN2CCodeLanguage::Python);
			if (TestEqual(TEXT("one diagnostic"), Report.Diagnostics.Num(), 1))
			{
				TestEqual(TEXT("line"), Report.Diagnostics[0].Line, 2);
				TestEqual(TEXT("column"), Report.Diagnostics[0].Column, 9);
			}
		});

		It(TEXT("finds nothing in valid Python, and does not run for another language"), [this]()
		{
			const FN2CTranslationResponse Valid = OneGraph(TEXT("DoThings"),
				TEXT("def do_things(self):\n    unreal.SystemLibrary.print_string(self, \"Hello\")\n    return {'a': [1, 2]}\n"));
			const FN2CVerifyReport Report = FN2CPythonSyntaxVerifier().Verify(Valid, FN2CBlueprint(), EN2CCodeLanguage::Python);
			TestTrue(TEXT("it ran"), Report.bRan);
			TestEqual(TEXT("no diagnostic"), Report.Diagnostics.Num(), 0);

			const FN2CVerifyReport Cpp = FN2CPythonSyntaxVerifier().Verify(OneGraph(TEXT("X"), TEXT("int x = ;")), FN2CBlueprint(),
				EN2CCodeLanguage::Cpp);
			TestFalse(TEXT("C++ is not its language"), Cpp.bRan);
			TestEqual(TEXT("no diagnostic for C++"), Cpp.Diagnostics.Num(), 0);
		});

		It(TEXT("checks each graph and code separately and keeps code with quotes and backslashes intact"), [this]()
		{
			FN2CTranslationResponse Translation = OneGraph(TEXT("Good"), TEXT("s = 'a\\\\b \"c\" \\u00e9'\n"));
			FN2CGraphTranslation& Bad = Translation.Graphs.AddDefaulted_GetRef();
			Bad.GraphName = TEXT("Bad");
			Bad.Code.GraphImplementation = TEXT("def f(:\n    pass\n");
			const FN2CVerifyReport Report = FN2CPythonSyntaxVerifier().Verify(Translation, FN2CBlueprint(), EN2CCodeLanguage::Python);
			if (TestEqual(TEXT("one diagnostic"), Report.Diagnostics.Num(), 1))
			{
				TestEqual(TEXT("on the bad graph"), Report.Diagnostics[0].Graph, FString(TEXT("Bad")));
				TestEqual(TEXT("line 1"), Report.Diagnostics[0].Line, 1);
			}
		});
	});

	Describe(TEXT("FN2CCaptureCoverageVerifier"), [this]()
	{
		It(TEXT("names a function call of the capture that the generated code leaves out"), [this]()
		{
			// Code that calls BeginPlay, PrintString and GetActorLocation but never reads Health or calls the disabled node
			const FN2CTranslationResponse Translation = OneGraph(TEXT("DoThings"),
				TEXT("void ABP_Verify::BeginPlay()\n{\n    FVector Location = GetActorLocation();\n    UKismetSystemLibrary::PrintString(this, TEXT(\"Hi\"));\n}\n"));
			const FN2CVerifyReport Report = FN2CCaptureCoverageVerifier().Verify(Translation, Capture(), EN2CCodeLanguage::Cpp);
			TestTrue(TEXT("it ran"), Report.bRan);
			if (TestEqual(TEXT("one diagnostic"), Report.Diagnostics.Num(), 1))
			{
				const FN2CVerifyDiagnostic& Diagnostic = Report.Diagnostics[0];
				TestEqual(TEXT("code"), Diagnostic.Code, FString(N2CVerify::NodeNotCovered));
				TestEqual(TEXT("graph"), Diagnostic.Graph, FString(TEXT("DoThings")));
				TestTrue(TEXT("a warning"), Diagnostic.Severity == EN2CDiagnosticSeverity::Warning);
				TestTrue(TEXT("it names Health and its node: ") + Diagnostic.Message,
					Diagnostic.Message.Contains(TEXT("BP_Verify.Health")) && Diagnostic.Message.Contains(TEXT("node N4")));
			}
		});

		It(TEXT("matches snake_case Python names and names a missing call"), [this]()
		{
			const FN2CTranslationResponse Translation = OneGraph(TEXT("DoThings"),
				TEXT("def receive_begin_play(self):\n    health = self.health\n    loc = self.get_actor_location()\n"));
			const FN2CVerifyReport Report = FN2CCaptureCoverageVerifier().Verify(Translation, Capture(), EN2CCodeLanguage::Python);
			if (TestEqual(TEXT("one diagnostic"), Report.Diagnostics.Num(), 1))
			{
				TestTrue(TEXT("PrintString is missing: ") + Report.Diagnostics[0].Message,
					Report.Diagnostics[0].Message.Contains(TEXT("KismetSystemLibrary.PrintString")));
			}
		});

		It(TEXT("compares whole identifiers, drops a Boolean's b, and reads pseudocode as words"), [this]()
		{
			FN2CBlueprint Blueprint;
			FN2CGraph& Graph = Blueprint.Graphs.AddDefaulted_GetRef();
			Graph.Name = TEXT("Check");
			Graph.Nodes.Add(Node(TEXT("N1"), EN2CNodeType::VariableGet, TEXT("BP_Verify"), TEXT("Health")));
			Graph.Nodes.Add(Node(TEXT("N2"), EN2CNodeType::VariableGet, TEXT("BP_Verify"), TEXT("bIsDead")));
			Graph.Nodes.Add(Node(TEXT("N3"), EN2CNodeType::CallFunction, TEXT("KismetSystemLibrary"), TEXT("PrintString")));

			// MaxHealth is another identifier: Health is still missing; is_dead is bIsDead
			const FN2CVerifyReport Code = FN2CCaptureCoverageVerifier().Verify(OneGraph(TEXT("Check"),
				TEXT("def check(self):\n    if self.is_dead:\n        print_string(self.max_health)\n")), Blueprint, EN2CCodeLanguage::Python);
			if (TestEqual(TEXT("one diagnostic in code"), Code.Diagnostics.Num(), 1))
			{
				TestTrue(TEXT("Health is missing: ") + Code.Diagnostics[0].Message,
					Code.Diagnostics[0].Message.Contains(TEXT("BP_Verify.Health ")));
			}

			const FN2CVerifyReport Prose = FN2CCaptureCoverageVerifier().Verify(OneGraph(TEXT("Check"),
				TEXT("If Is Dead is true, Print String with the current Health.")), Blueprint, EN2CCodeLanguage::Pseudocode);
			TestEqual(TEXT("pseudocode covers every node in words"), Prose.Diagnostics.Num(), 0);
		});

		It(TEXT("expects nothing of a graph the capture does not have"), [this]()
		{
			const FN2CVerifyReport Report = FN2CCaptureCoverageVerifier().Verify(OneGraph(TEXT("S_Item"), TEXT("struct FS_Item {};")),
				Capture(), EN2CCodeLanguage::Cpp);
			TestEqual(TEXT("no diagnostic"), Report.Diagnostics.Num(), 0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
