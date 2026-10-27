// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Core/N2CSettings.h"
#include "Core/Widgets/SN2CGraphOverlay.h"
#include "Core/Widgets/SN2CTagManager.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDetailsView.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Models/N2CTranslation.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/AppStyle.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"

/**
 * The capture harness for the wiki's screenshots (card 0410, docs-parity's Unreal recipe). One automation test per
 * capture group, named N2CDocs.Shots.<group> after the group's surface in .agents/docs-parity.json, so the build's
 * "NodeToCode" test filter never runs them. Each draws NodeToCode's own widgets, built in a set-up state from fixture
 * data (fictional Blueprints and tags, a sample translation, no API key), with FWidgetRenderer at DPI scale 2 into a
 * render target, and writes one PNG per shot, <folder>/<group>/<name>.png for -N2CDocsShots=<folder>, cropped to the widget.
 * Without -N2CDocsShots the tests do nothing; without an RHI (-nullrhi) they fail. Run them through
 * Tests/DocsShots/capture.py, which starts a GPU -RenderOffScreen editor on the 5.8 host and moves the files into
 * wiki/images/<group>/ with the shots manifest.
 */
namespace N2CDocsShots
{
	/** Capture scale: pictures are taken at 2x and shown at 1x (shot contract) */
	constexpr float Scale = 2.0f;

	/** The output folder from -N2CDocsShots=<folder>, or empty when the tests should not capture */
	static FString OutputFolder()
	{
		FString Folder;
		if (!FParse::Value(FCommandLine::Get(), TEXT("N2CDocsShots="), Folder))
		{
			return FString();
		}
		Folder.TrimQuotesInline();
		return FPaths::ConvertRelativePathToFull(Folder);
	}

	/** Whether this run captures; logs why not. Fails the test when asked to capture without a renderer. */
	static bool ShouldCapture(FAutomationTestBase& Test, FString& OutFolder)
	{
		OutFolder = OutputFolder();
		if (OutFolder.IsEmpty())
		{
			Test.AddInfo(TEXT("No -N2CDocsShots=<folder>: nothing captured (run Tests/DocsShots/capture.py)"));
			return false;
		}
		if (!FApp::CanEverRender() || !FSlateApplication::IsInitialized())
		{
			Test.AddError(TEXT("Docs shots need a renderer: start the editor with -RenderOffScreen and without -nullrhi"));
			return false;
		}
		return true;
	}

	/**
	 * Draws Widget at LogicalSize (Slate units) at 2x into a render target and writes it as <Folder>/<Name>.png
 * (Name is "<group>/<shot>").
	 * Several frames are drawn first so fonts, brushes and list rows settle; the last one is kept.
	 */
	static bool Shoot(FAutomationTestBase& Test, const TSharedRef<SWidget>& Widget, const FVector2D LogicalSize,
		const FString& Folder, const FString& Name)
	{
		const FVector2D Pixels(FMath::CeilToDouble(LogicalSize.X * Scale), FMath::CeilToDouble(LogicalSize.Y * Scale));
		TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(Pixels, TF_Bilinear, true));
		if (!Target.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s: no render target"), *Name));
			return false;
		}
		Target->ClearColor = FLinearColor::Black;
		FWidgetRenderer Renderer(false, true);
		// Icons (vector brushes) first drawn here go into Slate's texture atlases, which only the application's own
		// window drawing updates: tick Slate between frames, or they come out as blank squares
		for (int32 Frame = 0; Frame < 12; ++Frame)
		{
			Renderer.DrawWidget(Target.Get(), Widget, Scale, Pixels, 1.0f / 30.0f);
			FSlateApplication::Get().Tick();
			FPlatformProcess::Sleep(0.02f);
		}

		// The renderer writes linear values (no gamma in its shader) and the read converts them to sRGB once, as the
		// editor's window shows them; with the gamma in the shader too the picture came out washed out (converted twice)
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		TArray<FColor> Colors;
		FReadSurfaceDataFlags Flags(RCM_UNorm);
		Flags.SetLinearToGamma(true);
		if (!Resource || !Resource->ReadPixels(Colors, Flags) || Colors.Num() != int32(Pixels.X) * int32(Pixels.Y))
		{
			Test.AddError(FString::Printf(TEXT("%s: the render target could not be read"), *Name));
			return false;
		}
		for (FColor& Color : Colors)
		{
			Color.A = 255;
		}
		const FImageView Image(Colors.GetData(), int32(Pixels.X), int32(Pixels.Y), EGammaSpace::sRGB);
		const FString Path = FPaths::Combine(Folder, Name + TEXT(".png"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		if (!FImageUtils::SaveImageByExtension(*Path, Image))
		{
			Test.AddError(FString::Printf(TEXT("%s: could not write %s"), *Name, *Path));
			return false;
		}
		Test.AddInfo(FString::Printf(TEXT("shot %s (%dx%d)"), *Path, Image.SizeX, Image.SizeY));
		return true;
	}

	/** The widget on the editor's panel background, so translucent edges look as they do in the editor */
	static TSharedRef<SWidget> OnPanel(const TSharedRef<SWidget>& Content, const FMargin Padding = FMargin(0.0f))
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
			.Padding(Padding)
			[
				Content
			];
	}

	/** The fictional cast every shot uses: two Blueprints of a sample game, four tagged graphs */
	static TArray<FN2CTaggedBlueprintGraph> SampleTags()
	{
		TArray<FN2CTaggedBlueprintGraph> Tags;
		auto Add = [&Tags](uint32 Seed, const TCHAR* Graph, const TCHAR* Blueprint, const TCHAR* Tag, const TCHAR* Category)
		{
			FN2CTaggedBlueprintGraph Tagged;
			Tagged.GraphGuid = FGuid(Seed, Seed + 1, Seed + 2, Seed + 3);
			Tagged.GraphName = Graph;
			Tagged.OwningBlueprint = FSoftObjectPath(Blueprint);
			Tagged.Tag = Tag;
			Tagged.Category = Category;
			Tags.Add(Tagged);
		};
		Add(0x1000, TEXT("Fire"), TEXT("/Game/Sample/BP_Rifle.BP_Rifle"), TEXT("Combat"), TEXT("Gameplay"));
		Add(0x1000, TEXT("Fire"), TEXT("/Game/Sample/BP_Rifle.BP_Rifle"), TEXT("Weapons"), TEXT("Gameplay"));
		Add(0x2000, TEXT("Reload"), TEXT("/Game/Sample/BP_Rifle.BP_Rifle"), TEXT("Combat"), TEXT("Gameplay"));
		Add(0x3000, TEXT("EventGraph"), TEXT("/Game/Sample/BP_Door.BP_Door"), TEXT("Interaction"), TEXT("Gameplay"));
		Add(0x4000, TEXT("Patrol"), TEXT("/Game/Sample/BP_Guard.BP_Guard"), TEXT("AI"), TEXT("Default"));
		return Tags;
	}

	/** A sample C++ translation of BP_Door's EventGraph */
	static FN2CGraphTranslation SampleTranslation()
	{
		FN2CGraphTranslation Translation;
		Translation.GraphName = TEXT("EventGraph");
		Translation.GraphType = TEXT("EventGraph");
		Translation.GraphClass = TEXT("BP_Door");
		Translation.Code.GraphDeclaration = TEXT("virtual void BeginPlay() override;\n\nUFUNCTION(BlueprintCallable, Category = \"Door\")\nvoid Open();\n\nUPROPERTY(EditAnywhere, Category = \"Door\")\nfloat OpenAngle = 90.0f;\n");
		Translation.Code.GraphImplementation = TEXT(
			"void ABP_Door::BeginPlay()\n"
			"{\n"
			"\tSuper::BeginPlay();\n"
			"\tUKismetSystemLibrary::PrintString(this, TEXT(\"Door ready\"));\n"
			"}\n"
			"\n"
			"void ABP_Door::Open()\n"
			"{\n"
			"\tif (bIsOpen)\n"
			"\t{\n"
			"\t\treturn;\n"
			"\t}\n"
			"\tbIsOpen = true;\n"
			"\tSetActorRotation(FRotator(0.0f, OpenAngle, 0.0f));\n"
			"}\n");
		Translation.Code.ImplementationNotes = TEXT(
			"BeginPlay calls Super::BeginPlay() first, as the Blueprint event does. Open returns early when the door is "
			"already open, matching the Branch on Is Open. Add #include \"Kismet/KismetSystemLibrary.h\" for PrintString.");
		return Translation;
	}

	/** The settings object's API key status rows, when one would show part of a saved key (never on screen) */
	static TArray<FString> KeyLeaks(const UObject* Settings)
	{
		TArray<FString> Leaks;
		for (TFieldIterator<FStrProperty> It(Settings->GetClass()); It; ++It)
		{
			const FString Name = It->GetName();
			const FString* Value = It->ContainerPtrToValuePtr<FString>(Settings);
			if (!Value || Value->IsEmpty())
			{
				continue;
			}
			if (Name.EndsWith(TEXT("_API_Key_UI")) || (Name.EndsWith(TEXT("_API_Key_Source")) && Value->Contains(TEXT("ends ..."))))
			{
				Leaks.Add(Name);
			}
		}
		return Leaks;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CDocsShotsMainWindow, "N2CDocs.Shots.main-window", N2C_TEST_FLAGS)

/** main-window: the Node to Code window's tag manager (a category selected) and its translation viewer */
bool FN2CDocsShotsMainWindow::RunTest(const FString& Parameters)
{
	FString Folder;
	if (!N2CDocsShots::ShouldCapture(*this, Folder))
	{
		return !HasAnyErrors();
	}

	const TArray<FN2CTaggedBlueprintGraph> Tags = N2CDocsShots::SampleTags();
	// The Output row shows the translations folder: a sample project's, not this machine's (in memory, never saved)
	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	const FString SavedOutput = Settings->CustomTranslationOutputDirectory.Path;
	Settings->CustomTranslationOutputDirectory.Path = TEXT("C:/Projects/SampleGame/Saved/NodeToCode/Translations");
	const TSharedRef<SN2CTagManager> Manager = SNew(SN2CTagManager)
		.ShowActionBar(true)
		.TagSource([Tags]() { return Tags; });
	Settings->CustomTranslationOutputDirectory.Path = SavedOutput;
	Manager->SelectCategory(TEXT("Gameplay"));
	N2CDocsShots::Shoot(*this, N2CDocsShots::OnPanel(Manager), FVector2D(880.0, 460.0), Folder, TEXT("main-window/tag-manager"));

	const TSharedRef<SN2CTranslationViewer> Viewer = SNew(SN2CTranslationViewer);
	Viewer->LoadTranslation(N2CDocsShots::SampleTranslation(), TEXT("EventGraph"), FString(), EN2CCodeLanguage::Cpp);
	N2CDocsShots::Shoot(*this, N2CDocsShots::OnPanel(Viewer), FVector2D(880.0, 560.0), Folder, TEXT("main-window/translation-viewer"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CDocsShotsTranslateEntry, "N2CDocs.Shots.translate-entry", N2C_TEST_FLAGS)

/** translate-entry: the overlay NodeToCode puts on a Blueprint graph tab (Copy JSON, tags, Translate), on a graph */
bool FN2CDocsShotsTranslateEntry::RunTest(const FString& Parameters)
{
	FString Folder;
	if (!N2CDocsShots::ShouldCapture(*this, Folder))
	{
		return !HasAnyErrors();
	}

	N2CTest::FTestBlueprint Blueprint(TEXT("BP_Door"));
	UEdGraph* Graph = Blueprint.EventGraph();
	if (!TestNotNull(TEXT("the sample Blueprint's event graph"), Graph))
	{
		return false;
	}
	const TSharedRef<SWidget> Overlay = SNew(SN2CGraphOverlay)
		.GraphGuid(Graph->GraphGuid)
		.BlueprintPath(Blueprint.GetBlueprint()->GetPathName())
		.GraphName(Graph->GetName())
		.Graph(Graph);
	const TSharedRef<SWidget> OnGraph = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush(TEXT("Graph.Panel.SolidBackground")))
		.Padding(FMargin(16.0f))
		[
			Overlay
		];
	OnGraph->SlatePrepass(N2CDocsShots::Scale);
	const FVector2D Size = OnGraph->GetDesiredSize();
	TestTrue(TEXT("the overlay has a size"), Size.X > 1.0 && Size.Y > 1.0);
	N2CDocsShots::Shoot(*this, OnGraph, FVector2D(FMath::CeilToDouble(Size.X), FMath::CeilToDouble(Size.Y)), Folder,
		TEXT("translate-entry/graph-overlay"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CDocsShotsSettings, "N2CDocs.Shots.settings", N2C_TEST_FLAGS)

/**
 * settings: NodeToCode's Project Settings page (Plugins > Node to Code) as its details panel shows it, one shot per
 * area (the provider choice with the Anthropic rows, and Code Generation). Refuses to capture when any API key
 * field holds text or a status row shows the end of a saved key.
 */
bool FN2CDocsShotsSettings::RunTest(const FString& Parameters)
{
	FString Folder;
	if (!N2CDocsShots::ShouldCapture(*this, Folder))
	{
		return !HasAnyErrors();
	}

	UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
	Settings->RefreshApiKeySources();
	const TArray<FString> Leaks = N2CDocsShots::KeyLeaks(Settings);
	if (Leaks.Num() > 0)
	{
		AddError(FString::Printf(TEXT("Not captured: %s would show (part of) a saved key. Run with no key saved for NodeToCode."),
			*FString::Join(Leaks, TEXT(", "))));
		return false;
	}

	// One shot per settings area the pages describe, each the details panel filtered to that area's categories
	struct FArea { const TCHAR* Name; TArray<FString> Categories; double Height; };
	const TArray<FArea> Areas = {
		{ TEXT("settings/llm-provider"), { TEXT("Node to Code | LLM Provider"), TEXT("Node to Code | LLM Services | Anthropic") }, 300.0 },
		{ TEXT("settings/code-generation"), { TEXT("Node to Code | Code Generation") }, 222.0 },
	};
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	for (const FArea& Area : Areas)
	{
		FDetailsViewArgs Args;
		Args.bAllowSearch = false;
		Args.bHideSelectionTip = true;
		Args.bShowOptions = false;
		Args.bShowPropertyMatrixButton = false;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		const TSharedRef<IDetailsView> Details = PropertyEditor.CreateDetailView(Args);
		const TArray<FString> Categories = Area.Categories;
		Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([Categories](const FPropertyAndParent& Item)
		{
			const FProperty* Top = Item.ParentProperties.Num() > 0 ? Item.ParentProperties.Last() : &Item.Property;
			return Categories.Contains(Top->GetMetaData(TEXT("Category")));
		}));
		Details->SetObject(Settings, true);
#if ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 7
		// Every category open, as the page shows it (UE 5.6 has no call for it; shots come from 5.8)
		Details->SetRootExpansionStates(true, true);
#endif
		N2CDocsShots::Shoot(*this, N2CDocsShots::OnPanel(Details), FVector2D(760.0, Area.Height), Folder, Area.Name);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
