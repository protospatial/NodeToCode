// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/Style/N2CStyle.h"
#include "Core/Style/N2CUIColors.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Framework/Application/SlateApplication.h"

TSharedPtr<FSlateStyleSet> N2CStyle::StyleInstance = nullptr;

// Static FButtonStyle instances — stable addresses allow SButton to see live updates
FButtonStyle N2CStyle::N2CButtonStyle;
FButtonStyle N2CStyle::N2CSimpleButtonStyle;
FButtonStyle N2CStyle::N2CNoBorderStyle;

// Static FSlateBrush instances — stable addresses allow SBorder to see live updates
FSlateBrush N2CStyle::N2CPanelBorderBrush;
FSlateBrush N2CStyle::N2CDarkPanelBorderBrush;

// Static combo box styles — stable addresses allow SComboBox to see live updates
FComboBoxStyle N2CStyle::N2CComboBoxStyle;
FTableRowStyle N2CStyle::N2CComboRowStyle;

// Static widget styles — stable addresses for live updates
FCheckBoxStyle N2CStyle::N2CCheckBoxStyle;
FEditableTextBoxStyle N2CStyle::N2CEditableTextBoxStyle;
FTableRowStyle N2CStyle::N2CTableRowStyle;
FHeaderRowStyle N2CStyle::N2CHeaderRowStyle;
FScrollBarStyle N2CStyle::N2CScrollBarStyle;

void N2CStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance.Get());
	}
}

void N2CStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance.Get());
		StyleInstance.Reset();
	}
}

void N2CStyle::Reinitialize()
{
	Shutdown();
	Initialize();
}

const ISlateStyle& N2CStyle::Get()
{
	if (!StyleInstance.IsValid())
	{
		Initialize();
	}
	return *StyleInstance.Get();
}

FString N2CStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
    static FString Content = IPluginManager::Get().FindPlugin(TEXT("NodeToCode"))->GetBaseDir() / TEXT("Resources");
    return (Content / RelativePath) + Extension;
}

const FName& N2CStyle::GetStyleSetName() const
{
    static FName StyleName(TEXT("NodeToCodeStyle"));
    return StyleName;
}

const FButtonStyle& N2CStyle::GetButtonStyle() { return N2CButtonStyle; }
const FButtonStyle& N2CStyle::GetSimpleButtonStyle() { return N2CSimpleButtonStyle; }
const FButtonStyle& N2CStyle::GetNoBorderStyle() { return N2CNoBorderStyle; }
const FSlateBrush& N2CStyle::GetPanelBorderBrush() { return N2CPanelBorderBrush; }
const FSlateBrush& N2CStyle::GetDarkPanelBorderBrush() { return N2CDarkPanelBorderBrush; }
const FComboBoxStyle& N2CStyle::GetComboBoxStyle() { return N2CComboBoxStyle; }
const FTableRowStyle& N2CStyle::GetComboRowStyle() { return N2CComboRowStyle; }
const FCheckBoxStyle& N2CStyle::GetCheckBoxStyle() { return N2CCheckBoxStyle; }
const FEditableTextBoxStyle& N2CStyle::GetEditableTextBoxStyle() { return N2CEditableTextBoxStyle; }
const FTableRowStyle& N2CStyle::GetTableRowStyle() { return N2CTableRowStyle; }
const FHeaderRowStyle& N2CStyle::GetHeaderRowStyle() { return N2CHeaderRowStyle; }
const FScrollBarStyle& N2CStyle::GetScrollBarStyle() { return N2CScrollBarStyle; }

TSharedRef<FSlateStyleSet> N2CStyle::Create()
{
    static FName StyleSetName(TEXT("NodeToCodeStyle"));
    TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(StyleSetName));

    // Set content root using plugin manager
    Style->SetContentRoot(IPluginManager::Get().FindPlugin(TEXT("NodeToCode"))->GetBaseDir() / TEXT("Resources"));

    Style->Set("NodeToCode.ToolbarButton",
        new N2C_PLUGIN_BRUSH(TEXT("button_icon"), FVector2D(128.0f, 128.0f)));

    // Populate styles from current palette
    UpdateButtonStyles();
    UpdateBorderBrushes();
    UpdateComboBoxStyle();
    UpdateWidgetStyles();

    return Style;
}

void N2CStyle::UpdateButtonStyles()
{
	const FN2CUIColors& Colors = N2C::UI::Colors();

	const FSlateColor NormalFg(FN2CUIColors::ToLinear(Colors.BtnForeground));
	const FSlateColor HoveredFg(FN2CUIColors::ToLinear(Colors.BtnForegroundHover));
	const FSlateColor DisabledFg(FN2CUIColors::ToLinear(Colors.BtnForegroundDisabled));

	// ── N2C.Button ── Solid background action buttons
	N2CButtonStyle.SetNormal(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnNormal), 4.0f));
	N2CButtonStyle.SetHovered(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnHovered), 4.0f));
	N2CButtonStyle.SetPressed(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnPressed), 4.0f));
	N2CButtonStyle.SetDisabled(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnDisabled), 4.0f));
	N2CButtonStyle.SetNormalForeground(NormalFg);
	N2CButtonStyle.SetHoveredForeground(HoveredFg);
	N2CButtonStyle.SetPressedForeground(NormalFg);
	N2CButtonStyle.SetDisabledForeground(DisabledFg);
	N2CButtonStyle.SetNormalPadding(FMargin(4.0f, 2.0f));
	N2CButtonStyle.SetPressedPadding(FMargin(4.0f, 3.0f, 4.0f, 1.0f));

	// ── N2C.SimpleButton ── Transparent/minimal icon buttons
	N2CSimpleButtonStyle.SetNormal(FSlateNoResource());
	N2CSimpleButtonStyle.SetHovered(FSlateRoundedBoxBrush(FN2CUIColors::ToLinearWithAlpha(Colors.BtnHovered, 0.5f), 4.0f));
	N2CSimpleButtonStyle.SetPressed(FSlateRoundedBoxBrush(FN2CUIColors::ToLinearWithAlpha(Colors.BtnPressed, 0.6f), 4.0f));
	N2CSimpleButtonStyle.SetDisabled(FSlateNoResource());
	N2CSimpleButtonStyle.SetNormalForeground(NormalFg);
	N2CSimpleButtonStyle.SetHoveredForeground(HoveredFg);
	N2CSimpleButtonStyle.SetPressedForeground(NormalFg);
	N2CSimpleButtonStyle.SetDisabledForeground(DisabledFg);
	N2CSimpleButtonStyle.SetNormalPadding(FMargin(0.0f));
	N2CSimpleButtonStyle.SetPressedPadding(FMargin(0.0f));

	// ── N2C.NoBorder ── Completely invisible button for wrappers/close
	N2CNoBorderStyle.SetNormal(FSlateNoResource());
	N2CNoBorderStyle.SetHovered(FSlateNoResource());
	N2CNoBorderStyle.SetPressed(FSlateNoResource());
	N2CNoBorderStyle.SetDisabled(FSlateNoResource());
	N2CNoBorderStyle.SetNormalForeground(NormalFg);
	N2CNoBorderStyle.SetHoveredForeground(HoveredFg);
	N2CNoBorderStyle.SetPressedForeground(NormalFg);
	N2CNoBorderStyle.SetDisabledForeground(DisabledFg);
	N2CNoBorderStyle.SetNormalPadding(FMargin(0.0f));
	N2CNoBorderStyle.SetPressedPadding(FMargin(0.0f));
}

void N2CStyle::UpdateBorderBrushes()
{
	const FN2CUIColors& Colors = N2C::UI::Colors();

	// Panel border: rounded box with subtle outline, fill tinted by BorderBackgroundColor at widget level
	N2CPanelBorderBrush = FSlateRoundedBoxBrush(
		FLinearColor::White, 4.0f,
		FN2CUIColors::ToLinear(Colors.BorderSubtle), 1.0f
	);

	// Dark panel border: same shape, darker context
	N2CDarkPanelBorderBrush = FSlateRoundedBoxBrush(
		FLinearColor::White, 4.0f,
		FN2CUIColors::ToLinear(Colors.BorderSubtle), 1.0f
	);
}

void N2CStyle::UpdateWidgetStyles()
{
	const FN2CUIColors& Colors = N2C::UI::Colors();

	// ── CheckBox Style ──
	// Follows Starship's two-layer pattern:
	//   Layer 0 (BackgroundImage): the box outline/fill — always visible, varies by hover/press
	//   Layer 1 (CheckImage): the checkmark shape — only visible when checked/undetermined
	{
		const float CornerRadius = 3.0f;
		const float OutlineWidth = 1.0f;
		const FVector2D BoxSize(18.0f, 18.0f);

		const FLinearColor InputLin   = FN2CUIColors::ToLinear(Colors.BgInput);
		const FLinearColor HoverLin   = FN2CUIColors::ToLinear(Colors.BgHover);
		const FLinearColor BorderLin  = FN2CUIColors::ToLinear(Colors.BorderColor);
		const FLinearColor SubtleLin  = FN2CUIColors::ToLinear(Colors.BorderSubtle);
		const FLinearColor OrangeLin  = FN2CUIColors::ToLinear(Colors.AccentOrange);
		const FLinearColor OrangeDim  = FN2CUIColors::ToLinearWithAlpha(Colors.AccentOrange, 0.85f);

		// Box background (always visible — provides the checkbox outline/fill)
		N2CCheckBoxStyle.SetBackgroundImage(FSlateRoundedBoxBrush(InputLin, CornerRadius, SubtleLin, OutlineWidth, BoxSize));
		N2CCheckBoxStyle.SetBackgroundHoveredImage(FSlateRoundedBoxBrush(HoverLin, CornerRadius, BorderLin, OutlineWidth, BoxSize));
		N2CCheckBoxStyle.SetBackgroundPressedImage(FSlateRoundedBoxBrush(HoverLin, CornerRadius, BorderLin, OutlineWidth, BoxSize));

		// Unchecked: nothing on top of the box background
		N2CCheckBoxStyle.SetUncheckedImage(FSlateNoResource());
		N2CCheckBoxStyle.SetUncheckedHoveredImage(FSlateNoResource());
		N2CCheckBoxStyle.SetUncheckedPressedImage(FSlateNoResource());

		// Checked: copy engine's checkmark SVG, retint with AccentOrange
		const FCheckBoxStyle& EngineCheckBox = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");

		FSlateBrush CheckedBrush = EngineCheckBox.CheckedImage;
		CheckedBrush.TintColor = FSlateColor(OrangeLin);
		N2CCheckBoxStyle.SetCheckedImage(CheckedBrush);

		FSlateBrush CheckedHovBrush = EngineCheckBox.CheckedHoveredImage;
		CheckedHovBrush.TintColor = FSlateColor(OrangeDim);
		N2CCheckBoxStyle.SetCheckedHoveredImage(CheckedHovBrush);

		FSlateBrush CheckedPressBrush = EngineCheckBox.CheckedPressedImage;
		CheckedPressBrush.TintColor = FSlateColor(OrangeLin);
		N2CCheckBoxStyle.SetCheckedPressedImage(CheckedPressBrush);

		// Undetermined: copy engine's indeterminate SVG, retint
		FSlateBrush UndBrush = EngineCheckBox.UndeterminedImage;
		UndBrush.TintColor = FSlateColor(OrangeDim);
		N2CCheckBoxStyle.SetUndeterminedImage(UndBrush);

		FSlateBrush UndHovBrush = EngineCheckBox.UndeterminedHoveredImage;
		UndHovBrush.TintColor = FSlateColor(OrangeDim);
		N2CCheckBoxStyle.SetUndeterminedHoveredImage(UndHovBrush);

		FSlateBrush UndPressBrush = EngineCheckBox.UndeterminedPressedImage;
		UndPressBrush.TintColor = FSlateColor(OrangeLin);
		N2CCheckBoxStyle.SetUndeterminedPressedImage(UndPressBrush);

		// White foreground so TintColor alone determines the check image color
		N2CCheckBoxStyle.SetForegroundColor(FSlateColor(FLinearColor::White));
		N2CCheckBoxStyle.SetHoveredForegroundColor(FSlateColor(FLinearColor::White));
		N2CCheckBoxStyle.SetPressedForegroundColor(FSlateColor(FLinearColor::White));
		N2CCheckBoxStyle.SetCheckedForegroundColor(FSlateColor(FLinearColor::White));
		N2CCheckBoxStyle.SetCheckedHoveredForegroundColor(FSlateColor(FLinearColor::White));
		N2CCheckBoxStyle.SetCheckedPressedForegroundColor(FSlateColor(FLinearColor::White));
		N2CCheckBoxStyle.SetUndeterminedForegroundColor(FSlateColor(FLinearColor::White));
	}

	// ── EditableTextBox Style ──
	{
		const FSlateBrush NormalBg = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgInput), 4.0f,
			FN2CUIColors::ToLinear(Colors.BorderSubtle), 1.0f);
		const FSlateBrush HoveredBg = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgInput), 4.0f,
			FN2CUIColors::ToLinear(Colors.BorderColor), 1.0f);
		const FSlateBrush FocusedBg = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgInput), 4.0f,
			FN2CUIColors::ToLinear(Colors.AccentOrange), 1.0f);
		const FSlateBrush ReadOnlyBg = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgPanelDarker), 4.0f,
			FN2CUIColors::ToLinear(Colors.BorderSubtle), 1.0f);

		N2CEditableTextBoxStyle.SetBackgroundImageNormal(NormalBg);
		N2CEditableTextBoxStyle.SetBackgroundImageHovered(HoveredBg);
		N2CEditableTextBoxStyle.SetBackgroundImageFocused(FocusedBg);
		N2CEditableTextBoxStyle.SetBackgroundImageReadOnly(ReadOnlyBg);
		N2CEditableTextBoxStyle.SetForegroundColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextPrimary)));
		N2CEditableTextBoxStyle.SetFocusedForegroundColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextPrimary)));
		N2CEditableTextBoxStyle.SetReadOnlyForegroundColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextSecondary)));
		N2CEditableTextBoxStyle.SetPadding(FMargin(4.0f, 2.0f));
	}

	// ── TableRow Style ──
	// Three visual states only: default, hovered, selected.
	// All selected sub-states (active/inactive, hovered/unhovered) use the same brush
	// so there's no visual flickering when focus changes.
	{
		const FSlateBrush TransparentBrush = FSlateNoResource();
		const FSlateColorBrush HoverBrush(FN2CUIColors::ToLinearWithAlpha(Colors.BgHover, 0.5f));
		const FSlateColorBrush SelectedBrush(FN2CUIColors::ToLinearWithAlpha(Colors.AccentOrangeDim, 0.5f));

		// SelectorFocusedBrush defaults to FSlateBrush() which renders as solid white
		// (DrawAs=Image, TintColor=White, no resource → white fallback texture).
		// Must explicitly clear it so it doesn't bleed through semi-transparent selection brushes.
		N2CTableRowStyle.SetSelectorFocusedBrush(TransparentBrush);
		N2CTableRowStyle.SetEvenRowBackgroundBrush(TransparentBrush);
		N2CTableRowStyle.SetOddRowBackgroundBrush(TransparentBrush);
		N2CTableRowStyle.SetEvenRowBackgroundHoveredBrush(HoverBrush);
		N2CTableRowStyle.SetOddRowBackgroundHoveredBrush(HoverBrush);
		N2CTableRowStyle.SetActiveBrush(SelectedBrush);
		N2CTableRowStyle.SetActiveHoveredBrush(SelectedBrush);
		N2CTableRowStyle.SetInactiveBrush(SelectedBrush);
		N2CTableRowStyle.SetInactiveHoveredBrush(SelectedBrush);
		N2CTableRowStyle.SetActiveHighlightedBrush(SelectedBrush);
		N2CTableRowStyle.SetInactiveHighlightedBrush(SelectedBrush);
		N2CTableRowStyle.SetTextColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextPrimary)));
		N2CTableRowStyle.SetSelectedTextColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextPrimary)));
	}

	// ── HeaderRow Style ──
	{
		FTableColumnHeaderStyle ColumnStyle;
		ColumnStyle.SetNormalBrush(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgPanelDarker), 0.0f));
		ColumnStyle.SetHoveredBrush(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgHover), 0.0f));

		N2CHeaderRowStyle.SetColumnStyle(ColumnStyle);
		N2CHeaderRowStyle.SetLastColumnStyle(ColumnStyle);
		N2CHeaderRowStyle.SetBackgroundBrush(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgPanelDarker), 0.0f));
		N2CHeaderRowStyle.SetForegroundColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextSecondary)));
		N2CHeaderRowStyle.SetHorizontalSeparatorBrush(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BorderSubtle), 0.0f));
		N2CHeaderRowStyle.SetHorizontalSeparatorThickness(1.0f);
	}

	// ── ScrollBar Style ──
	{
		const FSlateBrush TrackBrush = FSlateRoundedBoxBrush(FN2CUIColors::ToLinearWithAlpha(Colors.BgPanelDarker, 0.5f), 4.0f);
		const FSlateBrush ThumbNormal = FSlateRoundedBoxBrush(FN2CUIColors::ToLinearWithAlpha(Colors.BorderColor, 0.6f), 4.0f);
		const FSlateBrush ThumbHover = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgHover), 4.0f);
		const FSlateBrush ThumbDragged = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnHovered), 4.0f);

		N2CScrollBarStyle.SetHorizontalBackgroundImage(TrackBrush);
		N2CScrollBarStyle.SetVerticalBackgroundImage(TrackBrush);
		N2CScrollBarStyle.SetNormalThumbImage(ThumbNormal);
		N2CScrollBarStyle.SetHoveredThumbImage(ThumbHover);
		N2CScrollBarStyle.SetDraggedThumbImage(ThumbDragged);
	}
}

void N2CStyle::UpdateComboBoxStyle()
{
	const FN2CUIColors& Colors = N2C::UI::Colors();

	const FSlateColor NormalFg(FN2CUIColors::ToLinear(Colors.BtnForeground));
	const FSlateColor HoveredFg(FN2CUIColors::ToLinear(Colors.BtnForegroundHover));
	const FSlateColor DisabledFg(FN2CUIColors::ToLinear(Colors.BtnForegroundDisabled));

	// ── Combo button style ──
	FButtonStyle ComboBtn;
	ComboBtn.SetNormal(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgInput), 4.0f));
	ComboBtn.SetHovered(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgHover), 4.0f));
	ComboBtn.SetPressed(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnPressed), 4.0f));
	ComboBtn.SetDisabled(FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnDisabled), 4.0f));
	ComboBtn.SetNormalForeground(NormalFg);
	ComboBtn.SetHoveredForeground(HoveredFg);
	ComboBtn.SetPressedForeground(NormalFg);
	ComboBtn.SetDisabledForeground(DisabledFg);
	ComboBtn.SetNormalPadding(FMargin(4.0f, 2.0f));
	ComboBtn.SetPressedPadding(FMargin(4.0f, 3.0f, 4.0f, 1.0f));

	N2CComboBoxStyle.ComboButtonStyle.SetButtonStyle(ComboBtn);

	// Copy the engine's registered down arrow brush and tint with our foreground color
	const FComboButtonStyle& EngineComboBtn = FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");
	FSlateBrush ArrowBrush = EngineComboBtn.DownArrowImage;
	ArrowBrush.TintColor = NormalFg;
	N2CComboBoxStyle.ComboButtonStyle.SetDownArrowImage(ArrowBrush);
	N2CComboBoxStyle.ComboButtonStyle.SetDownArrowPadding(FMargin(2.0f, 0.0f));

	N2CComboBoxStyle.ComboButtonStyle.SetMenuBorderBrush(
		FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgPanelDarker), 4.0f,
			FN2CUIColors::ToLinear(Colors.BorderColor), 1.0f));
	N2CComboBoxStyle.ComboButtonStyle.SetMenuBorderPadding(FMargin(2.0f));
	N2CComboBoxStyle.SetContentPadding(FMargin(4.0f, 2.0f));
	N2CComboBoxStyle.SetMenuRowPadding(FMargin(2.0f));

	// ── Dropdown row style ──
	const FSlateBrush TransparentBrush = FSlateNoResource();
	const FSlateBrush HoverBrush = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BgHover), 4.0f);
	const FSlateBrush ActiveBrush = FSlateRoundedBoxBrush(FN2CUIColors::ToLinear(Colors.BtnPressed), 4.0f);

	N2CComboRowStyle.SetEvenRowBackgroundBrush(TransparentBrush);
	N2CComboRowStyle.SetOddRowBackgroundBrush(TransparentBrush);
	N2CComboRowStyle.SetEvenRowBackgroundHoveredBrush(HoverBrush);
	N2CComboRowStyle.SetOddRowBackgroundHoveredBrush(HoverBrush);
	N2CComboRowStyle.SetActiveBrush(ActiveBrush);
	N2CComboRowStyle.SetActiveHoveredBrush(HoverBrush);
	N2CComboRowStyle.SetInactiveBrush(ActiveBrush);
	N2CComboRowStyle.SetInactiveHoveredBrush(HoverBrush);
	N2CComboRowStyle.SetTextColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextPrimary)));
	N2CComboRowStyle.SetSelectedTextColor(FSlateColor(FN2CUIColors::ToLinear(Colors.TextPrimary)));
}
