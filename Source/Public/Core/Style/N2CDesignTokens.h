// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"

/**
 * Design token constants for the NodeToCode UI system.
 * These complement the color tokens in FN2CUIColors (Core/Style/N2CUIColors.h).
 *
 * Usage: #include "Core/Style/N2CDesignTokens.h"
 *   FMargin pad = FN2CSpacing::SM();
 *   FSlateFontInfo font = FN2CFonts::SmallBold();
 */

// ── Spacing Tokens ──────────────────────────────────────────────────

namespace FN2CSpacing
{
	inline constexpr float XXS = 2.0f;
	inline constexpr float XS  = 4.0f;
	inline constexpr float SM  = 6.0f;
	inline constexpr float MD  = 8.0f;
	inline constexpr float LG  = 12.0f;
	inline constexpr float XL  = 16.0f;
	inline constexpr float XXL = 20.0f;
}

// ── Font Tokens ─────────────────────────────────────────────────────

namespace FN2CFonts
{
	inline FSlateFontInfo Tiny()        { return FCoreStyle::GetDefaultFontStyle("Regular", 7); }
	inline FSlateFontInfo Small()       { return FCoreStyle::GetDefaultFontStyle("Regular", 8); }
	inline FSlateFontInfo Regular()     { return FCoreStyle::GetDefaultFontStyle("Regular", 10); }
	inline FSlateFontInfo Medium()      { return FCoreStyle::GetDefaultFontStyle("Regular", 11); }
	inline FSlateFontInfo Large()       { return FCoreStyle::GetDefaultFontStyle("Regular", 13); }

	inline FSlateFontInfo TinyBold()    { return FCoreStyle::GetDefaultFontStyle("Bold", 7); }
	inline FSlateFontInfo SmallBold()   { return FCoreStyle::GetDefaultFontStyle("Bold", 8); }
	inline FSlateFontInfo RegularBold() { return FCoreStyle::GetDefaultFontStyle("Bold", 10); }
	inline FSlateFontInfo MediumBold()  { return FCoreStyle::GetDefaultFontStyle("Bold", 11); }
	inline FSlateFontInfo LargeBold()   { return FCoreStyle::GetDefaultFontStyle("Bold", 13); }

	inline FSlateFontInfo Mono()        { return FCoreStyle::GetDefaultFontStyle("Mono", 12); }
	inline FSlateFontInfo MonoSmall()   { return FCoreStyle::GetDefaultFontStyle("Mono", 11); }
}

// ── Sizing Tokens ───────────────────────────────────────────────────

namespace FN2CSizing
{
	inline constexpr float IconSM  = 14.0f;
	inline constexpr float IconMD  = 16.0f;
	inline constexpr float IconLG  = 20.0f;

	inline constexpr float SeparatorThickness = 1.0f;
}
