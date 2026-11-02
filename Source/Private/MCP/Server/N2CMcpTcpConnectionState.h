// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"

/**
 * Whether the client of a held MCP request is still connected, read from the operating system's TCP table (card 0512).
 *
 * A long-running tools/call that cannot stream (UE 5.6 and 5.7 always; UE 5.8 when the client's Accept has no
 * text/event-stream) holds its HTTP request open until the call ends. The engine's HTTPServer does not read the socket
 * while a handler is pending, so a client that goes away (Claude Code's Esc or Ctrl+C closes the connection; it sends
 * no notifications/cancelled) is never noticed and the translation runs on. The server side of that connection is then
 * in CLOSE_WAIT, or gone after a reset. FN2CMcpProtocol polls Query at most every 2 seconds, only while such a call is
 * held, and cancels the call when the answer is Gone.
 *
 * Windows only (decision 0001: 2.0-beta is Win64): GetExtendedTcpTable from iphlpapi. On other platforms IsSupported
 * is false, ReadTable fails and Query answers Unknown, so nothing is cancelled (a documented limitation).
 */
namespace N2CMcpTcpConnectionState
{
	/** MIB_TCP_STATE values (tcpmib.h), kept here so the matcher builds and is tested on every platform */
	namespace States
	{
		inline constexpr int32 Closed = 1;
		inline constexpr int32 Listen = 2;
		inline constexpr int32 SynSent = 3;
		inline constexpr int32 SynReceived = 4;
		inline constexpr int32 Established = 5;
		inline constexpr int32 FinWait1 = 6;
		inline constexpr int32 FinWait2 = 7;
		inline constexpr int32 CloseWait = 8;
		inline constexpr int32 Closing = 9;
		inline constexpr int32 LastAck = 10;
		inline constexpr int32 TimeWait = 11;
		inline constexpr int32 DeleteTcb = 12;
	}

	/** One row of the TCP table (IPv4 and IPv6 rows alike): ports in host order */
	struct FTcpRow
	{
		int32 LocalPort = 0;
		int32 RemotePort = 0;
		int32 State = 0;
		uint32 OwningPid = 0;
	};

	enum class EClientState : uint8
	{
		/** The connection is established: the client is there */
		Open,
		/** The client closed or reset the connection */
		Gone,
		/** The table could not be read, or does not show this server: never a reason to cancel */
		Unknown,
	};

	/** True where ReadTable can read the TCP table (Windows) */
	bool IsSupported();

	/**
	 * The state of the server side of the connection from PeerPort to LocalPort, from Rows (the matcher; pure).
	 * Only rows of OwningPid count (0: any process). A row with LocalPort and RemotePort == PeerPort decides: Open
	 * while it is being set up or established, Gone once the client closed it (CLOSE_WAIT, CLOSING, LAST_ACK, CLOSED,
	 * TIME_WAIT, DELETE_TCB). With no such row the connection is gone (a reset removes it), but only when the table
	 * holds this server's LISTEN row on LocalPort; otherwise the table is not this server's and the answer is Unknown.
	 */
	EClientState Classify(const TArray<FTcpRow>& Rows, int32 LocalPort, int32 PeerPort, uint32 OwningPid);

	/** The TCP table of this machine, IPv4 and IPv6 (Windows); false with OutError elsewhere or on failure */
	bool ReadTable(TArray<FTcpRow>& OutRows, FString& OutError);

	/** Classify over ReadTable for this process; a failed read is logged at Verbose and answers Unknown */
	EClientState Query(int32 LocalPort, int32 PeerPort);
}
