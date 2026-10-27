#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Toolsets/N2CBlueprintToolset.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FN2CBlueprintInterfaceMigrationReviewGateTest,
    "NodeToCode.Blueprint.InterfaceMigrationRequiresReviewedCallers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CBlueprintInterfaceMigrationReviewGateTest::RunTest(
    const FString& Parameters)
{
    (void)Parameters;
    const FN2CBlueprintInterfaceMigrationResult Result =
        UN2CBlueprintToolset::MigrateBlueprintInterface(
            TEXT("/Game/NotLoaded.NotLoaded"),
            TEXT("/Script/FactoryGame.CharacterInterface"),
            TEXT("/Script/FactoryGame.CharacterInterface"),
            {}, {}, false, false, false, TEXT(""));

    TestFalse(TEXT("An unreviewed interface migration is rejected"), Result.bSuccess);
    TestFalse(TEXT("Rejection does not mutate the Blueprint"), Result.bApplied);
    TestFalse(TEXT("Rejection does not save a package"), Result.bSaved);
    TestTrue(TEXT("The error points to the caller-review gate"),
             Result.Error.Contains(TEXT("Review references within the migration scope")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
