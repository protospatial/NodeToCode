// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CCapture.h"
#include "EdGraphSchema_K2.h"
#include "HAL/FileManager.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Tests/Capture/N2CTestNodeClasses.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "Utils/N2CLogger.h"

namespace N2CLoggingSpec
{
	/** A node of class T in Graph calling Function, set up as the graph editor places it */
	template <typename T>
	T* AddCallNode(UEdGraph* Graph, UFunction* Function)
	{
		T* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
		Graph->AddNode(Node, /*bUserAction*/ false, /*bSelectNewNode*/ false);
		Node->SetFromFunction(Function);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		if (Node->Pins.Num() == 0)
		{
			Node->AllocateDefaultPins();
		}
		return Node;
	}

	/** True when Bytes is well-formed UTF-8 (no byte order mark, no NUL) */
	bool IsUtf8(const TArray<uint8>& Bytes)
	{
		if (Bytes.Num() >= 2 && ((Bytes[0] == 0xFF && Bytes[1] == 0xFE) || (Bytes[0] == 0xFE && Bytes[1] == 0xFF)))
		{
			return false;
		}
		int32 Index = 0;
		while (Index < Bytes.Num())
		{
			const uint8 Lead = Bytes[Index];
			int32 Continuation = 0;
			if (Lead == 0)
			{
				return false;
			}
			if (Lead < 0x80)
			{
				Continuation = 0;
			}
			else if ((Lead & 0xE0) == 0xC0)
			{
				Continuation = 1;
			}
			else if ((Lead & 0xF0) == 0xE0)
			{
				Continuation = 2;
			}
			else if ((Lead & 0xF8) == 0xF0)
			{
				Continuation = 3;
			}
			else
			{
				return false;
			}
			if (Index + Continuation >= Bytes.Num())
			{
				return false;
			}
			for (int32 Next = 1; Next <= Continuation; ++Next)
			{
				if ((Bytes[Index + Next] & 0xC0) != 0x80)
				{
					return false;
				}
			}
			Index += Continuation + 1;
		}
		return true;
	}
}

/**
 * Logging that costs nothing when it is off (card 0193, imp-translation-core-7): N2C_LOG and FN2CLogger::IsEnabled skip
 * building a message below the minimum severity, capture builds its per-node and per-pin Debug lines only at Debug, and
 * file logging writes every line as UTF-8 through one open file instead of reopening it per line with an encoding
 * picked per line.
 */
BEGIN_DEFINE_SPEC(FN2CLoggingSpec, "NodeToCode.Logging", N2C_TEST_FLAGS)

	EN2CLogSeverity SavedSeverity = EN2CLogSeverity::Info;

END_DEFINE_SPEC(FN2CLoggingSpec)

void FN2CLoggingSpec::Define()
{
	using namespace N2CLoggingSpec;

	BeforeEach([this]()
	{
		SavedSeverity = FN2CLogger::Get().GetMinSeverity();
	});

	AfterEach([this]()
	{
		FN2CLogger::Get().SetMinSeverity(SavedSeverity);
	});

	Describe(TEXT("N2C_LOG"), [this]()
	{
		It(TEXT("evaluates neither the format arguments nor the context below the minimum severity"), [this]()
		{
			FN2CLogger::Get().SetMinSeverity(EN2CLogSeverity::Info);
			int32 Evaluations = 0;
			auto Costly = [&Evaluations]() { ++Evaluations; return FString(TEXT("costly")); };

			N2C_LOG(Debug, TEXT("debug %s"), *Costly());
			N2C_LOG_CONTEXT(Debug, Costly(), TEXT("debug with context %s"), *Costly());
			TestEqual(TEXT("nothing evaluated at Debug while the minimum is Info"), Evaluations, 0);
			TestFalse(TEXT("IsEnabled(Debug)"), FN2CLogger::IsEnabled(EN2CLogSeverity::Debug));
			TestTrue(TEXT("IsEnabled(Info)"), FN2CLogger::IsEnabled(EN2CLogSeverity::Info));
			TestTrue(TEXT("IsEnabled(Error)"), FN2CLogger::IsEnabled(EN2CLogSeverity::Error));

			FN2CLogger::Get().ClearErrors();
			N2C_LOG(Info, TEXT("N2CLoggingSpec info %s"), *Costly());
			TestEqual(TEXT("evaluated once at Info"), Evaluations, 1);
			const TArray<FN2CError> Records = FN2CLogger::Get().GetErrors();
			bool bFound = false;
			for (const FN2CError& Record : Records)
			{
				bFound |= Record.Message == TEXT("N2CLoggingSpec info costly") && Record.Severity == EN2CLogSeverity::Info;
			}
			TestTrue(TEXT("the Info line was logged"), bFound);

			FN2CLogger::Get().SetMinSeverity(EN2CLogSeverity::Debug);
			N2C_LOG(Debug, TEXT("N2CLoggingSpec debug %s"), *Costly());
			TestEqual(TEXT("evaluated at Debug once the minimum is Debug"), Evaluations, 2);
			FN2CLogger::Get().ClearErrors();
		});
	});

	Describe(TEXT("capture"), [this]()
	{
		It(TEXT("of 120 nodes at Info makes no GetNodeTitle calls for Debug lines"), [this]()
		{
			using N2CTest::FTestBlueprint;

			FTestBlueprint BP(TEXT("BP_N2CLoggingCost"));
			if (!TestNotNull(TEXT("test Blueprint"), BP.GetBlueprint()))
			{
				return;
			}
			UFunction* PrintString = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString));
			UFunction* Concat = UKismetStringLibrary::StaticClass()->FindFunctionByName(
				GET_FUNCTION_NAME_CHECKED(UKismetStringLibrary, Concat_StrStr));
			UEdGraph* Graph = BP.AddFunction(TEXT("LoggingCost"));
			if (!TestNotNull(TEXT("PrintString"), PrintString) || !TestNotNull(TEXT("Concat_StrStr"), Concat) || !TestNotNull(TEXT("graph"), Graph))
			{
				return;
			}

			// Entry -> 60 Print Strings in one exec chain, each printing the result of its own Append: 120 counting
			// nodes with exec links, data links and unlinked input pins (every per-node, per-pin and per-flow line)
			UEdGraphNode* Previous = BP.FunctionEntry(Graph);
			for (int32 Index = 0; Index < 60; ++Index)
			{
				UK2Node_N2CTestTitleCounter* Print = AddCallNode<UK2Node_N2CTestTitleCounter>(Graph, PrintString);
				UK2Node_N2CTestTitleCounter* Append = AddCallNode<UK2Node_N2CTestTitleCounter>(Graph, Concat);
				BP.Link(Previous, UEdGraphSchema_K2::PN_Then, Print, UEdGraphSchema_K2::PN_Execute);
				BP.Link(Append, UEdGraphSchema_K2::PN_ReturnValue, Print, TEXT("InString"));
				Previous = Print;
			}
			for (const FString& Error : BP.Errors())
			{
				AddError(FString::Printf(TEXT("building the graph: %s"), *Error));
			}
			if (BP.Errors().Num() > 0)
			{
				return;
			}

			// Title calls of one capture at a minimum severity
			auto TitleCallsAt = [this, Graph](EN2CLogSeverity Severity, int32& OutNodes) -> int32
			{
				FN2CLogger::Get().SetMinSeverity(Severity);
				UK2Node_N2CTestTitleCounter::TitleCalls = 0;
				const TValueOrError<FN2CCapture, FString> Capture = N2C::CaptureGraph(*Graph);
				const int32 Calls = UK2Node_N2CTestTitleCounter::TitleCalls;
				OutNodes = Capture.HasValue() && Capture.GetValue().Blueprint.Graphs.Num() > 0
					? Capture.GetValue().Blueprint.Graphs[0].Nodes.Num() : 0;
				return Calls;
			};

			int32 NodesOff = 0;
			int32 NodesInfo = 0;
			int32 NodesDebug = 0;
			const int32 CallsOff = TitleCallsAt(EN2CLogSeverity::Fatal, NodesOff);
			const int32 CallsInfo = TitleCallsAt(EN2CLogSeverity::Info, NodesInfo);
			const int32 CallsDebug = TitleCallsAt(EN2CLogSeverity::Debug, NodesDebug);
			FN2CLogger::Get().ClearErrors();

			TestTrue(FString::Printf(TEXT("captured the entry and 120 nodes at every severity (%d, %d, %d)"), NodesOff, NodesInfo, NodesDebug),
				NodesInfo >= 121 && NodesOff == NodesInfo && NodesDebug == NodesInfo);
			// Capture's own data (title hints) may read titles; logging at Info must add no call to that
			TestEqual(FString::Printf(TEXT("GetNodeTitle calls at Info (%d) equal those with logging off (%d)"), CallsInfo, CallsOff),
				CallsInfo, CallsOff);
			// Before card 0193 the Debug lines' titles were read at every severity, Fatal included (about ten calls per
			// node here), so equality alone could pass: capture's own reads are a few per node, whatever the pins and flows
			TestTrue(FString::Printf(TEXT("at most 3 GetNodeTitle calls per counting node at Info (%d for 120 nodes)"), CallsInfo),
				CallsInfo <= 3 * 120);
			// The counter sees the Debug lines' titles, so the checks above would catch them at Info
			TestTrue(FString::Printf(TEXT("Debug lines read titles (%d calls at Debug, %d at Info)"), CallsDebug, CallsInfo),
				CallsDebug >= CallsInfo + 120);
		});
	});

	Describe(TEXT("file logging"), [this]()
	{
		It(TEXT("appends every line as UTF-8 to one file, ASCII and non-ASCII alike"), [this]()
		{
			N2CTest::FScratchDir Scratch(TEXT("N2CLogging"));
			const FString ScratchLogPath = Scratch / TEXT("Logs/NodeToCode.log");
			FN2CLogger& Logger = FN2CLogger::Get();
			const FString DefaultPath = FPaths::ProjectSavedDir() / TEXT("NodeToCode.log");
			ON_SCOPE_EXIT
			{
				FN2CLogger::Get().EnableFileLogging(false);
				FN2CLogger::Get().SetLogFilePath(DefaultPath);
				FN2CLogger::Get().ClearErrors();
			};

			Logger.SetMinSeverity(EN2CLogSeverity::Info);
			Logger.SetLogFilePath(ScratchLogPath);
			Logger.EnableFileLogging(true);
			const FString NonAscii = TEXT("Grüße 日本 Δ");
			Logger.Log(TEXT("N2CLoggingSpec plain line"), EN2CLogSeverity::Info);
			Logger.Log(FString::Printf(TEXT("N2CLoggingSpec %s"), *NonAscii), EN2CLogSeverity::Info);
			Logger.Log(TEXT("N2CLoggingSpec plain again"), EN2CLogSeverity::Info);
			N2C_LOG(Debug, TEXT("N2CLoggingSpec below severity"));

			// Readable while open (FILEWRITE_AllowRead), and complete: every line is flushed as it is written
			TArray<uint8> Bytes;
			TestTrue(TEXT("the log file can be read while logging"), FFileHelper::LoadFileToArray(Bytes, *ScratchLogPath, FILEREAD_AllowWrite));
			Logger.EnableFileLogging(false);
			TestTrue(TEXT("closed once file logging is off, so it can be deleted"), IFileManager::Get().Delete(*ScratchLogPath));

			TestTrue(TEXT("the file is UTF-8 throughout (no UTF-16 line, no byte order mark, no NUL)"), IsUtf8(Bytes));
			FString Text;
			FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);
			if (TestEqual(TEXT("three lines (the Debug one is below severity)"), Lines.Num(), 3))
			{
				TestTrue(TEXT("first line"), Lines[0].EndsWith(TEXT("N2CLoggingSpec plain line")));
				TestTrue(FString::Printf(TEXT("the non-ASCII line round-trips: %s"), *Lines[1]), Lines[1].EndsWith(NonAscii));
				TestTrue(TEXT("third line"), Lines[2].EndsWith(TEXT("N2CLoggingSpec plain again")));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
