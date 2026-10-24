// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CSettings.h"
#include "UObject/UnrealType.h"

namespace
{
struct FN2CCustomProviderCategoryOverride
{
    FN2CCustomProviderCategoryOverride()
    {
        if (FProperty* AnchorProperty = FindFProperty<FProperty>(
                UN2CSettings::StaticClass(),
                GET_MEMBER_NAME_CHECKED(UN2CSettings, bCustomProvidersUIAnchor)))
        {
            AnchorProperty->SetMetaData(
                TEXT("Category"),
                TEXT("Node to Code | Custom LLM Services"));
        }
    }
};

FN2CCustomProviderCategoryOverride GCustomProviderCategoryOverride;
}
