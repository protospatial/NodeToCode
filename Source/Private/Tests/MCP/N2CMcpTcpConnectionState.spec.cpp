// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Server/N2CMcpTcpConnectionState.h"
#include "HAL/PlatformProcess.h"

/**
 * Card 0512: a long-running call held as one JSON reply is cancelled when its client goes away, which only the TCP
 * table shows. These cases test the matcher (Classify) over fake tables, and that reading this machine's table works
 * where it is supported.
 */
BEGIN_DEFINE_SPEC(FN2CMcpTcpConnectionStateSpec, "NodeToCode.MCP.TcpConnectionState", N2C_TEST_FLAGS)
END_DEFINE_SPEC(FN2CMcpTcpConnectionStateSpec)

void FN2CMcpTcpConnectionStateSpec::Define()
{
	using namespace N2CMcpTcpConnectionState;
	constexpr int32 Server = 27000;
	constexpr int32 Peer = 51234;
	constexpr uint32 Pid = 4242;

	auto Row = [](int32 Local, int32 Remote, int32 State, uint32 Owner = 4242 /* Pid */)
	{
		FTcpRow R;
		R.LocalPort = Local;
		R.RemotePort = Remote;
		R.State = State;
		R.OwningPid = Owner;
		return R;
	};
	auto S = [](EClientState State) { return static_cast<int32>(State); };
	const FTcpRow Listening = Row(Server, 0, States::Listen);
	// The client's own end of the same connection (a local client): its ports are the other way round
	const FTcpRow ClientEnd = Row(Peer, Server, States::Established, 999);

	It(TEXT("is Open while the server side of the connection is established"), [=, this]()
	{
		TestEqual(TEXT("established"), S(Classify({ Listening, Row(Server, Peer, States::Established), ClientEnd }, Server, Peer, Pid)), S(EClientState::Open));
	});

	It(TEXT("is Gone once the client closed it (CLOSE_WAIT) or the connection is closing or closed"), [=, this]()
	{
		for (const int32 State : { States::CloseWait, States::LastAck, States::Closing, States::Closed, States::TimeWait, States::DeleteTcb })
		{
			TestEqual(FString::Printf(TEXT("state %d"), State), S(Classify({ Listening, Row(Server, Peer, State) }, Server, Peer, Pid)), S(EClientState::Gone));
		}
	});

	It(TEXT("is Gone when the server still listens but the connection has left the table (a reset)"), [=, this]()
	{
		TestEqual(TEXT("no row"), S(Classify({ Listening, Row(Server, 60000, States::Established) }, Server, Peer, Pid)), S(EClientState::Gone));
	});

	It(TEXT("is Unknown when the table does not show this server listening, and for missing ports"), [=, this]()
	{
		TestEqual(TEXT("empty table"), S(Classify({}, Server, Peer, Pid)), S(EClientState::Unknown));
		TestEqual(TEXT("another process listens"), S(Classify({ Row(Server, 0, States::Listen, 7) }, Server, Peer, Pid)), S(EClientState::Unknown));
		TestEqual(TEXT("no peer port"), S(Classify({ Listening, Row(Server, Peer, States::CloseWait) }, Server, 0, Pid)), S(EClientState::Unknown));
		TestEqual(TEXT("no server port"), S(Classify({ Listening }, 0, Peer, Pid)), S(EClientState::Unknown));
	});

	It(TEXT("matches the server's port and the peer's port only, and only rows of this process"), [=, this]()
	{
		// The client's end (ports reversed) and another process's row with the same ports do not decide
		TestEqual(TEXT("only the client's end is left: the server side is gone"), S(Classify({ Listening, Row(Peer, Server, States::CloseWait) }, Server, Peer, Pid)), S(EClientState::Gone));
		TestEqual(TEXT("another process's row is ignored"),
			S(Classify({ Listening, Row(Server, Peer, States::CloseWait, 7), Row(Server, Peer, States::Established) }, Server, Peer, Pid)), S(EClientState::Open));
		TestEqual(TEXT("pid 0 takes every row"), S(Classify({ Row(Server, Peer, States::Established, 7) }, Server, Peer, 0)), S(EClientState::Open));
	});

	It(TEXT("is Open when a new connection reuses the peer port of an old one still in TIME_WAIT"), [=, this]()
	{
		TestEqual(TEXT("reuse"), S(Classify({ Listening, Row(Server, Peer, States::TimeWait), Row(Server, Peer, States::Established) }, Server, Peer, Pid)), S(EClientState::Open));
	});

	It(TEXT("reads this machine's TCP table where it is supported, and answers Unknown elsewhere"), [=, this]()
	{
		TArray<FTcpRow> Rows;
		FString Error;
		const bool bRead = ReadTable(Rows, Error);
		TestEqual(TEXT("read where supported: ") + Error, bRead, IsSupported());
		if (!IsSupported())
		{
			TestEqual(TEXT("Query"), S(Query(Server, Peer)), S(EClientState::Unknown));
		}
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
