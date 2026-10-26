// Copyright (c) 2026 Nick McClure (Protospatial)
// SPDX-License-Identifier: Apache-2.0

#include "Core/Style/N2CUIColors.h"
#include "Core/N2CSettings.h"

const FN2CUIColors& N2C::UI::Colors()
{
    return GetDefault<UN2CSettings>()->UIColors;
}
