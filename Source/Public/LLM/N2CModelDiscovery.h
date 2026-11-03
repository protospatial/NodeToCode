// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "LLM/N2CCredentialSource.h"
#include "LLM/N2CLLMRequestHandle.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CModelCatalog.h"

class IN2CHttpTransport;

/** The shape of a provider's model list endpoint (one parser each) */
enum class EN2CModelListFormat : uint8
{
	/** {"data":[{"id", ...}]}: OpenAI, DeepSeek, LM Studio, vLLM, llama.cpp, OpenRouter and the other OpenAI-compatible hosts */
	OpenAI,
	/** {"data":[...], "has_more", "last_id"}: Anthropic GET /v1/models, paged with after_id */
	Anthropic,
	/** {"models":[{"name":"models/..."}], "nextPageToken"}: Gemini GET v1beta/models, paged with pageToken */
	Gemini,
	/** {"models":[{"name"}]}: Ollama GET /api/tags */
	Ollama,
};

/** One model a provider's list names, with what the list says about it (zero or empty where it says nothing) */
struct NODETOCODE_API FN2CDiscoveredModel
{
	/** The model ID requests send (Gemini's "models/" prefix removed) */
	FString Id;

	/** The provider's own name for it; empty when the list gives none */
	FString DisplayName;

	/** Input context in tokens (Anthropic max_input_tokens, Gemini inputTokenLimit, context_window, context_length, max_model_len); 0 unknown */
	int32 ContextWindow = 0;

	/** Output limit in tokens (Anthropic max_tokens, Gemini outputTokenLimit, max_output_tokens); 0 unknown */
	int32 MaxOutput = 0;

	/** The provider marks it deprecated (Anthropic lifecycle or deprecated_at, or a shutdown date) */
	bool bDeprecated = false;

	/** The provider's shutdown date, YYYY-MM-DD (Anthropic retires_at, OpenAI shutdown_date); empty when none */
	FString ShutdownDate;
};

/** A provider's model list as last fetched (in memory, and cached under Saved/NodeToCode/ModelCache) */
struct NODETOCODE_API FN2CModelList
{
	/** Catalog provider ID or the OpenAI-Compatible profile ID the list belongs to */
	FName ProviderId;

	/** The first page's URL (no key: keys travel in headers only) */
	FString Endpoint;

	FDateTime FetchedAt;

	/** In the provider's order */
	TArray<FN2CDiscoveredModel> Models;

	const FN2CDiscoveredModel* Find(const FString& ModelId) const;

	/** The cache file's JSON (schema_version 1) */
	FString ToJson() const;

	/** Reads a cache file's JSON; false with OutError when it is not one */
	static bool FromJson(const FString& Json, FN2CModelList& Out, FString& OutError);
};

/** One page of a model list, and where the next page starts (empty on the last page) */
struct NODETOCODE_API FN2CModelListPage
{
	TArray<FN2CDiscoveredModel> Models;
	FString NextCursor;
};

/** Why a refresh was not sent */
enum class EN2CModelDiscoverySkip : uint8
{
	None,
	/** A commandlet (builds, cooks): never */
	Commandlet,
	/** -unattended (the smoke check, automation runs, build agents): never */
	Unattended,
	/** The provider is not in Project Settings > Live Model Lists */
	NotOptedIn,
	/** The provider needs a key and none is saved or in the environment */
	NoKey,
	/** No profile for the provider (an unknown OpenAI-Compatible profile ID) */
	NoProfile,
	/** A fetch for the provider is already running */
	Busy,
};

/**
 * Live model lists (card 0216, decision 0004 (c)). Fetches a provider's model list endpoint (the profile's ModelsPath)
 * through the LLM layer's HTTP transport, follows its pages, caches the result per provider, and gives Project Settings
 * the discovery layer under the catalog: the catalog's rows win for IDs it knows, the list adds IDs it does not (shown
 * with their limits and an unknown price, never $0), and the selected model gets a warning when the list lacks it or
 * the provider marks it deprecated.
 *
 * Off by default: a provider is fetched only when it is in Project Settings > Live Model Lists, only with a configured
 * key, never in a commandlet or an -unattended session (RequestRefresh checks all three), and only for the active
 * provider: at editor start, when its key changes, when it is opted in, and from the Refresh Model List button.
 * Game thread only.
 */
class NODETOCODE_API FN2CModelDiscovery
{
public:
	static FN2CModelDiscovery& Get();

	// --- Parsing (no state) ---

	/** The list format of a profile's ModelsPath: by its dialect (anthropic_messages, gemini, ollama, else OpenAI) */
	static EN2CModelListFormat FormatFor(const FN2CProviderProfile& Profile);

	/** Parses one page of Format; false with OutError when Body is not such a page */
	static bool ParsePage(EN2CModelListFormat Format, const FString& Body, FN2CModelListPage& OutPage, FString& OutError);

	/** BaseUrl + ModelsPath, with the page size and Cursor as the format pages them (limit/after_id, pageSize/pageToken, after) */
	static FString MakePageUrl(const FN2CProviderProfile& Profile, const FString& Cursor);

	// --- Where and whether ---

	/** Commandlet or Unattended for those environments, else None */
	static EN2CModelDiscoverySkip GetEnvironmentSkip(bool bIsCommandlet, bool bIsUnattended);

	/** GetEnvironmentSkip for this process (IsRunningCommandlet(), FApp::IsUnattended()) */
	static EN2CModelDiscoverySkip GetEnvironmentSkip();

	/** The list's ID for a provider: its catalog ID, or for OpenAI-Compatible the profile (ProfileId, else the user's Provider Profile) */
	static FName ProviderIdFor(EN2CLLMProvider Provider, const FString& ProfileId = FString());

	/** ProviderIdFor the user's Provider (and Provider Profile) */
	static FName GetActiveProviderId();

	/** Whether ProviderId is in Project Settings > Live Model Lists */
	static bool IsOptedIn(FName ProviderId);

	/** What a fetch needs: the list's ID, the profile (endpoint, ModelsPath, dialect) and the key */
	struct FTarget
	{
		FName ProviderId;
		FN2CProviderProfile Profile;
		FN2CCredential Credential;
	};

	/**
	 * The profile a provider's list is fetched from: the catalog row, with the user's endpoint for Ollama and LM Studio
	 * and the user's profile and Base URL Override for OpenAI-Compatible. No key yet. False with OutError when there is none.
	 */
	static bool MakeTarget(EN2CLLMProvider Provider, const FString& ProfileId, FTarget& OutTarget, FString& OutError);

	/** Saved/NodeToCode/ModelCache under the project */
	static FString GetDefaultCacheDir();

	// --- Fetching ---

	using FOnFetched = TFunction<void(bool bSucceeded, const FN2CModelList& List, const FString& Error)>;

	/**
	 * Fetches every page of Target's list (at most MaxPages) and calls OnDone once. Not gated: RequestRefresh is the gated
	 * entry; specs call this with a fixture transport. Cancel the returned handle to drop the fetch.
	 */
	FN2CLLMRequestHandleRef Fetch(const FTarget& Target, FOnFetched OnDone);

	/**
	 * The gated refresh: skipped in commandlets and -unattended sessions, for a provider not opted in, without a key, or
	 * while one runs. Otherwise fetches, keeps and caches the list, logs it, and warns when Provider is the active
	 * provider and its selected model is missing or deprecated. bNotify shows the outcome as a notification too (the
	 * button). Returns why it was skipped (None when sent).
	 */
	EN2CModelDiscoverySkip RequestRefresh(EN2CLLMProvider Provider, const FString& ProfileId, const TCHAR* Reason, bool bNotify = false);

	/** RequestRefresh for the user's Provider (and Provider Profile) */
	EN2CModelDiscoverySkip RequestActiveRefresh(const TCHAR* Reason, bool bNotify = false);

	/** The user-facing reason for a skip */
	static FString DescribeSkip(EN2CModelDiscoverySkip Skip, FName ProviderId);

	bool IsFetching(FName ProviderId) const { return InFlight.Contains(ProviderId); }

	/** Replaces the transport (specs); null restores the engine's HTTP */
	void SetTransport(TSharedPtr<IN2CHttpTransport> InTransport);

	// --- Lists ---

	/** The list in memory for ProviderId; null when none was fetched or loaded */
	const FN2CModelList* GetList(FName ProviderId) const;

	/** GetList, only when ProviderId is opted in (what Project Settings and the warnings read) */
	const FN2CModelList* GetListIfOptedIn(FName ProviderId) const;

	/** Keeps List in memory (replacing ProviderId's) and broadcasts OnListsChanged */
	void SetList(const FN2CModelList& List);

	/** Forgets ProviderId's list in memory (the cache file stays) */
	void RemoveList(FName ProviderId);

	/** Writes List to Dir/<ProviderId>.json */
	static bool SaveCache(const FN2CModelList& List, const FString& Dir);

	/** Reads Dir/<ProviderId>.json */
	static bool LoadCache(FName ProviderId, const FString& Dir, FN2CModelList& OutList);

	/** Called when a list in memory changes (Project Settings redraws its drop-downs) */
	FSimpleMulticastDelegate OnListsChanged;

	// --- What Project Settings shows ---

	/** The model ID requests to Provider send now (the custom ID, the selection, or the OpenAI-Compatible profile's default) */
	static FString GetSelectedModelId(EN2CLLMProvider Provider);

	/**
	 * A warning for Provider's selected model, empty when there is none: the catalog marks it deprecated with a shutdown
	 * date, or (opted in, with a list) the list does not name it or the provider marks it deprecated.
	 */
	FString GetSelectedModelWarning(EN2CLLMProvider Provider) const;

	/** One line for the Model List row: the active provider's list (models, when fetched) or why there is none */
	FString DescribeActiveStatus() const;

	// --- Editor lifetime ---

	/**
	 * Editor start (NodeToCode.cpp): unless in a commandlet or -unattended, loads the cached lists of the opted-in
	 * providers and, a few seconds later, refreshes the active provider when it is opted in.
	 */
	void Startup();

	/** Cancels running fetches and the startup refresh */
	void Shutdown();

	/** Pages followed per fetch before it stops (a guard against a server that never ends its list) */
	static constexpr int32 MaxPages = 20;

private:
	FN2CModelDiscovery() = default;

	struct FFetchState;
	void FetchPage(const TSharedRef<FFetchState>& State);
	void SendFetch(const FTarget& Target, const TCHAR* Reason, bool bNotify, EN2CLLMProvider Provider);

	TSharedRef<IN2CHttpTransport> GetTransport() const;

	TMap<FName, FN2CModelList> Lists;
	TMap<FName, FN2CLLMRequestHandleRef> InFlight;
	TSharedPtr<IN2CHttpTransport> Transport;
	FTSTicker::FDelegateHandle StartupTick;
};
