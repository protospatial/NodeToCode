// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace N2CTest
{
	/**
	 * A loopback TCP port the operating system picked as free: a socket bound to port 0, its port read back, the socket
	 * closed. -1 when no socket could be made. The OS hands out each ephemeral port in turn, so unlike a random pick in a
	 * fixed range it does not collide with a stub in another editor (another checkout's tests running at the same time).
	 */
	inline int32 EphemeralLoopbackPort()
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!Sockets)
		{
			return -1;
		}
		FUniqueSocket Socket = Sockets->CreateUniqueSocket(NAME_Stream, TEXT("N2CSpecPortPick"));
		if (!Socket)
		{
			return -1;
		}
		TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
		Address->SetLoopbackAddress();
		Address->SetPort(0);
		if (!Socket->Bind(*Address))
		{
			return -1;
		}
		const int32 Port = Socket->GetPortNo();
		return Port > 0 ? Port : -1;
	}

	/** True when a plain bind of 127.0.0.1:Port, without address reuse, succeeds (nothing in any process holds it) */
	inline bool IsLoopbackPortFree(int32 Port)
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		FUniqueSocket Socket = Sockets ? Sockets->CreateUniqueSocket(NAME_Stream, TEXT("N2CSpecPortCheck")) : FUniqueSocket();
		if (!Socket)
		{
			return false;
		}
		TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
		Address->SetLoopbackAddress();
		Address->SetPort(Port);
		return Socket->Bind(*Address);
	}

	/**
	 * An HTTPServer router for a spec's local stub endpoint, already listening on a port the OS picked (OutPort).
	 * Listeners are started first, so GetHttpRouter binds at once and a port taken between the pick and the bind gives
	 * no router and the next pick is tried (a port another process holds is skipped before HTTPServer sees it, since
	 * HTTPServer logs an error for a failed bind and the error fails the spec). Null after a few tries.
	 */
	inline TSharedPtr<IHttpRouter> StartStubRouter(uint32& OutPort)
	{
		FHttpServerModule& Server = FHttpServerModule::Get();
		Server.StartAllListeners();
		for (int32 Attempt = 0; Attempt < 5; ++Attempt)
		{
			const int32 Port = EphemeralLoopbackPort();
			if (Port <= 0 || !IsLoopbackPortFree(Port))
			{
				continue;
			}
			if (TSharedPtr<IHttpRouter> Router = Server.GetHttpRouter(static_cast<uint32>(Port), /*bFailOnBindFailure=*/true))
			{
				OutPort = static_cast<uint32>(Port);
				return Router;
			}
		}
		OutPort = 0;
		return nullptr;
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
