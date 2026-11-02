// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "N2CMcpTcpConnectionState.h"

#include "HAL/PlatformProcess.h"
#include "Models/N2CLogging.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace N2CMcpTcpConnectionState
{
	bool IsSupported()
	{
		return PLATFORM_WINDOWS != 0;
	}

	EClientState Classify(const TArray<FTcpRow>& Rows, int32 LocalPort, int32 PeerPort, uint32 OwningPid)
	{
		if (LocalPort <= 0 || PeerPort <= 0)
		{
			return EClientState::Unknown;
		}
		bool bListening = false;
		bool bFound = false;
		bool bOpen = false;
		for (const FTcpRow& Row : Rows)
		{
			if (OwningPid != 0 && Row.OwningPid != OwningPid)
			{
				continue;
			}
			if (Row.LocalPort != LocalPort)
			{
				continue;
			}
			if (Row.State == States::Listen)
			{
				bListening = true;
				continue;
			}
			if (Row.RemotePort != PeerPort)
			{
				continue;
			}
			bFound = true;
			switch (Row.State)
			{
			case States::SynReceived:
			case States::Established:
			case States::FinWait1:
			case States::FinWait2:
				bOpen = true;
				break;
			default:
				// CLOSE_WAIT (the client sent FIN), CLOSING, LAST_ACK, CLOSED, TIME_WAIT, DELETE_TCB
				break;
			}
		}
		if (bOpen)
		{
			return EClientState::Open;
		}
		if (bFound)
		{
			return EClientState::Gone;
		}
		return bListening ? EClientState::Gone : EClientState::Unknown;
	}

#if PLATFORM_WINDOWS
	namespace
	{
		/** GetExtendedTcpTable(TCP_TABLE_OWNER_PID_ALL) for one address family into Buffer; false with OutError */
		bool ReadFamily(ULONG Family, TArray<uint8>& Buffer, FString& OutError)
		{
			for (int32 Attempt = 0; Attempt < 3; ++Attempt)
			{
				DWORD Size = static_cast<DWORD>(Buffer.Num());
				const DWORD Result = ::GetExtendedTcpTable(Buffer.Num() > 0 ? Buffer.GetData() : nullptr, &Size, /*bOrder*/ 0, Family,
					TCP_TABLE_OWNER_PID_ALL, 0);
				if (Result == NO_ERROR)
				{
					return true;
				}
				if (Result != ERROR_INSUFFICIENT_BUFFER)
				{
					OutError = FString::Printf(TEXT("GetExtendedTcpTable(%s) failed: %u"), Family == AF_INET ? TEXT("IPv4") : TEXT("IPv6"),
						static_cast<uint32>(Result));
					return false;
				}
				// The table grew between the two calls: try again with the size asked for plus room
				Buffer.SetNumUninitialized(static_cast<int32>(Size) + 4096);
			}
			OutError = TEXT("GetExtendedTcpTable kept growing");
			return false;
		}
	}
#endif

	bool ReadTable(TArray<FTcpRow>& OutRows, FString& OutError)
	{
		OutRows.Reset();
#if PLATFORM_WINDOWS
		TArray<uint8> Buffer;
		if (!ReadFamily(AF_INET, Buffer, OutError))
		{
			return false;
		}
		const MIB_TCPTABLE_OWNER_PID* Table4 = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(Buffer.GetData());
		for (DWORD Index = 0; Table4 && Index < Table4->dwNumEntries; ++Index)
		{
			const MIB_TCPROW_OWNER_PID& Row = Table4->table[Index];
			OutRows.Add({ ntohs(static_cast<u_short>(Row.dwLocalPort)), ntohs(static_cast<u_short>(Row.dwRemotePort)),
				static_cast<int32>(Row.dwState), static_cast<uint32>(Row.dwOwningPid) });
		}
		// IPv6 rows too, for a listener bound to ::1; a failure here keeps the IPv4 rows
		FString Error6;
		TArray<uint8> Buffer6;
		if (ReadFamily(AF_INET6, Buffer6, Error6))
		{
			const MIB_TCP6TABLE_OWNER_PID* Table6 = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(Buffer6.GetData());
			for (DWORD Index = 0; Table6 && Index < Table6->dwNumEntries; ++Index)
			{
				const MIB_TCP6ROW_OWNER_PID& Row = Table6->table[Index];
				OutRows.Add({ ntohs(static_cast<u_short>(Row.dwLocalPort)), ntohs(static_cast<u_short>(Row.dwRemotePort)),
					static_cast<int32>(Row.dwState), static_cast<uint32>(Row.dwOwningPid) });
			}
		}
		return true;
#else
		OutError = TEXT("reading the TCP table is implemented on Windows only");
		return false;
#endif
	}

	EClientState Query(int32 LocalPort, int32 PeerPort)
	{
		TArray<FTcpRow> Rows;
		FString Error;
		if (!ReadTable(Rows, Error))
		{
			UE_LOG(LogNodeToCode, Verbose, TEXT("MCP: cannot tell whether the client on port %d is still connected: %s"), PeerPort, *Error);
			return EClientState::Unknown;
		}
		return Classify(Rows, LocalPort, PeerPort, FPlatformProcess::GetCurrentProcessId());
	}
}
