// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MCP/Server/N2CMcpProtocol.h"

namespace N2CTest
{
	/**
	 * An in-memory IN2CMcpResponder: records what the protocol core answers, so specs drive FN2CMcpProtocol::Handle
	 * without a listener or a socket. bCanStream says whether it claims it can stream (any engine; the HTTP transport
	 * streams on UE 5.8 only).
	 */
	class FN2CMcpMemoryResponder : public IN2CMcpResponder
	{
	public:
		explicit FN2CMcpMemoryResponder(bool bInCanStream = false) : bCanStream(bInCanStream) {}

		virtual bool CanStream() const override { return bCanStream; }

		virtual void Reply(const FN2CMcpReply& InReply) override
		{
			++ReplyCount;
			Replies.Add(InReply);
		}

		virtual void Stream(const FString& MessageJson, const FString& SessionId) override
		{
			Streamed.Add(MessageJson);
			StreamSessionId = SessionId;
		}

		virtual void Close() override { ++CloseCount; }

		virtual void StreamKeepAlive(const FString& SessionId) override { ++KeepAliveCount; }

		/** A spec sets bClosedByClient to act as a client that closed its stream */
		virtual bool IsClosedByClient() const override { return bClosedByClient; }

		/** Something was answered: a reply, or a stream that was closed */
		bool IsAnswered() const { return ReplyCount > 0 || CloseCount > 0; }

		/** The last reply (an empty one when there is none) */
		FN2CMcpReply LastReply() const { return Replies.Num() > 0 ? Replies.Last() : FN2CMcpReply(); }

		bool bCanStream = false;
		int32 ReplyCount = 0;
		int32 CloseCount = 0;
		TArray<FN2CMcpReply> Replies;
		TArray<FString> Streamed;
		FString StreamSessionId;
		int32 KeepAliveCount = 0;
		bool bClosedByClient = false;
	};

	/** An inbound POST with a JSON body and the given Accept header (and Mcp-Session-Id when not empty) */
	inline FN2CMcpInbound MakeMcpInbound(const FString& Body, const FString& Accept = TEXT("application/json, text/event-stream"),
		const FString& SessionId = FString())
	{
		FN2CMcpInbound Inbound;
		Inbound.Verb = TEXT("POST");
		Inbound.Headers.Add(TEXT("Content-Type"), { TEXT("application/json") });
		Inbound.Headers.Add(TEXT("Accept"), { Accept });
		if (!SessionId.IsEmpty())
		{
			Inbound.Headers.Add(TEXT("Mcp-Session-Id"), { SessionId });
		}
		const FTCHARToUTF8 Utf8(*Body);
		Inbound.Body.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return Inbound;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
