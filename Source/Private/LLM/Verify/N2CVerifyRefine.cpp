// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "LLM/Verify/N2CVerifyRefine.h"

#include "Core/N2CSettings.h"
#include "Dom/JsonObject.h"
#include "LLM/Verify/N2CCodeVerifiers.h"
#include "LLM/Verify/N2CCppScratchModuleVerifier.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utils/N2CLogger.h"

#define LOCTEXT_NAMESPACE "N2CVerifyRefine"

namespace N2CVerifyRefinePrivate
{
	static FString SeverityName(EN2CDiagnosticSeverity Severity)
	{
		return Severity == EN2CDiagnosticSeverity::Error ? TEXT("error") : TEXT("warning");
	}

	static FString StatusName(EN2CVerifyStatus Status)
	{
		switch (Status)
		{
		case EN2CVerifyStatus::Passed: return TEXT("passed");
		case EN2CVerifyStatus::Failed: return TEXT("failed");
		case EN2CVerifyStatus::Skipped: return TEXT("skipped");
		default: return TEXT("off");
		}
	}

	static FString Write(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Object, Writer);
		return Text;
	}

	static FString DescribeDiagnostic(const FN2CVerifyDiagnostic& Diagnostic)
	{
		FString Where = Diagnostic.Graph;
		if (!Diagnostic.File.IsEmpty())
		{
			Where += (Where.IsEmpty() ? TEXT("") : TEXT(" ")) + Diagnostic.File;
		}
		if (Diagnostic.Line > 0)
		{
			Where += FString::Printf(TEXT(" line %d"), Diagnostic.Line);
			if (Diagnostic.Column > 0)
			{
				Where += FString::Printf(TEXT(" column %d"), Diagnostic.Column);
			}
		}
		return FString::Printf(TEXT("- [%s] %s%s%s: %s"), *SeverityName(Diagnostic.Severity), *Diagnostic.Code,
			Where.IsEmpty() ? TEXT("") : TEXT(" at "), *Where, *Diagnostic.Message);
	}

	static void AddUsage(FN2CTranslationUsage& Total, const FN2CTranslationUsage& Turn)
	{
		Total.InputTokens += Turn.InputTokens;
		Total.OutputTokens += Turn.OutputTokens;
		Total.CacheReadInputTokens += Turn.CacheReadInputTokens;
		Total.CacheWriteInputTokens += Turn.CacheWriteInputTokens;
		Total.ReasoningTokens += Turn.ReasoningTokens;
	}
}

bool FN2CVerifyAttempt::AnyRan() const
{
	return Reports.ContainsByPredicate([](const FN2CVerifyReport& Report) { return Report.bRan; });
}

bool FN2CVerifyAttempt::Passed() const
{
	return !Reports.ContainsByPredicate([](const FN2CVerifyReport& Report) { return Report.HasErrors(); });
}

int32 FN2CVerifyAttempt::CountSeverity(EN2CDiagnosticSeverity Severity) const
{
	int32 Count = 0;
	for (const FN2CVerifyReport& Report : Reports)
	{
		for (const FN2CVerifyDiagnostic& Diagnostic : Report.Diagnostics)
		{
			Count += Diagnostic.Severity == Severity ? 1 : 0;
		}
	}
	return Count;
}

FString FN2CVerifyOutcome::GetStatusString() const
{
	using namespace N2CVerifyRefinePrivate;
	return Status == EN2CVerifyStatus::Failed ? FString::Printf(TEXT("failed-after-%d"), RefineTurns) : StatusName(Status);
}

FText FN2CVerifyOutcome::GetStatusText() const
{
	const int32 Warnings = Attempts.Num() > 0 ? Attempts.Last().CountSeverity(EN2CDiagnosticSeverity::Warning) : 0;
	const int32 Errors = Attempts.Num() > 0 ? Attempts.Last().CountSeverity(EN2CDiagnosticSeverity::Error) : 0;
	const FText WarningText = Warnings > 0
		? FText::Format(LOCTEXT("Warnings", " ({0} {0}|plural(one=warning,other=warnings))"), Warnings) : FText::GetEmpty();
	switch (Status)
	{
	case EN2CVerifyStatus::Passed:
		return RefineTurns == 0
			? FText::Format(LOCTEXT("Passed", "Verified: passed{0}"), WarningText)
			: FText::Format(LOCTEXT("PassedAfter", "Verified: passed after {0} refine {0}|plural(one=turn,other=turns){1}"), RefineTurns, WarningText);
	case EN2CVerifyStatus::Failed:
		return FText::Format(LOCTEXT("Failed", "Verified: {0} {0}|plural(one=error,other=errors) left after {1} refine {1}|plural(one=turn,other=turns){2}"),
			Errors, RefineTurns, WarningText);
	case EN2CVerifyStatus::Skipped:
		return LOCTEXT("Skipped", "Verified: skipped (no verifier could check this code)");
	default:
		return FText::GetEmpty();
	}
}

FString FN2CVerifyOutcome::ToJsonString() const
{
	using namespace N2CVerifyRefinePrivate;
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), GetStatusString());
	Root->SetNumberField(TEXT("refine_turns"), RefineTurns);
	Root->SetNumberField(TEXT("max_refine_turns"), MaxRefineTurns);
	Root->SetStringField(TEXT("language"), UEnum::GetValueAsString(Language));
	if (!Error.IsEmpty())
	{
		Root->SetStringField(TEXT("error"), Error);
	}
	TArray<TSharedPtr<FJsonValue>> AttemptValues;
	for (const FN2CVerifyAttempt& Attempt : Attempts)
	{
		const TSharedRef<FJsonObject> AttemptObject = MakeShared<FJsonObject>();
		AttemptObject->SetNumberField(TEXT("turn"), Attempt.Turn);
		AttemptObject->SetBoolField(TEXT("passed"), Attempt.Passed());
		AttemptObject->SetNumberField(TEXT("errors"), Attempt.CountSeverity(EN2CDiagnosticSeverity::Error));
		AttemptObject->SetNumberField(TEXT("warnings"), Attempt.CountSeverity(EN2CDiagnosticSeverity::Warning));
		TArray<TSharedPtr<FJsonValue>> ReportValues;
		for (const FN2CVerifyReport& Report : Attempt.Reports)
		{
			const TSharedRef<FJsonObject> ReportObject = MakeShared<FJsonObject>();
			ReportObject->SetStringField(TEXT("verifier"), Report.Verifier);
			ReportObject->SetBoolField(TEXT("ran"), Report.bRan);
			TArray<TSharedPtr<FJsonValue>> DiagnosticValues;
			for (const FN2CVerifyDiagnostic& Diagnostic : Report.Diagnostics)
			{
				const TSharedRef<FJsonObject> DiagnosticObject = MakeShared<FJsonObject>();
				DiagnosticObject->SetStringField(TEXT("graph"), Diagnostic.Graph);
				DiagnosticObject->SetStringField(TEXT("file"), Diagnostic.File);
				DiagnosticObject->SetNumberField(TEXT("line"), Diagnostic.Line);
				DiagnosticObject->SetNumberField(TEXT("column"), Diagnostic.Column);
				DiagnosticObject->SetStringField(TEXT("severity"), SeverityName(Diagnostic.Severity));
				DiagnosticObject->SetStringField(TEXT("code"), Diagnostic.Code);
				DiagnosticObject->SetStringField(TEXT("message"), Diagnostic.Message);
				DiagnosticValues.Add(MakeShared<FJsonValueObject>(DiagnosticObject));
			}
			ReportObject->SetArrayField(TEXT("diagnostics"), DiagnosticValues);
			ReportValues.Add(MakeShared<FJsonValueObject>(ReportObject));
		}
		AttemptObject->SetArrayField(TEXT("reports"), ReportValues);
		AttemptValues.Add(MakeShared<FJsonValueObject>(AttemptObject));
	}
	Root->SetArrayField(TEXT("attempts"), AttemptValues);
	return Write(Root);
}

bool FN2CVerifyOutcome::FromJsonString(const FString& Text, FN2CVerifyOutcome& OutOutcome)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	FString Status;
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid() || !Root->TryGetStringField(TEXT("status"), Status))
	{
		return false;
	}
	OutOutcome = FN2CVerifyOutcome();
	if (Status == TEXT("passed"))
	{
		OutOutcome.Status = EN2CVerifyStatus::Passed;
	}
	else if (Status.StartsWith(TEXT("failed")))
	{
		OutOutcome.Status = EN2CVerifyStatus::Failed;
	}
	else if (Status == TEXT("skipped"))
	{
		OutOutcome.Status = EN2CVerifyStatus::Skipped;
	}
	else if (Status != TEXT("off"))
	{
		return false;
	}
	Root->TryGetNumberField(TEXT("refine_turns"), OutOutcome.RefineTurns);
	Root->TryGetNumberField(TEXT("max_refine_turns"), OutOutcome.MaxRefineTurns);
	Root->TryGetStringField(TEXT("error"), OutOutcome.Error);

	// The attempts with their reports and diagnostics (what the viewer counts)
	const TArray<TSharedPtr<FJsonValue>>* AttemptValues = nullptr;
	if (Root->TryGetArrayField(TEXT("attempts"), AttemptValues))
	{
		for (const TSharedPtr<FJsonValue>& AttemptValue : *AttemptValues)
		{
			const TSharedPtr<FJsonObject> AttemptObject = AttemptValue.IsValid() ? AttemptValue->AsObject() : nullptr;
			if (!AttemptObject.IsValid())
			{
				continue;
			}
			FN2CVerifyAttempt& Attempt = OutOutcome.Attempts.AddDefaulted_GetRef();
			AttemptObject->TryGetNumberField(TEXT("turn"), Attempt.Turn);
			const TArray<TSharedPtr<FJsonValue>>* ReportValues = nullptr;
			if (!AttemptObject->TryGetArrayField(TEXT("reports"), ReportValues))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& ReportValue : *ReportValues)
			{
				const TSharedPtr<FJsonObject> ReportObject = ReportValue.IsValid() ? ReportValue->AsObject() : nullptr;
				if (!ReportObject.IsValid())
				{
					continue;
				}
				FN2CVerifyReport& Report = Attempt.Reports.AddDefaulted_GetRef();
				ReportObject->TryGetStringField(TEXT("verifier"), Report.Verifier);
				ReportObject->TryGetBoolField(TEXT("ran"), Report.bRan);
				const TArray<TSharedPtr<FJsonValue>>* DiagnosticValues = nullptr;
				if (!ReportObject->TryGetArrayField(TEXT("diagnostics"), DiagnosticValues))
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& DiagnosticValue : *DiagnosticValues)
				{
					const TSharedPtr<FJsonObject> DiagnosticObject = DiagnosticValue.IsValid() ? DiagnosticValue->AsObject() : nullptr;
					if (!DiagnosticObject.IsValid())
					{
						continue;
					}
					FN2CVerifyDiagnostic& Diagnostic = Report.Diagnostics.AddDefaulted_GetRef();
					DiagnosticObject->TryGetStringField(TEXT("graph"), Diagnostic.Graph);
					DiagnosticObject->TryGetStringField(TEXT("file"), Diagnostic.File);
					DiagnosticObject->TryGetNumberField(TEXT("line"), Diagnostic.Line);
					DiagnosticObject->TryGetNumberField(TEXT("column"), Diagnostic.Column);
					DiagnosticObject->TryGetStringField(TEXT("code"), Diagnostic.Code);
					DiagnosticObject->TryGetStringField(TEXT("message"), Diagnostic.Message);
					FString Severity;
					DiagnosticObject->TryGetStringField(TEXT("severity"), Severity);
					Diagnostic.Severity = Severity == TEXT("error") ? EN2CDiagnosticSeverity::Error : EN2CDiagnosticSeverity::Warning;
				}
			}
		}
	}
	return true;
}

bool N2CVerifyRefine::IsEnabledFor(EN2CCodeLanguage Language)
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return Settings && Settings->VerifyLanguages.Contains(Language);
}

int32 N2CVerifyRefine::GetMaxRefineTurns()
{
	const UN2CSettings* Settings = GetDefault<UN2CSettings>();
	return FMath::Clamp(Settings ? Settings->MaxRefineTurns : 2, 0, 5);
}

FN2CVerifyAttempt N2CVerifyRefine::VerifyResponse(const FN2CTranslationResponse& Response, const FN2CBlueprint& Capture,
	EN2CCodeLanguage Language, int32 Turn)
{
	FN2CVerifyAttempt Attempt;
	Attempt.Turn = Turn;
	const FN2CPythonSyntaxVerifier PythonSyntax;
	const FN2CCaptureCoverageVerifier Coverage;
	const FN2CCppScratchModuleVerifier CppCompile;
	const IN2CCodeVerifier* Verifiers[] = { &PythonSyntax, &Coverage, &CppCompile };
	for (const IN2CCodeVerifier* Verifier : Verifiers)
	{
		if (Verifier->SupportsLanguage(Language))
		{
			Attempt.Reports.Add(Verifier->Verify(Response, Capture, Language));
		}
	}
	return Attempt;
}

FString N2CVerifyRefine::ResponseToContractJson(const FN2CTranslationResponse& Response)
{
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Graphs;
	for (const FN2CGraphTranslation& Graph : Response.Graphs)
	{
		const TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
		GraphObject->SetStringField(TEXT("graph_name"), Graph.GraphName);
		GraphObject->SetStringField(TEXT("graph_type"), Graph.GraphType);
		GraphObject->SetStringField(TEXT("graph_class"), Graph.GraphClass);
		const TSharedRef<FJsonObject> Code = MakeShared<FJsonObject>();
		Code->SetStringField(TEXT("graphDeclaration"), Graph.Code.GraphDeclaration);
		Code->SetStringField(TEXT("graphImplementation"), Graph.Code.GraphImplementation);
		Code->SetStringField(TEXT("implementationNotes"), Graph.Code.ImplementationNotes);
		GraphObject->SetObjectField(TEXT("code"), Code);
		Graphs.Add(MakeShared<FJsonValueObject>(GraphObject));
	}
	Root->SetArrayField(TEXT("graphs"), Graphs);
	return N2CVerifyRefinePrivate::Write(Root);
}

FString N2CVerifyRefine::BuildRefineTurn(const FN2CTranslationResponse& Previous, const FN2CVerifyAttempt& Attempt,
	int32 TurnNumber, int32 MaxTurns)
{
	using namespace N2CVerifyRefinePrivate;
	TArray<FString> Lines;
	for (const FN2CVerifyReport& Report : Attempt.Reports)
	{
		for (const FN2CVerifyDiagnostic& Diagnostic : Report.Diagnostics)
		{
			if (Diagnostic.Code != N2CVerify::VerifierUnavailable)
			{
				Lines.Add(DescribeDiagnostic(Diagnostic));
			}
		}
	}
	return FString::Printf(TEXT(
		"<previous_response>\n%s\n</previous_response>\n\n"
		"<verify_diagnostics>\n%s\n</verify_diagnostics>\n\n"
		"<refine_instructions>\n"
		"This is refine turn %d of %d. Your previous response to the N2C JSON above (in <previous_response>) failed "
		"verification: NodeToCode checked the generated code and found the problems listed in <verify_diagnostics> "
		"(each names the graph, \"declaration\" or \"implementation\", and the line and column in that code). Fix every "
		"error, and the warnings where they apply, without changing what the code does. Respond with the complete "
		"corrected response in exactly the same JSON shape as before ({\"graphs\": [...]} with every graph, struct and "
		"enum, not only the changed ones) and nothing else.\n"
		"</refine_instructions>"),
		*ResponseToContractJson(Previous), *FString::Join(Lines, TEXT("\n")), TurnNumber, MaxTurns);
}

FN2CVerifyRefineLoop::FN2CVerifyRefineLoop(TSharedPtr<const FN2CBlueprint> InCapture, EN2CCodeLanguage InLanguage, bool bInVerify,
	int32 InMaxTurns)
	: Capture(MoveTemp(InCapture))
	, Language(InLanguage)
	, bVerify(bInVerify && Capture.IsValid())
	, MaxTurns(FMath::Max(0, InMaxTurns))
{
	Outcome.Language = Language;
	Outcome.MaxRefineTurns = bVerify ? MaxTurns : 0;
}

void FN2CVerifyRefineLoop::Start(FSendTurn InSend, FOnDone InOnDone, FOnRefine InOnRefine)
{
	check(IsInGameThread());
	Send = MoveTemp(InSend);
	OnDone = MoveTemp(InOnDone);
	OnRefine = MoveTemp(InOnRefine);
	SendTurn(FString());
}

void FN2CVerifyRefineLoop::SendTurn(const FString& RefineTurn)
{
	// The result may arrive inside Send (an error found before sending): the handle is kept only while in flight
	TWeakPtr<FN2CVerifyRefineLoop> WeakLoop = AsShared();
	const int32 TurnSerial = Outcome.Attempts.Num();
	const FOnN2CTranslationParsed OnParsed = FOnN2CTranslationParsed::CreateLambda(
		[WeakLoop, TurnSerial](const FN2CTranslationResponse& Response, bool bSuccess)
		{
			const TSharedPtr<FN2CVerifyRefineLoop> This = WeakLoop.Pin();
			// Only the answer to the turn this loop is waiting for
			if (This.IsValid() && !This->bFinished && !This->bCancelled && This->Outcome.Attempts.Num() == TurnSerial)
			{
				This->HandleParsed(Response, bSuccess);
			}
		});
	FN2CLLMRequestHandleRef Request = Send(RefineTurn, OnParsed);
	if (!bFinished && !bCancelled && Outcome.Attempts.Num() == TurnSerial && !Request->IsFinished())
	{
		Handle = Request;
	}
}

void FN2CVerifyRefineLoop::HandleParsed(const FN2CTranslationResponse& Response, bool bSuccess)
{
	using namespace N2CVerifyRefinePrivate;
	Handle.Reset();
	if (bSuccess)
	{
		AddUsage(Usage, Response.Usage);
	}

	if (!bSuccess)
	{
		if (!bHasGood)
		{
			// The first request failed: that failure is the result
			Finish(Response, false);
			return;
		}
		// A refine turn failed: the last good answer stays, with what was found in it
		Outcome.Status = EN2CVerifyStatus::Failed;
		Outcome.Error = Response.Error.IsError() ? Response.Error.ToDisplayString() : FString(TEXT("The refine turn failed"));
		FN2CLogger::Get().LogWarning(FString::Printf(TEXT("Verify and refine: refine turn %d failed: %s"), Outcome.RefineTurns,
			*Outcome.Error), TEXT("VerifyRefine"));
		Finish(LastGood, true);
		return;
	}

	LastGood = Response;
	bHasGood = true;
	if (!bVerify)
	{
		Outcome.Status = EN2CVerifyStatus::Off;
		Finish(LastGood, true);
		return;
	}

	FN2CVerifyAttempt Attempt = N2CVerifyRefine::VerifyResponse(Response, *Capture, Language, Outcome.Attempts.Num());
	const bool bRan = Attempt.AnyRan();
	const bool bPassed = Attempt.Passed();
	FN2CLogger::Get().Log(FString::Printf(TEXT("Verify and refine: turn %d %s (%d errors, %d warnings)"), Attempt.Turn,
		!bRan ? TEXT("not checked") : bPassed ? TEXT("passed") : TEXT("failed"),
		Attempt.CountSeverity(EN2CDiagnosticSeverity::Error), Attempt.CountSeverity(EN2CDiagnosticSeverity::Warning)),
		EN2CLogSeverity::Info, TEXT("VerifyRefine"));
	Outcome.Attempts.Add(Attempt);

	if (!bRan)
	{
		Outcome.Status = EN2CVerifyStatus::Skipped;
		Finish(LastGood, true);
		return;
	}
	if (bPassed)
	{
		Outcome.Status = EN2CVerifyStatus::Passed;
		Finish(LastGood, true);
		return;
	}
	if (Outcome.RefineTurns >= MaxTurns)
	{
		Outcome.Status = EN2CVerifyStatus::Failed;
		Finish(LastGood, true);
		return;
	}

	++Outcome.RefineTurns;
	if (OnRefine)
	{
		OnRefine(Outcome.RefineTurns, Outcome.Attempts.Last());
		if (bCancelled || bFinished)
		{
			return;
		}
	}
	SendTurn(N2CVerifyRefine::BuildRefineTurn(LastGood, Outcome.Attempts.Last(), Outcome.RefineTurns, MaxTurns));
}

void FN2CVerifyRefineLoop::Finish(const FN2CTranslationResponse& Response, bool bSuccess)
{
	bFinished = true;
	Handle.Reset();
	FN2CTranslationResponse Final = Response;
	if (bSuccess)
	{
		Final.Usage = Usage;
	}
	// The caller may drop the loop inside OnDone
	const TSharedRef<FN2CVerifyRefineLoop> Hold = AsShared();
	if (OnDone)
	{
		OnDone(Final, bSuccess, Outcome);
	}
}

bool FN2CVerifyRefineLoop::Cancel()
{
	if (bFinished || bCancelled)
	{
		return false;
	}
	if (Handle.IsValid() && !Handle->Cancel())
	{
		return false;
	}
	Handle.Reset();
	bCancelled = true;
	return true;
}

#undef LOCTEXT_NAMESPACE
