// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CToolSpecHarness.h"
#include "Core/N2CSettings.h"
#include "Utils/N2CPathUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

/**
 * read-file and read-path as an agent uses them (card 0184): bounded reads in whole lines with truncated and
 * nextOffset, a text-only extension allowlist and binary sniffing, offset and limit, paged directory listings with a
 * cursor and a wildcard filter, project-relative paths, "." as the project folder, and the translation output
 * directory as a second root when it is outside the project. The path jail and deny-list themselves are
 * NodeToCode.PathPolicy's. Review ids: mcp-tools-contentbrowser-fs-19, mcp-tools-contentbrowser-fs-20,
 * imp-mcp-tools-contentbrowser-fs-3.
 */
BEGIN_DEFINE_SPEC(FN2CFileSystemToolsSpec, "NodeToCode.MCP.FileSystemTools", N2C_TEST_FLAGS)

	TUniquePtr<N2CTest::FScratchDir> Scratch;

	/** 500 KB, FN2CMcpReadFileTool::MaxContentBytes */
	static constexpr int32 MaxContentBytes = 500 * 1024;

	/** A scratch path relative to the project folder, as a client passes it */
	FString Rel(const FString& Absolute) const
	{
		FString Relative = FPaths::ConvertRelativePathToFull(Absolute);
		FPaths::MakePathRelativeTo(Relative, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
		return Relative;
	}

	FMcpToolCallResult Call(const TCHAR* Tool, const FString& Path, TFunction<void(FJsonObject&)> More = nullptr)
	{
		const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("relativePath"), Path);
		if (More)
		{
			More(*Args);
		}
		return N2CTest::RunToolUnmarked(Tool, Args);
	}

	TSharedPtr<FJsonObject> Json(const FMcpToolCallResult& Result)
	{
		TestFalse(FString::Printf(TEXT("the call succeeds: %s"), *N2CTest::ResultText(Result).Left(300)), Result.bIsError);
		const TSharedPtr<FJsonObject> Object = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
		TestTrue(TEXT("the result is a JSON object"), Object.IsValid());
		return Object.IsValid() ? Object : MakeShared<FJsonObject>();
	}

	static FString Lines(int32 First, int32 Last)
	{
		FString Text;
		for (int32 Line = First; Line <= Last; ++Line)
		{
			Text += FString::Printf(TEXT("line %d\n"), Line);
		}
		return Text;
	}

	static bool SaveBytes(const TArray<uint8>& Bytes, const FString& Path)
	{
		return FFileHelper::SaveArrayToFile(Bytes, *Path);
	}

	/** Sets the translation output directory for one case and puts it back */
	struct FScopedOutputDirectory
	{
		FString Saved;
		explicit FScopedOutputDirectory(const FString& Path)
		{
			UN2CSettings* Settings = GetMutableDefault<UN2CSettings>();
			Saved = Settings->CustomTranslationOutputDirectory.Path;
			Settings->CustomTranslationOutputDirectory.Path = Path;
		}
		~FScopedOutputDirectory()
		{
			GetMutableDefault<UN2CSettings>()->CustomTranslationOutputDirectory.Path = Saved;
		}
	};

END_DEFINE_SPEC(FN2CFileSystemToolsSpec)

void FN2CFileSystemToolsSpec::Define()
{
	BeforeEach([this]()
	{
		Scratch = MakeUnique<N2CTest::FScratchDir>(TEXT("N2CFileSystemTools"));
	});

	AfterEach([this]()
	{
		Scratch.Reset();
	});

	Describe(TEXT("read-file"), [this]()
	{
		It(TEXT("returns at most 500 KB of whole lines from a 2 MB log, with truncated and nextOffset"), [this]()
		{
			// 2 MB of 100-byte lines
			FString Line = FString::ChrN(99, TEXT('x')) + TEXT("\n");
			FString Text;
			Text.Reserve(2 * 1024 * 1024 + 100);
			while (Text.Len() < 2 * 1024 * 1024)
			{
				Text += Line;
			}
			const FString Path = *Scratch / TEXT("big.log");
			FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			const int64 FileSize = IFileManager::Get().FileSize(*Path);

			const TSharedPtr<FJsonObject> Result = Json(Call(TEXT("read-file"), Rel(Path)));
			FString Content;
			Result->TryGetStringField(TEXT("content"), Content);
			const int32 Bytes = FTCHARToUTF8(*Content).Length();
			TestTrue(FString::Printf(TEXT("content is at most 500 KB (%d bytes)"), Bytes), Bytes <= MaxContentBytes);
			TestTrue(TEXT("content is close to the cap"), Bytes > MaxContentBytes - 100);
			TestTrue(TEXT("content ends on a whole line"), Content.EndsWith(TEXT("\n")));
			TestTrue(TEXT("truncated is true"), Result->GetBoolField(TEXT("truncated")));
			TestEqual(TEXT("size is the file's size"), static_cast<int64>(Result->GetNumberField(TEXT("size"))), FileSize);
			const int32 LinesReturned = Bytes / 100;
			TestEqual(TEXT("lineCount"), static_cast<int32>(Result->GetNumberField(TEXT("lineCount"))), LinesReturned);
			TestEqual(TEXT("nextOffset follows the last line"), static_cast<int32>(Result->GetNumberField(TEXT("nextOffset"))), LinesReturned + 1);
			TestEqual(TEXT("totalLines"), static_cast<int64>(Result->GetNumberField(TEXT("totalLines"))), FileSize / 100);

			// The next part starts where the first stopped
			const TSharedPtr<FJsonObject> Next = Json(Call(TEXT("read-file"), Rel(Path), [LinesReturned](FJsonObject& Args)
			{
				Args.SetNumberField(TEXT("offset"), LinesReturned + 1);
			}));
			TestEqual(TEXT("the next part starts at nextOffset"), static_cast<int32>(Next->GetNumberField(TEXT("startLine"))), LinesReturned + 1);
		});

		It(TEXT("returns exactly the lines offset and limit ask for"), [this]()
		{
			const FString Path = *Scratch / TEXT("lines.txt");
			FFileHelper::SaveStringToFile(Lines(1, 100), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

			const TSharedPtr<FJsonObject> Middle = Json(Call(TEXT("read-file"), Rel(Path), [](FJsonObject& Args)
			{
				Args.SetNumberField(TEXT("offset"), 10);
				Args.SetNumberField(TEXT("limit"), 5);
			}));
			TestEqual(TEXT("lines 10 to 14"), Middle->GetStringField(TEXT("content")), Lines(10, 14));
			TestEqual(TEXT("startLine"), static_cast<int32>(Middle->GetNumberField(TEXT("startLine"))), 10);
			TestEqual(TEXT("endLine"), static_cast<int32>(Middle->GetNumberField(TEXT("endLine"))), 14);
			TestEqual(TEXT("lineCount"), static_cast<int32>(Middle->GetNumberField(TEXT("lineCount"))), 5);
			TestEqual(TEXT("nextOffset"), static_cast<int32>(Middle->GetNumberField(TEXT("nextOffset"))), 15);
			TestEqual(TEXT("totalLines"), static_cast<int32>(Middle->GetNumberField(TEXT("totalLines"))), 100);
			TestFalse(TEXT("not truncated"), Middle->GetBoolField(TEXT("truncated")));

			// Numeric strings work as well, and the last lines leave no nextOffset
			const TSharedPtr<FJsonObject> End = Json(Call(TEXT("read-file"), Rel(Path), [](FJsonObject& Args)
			{
				Args.SetStringField(TEXT("offset"), TEXT("98"));
				Args.SetStringField(TEXT("limit"), TEXT("5"));
			}));
			TestEqual(TEXT("lines 98 to 100"), End->GetStringField(TEXT("content")), Lines(98, 100));
			TestFalse(TEXT("no nextOffset at the end"), End->HasField(TEXT("nextOffset")));

			const TSharedPtr<FJsonObject> Whole = Json(Call(TEXT("read-file"), Rel(Path)));
			TestEqual(TEXT("no offset or limit reads the whole file"), Whole->GetStringField(TEXT("content")), Lines(1, 100));
			TestFalse(TEXT("no nextOffset for a whole file"), Whole->HasField(TEXT("nextOffset")));

			// A last line without a newline is a line
			const FString NoNewline = *Scratch / TEXT("nonewline.txt");
			FFileHelper::SaveStringToFile(TEXT("a\nb\nc"), *NoNewline, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			const TSharedPtr<FJsonObject> Last = Json(Call(TEXT("read-file"), Rel(NoNewline), [](FJsonObject& Args)
			{
				Args.SetNumberField(TEXT("offset"), 3);
			}));
			TestEqual(TEXT("the unterminated last line"), Last->GetStringField(TEXT("content")), FString(TEXT("c")));
			TestEqual(TEXT("three lines"), static_cast<int32>(Last->GetNumberField(TEXT("totalLines"))), 3);

			for (const TPair<FString, double>& Bad : TArray<TPair<FString, double>>{ { TEXT("offset"), -1.0 }, { TEXT("limit"), 0.0 }, { TEXT("limit"), 2.5 } })
			{
				const FMcpToolCallResult Refused = Call(TEXT("read-file"), Rel(Path), [Bad](FJsonObject& Args) { Args.SetNumberField(Bad.Key, Bad.Value); });
				TestTrue(FString::Printf(TEXT("%s %g is refused"), *Bad.Key, Bad.Value), Refused.bIsError);
			}
		});

		It(TEXT("refuses binary types by extension and names the type"), [this]()
		{
			for (const TCHAR* Extension : { TEXT("uexp"), TEXT("png"), TEXT("uasset"), TEXT("dll"), TEXT("pdb"), TEXT("ubulk") })
			{
				const FString Path = *Scratch / FString::Printf(TEXT("file.%s"), Extension);
				FFileHelper::SaveStringToFile(TEXT("not really binary"), *Path);
				const FMcpToolCallResult Result = Call(TEXT("read-file"), Rel(Path));
				TestTrue(FString::Printf(TEXT(".%s is an error"), Extension), Result.bIsError);
				TestTrue(FString::Printf(TEXT("the error names .%s: %s"), Extension, *N2CTest::ResultText(Result).Left(120)),
					N2CTest::ResultText(Result).Contains(FString::Printf(TEXT("Unsupported file type '.%s'"), Extension)));
			}
		});

		It(TEXT("refuses a text extension whose content is binary or not UTF-8"), [this]()
		{
			const FString Nul = *Scratch / TEXT("nul.txt");
			SaveBytes({ 'a', 'b', 0, 'c', '\n' }, Nul);
			const FMcpToolCallResult NulResult = Call(TEXT("read-file"), Rel(Nul));
			TestTrue(TEXT("a NUL byte is refused"), NulResult.bIsError && N2CTest::ResultText(NulResult).Contains(TEXT("binary")));

			const FString Latin1 = *Scratch / TEXT("latin1.txt");
			SaveBytes({ 'c', 'a', 'f', 0xE9, '\n' }, Latin1);
			const FMcpToolCallResult Latin1Result = Call(TEXT("read-file"), Rel(Latin1));
			TestTrue(TEXT("invalid UTF-8 is refused"), Latin1Result.bIsError && N2CTest::ResultText(Latin1Result).Contains(TEXT("UTF-8")));
		});

		It(TEXT("reads UTF-8 with a BOM, UTF-16 with a BOM and files with no extension"), [this]()
		{
			const FString Utf16 = *Scratch / TEXT("utf16.log");
			FFileHelper::SaveStringToFile(TEXT("café \U0001F600\nzwei\n"), *Utf16, FFileHelper::EEncodingOptions::ForceUnicode);
			const TSharedPtr<FJsonObject> Utf16Result = Json(Call(TEXT("read-file"), Rel(Utf16)));
			TestEqual(TEXT("UTF-16 content"), Utf16Result->GetStringField(TEXT("content")), FString(TEXT("café \U0001F600\nzwei\n")));
			TestEqual(TEXT("UTF-16 encoding"), Utf16Result->GetStringField(TEXT("encoding")), FString(TEXT("utf-16le")));

			const FString Bom = *Scratch / TEXT("bom.json");
			FFileHelper::SaveStringToFile(TEXT("{\"a\": \"é\"}"), *Bom, FFileHelper::EEncodingOptions::ForceUTF8);
			const TSharedPtr<FJsonObject> BomResult = Json(Call(TEXT("read-file"), Rel(Bom)));
			TestEqual(TEXT("BOM stripped"), BomResult->GetStringField(TEXT("content")), FString(TEXT("{\"a\": \"é\"}")));

			const FString Bare = *Scratch / TEXT("README");
			FFileHelper::SaveStringToFile(TEXT("plain\n"), *Bare, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			TestEqual(TEXT("no extension"), Json(Call(TEXT("read-file"), Rel(Bare)))->GetStringField(TEXT("content")), FString(TEXT("plain\n")));
		});

		It(TEXT("returns the start of a single line longer than 500 KB"), [this]()
		{
			const FString Path = *Scratch / TEXT("oneline.json");
			FFileHelper::SaveStringToFile(FString::ChrN(MaxContentBytes + 1000, TEXT('y')) + TEXT("\nnext\n"), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			const TSharedPtr<FJsonObject> Result = Json(Call(TEXT("read-file"), Rel(Path)));
			TestEqual(TEXT("exactly the cap"), Result->GetStringField(TEXT("content")).Len(), MaxContentBytes);
			TestTrue(TEXT("truncated"), Result->GetBoolField(TEXT("truncated")));
			TestTrue(TEXT("a note says the line was cut"), Result->HasField(TEXT("note")));
			TestEqual(TEXT("nextOffset is the next line"), static_cast<int32>(Result->GetNumberField(TEXT("nextOffset"))), 2);
		});

		It(TEXT("reports the path relative to the project and refuses a directory"), [this]()
		{
			const FString Path = *Scratch / TEXT("Sub/rel.txt");
			FFileHelper::SaveStringToFile(TEXT("x"), *Path);
			const FString Relative = Rel(Path);
			TestEqual(TEXT("path is project-relative"), Json(Call(TEXT("read-file"), Relative.Replace(TEXT("/"), TEXT("\\"))))->GetStringField(TEXT("path")), Relative);
			TestFalse(TEXT("no absolutePath"), Json(Call(TEXT("read-file"), Relative))->HasField(TEXT("absolutePath")));

			// An absolute path inside the project names the same file
			TestEqual(TEXT("absolute path inside the project"), Json(Call(TEXT("read-file"), FPaths::ConvertRelativePathToFull(Path)))->GetStringField(TEXT("path")), Relative);

			const FMcpToolCallResult Directory = Call(TEXT("read-file"), Rel(*Scratch / TEXT("Sub")));
			TestTrue(TEXT("a directory is an error that points at read-path"), Directory.bIsError && N2CTest::ResultText(Directory).Contains(TEXT("read-path")));
		});
	});

	Describe(TEXT("read-path"), [this]()
	{
		It(TEXT("pages a folder of 1,200 files with a total and a cursor"), [this]()
		{
			const FString Folder = *Scratch / TEXT("Many");
			IFileManager::Get().MakeDirectory(*(Folder / TEXT("SubA")), true);
			IFileManager::Get().MakeDirectory(*(Folder / TEXT("SubB")), true);
			for (int32 Index = 0; Index < 1200; ++Index)
			{
				FFileHelper::SaveStringToFile(TEXT(""), *(Folder / FString::Printf(TEXT("f%04d.%s"), Index, Index % 4 == 0 ? TEXT("json") : TEXT("txt"))));
			}

			const TSharedPtr<FJsonObject> First = Json(Call(TEXT("read-path"), Rel(Folder)));
			TestEqual(TEXT("total"), static_cast<int32>(First->GetNumberField(TEXT("total"))), 1202);
			TestEqual(TEXT("fileCount"), static_cast<int32>(First->GetNumberField(TEXT("fileCount"))), 1200);
			TestEqual(TEXT("directoryCount"), static_cast<int32>(First->GetNumberField(TEXT("directoryCount"))), 2);
			TestEqual(TEXT("the default page is 200 entries"), First->GetArrayField(TEXT("files")).Num() + First->GetArrayField(TEXT("directories")).Num(), 200);
			TestEqual(TEXT("folders come first"), First->GetArrayField(TEXT("directories")).Num(), 2);
			TestTrue(TEXT("truncated"), First->GetBoolField(TEXT("truncated")));
			TestEqual(TEXT("nextCursor"), First->GetStringField(TEXT("nextCursor")), FString(TEXT("200")));
			TestEqual(TEXT("path is project-relative"), First->GetStringField(TEXT("path")), Rel(Folder));

			// Following the cursor visits every entry once
			TSet<FString> Seen;
			FString Cursor;
			int32 Pages = 0;
			do
			{
				const TSharedPtr<FJsonObject> Page = Json(Call(TEXT("read-path"), Rel(Folder), [Cursor](FJsonObject& Args)
				{
					Args.SetNumberField(TEXT("pageSize"), 500);
					if (!Cursor.IsEmpty())
					{
						Args.SetStringField(TEXT("cursor"), Cursor);
					}
				}));
				for (const TCHAR* Field : { TEXT("directories"), TEXT("files") })
				{
					for (const TSharedPtr<FJsonValue>& Name : Page->GetArrayField(Field))
					{
						Seen.Add(Name->AsString());
					}
				}
				Cursor = Page->HasField(TEXT("nextCursor")) ? Page->GetStringField(TEXT("nextCursor")) : FString();
				TestEqual(TEXT("truncated matches nextCursor"), Page->GetBoolField(TEXT("truncated")), !Cursor.IsEmpty());
			}
			while (!Cursor.IsEmpty() && ++Pages < 10);
			TestEqual(TEXT("every entry once"), Seen.Num(), 1202);
			TestEqual(TEXT("three pages of up to 500 (two cursors)"), Pages, 2);

			const TSharedPtr<FJsonObject> Json1 = Json(Call(TEXT("read-path"), Rel(Folder), [](FJsonObject& Args)
			{
				Args.SetStringField(TEXT("filter"), TEXT("*.JSON"));
				Args.SetNumberField(TEXT("pageSize"), 1000);
			}));
			TestEqual(TEXT("the filter keeps the .json files, ignoring case"), static_cast<int32>(Json1->GetNumberField(TEXT("total"))), 300);
			TestFalse(TEXT("one page holds them"), Json1->GetBoolField(TEXT("truncated")));

			for (const TCHAR* BadCursor : { TEXT("abc"), TEXT("-5"), TEXT("99999") })
			{
				const FMcpToolCallResult Refused = Call(TEXT("read-path"), Rel(Folder), [BadCursor](FJsonObject& Args) { Args.SetStringField(TEXT("cursor"), BadCursor); });
				TestTrue(FString::Printf(TEXT("cursor '%s' is refused"), BadCursor), Refused.bIsError);
			}
			for (const double BadSize : { 0.0, 1001.0 })
			{
				const FMcpToolCallResult Refused = Call(TEXT("read-path"), Rel(Folder), [BadSize](FJsonObject& Args) { Args.SetNumberField(TEXT("pageSize"), BadSize); });
				TestTrue(FString::Printf(TEXT("pageSize %g is refused"), BadSize), Refused.bIsError);
			}
		});

		It(TEXT("lists the project folder for \"\", \".\", \"./\" and \"/\""), [this]()
		{
			for (const TCHAR* Root : { TEXT(""), TEXT("."), TEXT("./"), TEXT("/") })
			{
				const FMcpToolCallResult Result = Call(TEXT("read-path"), Root);
				TestFalse(FString::Printf(TEXT("'%s' succeeds: %s"), Root, *N2CTest::ResultText(Result).Left(200)), Result.bIsError);
				const TSharedPtr<FJsonObject> Object = N2CTest::ParseJsonObject(N2CTest::ResultText(Result));
				TestTrue(FString::Printf(TEXT("'%s' is reported as \"\""), Root), Object.IsValid() && Object->GetStringField(TEXT("path")).IsEmpty());
			}
		});
	});

	Describe(TEXT("the translation output directory"), [this]()
	{
		It(TEXT("is readable when it is outside the project, credentials and other absolute paths still refused"), [this]()
		{
			const FString Outside = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::UserTempDir(),
				FString::Printf(TEXT("N2CTranslations_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			const FString Sibling = Outside + TEXT("_Sibling");
			ON_SCOPE_EXIT
			{
				IFileManager::Get().DeleteDirectory(*Outside, false, true);
				IFileManager::Get().DeleteDirectory(*Sibling, false, true);
			};
			FFileHelper::SaveStringToFile(TEXT("void Run();\n"), *(Outside / TEXT("BP_Player_2026-01-15-10.30.00/EventGraph/EventGraph.h")));
			FFileHelper::SaveStringToFile(TEXT("{}"), *(Outside / TEXT("secrets.json")));
			FFileHelper::SaveStringToFile(TEXT("other"), *(Sibling / TEXT("other.txt")));
			TestFalse(TEXT("the folder is outside the project"),
				FPaths::IsUnderDirectory(Outside, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())));

			const FString File = Outside / TEXT("BP_Player_2026-01-15-10.30.00/EventGraph/EventGraph.h");
			{
				AddExpectedErrorPlain(TEXT("Blocked MCP"), EAutomationExpectedErrorFlags::Contains, 0);
				const FScopedOutputDirectory Output(Outside);
				const TSharedPtr<FJsonObject> Read = Json(Call(TEXT("read-file"), File));
				TestEqual(TEXT("the translation is read"), Read->GetStringField(TEXT("content")), FString(TEXT("void Run();\n")));
				TestTrue(TEXT("its path is absolute (outside the project)"), Read->GetStringField(TEXT("path")).EndsWith(TEXT("EventGraph/EventGraph.h")));

				const TSharedPtr<FJsonObject> Listed = Json(Call(TEXT("read-path"), Outside));
				TestEqual(TEXT("the root lists its translation folder"), Listed->GetArrayField(TEXT("directories")).Num(), 1);

				const FMcpToolCallResult Secrets = Call(TEXT("read-file"), Outside / TEXT("secrets.json"));
				TestTrue(TEXT("secrets.json in the translation root is refused"), Secrets.bIsError && N2CTest::ResultText(Secrets).Contains(TEXT("credentials")));

				const FMcpToolCallResult Other = Call(TEXT("read-file"), Sibling / TEXT("other.txt"));
				TestTrue(TEXT("a sibling folder is refused"), Other.bIsError && N2CTest::ResultText(Other).Contains(TEXT("absolute path")));

				const FMcpToolCallResult Climb = Call(TEXT("read-file"), Outside / TEXT("../") + FPaths::GetCleanFilename(Sibling) + TEXT("/other.txt"));
				TestTrue(TEXT("climbing out of the root is refused"), Climb.bIsError);
			}

			// Without the setting the same absolute path is refused
			const FMcpToolCallResult Unset = Call(TEXT("read-file"), File);
			TestTrue(TEXT("refused when it is not the output directory"), Unset.bIsError);
		});

		It(TEXT("is not a root when it is a drive root or holds the project"), [this]()
		{
			AddExpectedErrorPlain(TEXT("too broad to be an MCP read root"), EAutomationExpectedErrorFlags::Contains, 0);
			AddExpectedErrorPlain(TEXT("Blocked MCP read of an absolute path"), EAutomationExpectedErrorFlags::Contains, 0);
			const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			const FString Parent = FPaths::GetPath(Project.LeftChop(1));
			for (const FString& Broad : { Parent, Project.Left(3) })
			{
				const FScopedOutputDirectory Output(Broad);
				TestEqual(FString::Printf(TEXT("'%s' adds no root"), *Broad), FN2CPathUtils::GetAgentReadRoots().Num(), 1);
			}
			{
				const FScopedOutputDirectory Output(Parent);
				const FMcpToolCallResult Result = Call(TEXT("read-path"), Parent);
				TestTrue(TEXT("the project's parent is not listable"), Result.bIsError);
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
