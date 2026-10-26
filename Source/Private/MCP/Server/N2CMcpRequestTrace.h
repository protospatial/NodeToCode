// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MCP/Server/N2CMcpProtocol.h"

class FJsonValue;

/**
 * Writes one Info line per MCP request (card 0193), when its answer is written:
 *
 *   MCP request: method=tools/call tool=get-focused-blueprint status=200 isError=false duration_ms=12.3 traceparent=00-...
 *
 * It wraps the transport's responder, so the duration runs from Handle to the reply, the end of a stream or, for a
 * long-running tools/call, the answer written after Handle returned. isError is true for a transport error (status 400
 * and up), a JSON-RPC error response, or a tools/call result with isError. traceparent is params._meta.traceparent
 * (W3C Trace Context) when the client sent one in that shape; it is logged only, never answered or forwarded. The MCP
 * messages themselves are not changed.
 */
class FN2CMcpTracingResponder : public IN2CMcpResponder
{
public:
	FN2CMcpTracingResponder(const FN2CMcpInbound& Inbound, const TSharedRef<IN2CMcpResponder>& InInner);
	virtual ~FN2CMcpTracingResponder() override;

	/** Takes the method, tool name and traceparent from the parsed body (null when it is not valid JSON) */
	void Describe(const TSharedPtr<FJsonValue>& Message);

	virtual bool CanStream() const override;
	virtual void Reply(const FN2CMcpReply& InReply) override;
	virtual void Stream(const FString& MessageJson, const FString& SessionId) override;
	virtual void Close() override;

	/** The line written for this request so far: what Reply or Close logs (for the specs) */
	FString FormatLine(int32 Status, bool bIsError) const;

	/**
	 * True when Body, a JSON-RPC response as FJsonRpcUtils::SerializeResponse writes it (jsonrpc, id, then result or
	 * error), is an error response or a CallToolResult with isError (FMcpToolCallResult::ToJson writes it last). It reads
	 * the start and the end of the body only, so a multi-megabyte result costs nothing to classify.
	 */
	static bool IsErrorResponse(const FString& Body);

	/** Value when it has the W3C traceparent shape (version-traceid-parentid-flags, lowercase hex), else empty */
	static FString ValidTraceparent(const FString& Value);

private:
	void Finish(int32 Status, bool bIsError);

	TSharedRef<IN2CMcpResponder> Inner;
	double StartSeconds = 0.0;
	FString Method;
	FString ToolName;
	FString Traceparent;
	bool bLogged = false;
	bool bLastStreamedIsError = false;
};
