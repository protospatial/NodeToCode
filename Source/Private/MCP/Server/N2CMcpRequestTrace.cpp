// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "MCP/Server/N2CMcpRequestTrace.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "MCP/Tools/N2CMcpToolManager.h"
#include "Utils/N2CLogger.h"

namespace N2CMcpRequestTracePrivate
{
	/** A client-sent value safe for a log line: at most 64 characters, printable ASCII, no spaces */
	FString ForLog(const FString& Value)
	{
		FString Safe;
		for (const TCHAR Ch : Value.Left(64))
		{
			Safe.AppendChar(Ch > 0x20 && Ch <= 0x7E ? Ch : TEXT('?'));
		}
		return Safe;
	}

	bool IsLowerHex(const FString& Text, int32 Start, int32 Count)
	{
		for (int32 Index = Start; Index < Start + Count; ++Index)
		{
			const TCHAR Ch = Text[Index];
			if (!((Ch >= TEXT('0') && Ch <= TEXT('9')) || (Ch >= TEXT('a') && Ch <= TEXT('f'))))
			{
				return false;
			}
		}
		return true;
	}
}

FN2CMcpTracingResponder::FN2CMcpTracingResponder(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& InInner)
	: Inner(InInner)
	, StartSeconds(FPlatformTime::Seconds())
	, Method(Inbound.Verb.Equals(TEXT("POST"), ESearchCase::IgnoreCase) ? FString(TEXT("(unread)")) : N2CMcpRequestTracePrivate::ForLog(Inbound.Verb.ToUpper()))
{
}

FN2CMcpTracingResponder::~FN2CMcpTracingResponder()
{
	// The core answers every request; a responder dropped unanswered (an editor shutting down mid-call) still gets its line
	if (!bLogged)
	{
		Finish(0, true);
	}
}

void FN2CMcpTracingResponder::Describe(const TSharedPtr<FJsonValue>& Message)
{
	using namespace N2CMcpRequestTracePrivate;

	if (!Message.IsValid())
	{
		Method = TEXT("(invalid-json)");
		return;
	}
	if (Message->Type == EJson::Array)
	{
		Method = FString::Printf(TEXT("batch[%d]"), Message->AsArray().Num());
		return;
	}
	const TSharedPtr<FJsonObject> Object = Message->Type == EJson::Object ? Message->AsObject() : nullptr;
	FString MethodName;
	if (!Object.IsValid() || !Object->TryGetStringField(TEXT("method"), MethodName))
	{
		// A client's JSON-RPC response (or not a JSON-RPC message at all)
		Method = Object.IsValid() && (Object->HasField(TEXT("result")) || Object->HasField(TEXT("error"))) ? TEXT("(response)") : TEXT("(no-method)");
		return;
	}
	Method = ForLog(MethodName);

	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (!Object->TryGetObjectField(TEXT("params"), Params) || !Params || !Params->IsValid())
	{
		return;
	}
	FString Name;
	if (MethodName == TEXT("tools/call") && (*Params)->TryGetStringField(TEXT("name"), Name))
	{
		ToolName = ForLog(Name);
		// call_tool runs another tool: name both
		const TSharedPtr<FJsonObject>* Arguments = nullptr;
		FString InnerTool;
		if (Name == FN2CMcpToolManager::CallToolName && (*Params)->TryGetObjectField(TEXT("arguments"), Arguments)
			&& Arguments && Arguments->IsValid() && (*Arguments)->TryGetStringField(TEXT("tool_name"), InnerTool))
		{
			ToolName += TEXT(":") + ForLog(InnerTool);
		}
	}
	const TSharedPtr<FJsonObject>* Meta = nullptr;
	FString Trace;
	if ((*Params)->TryGetObjectField(TEXT("_meta"), Meta) && Meta && Meta->IsValid()
		&& (*Meta)->TryGetStringField(TEXT("traceparent"), Trace))
	{
		Traceparent = ValidTraceparent(Trace);
		if (Traceparent.IsEmpty())
		{
			Traceparent = TEXT("(invalid)");
		}
	}
}

bool FN2CMcpTracingResponder::CanStream() const
{
	return Inner->CanStream();
}

void FN2CMcpTracingResponder::Reply(const FN2CMcpReply& InReply)
{
	Inner->Reply(InReply);
	Finish(InReply.Status, InReply.Status >= 400 || IsErrorResponse(InReply.Body));
}

void FN2CMcpTracingResponder::Stream(const FString& MessageJson, const FString& SessionId)
{
	// The last message written before Close is the response (or a notification, when the call was cancelled)
	bLastStreamedIsError = IsErrorResponse(MessageJson);
	Inner->Stream(MessageJson, SessionId);
}

void FN2CMcpTracingResponder::Close()
{
	Inner->Close();
	Finish(200, bLastStreamedIsError);
}

FString FN2CMcpTracingResponder::FormatLine(int32 Status, bool bIsError) const
{
	FString Line = FString::Printf(TEXT("MCP request: method=%s"), *Method);
	if (!ToolName.IsEmpty())
	{
		Line += FString::Printf(TEXT(" tool=%s"), *ToolName);
	}
	Line += FString::Printf(TEXT(" status=%s isError=%s duration_ms=%.1f"),
		Status > 0 ? *FString::FromInt(Status) : TEXT("unanswered"), bIsError ? TEXT("true") : TEXT("false"),
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	if (!Traceparent.IsEmpty())
	{
		Line += FString::Printf(TEXT(" traceparent=%s"), *Traceparent);
	}
	return Line;
}

void FN2CMcpTracingResponder::Finish(int32 Status, bool bIsError)
{
	if (bLogged)
	{
		return;
	}
	bLogged = true;
	FN2CLogger::Get().Log(FormatLine(Status, bIsError), EN2CLogSeverity::Info);
}

bool FN2CMcpTracingResponder::IsErrorResponse(const FString& Body)
{
	using namespace N2CMcpRequestTracePrivate;

	// A JSON-RPC error: "error" comes before any "result" (only jsonrpc and a short id precede either). The search for
	// "error" stops where "result" starts, so a large result is not scanned for it.
	static const FString ResultKey = TEXT("\"result\":");
	static const FString ErrorKey = TEXT("\"error\":");
	const int32 ResultAt = Body.Find(ResultKey, ESearchCase::CaseSensitive);
	const int32 ErrorAt = ResultAt == INDEX_NONE
		? Body.Find(ErrorKey, ESearchCase::CaseSensitive)
		: Body.Left(ResultAt).Find(ErrorKey, ESearchCase::CaseSensitive);
	if (ErrorAt != INDEX_NONE)
	{
		return true;
	}
	if (ResultAt == INDEX_NONE)
	{
		return false;
	}

	// A tool error: the CallToolResult ends with "isError":true, then closes the result and the response
	int32 End = Body.Len();
	for (int32 Braces = 0; Braces < 2; ++Braces)
	{
		while (End > 0 && FChar::IsWhitespace(Body[End - 1]))
		{
			--End;
		}
		if (End == 0 || Body[End - 1] != TEXT('}'))
		{
			return false;
		}
		--End;
	}
	static const FString IsErrorTrue = TEXT("\"isError\":true");
	while (End > 0 && FChar::IsWhitespace(Body[End - 1]))
	{
		--End;
	}
	return End >= IsErrorTrue.Len() && FCString::Strncmp(*Body + End - IsErrorTrue.Len(), *IsErrorTrue, IsErrorTrue.Len()) == 0;
}

FString FN2CMcpTracingResponder::ValidTraceparent(const FString& Value)
{
	using namespace N2CMcpRequestTracePrivate;

	// version(2)-trace-id(32)-parent-id(16)-flags(2): 55 characters, lowercase hex, dashes between
	const FString Trimmed = Value.TrimStartAndEnd();
	if (Trimmed.Len() != 55 || Trimmed[2] != TEXT('-') || Trimmed[35] != TEXT('-') || Trimmed[52] != TEXT('-'))
	{
		return FString();
	}
	if (!IsLowerHex(Trimmed, 0, 2) || !IsLowerHex(Trimmed, 3, 32) || !IsLowerHex(Trimmed, 36, 16) || !IsLowerHex(Trimmed, 53, 2))
	{
		return FString();
	}
	return Trimmed;
}
