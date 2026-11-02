// Copyright (c) 2025 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/Verify/N2CCodeVerifiers.h"

namespace N2CCaptureCoverageVerifierPrivate
{
	/** The node kinds whose member must appear in code: calls, variable reads and writes, events */
	bool IsCovered(const EN2CNodeType Type)
	{
		switch (Type)
		{
		case EN2CNodeType::CallFunction:
		case EN2CNodeType::CallArrayFunction:
		case EN2CNodeType::CallDataTableFunction:
		case EN2CNodeType::CallFunctionOnMember:
		case EN2CNodeType::CallParentFunction:
		case EN2CNodeType::VariableGet:
		case EN2CNodeType::VariableSet:
		case EN2CNodeType::LocalVariableGet:
		case EN2CNodeType::LocalVariableSet:
		case EN2CNodeType::Event:
		case EN2CNodeType::CustomEvent:
			return true;
		default:
			return false;
		}
	}

	FString KindOf(const EN2CNodeType Type)
	{
		switch (Type)
		{
		case EN2CNodeType::VariableGet:
		case EN2CNodeType::VariableSet:
		case EN2CNodeType::LocalVariableGet:
		case EN2CNodeType::LocalVariableSet:
			return TEXT("variable");
		case EN2CNodeType::Event:
		case EN2CNodeType::CustomEvent:
			return TEXT("event");
		default:
			return TEXT("function call");
		}
	}
}

FString FN2CCaptureCoverageVerifier::Normalize(const FString& Text)
{
	// Lowercase letters and digits only: print_string, PrintString and a variable named "Print String" compare equal
	FString Out;
	Out.Reserve(Text.Len());
	for (const TCHAR Char : Text)
	{
		if (FChar::IsAlnum(Char))
		{
			Out.AppendChar(FChar::ToLower(Char));
		}
	}
	return Out;
}

TSet<FString> FN2CCaptureCoverageVerifier::Identifiers(const FString& Code)
{
	// Each run of letters, digits and underscores is one identifier, normalized: self.get_actor_location() gives
	// "self" and "getactorlocation", UKismetSystemLibrary::PrintString gives "ukismetsystemlibrary" and "printstring"
	TSet<FString> Out;
	FString Current;
	for (const TCHAR Char : Code)
	{
		if (FChar::IsAlnum(Char) || Char == TEXT('_'))
		{
			Current.AppendChar(Char);
			continue;
		}
		if (!Current.IsEmpty())
		{
			Out.Add(Normalize(Current));
			Current.Reset();
		}
	}
	if (!Current.IsEmpty())
	{
		Out.Add(Normalize(Current));
	}
	return Out;
}

TArray<FString> FN2CCaptureCoverageVerifier::CandidateNames(const FString& MemberName)
{
	// The name as declared, and without the prefixes generated code drops: the engine's "K2_" (K2_GetActorLocation is
	// GetActorLocation in C++), an override event's "Receive" (ReceiveBeginPlay is BeginPlay) and a Boolean's "b"
	// (bIsDead is is_dead in Python)
	TArray<FString> Names;
	Names.Add(Normalize(MemberName));
	for (const TCHAR* Prefix : { TEXT("K2_"), TEXT("Receive") })
	{
		if (MemberName.StartsWith(Prefix, ESearchCase::CaseSensitive) && MemberName.Len() > FCString::Strlen(Prefix))
		{
			Names.AddUnique(Normalize(MemberName.RightChop(FCString::Strlen(Prefix))));
		}
	}
	if (MemberName.Len() > 1 && MemberName[0] == TEXT('b') && FChar::IsUpper(MemberName[1]))
	{
		Names.AddUnique(Normalize(MemberName.RightChop(1)));
	}
	Names.RemoveAll([](const FString& Name) { return Name.IsEmpty(); });
	return Names;
}

FN2CVerifyReport FN2CCaptureCoverageVerifier::Verify(const FN2CTranslationResponse& Translation, const FN2CBlueprint& Capture,
	EN2CCodeLanguage Language) const
{
	using namespace N2CCaptureCoverageVerifierPrivate;
	FN2CVerifyReport Report;
	Report.Verifier = GetName();
	Report.bRan = true;

	for (const FN2CGraphTranslation& Translated : Translation.Graphs)
	{
		const FN2CGraph* Graph = Capture.Graphs.FindByPredicate([&Translated](const FN2CGraph& Candidate)
		{
			return Candidate.Name == Translated.GraphName;
		});
		const FString RawCode = Translated.Code.GraphDeclaration + TEXT("\n") + Translated.Code.GraphImplementation;
		if (!Graph || RawCode.TrimStartAndEnd().IsEmpty())
		{
			// A struct or enum entry, a graph the capture does not have, or a graph with no code: nothing to expect
			continue;
		}

		// Code is compared identifier by identifier (MaxHealth does not cover Health); pseudocode is prose, so there a
		// name may also be written in words ("Print String")
		const bool bProse = Language == EN2CCodeLanguage::Pseudocode;
		const TSet<FString> CodeIdentifiers = bProse ? TSet<FString>() : Identifiers(RawCode);
		const FString Prose = bProse ? Normalize(RawCode) : FString();

		TSet<FString> Reported;
		for (const FN2CNodeDefinition& Node : Graph->Nodes)
		{
			// Blueprint never runs these, and the shared spec asks for them in the notes, not the code
			if (!IsCovered(Node.NodeType) || Node.MemberName.IsEmpty() || Node.MemberName == TEXT("None")
				|| Node.bUnused || Node.bUnreachable || Node.bDisabled)
			{
				continue;
			}
			const TArray<FString> Names = CandidateNames(Node.MemberName);
			const bool bCovered = Names.ContainsByPredicate([&](const FString& Name)
			{
				return bProse ? Prose.Contains(Name, ESearchCase::CaseSensitive) : CodeIdentifiers.Contains(Name);
			});
			if (bCovered)
			{
				continue;
			}
			// One finding per member: a function called by three nodes is one miss
			if (Reported.Contains(Node.MemberName))
			{
				continue;
			}
			Reported.Add(Node.MemberName);
			FN2CVerifyDiagnostic& Diagnostic = Report.Diagnostics.AddDefaulted_GetRef();
			Diagnostic.Graph = Translated.GraphName;
			Diagnostic.Severity = EN2CDiagnosticSeverity::Warning;
			Diagnostic.Code = N2CVerify::NodeNotCovered;
			Diagnostic.Message = FString::Printf(TEXT("%s: the %s %s%s%s (node %s) does not appear in the generated code"),
				*Translated.GraphName, *KindOf(Node.NodeType), *Node.MemberParent, Node.MemberParent.IsEmpty() ? TEXT("") : TEXT("."),
				*Node.MemberName, *Node.ID);
		}
	}
	return Report;
}
