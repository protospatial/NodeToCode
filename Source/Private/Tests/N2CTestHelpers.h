// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "MCP/Tools/IN2CMcpTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "MCP/Tools/N2CMcpToolTypes.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

/**
 * Shared helpers for the NodeToCode automation specs. See README.md in this folder for naming and layout.
 */

/** Test flags every NodeToCode spec uses: editor context (the module is editor-only), run under the Engine filter. */
#define N2C_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace N2CTest
{
	/**
	 * A unique folder under <Project>/Saved/Automation/<Prefix>_<guid> for one case's files. Deleted (with
	 * everything in it) when the object goes out of scope; call Remove() earlier to delete links first.
	 */
	struct FScratchDir
	{
		FString Path;

		explicit FScratchDir(const TCHAR* Prefix)
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation")
				/ FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			IFileManager::Get().MakeDirectory(*Path, true);
		}

		~FScratchDir() { Remove(); }

		FString operator/(const FString& Relative) const { return Path / Relative; }

		void Remove()
		{
			if (!Path.IsEmpty())
			{
				IFileManager::Get().DeleteDirectory(*Path, false, true);
			}
		}
	};

	/** Folder for checked-in input files: Source/Private/Tests/Fixtures/<Area>. */
	inline FString FixturesDir(const FString& Area)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
		return Plugin.IsValid()
			? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Source/Private/Tests/Fixtures") / Area)
			: FString();
	}

	/** Folder for checked-in expected outputs: Source/Private/Tests/Goldens/<Area>. */
	inline FString GoldensDir(const FString& Area)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"));
		return Plugin.IsValid()
			? FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Source/Private/Tests/Goldens") / Area)
			: FString();
	}

	/** Every MCP tool compiled into the module, whatever the active registration mode. */
	inline TArray<TSharedPtr<IN2CMcpTool>> AllRegisteredTools()
	{
		TArray<TSharedPtr<IN2CMcpTool>> Tools;
		for (const TSharedPtr<IN2CMcpTool>& Tool : FN2CMcpToolRegistry::Get().GetTools())
		{
			if (Tool.IsValid())
			{
				Tools.Add(Tool);
			}
		}
		return Tools;
	}

	/** The registered instance of the tool with this MCP name, or null. */
	inline TSharedPtr<IN2CMcpTool> FindRegisteredTool(const FString& Name)
	{
		for (const TSharedPtr<IN2CMcpTool>& Tool : AllRegisteredTools())
		{
			if (Tool->GetDefinition().Name == Name)
			{
				return Tool;
			}
		}
		return nullptr;
	}

	/** Runs a registered tool with string arguments (on the calling thread when that is the game thread). */
	inline FMcpToolCallResult CallTool(const FString& Name, const TMap<FString, FString>& StringArgs)
	{
		const TSharedPtr<IN2CMcpTool> Tool = FindRegisteredTool(Name);
		if (!Tool.IsValid())
		{
			return FMcpToolCallResult::CreateErrorResult(FString::Printf(TEXT("N2CTest: no registered tool '%s'"), *Name));
		}
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Pair : StringArgs)
		{
			Args->SetStringField(Pair.Key, Pair.Value);
		}
		return Tool->Execute(Args);
	}

	/** All text content of a tool result, joined with newlines. */
	inline FString ResultText(const FMcpToolCallResult& Result)
	{
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonObject>& Item : Result.Content)
		{
			FString Text;
			if (Item.IsValid() && Item->TryGetStringField(TEXT("text"), Text))
			{
				Parts.Add(Text);
			}
		}
		return FString::Join(Parts, TEXT("\n"));
	}

	/** Parses a JSON object; null when the text is not one. */
	inline TSharedPtr<FJsonObject> ParseJsonObject(const FString& Text)
	{
		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Json);
		return Json;
	}

	/**
	 * Reads a private member of a product class without changing the product code (explicit template instantiation
	 * is exempt from access checks). Declare the accessor at global scope with N2C_TEST_PRIVATE_MEMBER, then call
	 * N2CTest::GetPrivateMember(N2CTest::<Tag>()) to get the member pointer.
	 */
	template <typename TTag, typename TTag::FType Member>
	struct TPrivateMemberAccess
	{
		friend typename TTag::FType GetPrivateMember(TTag) { return Member; }
	};
}

/**
 * N2C_TEST_PRIVATE_MEMBER(FRenameValidateNewName, bool (FN2CMcpRenameFolderTool::*)(const FString&, FString&) const,
 *     &FN2CMcpRenameFolderTool::ValidateNewName)
 * Use at global scope in a .cpp, with a tag name unique across the module (unity builds merge files).
 */
#define N2C_TEST_PRIVATE_MEMBER(Tag, MemberPointerType, MemberPointer) \
	namespace N2CTest \
	{ \
		struct Tag \
		{ \
			using FType = MemberPointerType; \
			friend FType GetPrivateMember(Tag); \
		}; \
		Tag::FType GetPrivateMember(Tag); \
	} \
	template struct N2CTest::TPrivateMemberAccess<N2CTest::Tag, MemberPointer>;

#endif // WITH_DEV_AUTOMATION_TESTS
