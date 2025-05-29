#include "LLM/Providers/N2COpenAICompatibleService.h"

#include "Core/N2CSettings.h"

FString UN2COpenAICompatibleService::GetDefaultEndpoint() const
{
    const UN2CSettings* Settings = GetDefault<UN2CSettings>();
    return Settings->OpenAICompatibleBaseUrl;
}
