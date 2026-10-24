// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Server/N2CMcpHttpServerManager.h"
#include "Core/N2CSettings.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IPAddress.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

/**
 * The MCP server's port handling and shutdown (card 0112, findings mcp-transport-10 and mcp-transport-22): a port
 * another process holds is skipped with a warning notification naming the port and the Server Port setting, no free
 * port fails with an error notification, the discovery file names the bound port, and stopping or restarting the
 * server unbinds only its own routes, so another plugin's HTTPServer listener keeps answering. On UE 5.8, run with
 * -EnablePlugins=ModelContextProtocol -ModelContextProtocolStartServer to check Epic's Unreal MCP server as well.
 */
namespace N2CMcpServerLifecycleSpec
{
	/** A plain listening socket on 127.0.0.1:Port, the way another editor or program holds the port */
	struct FPortHolder
	{
		FUniqueSocket Socket;

		bool Hold(int32 Port)
		{
			ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
			Socket = Sockets->CreateUniqueSocket(NAME_Stream, TEXT("N2CSpecPortHolder"));
			TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
			Address->SetLoopbackAddress();
			Address->SetPort(Port);
			return Socket && Socket->Bind(*Address) && Socket->Listen(1);
		}
	};

	static bool IsFree(int32 Port)
	{
		FPortHolder Probe;
		return Probe.Hold(Port);
	}

	/** A port P with P..P+Count-1 all free */
	static int32 FreePorts(int32 Count)
	{
		for (int32 Attempt = 0; Attempt < 50; ++Attempt)
		{
			const int32 Port = 31000 + FMath::RandRange(0, 8000);
			bool bAllFree = true;
			for (int32 Offset = 0; Offset < Count && bAllFree; ++Offset)
			{
				bAllFree = IsFree(Port + Offset);
			}
			if (bAllFree)
			{
				return Port;
			}
		}
		return -1;
	}

	/** Ticks the HTTP server and the HTTP client until Seconds pass or Until is true */
	static void Pump(double Seconds, TFunctionRef<bool()> Until)
	{
		FHttpManager& Manager = FHttpModule::Get().GetHttpManager();
		FTSTickerObjectBase& ServerTicker = FHttpServerModule::Get();
		const double End = FPlatformTime::Seconds() + Seconds;
		double Last = FPlatformTime::Seconds();
		while (FPlatformTime::Seconds() < End && !Until())
		{
			FPlatformProcess::Sleep(0.005f);
			const double Now = FPlatformTime::Seconds();
			const float Delta = static_cast<float>(Now - Last);
			Last = Now;
			ServerTicker.Tick(Delta);
			Manager.Tick(Delta);
		}
	}

	/** Sends one request and returns its HTTP status, 0 when nothing answered */
	static int32 Request(const FString& Verb, const FString& Url, const FString& Body = FString())
	{
		const TSharedRef<int32> Code = MakeShared<int32>(-1);
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
		Http->SetVerb(Verb);
		Http->SetURL(Url);
		Http->SetTimeout(10.0f);
		if (!Body.IsEmpty())
		{
			Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Http->SetHeader(TEXT("Accept"), TEXT("application/json, text/event-stream"));
			Http->SetContentAsString(Body);
		}
		Http->OnProcessRequestComplete().BindLambda([Code](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
		{
			*Code = (bSucceeded && Response.IsValid()) ? Response->GetResponseCode() : 0;
		});
		Http->ProcessRequest();
		Pump(12.0, [Code] { return *Code >= 0; });
		if (*Code < 0)
		{
			Http->CancelRequest();
			return 0;
		}
		return *Code;
	}

	static FString HealthUrl(int32 Port)
	{
		return FString::Printf(TEXT("http://127.0.0.1:%d/mcp/health"), Port);
	}

	/** Another plugin's listener on its own port with one GET route, as Remote Control or Unreal MCP would have */
	struct FOtherPluginListener
	{
		int32 Port = -1;
		TSharedPtr<IHttpRouter> Router;
		FHttpRouteHandle Route;

		bool Start()
		{
			Port = FreePorts(1);
			Router = Port > 0 ? FHttpServerModule::Get().GetHttpRouter(Port, /*bFailOnBindFailure=*/true) : nullptr;
			if (!Router.IsValid())
			{
				return false;
			}
			Route = Router->BindRoute(FHttpPath(TEXT("/n2c-spec-other")), EHttpServerRequestVerbs::VERB_GET,
				FHttpRequestHandler::CreateLambda([](const FHttpServerRequest&, const FHttpResultCallback& OnComplete)
				{
					OnComplete(FHttpServerResponse::Create(TEXT("other"), TEXT("text/plain")));
					return true;
				}));
			FHttpServerModule::Get().StartAllListeners();
			return Route.IsValid();
		}

		void Stop()
		{
			if (Router.IsValid() && Route.IsValid())
			{
				Router->UnbindRoute(Route);
			}
			Route.Reset();
			Router.Reset();
		}

		FString Url() const
		{
			return FString::Printf(TEXT("http://127.0.0.1:%d/n2c-spec-other"), Port);
		}
	};

	struct FNotice
	{
		FString Message;
		bool bIsError = false;
	};

	/** The texts of every STextBlock under Widget */
	static void CollectTexts(const TSharedRef<SWidget>& Widget, TArray<FString>& OutTexts)
	{
		if (Widget->GetType() == FName(TEXT("STextBlock")))
		{
			OutTexts.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			CollectTexts(Children->GetChildAt(Index), OutTexts);
		}
	}

	/** The texts shown in the editor's notification windows */
	static TArray<FString> NotificationTexts()
	{
		TArray<TSharedRef<SWindow>> Windows;
		FSlateNotificationManager::Get().GetWindows(Windows);
		TArray<FString> Texts;
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			CollectTexts(Window, Texts);
		}
		return Texts;
	}
}

BEGIN_DEFINE_SPEC(FN2CMcpServerLifecycleSpec, "NodeToCode.MCP.ServerLifecycle", N2C_TEST_FLAGS)
	TUniquePtr<N2CTest::FScratchDir> Scratch;
	TSharedPtr<TArray<N2CMcpServerLifecycleSpec::FNotice>> Notices;
	bool bSkip = false;
	FString SavedStatus;

	FN2CMcpHttpServerManager& Server() { return FN2CMcpHttpServerManager::Get(); }

	TSharedPtr<FJsonObject> ReadDiscovery()
	{
		FString Json;
		return FFileHelper::LoadFileToString(Json, *Server().GetDiscoveryFilePath()) ? N2CTest::ParseJsonObject(Json) : nullptr;
	}
END_DEFINE_SPEC(FN2CMcpServerLifecycleSpec)

void FN2CMcpServerLifecycleSpec::Define()
{
	using namespace N2CMcpServerLifecycleSpec;

	BeforeEach([this]()
	{
		// An editor started with -N2CMcp serves real clients: leave its server alone
		bSkip = Server().IsServerRunning();
		if (bSkip)
		{
			AddInfo(TEXT("The MCP server is running in this editor (-N2CMcp); skipped so its clients are not disturbed"));
			return;
		}
		SavedStatus = GetDefault<UN2CSettings>()->McpServerStatus;
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CMcpServerLifecycle"));
		Server().SetDiscoveryPathsForTesting(*Scratch / TEXT("mcp-server.json"), *Scratch / TEXT("servers"));
		Notices = MakeShared<TArray<FNotice>>();
		TSharedPtr<TArray<FNotice>> Captured = Notices;
		Server().SetNotificationHandlerForTesting([Captured](const FString& Message, bool bIsError)
		{
			Captured->Add({ Message, bIsError });
		});
	});

	AfterEach([this]()
	{
		if (bSkip)
		{
			return;
		}
		Server().StopServer();
		Server().SetNotificationHandlerForTesting(nullptr);
		Server().SetDiscoveryPathsForTesting(FString(), FString());
		Scratch.Reset();
		GetMutableDefault<UN2CSettings>()->McpServerStatus = SavedStatus;
	});

	It(TEXT("starts on the asked port, writes the discovery file and removes it on stop"), [this]()
	{
		if (bSkip) { return; }
		const int32 Port = FreePorts(2);
		if (!TestTrue(TEXT("found free ports"), Port > 0)) { return; }

		TestTrue(TEXT("started"), Server().StartServer(Port, 3));
		TestEqual(TEXT("on the asked port"), Server().GetServerPort(), Port);
		TestEqual(TEXT("no notification for the asked port"), Notices->Num(), 0);
		TestEqual(TEXT("/mcp/health answers"), Request(TEXT("GET"), HealthUrl(Port)), 200);
		TestTrue(TEXT("the settings page shows the URL"), GetDefault<UN2CSettings>()->McpServerStatus.Contains(FString::Printf(TEXT("127.0.0.1:%d/mcp"), Port)));

		const TSharedPtr<FJsonObject> Discovery = ReadDiscovery();
		if (TestTrue(TEXT("discovery file written"), Discovery.IsValid()))
		{
			TestEqual(TEXT("discovery port"), static_cast<int32>(Discovery->GetNumberField(TEXT("port"))), Port);
			TestEqual(TEXT("discovery pid"), static_cast<uint32>(Discovery->GetNumberField(TEXT("pid"))), FPlatformProcess::GetCurrentProcessId());
			TestEqual(TEXT("discovery project"), Discovery->GetStringField(TEXT("projectFile")), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
			TestFalse(TEXT("discovery engine version"), Discovery->GetStringField(TEXT("engineVersion")).IsEmpty());
			const TArray<TSharedPtr<FJsonValue>>* Versions = nullptr;
			TestTrue(TEXT("discovery protocol versions"), Discovery->TryGetArrayField(TEXT("protocolVersions"), Versions) && Versions->Num() > 0);
		}
		TestTrue(TEXT("per-user index entry written"), FPaths::FileExists(Server().GetDiscoveryIndexFilePath()));

		Server().StopServer();
		TestFalse(TEXT("stopped"), Server().IsServerRunning());
		TestFalse(TEXT("discovery file removed"), FPaths::FileExists(Server().GetDiscoveryFilePath()));
		TestFalse(TEXT("per-user index entry removed"), FPaths::FileExists(Server().GetDiscoveryIndexFilePath()));
		TestEqual(TEXT("/mcp/health is unbound after stop"), Request(TEXT("GET"), HealthUrl(Port)), 404);
	});

	It(TEXT("keeps another plugin's listener answering after a stop and a restart"), [this]()
	{
		if (bSkip) { return; }
		FOtherPluginListener Other;
		if (!TestTrue(TEXT("other plugin's listener started"), Other.Start())) { return; }
		const int32 Port = FreePorts(2);
		if (!TestTrue(TEXT("found free ports"), Port > 0)) { Other.Stop(); return; }

		// UE 5.8's Unreal MCP plugin, when this editor runs it
		int32 EpicPort = 0;
		const bool bEpicMcp = FParse::Param(FCommandLine::Get(), TEXT("ModelContextProtocolStartServer"))
			&& FModuleManager::Get().IsModuleLoaded(TEXT("ModelContextProtocol"));
		if (bEpicMcp)
		{
			EpicPort = 8000;
			FParse::Value(FCommandLine::Get(), TEXT("ModelContextProtocolPort="), EpicPort);
		}
		const FString EpicUrl = FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), EpicPort);
		const FString EpicInitialize = TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\",")
			TEXT("\"capabilities\":{},\"clientInfo\":{\"name\":\"n2c-spec\",\"version\":\"1\"}}}");
		auto TestEpic = [&](const TCHAR* When)
		{
			if (!bEpicMcp) { return; }
			const int32 Code = Request(TEXT("POST"), EpicUrl, EpicInitialize);
			AddInfo(FString::Printf(TEXT("Unreal MCP on port %d %s: HTTP %d"), EpicPort, When, Code));
			TestTrue(FString::Printf(TEXT("Unreal MCP on port %d answers %s"), EpicPort, When), Code > 0 && Code != 404);
		};

		TestEqual(TEXT("other listener answers before"), Request(TEXT("GET"), Other.Url()), 200);
		TestEpic(TEXT("before"));
		TestTrue(TEXT("started"), Server().StartServer(Port, 1));
		Server().StopServer();
		TestEqual(TEXT("other listener answers after stop"), Request(TEXT("GET"), Other.Url()), 200);
		TestEpic(TEXT("after stop"));

		TestTrue(TEXT("restarted"), Server().StartServer(Port, 1));
		TestEqual(TEXT("restarted on the same port"), Server().GetServerPort(), Port);
		TestEqual(TEXT("/mcp/health answers after restart"), Request(TEXT("GET"), HealthUrl(Port)), 200);
		TestEqual(TEXT("other listener answers after restart"), Request(TEXT("GET"), Other.Url()), 200);
		Server().StopServer();
		TestEqual(TEXT("other listener answers after the second stop"), Request(TEXT("GET"), Other.Url()), 200);
		TestEpic(TEXT("after restart and stop"));
		Other.Stop();
	});

	It(TEXT("takes the next free port and says so when the asked port is held"), [this]()
	{
		if (bSkip) { return; }
		const int32 Port = FreePorts(3);
		if (!TestTrue(TEXT("found free ports"), Port > 0)) { return; }
		FPortHolder Holder;
		if (!TestTrue(TEXT("holding the port"), Holder.Hold(Port))) { return; }

		AddExpectedMessage(FString::Printf(TEXT("MCP port %d is in use"), Port), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(FString::Printf(TEXT("so the MCP server is on port %d"), Port + 1), EAutomationExpectedMessageFlags::Contains, 1);
		TestTrue(TEXT("started"), Server().StartServer(Port, 3));
		TestEqual(TEXT("on the next port"), Server().GetServerPort(), Port + 1);
		TestEqual(TEXT("/mcp/health answers on the next port"), Request(TEXT("GET"), HealthUrl(Port + 1)), 200);
		if (TestEqual(TEXT("one notification"), Notices->Num(), 1))
		{
			const FNotice& Notice = (*Notices)[0];
			TestFalse(TEXT("a warning, not an error"), Notice.bIsError);
			TestTrue(TEXT("names the held port"), Notice.Message.Contains(FString::Printf(TEXT("port %d is in use"), Port)));
			TestTrue(TEXT("names the URL it got"), Notice.Message.Contains(FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Port + 1)));
			TestTrue(TEXT("names the setting"), Notice.Message.Contains(TEXT("Server Port")));
		}
		const TSharedPtr<FJsonObject> Discovery = ReadDiscovery();
		if (TestTrue(TEXT("discovery file written"), Discovery.IsValid()))
		{
			TestEqual(TEXT("discovery names the bound port"), static_cast<int32>(Discovery->GetNumberField(TEXT("port"))), Port + 1);
			TestEqual(TEXT("discovery names the asked port"), static_cast<int32>(Discovery->GetNumberField(TEXT("requestedPort"))), Port);
		}
	});

	It(TEXT("fails with an error notification naming the ports and the setting when none is free"), [this]()
	{
		if (bSkip) { return; }
		const int32 Port = FreePorts(2);
		if (!TestTrue(TEXT("found free ports"), Port > 0)) { return; }
		FPortHolder First, Second;
		if (!TestTrue(TEXT("holding both ports"), First.Hold(Port) && Second.Hold(Port + 1))) { return; }

		AddExpectedMessage(FString::Printf(TEXT("MCP port %d is in use"), Port), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(FString::Printf(TEXT("MCP port %d is in use"), Port + 1), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedError(FString::Printf(TEXT("ports %d-%d are all in use"), Port, Port + 1), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("did not start"), Server().StartServer(Port, 2));
		TestFalse(TEXT("not running"), Server().IsServerRunning());
		TestEqual(TEXT("no port"), Server().GetServerPort(), -1);
		TestFalse(TEXT("no discovery file"), FPaths::FileExists(Server().GetDiscoveryFilePath()));
		if (TestEqual(TEXT("one notification"), Notices->Num(), 1))
		{
			const FNotice& Notice = (*Notices)[0];
			TestTrue(TEXT("an error"), Notice.bIsError);
			TestTrue(TEXT("names the ports"), Notice.Message.Contains(FString::Printf(TEXT("ports %d-%d"), Port, Port + 1)));
			TestTrue(TEXT("names the setting"), Notice.Message.Contains(TEXT("Server Port")));
		}
		TestTrue(TEXT("the settings page says why"), GetDefault<UN2CSettings>()->McpServerStatus.StartsWith(TEXT("Not running")));
	});

	It(TEXT("shows the conflict as an editor notification with the port and the setting"), [this]()
	{
		if (bSkip) { return; }
		if (!FSlateApplication::IsInitialized())
		{
			AddInfo(TEXT("No Slate application in this run; the notification text is checked through the handler cases"));
			return;
		}
		const int32 Port = FreePorts(3);
		if (!TestTrue(TEXT("found free ports"), Port > 0)) { return; }
		FPortHolder Holder;
		if (!TestTrue(TEXT("holding the port"), Holder.Hold(Port))) { return; }

		// The real notification path, not the test handler
		Server().SetNotificationHandlerForTesting(nullptr);
		AddExpectedMessage(FString::Printf(TEXT("MCP port %d is in use"), Port), EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessage(FString::Printf(TEXT("so the MCP server is on port %d"), Port + 1), EAutomationExpectedMessageFlags::Contains, 1);
		TestTrue(TEXT("started"), Server().StartServer(Port, 3));
		FSlateApplication::Get().Tick();

		const FString Expected = FString::Printf(TEXT("port %d is in use"), Port);
		const TArray<FString> Texts = NotificationTexts();
		const FString* Shown = Texts.FindByPredicate([&Expected](const FString& Text) { return Text.Contains(Expected); });
		if (TestNotNull(TEXT("a notification window shows the conflict"), Shown))
		{
			// (Not echoed with AddInfo: the expected-message counts above would see the text a second time)
			TestTrue(TEXT("it names the URL it got"), Shown->Contains(FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Port + 1)));
			TestTrue(TEXT("it names the setting"), Shown->Contains(TEXT("Server Port")));
		}
		TestTrue(TEXT("it links to the settings"), Texts.ContainsByPredicate([](const FString& Text) { return Text.Contains(TEXT("Open Node to Code settings")); }));
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
