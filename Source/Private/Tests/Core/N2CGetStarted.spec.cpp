// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/N2CUserSettings.h"
#include "Core/Services/N2CConnectionTest.h"
#include "Core/Widgets/SN2CGetStartedCard.h"
#include "Core/Widgets/SN2CTagCategoryTree.h"
#include "Core/Widgets/SN2CTagManager.h"
#include "Core/Widgets/SN2CTaggedGraphsList.h"
#include "Core/Widgets/SN2CTranslationViewer.h"
#include "Models/N2CTaggedBlueprintGraph.h"
#include "Models/N2CTranslation.h"
#include "Tests/N2CTestHelpers.h"
#include "Widgets/SToolTip.h"

N2C_TEST_PRIVATE_MEMBER(FN2CGetStartedList, TSharedPtr<SN2CTaggedGraphsList> SN2CTagManager::*, &SN2CTagManager::GraphsList)
N2C_TEST_PRIVATE_MEMBER(FN2CGetStartedTree, TSharedPtr<SN2CTagCategoryTree> SN2CTagManager::*, &SN2CTagManager::CategoryTree)
N2C_TEST_PRIVATE_MEMBER(FN2CGetStartedBatch, TSharedPtr<SWidget> SN2CTagManager::*, &SN2CTagManager::BatchTranslateButton)
N2C_TEST_PRIVATE_MEMBER(FN2CGetStartedExport, TSharedPtr<SWidget> SN2CTagManager::*, &SN2CTagManager::ExportJsonButton)
N2C_TEST_PRIVATE_MEMBER(FN2CGetStartedRemove, TSharedPtr<SWidget> SN2CTagManager::*, &SN2CTagManager::RemoveSelectedButton)

/**
 * First run and empty states of the Node to Code window (card 0269), on headless widgets fed from in-memory tags.
 * - The Get Started card shows while its setting is on and the project has no saved translation; closing it turns
 *   the setting off. Its connection test reads a provider's answer as success, or as the provider's error.
 * - The tag tree says how to tag a first graph when there are no tags, and that nothing matches a search; the empty
 *   viewer says how to get a translation instead of showing a blank editor.
 * - Batch Translate, Export JSON and Remove Selected are disabled, with a tooltip saying why, while no graph is checked.
 */
BEGIN_DEFINE_SPEC(FN2CGetStartedSpec, "NodeToCode.UI.GetStarted", N2C_TEST_FLAGS)

	TSharedPtr<TArray<FN2CTaggedBlueprintGraph>> Tags;

	void AddTag(const FGuid& Guid, const TCHAR* GraphName, const TCHAR* Tag, const TCHAR* Category)
	{
		FN2CTaggedBlueprintGraph Tagged;
		Tagged.GraphGuid = Guid;
		Tagged.GraphName = GraphName;
		Tagged.OwningBlueprint = FSoftObjectPath(TEXT("/Game/N2CTestUI/BP_Rifle.BP_Rifle"));
		Tagged.Tag = Tag;
		Tagged.Category = Category;
		Tags->Add(Tagged);
	}

	TSharedRef<SN2CTagManager> MakeManager(bool bActionBar) const
	{
		const TSharedPtr<TArray<FN2CTaggedBlueprintGraph>> Source = Tags;
		return SNew(SN2CTagManager)
			.ShowActionBar(bActionBar)
			.TagSource([Source]() { return *Source; });
	}

	/** The SButton inside an SN2CIconButton (the icon button forwards IsEnabled and the tooltip to it) */
	static TSharedPtr<SWidget> FindButton(const TSharedPtr<SWidget>& Root)
	{
		if (!Root.IsValid())
		{
			return nullptr;
		}
		if (Root->GetType() == FName(TEXT("SButton")))
		{
			return Root;
		}
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindButton(Children->GetChildAt(Index)))
			{
				return Found;
			}
		}
		return nullptr;
	}

	static FString TooltipOf(const TSharedPtr<SWidget>& Button)
	{
		const TSharedPtr<IToolTip> ToolTip = Button.IsValid() ? Button->GetToolTip() : nullptr;
		if (!ToolTip.IsValid())
		{
			return FString();
		}
		const TSharedRef<SWidget> Content = ToolTip->AsWidget();
		// SToolTip's text is in its content; the simple tooltip widget exposes it through GetTextTooltip
		return Content->GetType() == FName(TEXT("SToolTip")) ? StaticCastSharedRef<SToolTip>(Content)->GetTextTooltip().ToString() : FString();
	}

END_DEFINE_SPEC(FN2CGetStartedSpec)

void FN2CGetStartedSpec::Define()
{
	BeforeEach([this]()
	{
		Tags = MakeShared<TArray<FN2CTaggedBlueprintGraph>>();
	});

	AfterEach([this]()
	{
		Tags.Reset();
	});

	Describe(TEXT("the Get Started card"), [this]()
	{
		It(TEXT("shows only while its setting is on and nothing is translated yet"), [this]()
		{
			TestTrue(TEXT("setting on, no saved translation"), SN2CGetStartedCard::ShouldShow(true, 0));
			TestFalse(TEXT("setting on, a saved translation"), SN2CGetStartedCard::ShouldShow(true, 1));
			TestFalse(TEXT("setting off"), SN2CGetStartedCard::ShouldShow(false, 0));
		});

		It(TEXT("turns its setting off when closed, and builds without a window"), [this]()
		{
			UN2CUserSettings* UserSettings = GetMutableDefault<UN2CUserSettings>();
			const bool bWas = UserSettings->bShowGetStartedCard;
			UserSettings->bShowGetStartedCard = true;
			const TSharedRef<SN2CGetStartedCard> Card = SNew(SN2CGetStartedCard);
			SN2CGetStartedCard::Dismiss(false);
			TestFalse(TEXT("the setting is off"), UserSettings->bShowGetStartedCard);
			TestFalse(TEXT("the card no longer shows"), Card->IsShown());
			UserSettings->bShowGetStartedCard = bWas;
		});

		It(TEXT("offers every provider, and needs no key for the local ones"), [this]()
		{
			TestEqual(TEXT("six providers"), SN2CGetStartedCard::GetProviders().Num(), 6);
			TestTrue(TEXT("Ollama is local"), SN2CGetStartedCard::IsLocalProvider(EN2CLLMProvider::Ollama));
			TestTrue(TEXT("LM Studio is local"), SN2CGetStartedCard::IsLocalProvider(EN2CLLMProvider::LMStudio));
			TestFalse(TEXT("Anthropic takes a key"), SN2CGetStartedCard::IsLocalProvider(EN2CLLMProvider::Anthropic));
		});

		It(TEXT("reads the connection test's answer: a reply is success, an error is the provider's message"), [this]()
		{
			const FN2CConnectionTestResult Ok = FN2CConnectionTest::InterpretResponse(
				TEXT("{\"content\":[{\"type\":\"text\",\"text\":\"OK\"}]}"), EN2CLLMProvider::Anthropic, TEXT("claude-test"));
			TestTrue(TEXT("a reply is success"), Ok.bOk);
			TestTrue(TEXT("it names the provider and model"), Ok.Message.Contains(TEXT("Anthropic")) && Ok.Message.Contains(TEXT("claude-test")));

			const FN2CConnectionTestResult Auth = FN2CConnectionTest::InterpretResponse(
				TEXT("{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"},\"http_status\":401}"),
				EN2CLLMProvider::Anthropic, TEXT("claude-test"));
			TestFalse(TEXT("a provider error fails"), Auth.bOk);
			TestTrue(TEXT("with the provider's message"), Auth.Message.Contains(TEXT("invalid x-api-key")));

			const FN2CConnectionTestResult NoKey = FN2CConnectionTest::InterpretResponse(
				FN2CTranslationError::MakeErrorJson(TEXT("No API key for OpenAI")), EN2CLLMProvider::OpenAI, FString());
			TestFalse(TEXT("NodeToCode's own error fails"), NoKey.bOk);
			TestTrue(TEXT("with its message"), NoKey.Message.Contains(TEXT("No API key")));

			TestFalse(TEXT("an answer that is not JSON fails"),
				FN2CConnectionTest::InterpretResponse(TEXT("<html>proxy</html>"), EN2CLLMProvider::Gemini, FString()).bOk);
		});
	});

	Describe(TEXT("empty states"), [this]()
	{
		It(TEXT("the tag tree says how to tag a first graph when there are no tags"), [this]()
		{
			const TSharedRef<SN2CTagManager> Manager = MakeManager(false);
			const SN2CTagCategoryTree& Tree = *((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CGetStartedTree()));
			const FString Text = Tree.GetEmptyText().ToString();
			TestTrue(TEXT("no tags yet"), Text.Contains(TEXT("No tagged graphs yet")));
			TestTrue(TEXT("points at the N2C bar's tag button"), Text.Contains(TEXT("tag button")));
		});

		It(TEXT("the tag tree says nothing matches a search, and is silent when it has rows"), [this]()
		{
			AddTag(FGuid::NewGuid(), TEXT("Fire"), TEXT("Combat"), TEXT("Gameplay"));
			const TSharedRef<SN2CTagManager> Manager = MakeManager(false);
			SN2CTagCategoryTree& Tree = *((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CGetStartedTree()));
			TestTrue(TEXT("rows: no empty text"), Tree.GetEmptyText().IsEmpty());
			Manager->SetSearchFilter(TEXT("zzzz"));
			TestTrue(TEXT("no match"), Tree.GetEmptyText().ToString().Contains(TEXT("zzzz")));
		});

		It(TEXT("the viewer with nothing loaded shows guidance, not a blank editor"), [this]()
		{
			const TSharedRef<SN2CTranslationViewer> Viewer = SNew(SN2CTranslationViewer);
			TestTrue(TEXT("empty state shown"), Viewer->IsShowingEmptyState());
			TestTrue(TEXT("it says how to get a translation"), SN2CTranslationViewer::GetEmptyText().ToString().Contains(TEXT("Translate")));
		});
	});

	Describe(TEXT("selection actions"), [this]()
	{
		It(TEXT("Batch Translate, Export JSON and Remove Selected are disabled with a reason while nothing is checked"), [this]()
		{
			AddTag(FGuid::NewGuid(), TEXT("Fire"), TEXT("Combat"), TEXT("Gameplay"));
			AddTag(FGuid::NewGuid(), TEXT("Reload"), TEXT("Combat"), TEXT("Gameplay"));
			const TSharedRef<SN2CTagManager> Manager = MakeManager(true);
			Manager->SelectCategory(TEXT("Gameplay"));

			const TSharedPtr<SWidget> Buttons[] = {
				FindButton((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CGetStartedBatch())),
				FindButton((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CGetStartedExport())),
				FindButton((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CGetStartedRemove())) };
			const TCHAR* Names[] = { TEXT("Batch Translate"), TEXT("Export JSON"), TEXT("Remove Selected") };

			// Bound Slate attributes (IsEnabled) are read during the prepass, as each frame does
			Manager->SlatePrepass(1.0f);
			TestEqual(TEXT("nothing checked"), Manager->GetSelectedCount(), 0);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (!TestTrue(FString::Printf(TEXT("%s has its button"), Names[Index]), Buttons[Index].IsValid()))
				{
					return;
				}
				TestFalse(FString::Printf(TEXT("%s disabled"), Names[Index]), Buttons[Index]->IsEnabled());
				TestTrue(FString::Printf(TEXT("%s says to check graphs"), Names[Index]), TooltipOf(Buttons[Index]).Contains(TEXT("Check one or more graphs")));
			}

			SN2CTaggedGraphsList& List = *((*Manager).*N2CTest::GetPrivateMember(N2CTest::FN2CGetStartedList()));
			List.SelectAll();
			Manager->SlatePrepass(1.0f);
			TestEqual(TEXT("two checked"), Manager->GetSelectedCount(), 2);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				TestTrue(FString::Printf(TEXT("%s enabled"), Names[Index]), Buttons[Index]->IsEnabled());
				TestFalse(FString::Printf(TEXT("%s now says what it does"), Names[Index]), TooltipOf(Buttons[Index]).Contains(TEXT("Check one or more graphs")));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
