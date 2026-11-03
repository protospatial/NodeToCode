// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "LLM/N2CLLMTypes.h"
#include "Core/Style/N2CUIColors.h"
#include "N2CUserSettings.generated.h"

class FConfigBranch;

/**
 * Where UN2CUserSettings::UpgradeFromProjectSettings reads the per-user values NodeToCode saved in the project's
 * [/Script/NodeToCode.N2CSettings] section before settings version 5, and where it writes them. The editor uses
 * ForProject(); tests use scratch files loaded into GConfig, never the user's own config.
 */
struct NODETOCODE_API FN2CUserSettingsUpgradeSources
{
	/**
	 * GConfig files holding the old [/Script/NodeToCode.N2CSettings] values, lowest layer first: a later file's value
	 * wins (in the editor, one entry: the NodeToCode config branch, which already merges Config/DefaultNodeToCode.ini
	 * and Saved/Config/<Platform>Editor/NodeToCode.ini).
	 */
	TArray<FString> ReadConfigs;

	/** GConfig file the per-user values and SettingsVersion are written to (EditorPerProjectUserSettings in the editor) */
	FString UserConfig;

	/** The project folder: reference files under it stay project-shared, others are copied to the per-user list */
	FString ProjectDir;

	/** The current project's config; ProjectDir is FPaths::ProjectDir() */
	static FN2CUserSettingsUpgradeSources ForProject();
};

/** What UN2CUserSettings::UpgradeFromProjectSettings found and did; every list holds one readable entry per item */
struct NODETOCODE_API FN2CUserSettingsUpgradeReport
{
	/** Values copied from the project config ("Provider=OpenAI", "McpServerPort=27010") */
	TArray<FString> Copied;

	/** Keys the user's own config already held, which win over the project value (nothing copied) */
	TArray<FString> KeptUserValues;

	/** Project values this build cannot read ("Provider=ClaudeOAuth"): left in the project file, the default applies */
	TArray<FString> Unreadable;

	/** Old keys found in the project config: no longer read, safe to delete by hand once every user has upgraded */
	TArray<FString> LegacyKeys;

	/** Reference files outside the project copied to the per-user list */
	TArray<FString> CopiedReferenceFiles;

	/** Reference files outside the project that do not exist on this machine: left in the project file only */
	TArray<FString> SkippedReferenceFiles;

	/** True when a saved UIColors still held 0271's old Text Muted grey (107), which became the new default (148) */
	bool bUpdatedTextMuted = false;

	/** True when the upgrade ran (the user's SettingsVersion was below CurrentSettingsVersion) */
	bool bRan = false;

	/** True when SettingsVersion was written to the user config */
	bool bStamped = false;

	/** True when the user config could not be written */
	bool bWriteFailed = false;
};

/** One per-user switch as read by UN2CUserSettings::ResolvePerUserBool. */
struct FN2CPerUserBool
{
	/** The value the plugin uses. */
	bool bValue = false;

	/** True when the user's own layer (Saved/Config or a -ini: command-line override) set it. */
	bool bFromUserLayer = false;

	/** True when a project-shared layer (Default*, Base, platform, plugin) set a different value that was ignored. */
	bool bIgnoredSharedValue = false;
};

/**
 * Per-user Node to Code settings, saved to the user's Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini.
 *
 * Unreal also reads the project's Config/DefaultEditorPerProjectUserSettings.ini (and the Base and platform files) as
 * lower layers of that config, so a committed file could set these properties for everyone who opens the project.
 * The security switches are therefore not read from the loaded properties: IsMcpServerEnabled(),
 * IsMcpPythonExecutionAllowed(), AreMcpWritesOutsideProjectAllowed() and IsMcpBearerTokenRequired() take the value only
 * from the user's own layers and fall back to the code default, ignoring (with one warning) any value from a
 * project-shared layer (cards 0423, 0182, 0183).
 *
 * Since settings version 5 (card 0188) this class also holds the choices that differ per developer and used to be
 * written to the source-controlled Config/DefaultNodeToCode.ini: the provider, the LM Studio endpoint, the MCP server
 * port, the window colors, reference files outside the project and the translation output folder. UN2CSettings keeps
 * what a team shares (models, prices, target language, depth, project reference files, tool modes).
 * UpgradeFromProjectSettings copies a user's values over from the project config once.
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Node to Code (User)"))
class NODETOCODE_API UN2CUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** The text under the section's title in Editor Preferences (card 0487: not this class's doc comment) */
	virtual FText GetSectionDescription() const override;
#endif

	/**
	 * Layout version of this user's settings, the same counter as UN2CSettings::SettingsVersion. 5: the per-user fields
	 * below moved here from the project config (UpgradeFromProjectSettings). Not shown in the editor.
	 */
	UPROPERTY(Config)
	int32 SettingsVersion = 0;

	/** The version UpgradeFromProjectSettings stamps; UN2CSettings::CurrentSettingsVersion is never below it */
	static constexpr int32 CurrentSettingsVersion = 5;

	/** Selected LLM provider: the service translations are sent to. The provider's model is a project setting. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "LLM Provider",
		meta = (DisplayName = "Provider",
			ToolTip = "The LLM service your translations are sent to. Yours only: it is saved in your user settings, not in the project. Each provider's model, Max Output Tokens and prices are project settings (Project Settings > Plugins > Node to Code)."))
	EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;

	/** LM Studio server address (default http://localhost:1234); /v1/chat/completions is added when missing */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "LLM Provider|LM Studio",
		meta = (DisplayName = "LM Studio Server Endpoint",
			ToolTip = "Address of your LM Studio (or other OpenAI-compatible) server. Yours only: it is saved in your user settings, not in the project."))
	FString LMStudioEndpoint = TEXT("http://localhost:1234");

	/**
	 * MCP server port (http://127.0.0.1:<port>/mcp). When another program already uses it, the server takes the next free
	 * port in Port Fallback Range (a project setting) and says so in a notification. Changing it restarts the server.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Server Port", ClampMin = "1024", ClampMax = "65535", UIMin = "1024", UIMax = "65535",
			ToolTip = "Port of the MCP server (http://127.0.0.1:<port>/mcp). If another program already uses it, the server takes the next free port in Port Fallback Range (Project Settings) and shows a notification. Changing it restarts the server on the new port. Yours only: saved in your user settings."))
	int32 McpServerPort = 27000;

	/**
	 * Your own reference source files (any folder, absolute paths), sent with every translation after the project's
	 * Reference Source Files. A file outside the project added to the project list moves here.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Code Generation",
		meta = (DisplayName = "My Reference Source Files", FilePathFilter = "C++ Files (*.h;*.cpp)|*.h;*.cpp",
			ToolTip = "Source files of your own to include as context in translation prompts, for example headers outside this project. Yours only: saved in your user settings. Files inside the project belong in Project Settings > Plugins > Node to Code > Reference Source Files, which the whole team shares."))
	TArray<FFilePath> ReferenceSourceFilePaths;

	/** Folder translations are saved to instead of Saved/NodeToCode/Translations (a relative path is under the project) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Code Generation",
		meta = (DisplayName = "Custom Translation Output Directory",
			ToolTip = "If set, your translations are saved to this folder instead of Saved/NodeToCode/Translations. A relative path is under the project folder. Yours only: saved in your user settings."))
	FDirectoryPath CustomTranslationOutputDirectory;

	/** Color palette of every NodeToCode window and widget. Widgets read it through N2C::UI::Colors(). */
	UPROPERTY(Config, EditAnywhere, Category = "Theming", meta = (DisplayName = "UI Colors",
		ToolTip = "The colors of the Node to Code window and of the N2C bar on each graph. A change shows at once. Yours only: saved in your user settings."))
	FN2CUIColors UIColors;

	/** The per-user fields that settings version 5 moved here from UN2CSettings (same key names in both sections) */
	static TConstArrayView<FName> GetMovedPropertyNames();

	/**
	 * Settings version 5 upgrade (card 0188). While SettingsVersion is below CurrentSettingsVersion, copies each moved
	 * field's old value from Sources.ReadConfigs' [/Script/NodeToCode.N2CSettings] section into this object and
	 * Sources.UserConfig, unless the user config already holds the key (the user's value wins). Reference files: entries
	 * outside Sources.ProjectDir that exist on this machine are added to the per-user list; project files stay in the
	 * project list. UIColors: 0271's old Text Muted grey (107) becomes the new default (148), other saved colors are
	 * kept, and a palette left at the defaults is not copied. Then SettingsVersion is stamped and written, so it runs
	 * once per user; running it again changes nothing. The project config is only read: its old keys are reported in
	 * LegacyKeys and logged as safe to delete by hand (other developers still need them for their own upgrade).
	 */
	FN2CUserSettingsUpgradeReport UpgradeFromProjectSettings(const FN2CUserSettingsUpgradeSources& Sources);

	/**
	 * Saves the named config properties of this object to UserConfig (empty: this class's config, the user's
	 * Saved/Config/<Platform>Editor/EditorPerProjectUserSettings.ini), key by key: the switches and other values the user
	 * never set are not written. Used by the provider pickers in the NodeToCode window and by the upgrade.
	 * @return false when GConfig is missing or a name is not a config property of this class
	 */
	bool SaveToUserConfig(TConstArrayView<FName> PropertyNames, const FString& UserConfig = FString());

	/**
	 * The reference source files a translation sends, as absolute paths: the project's Reference Source Files
	 * (UN2CSettings, a relative entry is under the project folder) then this user's own, each once, files that do not
	 * exist left out.
	 */
	static TArray<FString> GetReferenceSourceFiles();

	/** GetReferenceSourceFiles over given lists: ProjectList's entries then UserList's, absolute, each once, existing only */
	static TArray<FString> CollectReferenceSourceFiles(const TArray<FFilePath>& ProjectList, const TArray<FFilePath>& UserList);

	/**
	 * Absolute path of a reference file entry: a relative path is under ProjectDir (FPaths::ProjectDir() when empty),
	 * normalized with forward slashes.
	 */
	static FString ResolveReferencePath(const FString& Path, const FString& ProjectDir = FString());

	/** True when the absolute path is inside ProjectDir (FPaths::ProjectDir() when empty) */
	static bool IsInsideProject(const FString& AbsolutePath, const FString& ProjectDir = FString());

	/** Code defaults of the security switches; a project-shared config layer cannot change them. */
	static constexpr bool bDefaultEnableMcpServer = true;
	static constexpr bool bDefaultAllowMcpPythonExecution = false;
	static constexpr bool bDefaultAllowMcpWritesOutsideProject = false;
	/** Off for 2.0-beta; turn it on by default once every generated client config sends the header (card 0183) */
	static constexpr bool bDefaultRequireMcpBearerToken = false;

	/** Start the MCP server when the editor starts. Requires an editor restart. Read through IsMcpServerEnabled(). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Enable MCP Server", ConfigRestartRequired = true,
			ToolTip = "Start the MCP server when the editor starts, so MCP clients (Claude Code, Cursor, VS Code) can reach this editor. Only your own setting counts: a value in the project's Config folder is ignored. Requires an editor restart."))
	bool bEnableMcpServer = bDefaultEnableMcpServer;

	/**
	 * Expose run-python over MCP. Any connected MCP client can then run arbitrary Python in the editor. Requires an
	 * editor restart. Read through
	 * IsMcpPythonExecutionAllowed(), never directly: this property also holds values from project-shared layers.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Allow MCP Python Execution", ConfigRestartRequired = true,
			ToolTip = "Lets any connected MCP client run arbitrary Python in the editor and save or delete script files. Off by default, and only your own setting counts: a value in the project's Config folder is ignored. Requires an editor restart."))
	bool bAllowMcpPythonExecution = bDefaultAllowMcpPythonExecution;

	/**
	 * Let the MCP tools that create, save, copy, move or rename content write outside the project's own content
	 * (/Game and the project's plugins): to /Engine, engine and Fab plugins, and NodeToCode's own /NodeToCode content.
	 * Such writes change files every project on the machine shares and are lost when the engine or plugin updates.
	 * Requires an editor restart. Read through AreMcpWritesOutsideProjectAllowed(), never directly.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Allow MCP Writes Outside Project", ConfigRestartRequired = true,
			ToolTip = "Lets MCP tools create, save, copy, move and rename assets and folders outside /Game and this project's own plugins: in /Engine, engine and Fab plugins, and /NodeToCode. Those files are shared by every project on this machine and replaced on update. Off by default, and only your own setting counts: a value in the project's Config folder is ignored. Requires an editor restart."))
	bool bAllowMcpWritesOutsideProject = bDefaultAllowMcpWritesOutsideProject;

	/**
	 * Answer every /mcp request without "Authorization: Bearer <token>" with 401, so a program on this machine that can
	 * reach the port but cannot read the user's files cannot call tools. The token is in
	 * <UserSettingsDir>/NodeToCode/mcp-token (FN2CMcpAuthToken); the stdio bridge reads it itself, HTTP clients send it
	 * as a header (N2C.Mcp.CopyToken copies it). Requires an editor restart. Read through IsMcpBearerTokenRequired().
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MCP Server",
		meta = (DisplayName = "Require MCP Bearer Token", ConfigRestartRequired = true,
			ToolTip = "Refuses (401) every MCP request that does not send Authorization: Bearer <token>. The token is in NodeToCode's mcp-token file in your user settings folder (%LOCALAPPDATA%/NodeToCode on Windows); the stdio bridge reads it itself, and the console command N2C.Mcp.CopyToken copies it for an HTTP client's header. Off by default, and only your own setting counts. Requires an editor restart."))
	bool bRequireMcpBearerToken = bDefaultRequireMcpBearerToken;

	/**
	 * Show the Get Started card at the top of the Node to Code window (card 0269). The card also hides once the project
	 * has a saved translation; its close button turns this off. Not a security switch: read the property directly.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Window",
		meta = (DisplayName = "Show Get Started Card",
			ToolTip = "Show the Get Started card (provider, API key, connection test, target language, tagging a graph) at the top of the Node to Code window until the project has a saved translation. Its close button turns this off."))
	bool bShowGetStartedCard = true;

	/**
	 * Whether the MCP server may start: the user's own Enable MCP Server value, or on when the user never set it.
	 * Read once per editor session (the setting needs a restart); a value from a project-shared layer is ignored.
	 */
	static bool IsMcpServerEnabled();

	/**
	 * Whether run-python may be registered: the user's own Allow MCP
	 * Python Execution value, or off when the user never set it. A committed project file cannot turn it on.
	 */
	static bool IsMcpPythonExecutionAllowed();

	/**
	 * Whether MCP tools may write content outside /Game and the project's plugins: the user's own Allow MCP Writes
	 * Outside Project value, or off when the user never set it. A committed project file cannot turn it on.
	 */
	static bool AreMcpWritesOutsideProjectAllowed();

	/**
	 * Whether /mcp requires the bearer token: the user's own Require MCP Bearer Token value, or off when the user never
	 * set it. A value from a project-shared layer is ignored, like the other switches.
	 */
	static bool IsMcpBearerTokenRequired();

	/**
	 * Reads Key in Section of Branch from the user's own layers only: the Saved/Config file (Branch->SavedLayer) and
	 * -ini: command-line overrides (Branch->CommandLineOverrides), the last one winning. Without either it returns
	 * bCodeDefault and reports whether the loaded value (from a project-shared or plugin layer) differed.
	 * A null Branch gives bCodeDefault. Public for the NodeToCode.Security.UserSettings spec.
	 */
	static FN2CPerUserBool ResolvePerUserBool(const FConfigBranch* Branch, const FString& Section, FName Key, bool bCodeDefault);

	/** Tools that only exist when bAllowMcpPythonExecution is on. */
	static bool IsPythonExecutionTool(const FString& ToolName)
	{
		return ToolName == TEXT("run-python");
	}
};
