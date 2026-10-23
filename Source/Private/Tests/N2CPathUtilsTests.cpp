// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Utils/N2CPathUtils.h"
#include "LLM/N2CLLMModule.h"
#include "Models/N2CBlueprint.h"
#include "Models/N2CTranslation.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CPathUtilsStemTest, "NodeToCode.Security.PathUtils.Stems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CPathUtilsStemTest::RunTest(const FString& Parameters)
{
	const FString Evil = FN2CPathUtils::MakeSafeFileStem(TEXT("../../Source/Evil"));
	TestFalse(TEXT("stem has no slash"), Evil.Contains(TEXT("/")) || Evil.Contains(TEXT("\\")));
	TestFalse(TEXT("stem has no .."), Evil.Contains(TEXT("..")));
	TestTrue(TEXT("stem is at most 64 chars"), Evil.Len() <= 64 && Evil.Len() > 0);
	for (TCHAR C : Evil)
	{
		TestTrue(TEXT("stem uses [A-Za-z0-9_] only"), FChar::IsAlnum(C) || C == TEXT('_'));
	}
	TestEqual(TEXT("spaces become underscores"), FN2CPathUtils::MakeSafeFileStem(TEXT("Event Graph")), FString(TEXT("Event_Graph")));
	TestEqual(TEXT("leading digit gets a prefix"), FN2CPathUtils::MakeSafeFileStem(TEXT("3D")), FString(TEXT("_3D")));
	TestEqual(TEXT("empty stays empty"), FN2CPathUtils::MakeSafeFileStem(TEXT("")), FString());

	// Translation stems keep plain names (older saves still load) and sanitize anything path-like
	TestEqual(TEXT("plain name kept"), FN2CPathUtils::GetTranslationFileStem(TEXT("Event Graph")), FString(TEXT("Event Graph")));
	TestFalse(TEXT("traversal sanitized"), FN2CPathUtils::GetTranslationFileStem(TEXT("../../../../Source/Evil")).Contains(TEXT("/")));
	TestFalse(TEXT("drive sanitized"), FN2CPathUtils::GetTranslationFileStem(TEXT("C:\\Windows")).Contains(TEXT(":")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CPathUtilsResolvedTest, "NodeToCode.Security.PathUtils.Resolved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CPathUtilsResolvedTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("N2CPathTest"));
	IFileManager::Get().MakeDirectory(*Root, true);

	TestFalse(TEXT("parent escape rejected"), FN2CPathUtils::IsUnderDirectoryResolved(Root / TEXT("../x"), Root));
	TestFalse(TEXT("sibling prefix rejected"), FN2CPathUtils::IsUnderDirectoryResolved(Root + TEXT("Sibling/x"), Root));
	TestTrue(TEXT("nested missing file accepted"), FN2CPathUtils::IsUnderDirectoryResolved(Root / TEXT("a/b.h"), Root));
	TestTrue(TEXT("root itself accepted"), FN2CPathUtils::IsUnderDirectoryResolved(Root, Root));

	FString Normalized;
	TestFalse(TEXT("bounds check rejects .."), FN2CPathUtils::ValidatePathWithinBounds(Root, Root / TEXT("../outside.txt"), Normalized));
	TestTrue(TEXT("bounds check accepts inside"), FN2CPathUtils::ValidatePathWithinBounds(Root, Root / TEXT("inside.txt"), Normalized));

	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CTranslationSaveContainmentTest, "NodeToCode.Security.PathUtils.SaveTranslation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FN2CTranslationSaveContainmentTest::RunTest(const FString& Parameters)
{
	UN2CLLMModule* Module = UN2CLLMModule::Get();
	if (!TestNotNull(TEXT("LLM module"), Module))
	{
		return false;
	}

	FN2CBlueprint Blueprint;
	Blueprint.Metadata.Name = TEXT("N2CPathTestFixture");
	Blueprint.Metadata.BlueprintClass = TEXT("Actor");
	// The fixture has no graphs, so the serializer's validator complains; that is not what this test checks
	AddExpectedError(TEXT("validation failed"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("No graphs found"), EAutomationExpectedErrorFlags::Contains, 0);

	FN2CGraphTranslation Graph;
	Graph.GraphName = TEXT("../../../../Source/N2CPathTestEvil");
	Graph.Code.GraphDeclaration = TEXT("// header");
	Graph.Code.GraphImplementation = TEXT("// impl");
	Graph.Code.ImplementationNotes = TEXT("notes");
	FN2CTranslationResponse Response;
	Response.Graphs.Add(Graph);

	Module->SaveTranslationToDisk(Response, Blueprint);

	// Nothing named after the evil graph may exist above the translation folders
	TArray<FString> Escaped;
	IFileManager::Get().FindFilesRecursive(Escaped, *FPaths::ProjectDir(), TEXT("N2CPathTestEvil*"), true, true, false);
	const FString TranslationsRoot = FPaths::ConvertRelativePathToFull(FN2CPathUtils::GetTranslationsBasePath());
	for (const FString& Found : Escaped)
	{
		TestTrue(FString::Printf(TEXT("%s stays inside the translation root"), *Found),
			FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Found), TranslationsRoot));
	}

	// Clean up the fixture's translation folders
	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *(TranslationsRoot / TEXT("N2CPathTestFixture_*")), false, true);
	for (const FString& Folder : Folders)
	{
		IFileManager::Get().DeleteDirectory(*(TranslationsRoot / Folder), false, true);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
