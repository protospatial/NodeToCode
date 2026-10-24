// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/N2CTestHelpers.h"
#include "MCP/Tools/Implementations/ContentBrowser/N2CMcpCreateFolderTool.h"
#include "MCP/Tools/Implementations/ContentBrowser/N2CMcpRenameFolderTool.h"

// The validators are private members of the tools; reach them without changing product code
N2C_TEST_PRIVATE_MEMBER(FN2CNamesRenameValidateNewName,
	bool (FN2CMcpRenameFolderTool::*)(const FString&, FString&) const, &FN2CMcpRenameFolderTool::ValidateNewName)
N2C_TEST_PRIVATE_MEMBER(FN2CNamesCreateValidateFolderPath,
	bool (FN2CMcpCreateFolderTool::*)(const FString&, FString&, FString&) const, &FN2CMcpCreateFolderTool::ValidateFolderPath)

/**
 * The reserved-name rules of create-folder and rename-folder. Both must refuse the Windows device names (CON, PRN,
 * AUX, NUL, COM1-9, LPT1-9) in any case, and both must accept ordinary folder names that merely start with those
 * letters (Components, Common, Combat, Lpthing). Review id: mcp-tools-contentbrowser-fs-11.
 */
BEGIN_DEFINE_SPEC(FN2CNameValidationSpec, "NodeToCode.Names", N2C_TEST_FLAGS)

	FN2CMcpRenameFolderTool RenameTool;
	FN2CMcpCreateFolderTool CreateTool;

	/** rename-folder's check of the new name */
	bool RenameAccepts(const FString& Name, FString& OutError) const
	{
		return (RenameTool.*N2CTest::GetPrivateMember(N2CTest::FN2CNamesRenameValidateNewName()))(Name, OutError);
	}

	/** create-folder's check of a /Game/<Name> path */
	bool CreateAccepts(const FString& Name, FString& OutError) const
	{
		FString Normalized;
		return (CreateTool.*N2CTest::GetPrivateMember(N2CTest::FN2CNamesCreateValidateFolderPath()))(
			TEXT("/Game/N2CNamesTest/") + Name, Normalized, OutError);
	}

	static TArray<FString> DeviceNames()
	{
		TArray<FString> Names = { TEXT("CON"), TEXT("PRN"), TEXT("AUX"), TEXT("NUL"), TEXT("con"), TEXT("Nul") };
		for (int32 Digit = 1; Digit <= 9; ++Digit)
		{
			Names.Add(FString::Printf(TEXT("COM%d"), Digit));
			Names.Add(FString::Printf(TEXT("LPT%d"), Digit));
		}
		Names.Add(TEXT("com1"));
		Names.Add(TEXT("Lpt9"));
		return Names;
	}

	static TArray<FString> OrdinaryNames()
	{
		return { TEXT("Components"), TEXT("Common"), TEXT("Combat"), TEXT("Compute"), TEXT("Comms"), TEXT("COMMON"),
			TEXT("Lpthing"), TEXT("Console"), TEXT("Printers"), TEXT("Auxiliary"), TEXT("Nullable"), TEXT("COM10"),
			TEXT("Blueprints"), TEXT("My_Folder") };
	}

END_DEFINE_SPEC(FN2CNameValidationSpec)

void FN2CNameValidationSpec::Define()
{
	Describe(TEXT("rename-folder"), [this]()
	{
		It(TEXT("refuses every Windows device name in any case"), [this]()
		{
			for (const FString& Name : DeviceNames())
			{
				FString Error;
				TestFalse(FString::Printf(TEXT("'%s' refused"), *Name), RenameAccepts(Name, Error));
				TestTrue(FString::Printf(TEXT("'%s' refused as reserved"), *Name), Error.Contains(TEXT("reserved")));
			}
		});

		// Known defect mcp-tools-contentbrowser-fs-11: ValidateNewName refuses any name that starts with COM or LPT.
		// Enable (xIt -> It) with the fix.
		xIt(TEXT("accepts ordinary names that start with a device name's letters"), [this]()
		{
			for (const FString& Name : OrdinaryNames())
			{
				FString Error;
				TestTrue(FString::Printf(TEXT("'%s' accepted (%s)"), *Name, *Error), RenameAccepts(Name, Error));
			}
		});

		It(TEXT("refuses empty names, separators, dots and wildcards"), [this]()
		{
			for (const TCHAR* Name : { TEXT(""), TEXT("a/b"), TEXT("a\\b"), TEXT(".."), TEXT("a.b"), TEXT("a b"), TEXT("a*"), TEXT("a?"), TEXT("~a") })
			{
				FString Error;
				TestFalse(FString::Printf(TEXT("'%s' refused"), Name), RenameAccepts(Name, Error));
			}
		});
	});

	Describe(TEXT("create-folder"), [this]()
	{
		It(TEXT("refuses CON, PRN, AUX and NUL in any case"), [this]()
		{
			for (const TCHAR* Name : { TEXT("CON"), TEXT("PRN"), TEXT("AUX"), TEXT("NUL"), TEXT("con"), TEXT("Nul") })
			{
				FString Error;
				TestFalse(FString::Printf(TEXT("'%s' refused"), Name), CreateAccepts(Name, Error));
				TestTrue(FString::Printf(TEXT("'%s' refused as reserved"), Name), Error.Contains(TEXT("reserved")));
			}
		});

		// Known defect mcp-tools-contentbrowser-fs-11: create-folder checks only CON, PRN, AUX and NUL, so COM1-9 and
		// LPT1-9 get through and the two tools disagree. Enable (xIt -> It) with the shared validator.
		xIt(TEXT("refuses COM1-9 and LPT1-9 like rename-folder does"), [this]()
		{
			for (const FString& Name : DeviceNames())
			{
				FString Error;
				TestFalse(FString::Printf(TEXT("'%s' refused"), *Name), CreateAccepts(Name, Error));
			}
		});

		It(TEXT("accepts ordinary names that start with a device name's letters"), [this]()
		{
			for (const FString& Name : OrdinaryNames())
			{
				FString Error;
				TestTrue(FString::Printf(TEXT("'%s' accepted (%s)"), *Name, *Error), CreateAccepts(Name, Error));
			}
		});

		It(TEXT("refuses dots, .. and wildcards in the folder path"), [this]()
		{
			for (const TCHAR* Name : { TEXT("a.b"), TEXT("../Escape"), TEXT("a*"), TEXT("a?"), TEXT("~a") })
			{
				FString Error;
				TestFalse(FString::Printf(TEXT("'%s' refused"), Name), CreateAccepts(Name, Error));
			}
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
