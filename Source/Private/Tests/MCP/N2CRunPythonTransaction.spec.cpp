// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "Tests/N2CToolSpecHarness.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/Selection.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "IPythonScriptPlugin.h"
#include "Misc/Guid.h"

/**
 * Card 0300 (gate 0403 steps 3 and 4 on 5.8): run-python is one undo step, "MCP: run-python", and its timeout stops a
 * runaway script. A property set with set_editor_property on a temporary actor and a selected actor moved with
 * set_actor_location (a plain setter, which calls no Modify) both come back with one Undo; a read-only script and a
 * failing one add no undo step; `while True: pass` with timeout 2 comes back with isError and timed_out true within
 * 5 s, and the editor ticks again afterwards. Calls the tool's Execute on the game thread (N2CTest::RunToolUnmarked).
 * Review ids: mcp-tools-python-translation-resources-5, -29, mcp-transport-7.
 */
BEGIN_DEFINE_SPEC(FN2CRunPythonTransactionSpec, "NodeToCode.MCP.RunPythonTransaction", N2C_TEST_FLAGS)

	AStaticMeshActor* Actor = nullptr;
	uint64 FrameAfterTimeout = 0;

	static bool PythonAvailable()
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		return Python && Python->IsPythonAvailable();
	}

	static UTransBuffer* Buffer()
	{
		return GEditor ? Cast<UTransBuffer>(GEditor->Trans) : nullptr;
	}

	/** Undo steps that can be undone: the queue less the undone ones a new transaction would drop (UndoCount) */
	static int32 QueueLength()
	{
		const UTransBuffer* Trans = Buffer();
		return Trans ? Trans->GetQueueLength() - Trans->UndoCount : 0;
	}

	FMcpToolCallResult Run(const FString& Code, double Timeout = 0.0)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("code"), Code);
		if (Timeout > 0.0)
		{
			Args->SetNumberField(TEXT("timeout"), Timeout);
		}
		return N2CTest::RunToolUnmarked(TEXT("run-python"), Args);
	}

	/** A Python expression for the temporary actor */
	FString ActorExpression() const
	{
		return FString::Printf(TEXT("unreal.find_object(None, '%s')"), *Actor->GetPathName());
	}

	bool Spawn()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return false;
		}
		FActorSpawnParameters Params;
		Params.Name = MakeUniqueObjectName(World->PersistentLevel, AStaticMeshActor::StaticClass(),
			FName(*FString::Printf(TEXT("N2CRunPythonUndo_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8))));
		Actor = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, 50.0), FRotator::ZeroRotator, Params);
		return Actor != nullptr;
	}

END_DEFINE_SPEC(FN2CRunPythonTransactionSpec)

void FN2CRunPythonTransactionSpec::Define()
{
	Describe(TEXT("one undo step per call"), [this]()
	{
		BeforeEach([this]()
		{
			TestTrue(TEXT("Python is available"), PythonAvailable());
			TestTrue(TEXT("the editor has a transaction buffer"), Buffer() != nullptr);
			TestTrue(TEXT("a temporary actor spawns in the editor world"), Spawn());
		});

		AfterEach([this]()
		{
			if (GEditor)
			{
				GEditor->SelectNone(false, true);
			}
			if (Actor && IsValid(Actor))
			{
				Actor->Destroy();
			}
			Actor = nullptr;
		});

		It(TEXT("undoes a property set with set_editor_property in one step named MCP: run-python"), [this]()
		{
			if (!Actor || !Buffer() || !PythonAvailable())
			{
				return;
			}
			UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
			Mesh->CastShadow = true;
			const int32 Before = QueueLength();
			const FMcpToolCallResult Result = Run(FString::Printf(
				TEXT("import unreal\n%s.static_mesh_component.set_editor_property('cast_shadow', False)\nresult = 'set'"), *ActorExpression()));
			if (!TestFalse(TEXT("the script succeeds: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError))
			{
				return;
			}
			TestFalse(TEXT("the script set the property"), Mesh->CastShadow);
			TestEqual(TEXT("one undo step"), QueueLength(), Before + 1);
			TestEqual(TEXT("named MCP: run-python"), Buffer()->GetUndoContext(true).Title.ToString(), FString(TEXT("MCP: run-python")));
			TestTrue(TEXT("Undo runs"), GEditor->UndoTransaction());
			TestTrue(TEXT("Undo restores the old value"), Mesh->CastShadow);
		});

		It(TEXT("undoes a selected actor moved with set_actor_location (gate 0403 step 3)"), [this]()
		{
			if (!Actor || !Buffer() || !PythonAvailable())
			{
				return;
			}
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Actor, true, true);
			const FVector Start = Actor->GetActorLocation();
			const int32 Before = QueueLength();
			const FMcpToolCallResult Result = Run(TEXT(
				"import unreal\n"
				"actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_selected_level_actors()[0]\n"
				"actor.set_actor_location(unreal.Vector(500, 300, 50), False, False)\n"
				"result = str(actor.get_actor_location())"));
			if (!TestFalse(TEXT("the script succeeds: ") + N2CTest::ResultText(Result).Left(300), Result.bIsError))
			{
				return;
			}
			TestTrue(TEXT("the script moved the actor"), Actor->GetActorLocation().Equals(FVector(500.0, 300.0, 50.0), 0.01));
			TestEqual(TEXT("one undo step"), QueueLength(), Before + 1);
			TestTrue(TEXT("Undo runs"), GEditor->UndoTransaction());
			TestTrue(FString::Printf(TEXT("Undo puts the actor back (at %s)"), *Actor->GetActorLocation().ToString()),
				Actor->GetActorLocation().Equals(Start, 0.01));
		});

		It(TEXT("adds no undo step for a read-only script, a selection it does not change, or a script that fails first"), [this]()
		{
			if (!Actor || !Buffer() || !PythonAvailable())
			{
				return;
			}
			GEditor->SelectActor(Actor, true, true);
			const int32 Before = QueueLength();
			const FMcpToolCallResult Read = Run(FString::Printf(
				TEXT("import unreal\nresult = %s.get_actor_location().z"), *ActorExpression()));
			TestFalse(TEXT("the read succeeds: ") + N2CTest::ResultText(Read).Left(300), Read.bIsError);
			TestEqual(TEXT("no undo step after a read"), QueueLength(), Before);
			const FMcpToolCallResult Failed = Run(TEXT("raise ValueError('before any edit')"));
			TestTrue(TEXT("the failing script is isError"), Failed.bIsError);
			TestEqual(TEXT("no undo step after a failure that edited nothing"), QueueLength(), Before);
		});
	});

	Describe(TEXT("the timeout"), [this]()
	{
		It(TEXT("stops while True: pass with timeout 2 within 5 s, as isError with timed_out (gate 0403 step 4)"), [this]()
		{
			if (!TestTrue(TEXT("Python is available"), PythonAvailable()))
			{
				return;
			}
			const double Start = FPlatformTime::Seconds();
			const FMcpToolCallResult Result = Run(TEXT("while True:\n    pass"), 2.0);
			const double Elapsed = FPlatformTime::Seconds() - Start;
			const TSharedPtr<FJsonObject> Body = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
			TestTrue(TEXT("isError"), Result.bIsError);
			TestTrue(FString::Printf(TEXT("returned within 5 s (took %.2f s)"), Elapsed), Elapsed < 5.0);
			TestTrue(FString::Printf(TEXT("not before the timeout (took %.2f s)"), Elapsed), Elapsed >= 1.9);
			if (TestTrue(TEXT("the answer is JSON"), Body.IsValid()))
			{
				bool bTimedOut = false;
				TestTrue(TEXT("timed_out is true"), Body->TryGetBoolField(TEXT("timed_out"), bTimedOut) && bTimedOut);
				TestTrue(TEXT("the error says it timed out: ") + Body->GetStringField(TEXT("error")),
					Body->GetStringField(TEXT("error")).StartsWith(TEXT("timed out")));
			}
			// The same interpreter takes the next call at once
			const FMcpToolCallResult Next = Run(TEXT("result = 7"));
			TestFalse(TEXT("the next call succeeds: ") + N2CTest::ResultText(Next).Left(300), Next.bIsError);
			FrameAfterTimeout = GFrameCounter;
		});

		LatentIt(TEXT("lets the editor tick again after a timed-out script"), FTimespan::FromSeconds(10), [this](const FDoneDelegate& Done)
		{
			if (!PythonAvailable())
			{
				Done.Execute();
				return;
			}
			Run(TEXT("while True:\n    pass"), 1.0);
			const uint64 Frame = GFrameCounter;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, Frame, Done](float) -> bool
			{
				if (GFrameCounter > Frame)
				{
					TestTrue(TEXT("the editor ticked after the timed-out script"), true);
					Done.Execute();
					return false;
				}
				return true;
			}));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
