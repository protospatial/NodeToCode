// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/Tools/N2CMcpToolBase.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2CCredentialSource.h"

/**
 * MCP tool that returns the list of configured LLM providers available for Blueprint translation.
 * A cloud provider is listed when the credential source requests use (IN2CCredentialSource) has a key for it, saved in
 * Project Settings or from an environment variable; each entry says where the key comes from, never the key. Local
 * providers are always listed.
 */
class FN2CMcpGetAvailableLLMProvidersTool : public FN2CMcpToolBase
{
public:
    // IN2CMcpTool interface
    virtual FMcpToolDefinition GetDefinition() const override;
    virtual FMcpToolCallResult Execute(const TSharedPtr<FJsonObject>& Arguments) override;
    
    // This tool requires Game Thread to access settings and user secrets
    virtual bool RequiresGameThread() const override { return true; }

    /**
     * Get the models offered for a provider: every enum entry except those hidden or marked retired in the
     * registry. Deprecated models that still work carry "deprecated" and "shutdownDate". Public for the
     * NodeToCode.LLM.Registry spec.
     * @param Provider The LLM provider
     * @return Array of model JSON objects
     */
    TArray<TSharedPtr<FJsonValue>> GetAvailableModels(EN2CLLMProvider Provider) const;

    /** Read keys' origins from Source instead of IN2CCredentialSource::Get() (the credential source spec) */
    void SetCredentialSource(TSharedPtr<const IN2CCredentialSource> Source) { CredentialSource = MoveTemp(Source); }

private:
    /** Set by SetCredentialSource; unset means IN2CCredentialSource::Get() */
    TSharedPtr<const IN2CCredentialSource> CredentialSource;

    TSharedRef<const IN2CCredentialSource> GetCredentialSource() const
    {
        return CredentialSource.IsValid() ? CredentialSource.ToSharedRef() : IN2CCredentialSource::Get();
    }

    /**
     * Check if a provider is configured (has API key for cloud providers, always true for local)
     * @param Provider The LLM provider to check
     * @return true if the provider is configured and available
     */
    bool IsProviderConfigured(EN2CLLMProvider Provider) const;
    
    /**
     * Get the display name for a provider
     * @param Provider The LLM provider
     * @return Human-readable provider name
     */
    FString GetProviderDisplayName(EN2CLLMProvider Provider) const;
    
    /**
     * Build a JSON object with all information about a provider
     * @param Provider The LLM provider
     * @return JSON object with provider details
     */
    TSharedPtr<FJsonObject> BuildProviderInfo(EN2CLLMProvider Provider) const;
    
    /**
     * Get the current selected model for a provider
     * @param Provider The LLM provider
     * @return Current model ID string
     */
    FString GetCurrentModel(EN2CLLMProvider Provider) const;
    
    /**
     * Check if a provider supports structured output
     * @param Provider The LLM provider
     * @return true if the provider supports structured output
     */
    bool SupportsStructuredOutput(EN2CLLMProvider Provider) const;
    
    /**
     * Get endpoint URL for local providers
     * @param Provider The LLM provider (should be Ollama or LMStudio)
     * @return Endpoint URL string
     */
    FString GetProviderEndpoint(EN2CLLMProvider Provider) const;
};