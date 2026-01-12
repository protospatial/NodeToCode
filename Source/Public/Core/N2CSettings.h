// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "N2CUserSecrets.h"
#include "Code Editor/Models/N2CCodeLanguage.h"
#include "Engine/DeveloperSettings.h"
#include "LLM/N2CLLMModels.h"
#include "LLM/N2CLLMTypes.h"
#include "LLM/N2COllamaConfig.h"
#include "Models/N2CLogging.h"
#include "Styling/SlateColor.h"
#include "N2CSettings.generated.h"

USTRUCT(BlueprintType)
struct FN2CCodeEditorColors
{
    GENERATED_BODY()

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Normal Text"))
    FColor NormalText = FColor(0xffd6d6d6);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Operators"))
    FColor Operators = FColor(0xffe87d3e);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Keywords"))
    FColor Keywords = FColor(0xff9e86c8);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Strings"))
    FColor Strings = FColor(0xffe5b567);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Numbers"))
    FColor Numbers = FColor(0xff1c33ff);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Comments"))
    FColor Comments = FColor(0xff797979);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Preprocessor"))
    FColor Preprocessor = FColor(0xfff75340);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Parentheses"))
    FColor Parentheses = FColor(0xff00bfff);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Curly Braces"))
    FColor CurlyBraces = FColor(0xffe87d3e);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Square Brackets"))
    FColor SquareBrackets = FColor(0xff98fb98);

    UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Node to Code | Settings | Theming | Colors", meta = (DisplayName = "Background"))
    FColor Background = FColor(0xff1e1e1e);  // Dark gray default, similar to VS Code
};

USTRUCT(BlueprintType)
struct FN2CCodeEditorThemes
{
    GENERATED_BODY()

    UPROPERTY(config, EditAnywhere, Category = "Node to Code | Settings | Theming", meta = (DisplayName = "Themes", 
        ToolTip = "Collection of color themes for syntax highlighting"))
    TMap<FName, FN2CCodeEditorColors> Themes;

    FN2CCodeEditorThemes()
    {

        FN2CCodeEditorColors Spacedust;
        Spacedust.Background    = FColor(0xff0b1a20); // Estimated dark teal/blue background (#0B1A20)
        Spacedust.NormalText    = FColor(0xfff0f1ce); // Light but not pure white (#F0F1CE)
        Spacedust.Keywords      = FColor(0xffe35b00); // Vibrant orange (#E35B00)
        Spacedust.Operators     = FColor(0xff06afc7); // Bright teal (#06AFC7)
        Spacedust.Strings       = FColor(0xffe3cd7b); // Subtle light yellow (#E3CD7B)
        Spacedust.Numbers       = FColor(0xff5cab96); // Muted teal (#5CAB96)
        Spacedust.Comments      = FColor(0xff684c31); // Softer brown for comments (#684C31)
        Spacedust.Preprocessor  = FColor(0xffff8a3a); // Bolder orange (#FF8A3A)
        Spacedust.Parentheses   = FColor(0xff67a0ce); // Cool blue (#67A0CE)
        Spacedust.CurlyBraces   = FColor(0xff83a7b4); // Gray‐blue highlight (#83A7B4)
        Spacedust.SquareBrackets= FColor(0xffaecab8); // Soft greenish tint (#AECAB8)

        Themes.Add("Spacedust", Spacedust);

        FN2CCodeEditorColors UbuntuCloneTheme;
        UbuntuCloneTheme.Background = FColor(0xff2D0A31); // Deep purple background matching Ubuntu's style
        UbuntuCloneTheme.NormalText = FColor(0xffEEEEEC); // Light gray for comfortable reading
        UbuntuCloneTheme.Keywords = FColor(0xff4E9A06); // Ubuntu-style green for emphasis
        UbuntuCloneTheme.Operators = FColor(0xffCC0000); // Classic Ubuntu red for operators
        UbuntuCloneTheme.Strings = FColor(0xffC4A000); // Warm gold for string literals
        UbuntuCloneTheme.Numbers = FColor(0xff729FCF); // Soft blue for numerical values
        UbuntuCloneTheme.Comments = FColor(0xff75507B); // Muted purple for comments
        UbuntuCloneTheme.Preprocessor = FColor(0xffEF2929); // Bright red for preprocessor directives
        UbuntuCloneTheme.Parentheses = FColor(0xff3465A4); // Deep blue for parentheses
        UbuntuCloneTheme.CurlyBraces = FColor(0xff06989A); // Teal for curly braces
        UbuntuCloneTheme.SquareBrackets = FColor(0xff8AE234); // Bright green for square brackets
        Themes.Add("Ubuntu", UbuntuCloneTheme); // Adding the Ubuntu-inspired theme

        FN2CCodeEditorColors Renaissance;
        Renaissance.Background = FColor(0xff1a1a1a); // Dark charcoal background for reduced eye strain
        Renaissance.NormalText = FColor(0xff9eb2b4); // Soft blue-gray for comfortable reading
        Renaissance.Keywords = FColor(0xffc36e28); // Warm orange for emphasis on important terms
        Renaissance.Operators = FColor(0xff9b291c); // Deep red for clear operator visibility
        Renaissance.Strings = FColor(0xfff7d75c); // Muted yellow for string literals
        Renaissance.Numbers = FColor(0xffff4331); // Bright red for numeric values
        Renaissance.Comments = FColor(0xff636232); // Olive green for subtle commenting
        Renaissance.Preprocessor = FColor(0xff874228); // Rich brown for preprocessor directives
        Renaissance.Parentheses = FColor(0xff515c5d); // Neutral gray for balanced visibility
        Renaissance.CurlyBraces = FColor(0xff8acd8f); // Soft green for block delimiters
        Renaissance.SquareBrackets = FColor(0xffff5b6a); // Coral pink for array operations
        Themes.Add("Renaissance", Renaissance); // Adding the earthy-toned theme
        
        FN2CCodeEditorColors UnrealEngineTheme;
        UnrealEngineTheme.Background = FColor(0xff242424); // Dark charcoal background matching UE5 editor
        UnrealEngineTheme.NormalText = FColor(0xffc0c0c0); // Soft silver for comfortable reading
        UnrealEngineTheme.Keywords = FColor(0xff0070e0); // Accent blue for emphasis on language keywords
        UnrealEngineTheme.Operators = FColor(0xffA8A8A8); // Accent orange for clear operator visibility
        UnrealEngineTheme.Strings = FColor(0xffffb800); // UE gold for string literals
        UnrealEngineTheme.Numbers = FColor(0xff8bc24a); // Primary UE blue for numerical values
        UnrealEngineTheme.Comments = FColor(0xff484848); // Darker grey for subtle comments
        UnrealEngineTheme.Preprocessor = FColor(0xffff4040); // Accent red for preprocessor directives
        UnrealEngineTheme.Parentheses = FColor(0xff0097E0); // Primary UE blue for grouping
        UnrealEngineTheme.CurlyBraces = FColor(0xfffe9b07); // Accent orange matching operators
        UnrealEngineTheme.SquareBrackets = FColor(0xff26bbff); // Single green element for visual interest
        Themes.Add("Unreal Engine", UnrealEngineTheme); // Professional dark theme inspired by UE5
        
        FN2CCodeEditorColors MidnightCode;
        MidnightCode.Background = FColor(0xff1e1e1e); // Dark charcoal background
        MidnightCode.NormalText = FColor(0xffd4d4d4); // Soft light gray for main text
        MidnightCode.Keywords = FColor(0xff569cd6); // Soft blue for keywords
        MidnightCode.Operators = FColor(0xffd4d4d4); // Light gray for operators
        MidnightCode.Strings = FColor(0xffce9178); // Muted orange for strings
        MidnightCode.Numbers = FColor(0xffb5cea8); // Sage green for numbers
        MidnightCode.Comments = FColor(0xff608b4e); // Forest green for comments
        MidnightCode.Preprocessor = FColor(0xff9b9b9b); // Medium gray for preprocessor
        MidnightCode.Parentheses = FColor(0xffffd700); // Soft gold for parentheses
        MidnightCode.CurlyBraces = FColor(0xffffd700); // Matching gold for curly braces
        MidnightCode.SquareBrackets = FColor(0xffffd700); // Consistent gold for brackets
        Themes.Add("Midnight Code", MidnightCode); // Modern dark theme inspired by popular IDEs

        FN2CCodeEditorColors MidnightNeon;
        MidnightNeon.Background = FColor(0xff171615); // Dark charcoal background for reduced eye strain
        MidnightNeon.NormalText = FColor(0xff61eeff); // Soft gray for main text
        MidnightNeon.Keywords = FColor(0xfffa7159); // Muted red for keywords
        MidnightNeon.Operators = FColor(0xfff898b5); // Warm orange for operators
        MidnightNeon.Strings = FColor(0xfffbd14c); // Soft coral for string literals
        MidnightNeon.Numbers = FColor(0xff0072ff); // Bright blue for numeric values
        MidnightNeon.Comments = FColor(0xffa39f9b); // Forest green for comments
        MidnightNeon.Preprocessor = FColor(0xff4c94ff); // Muted red for preprocessor directives
        MidnightNeon.Parentheses = FColor(0xff716cf7); // Cyan blue for parentheses
        MidnightNeon.CurlyBraces = FColor(0xfff88e06); // Dark orange for curly braces
        MidnightNeon.SquareBrackets = FColor(0xff9feb25); // Sea green for square brackets
        Themes.Add("Midnight Neon", MidnightNeon); // Theme inspired by neon lights at midnight

        FN2CCodeEditorColors MonoAmber;
        MonoAmber.Background = FColor(0xff1a1410); // Dark brown-black background for contrast
        MonoAmber.NormalText = FColor(0xffff9400); // Primary amber for main text
        MonoAmber.Keywords = FColor(0xffffb649); // Brighter amber for emphasis on keywords
        MonoAmber.Operators = FColor(0xffff8330); // Darker orange for operators
        MonoAmber.Strings = FColor(0xffffa54f); // Soft amber for string literals
        MonoAmber.Numbers = FColor(0xffff7f00); // Deep amber for numbers
        MonoAmber.Comments = FColor(0xff8b5000); // Muted amber for less emphasis on comments
        MonoAmber.Preprocessor = FColor(0xffffc87f); // Light amber for preprocessor directives
        MonoAmber.Parentheses = FColor(0xffff9933); // Medium amber for parentheses
        MonoAmber.CurlyBraces = FColor(0xffff9933); // Matching parentheses color for consistency
        MonoAmber.SquareBrackets = FColor(0xffff9933); // Matching bracket color for consistency
        Themes.Add("Mono Amber", MonoAmber); // Adding the amber monochrome theme
        
        FN2CCodeEditorColors BeigeEarth;
        BeigeEarth.Background = FColor(0xfff5f0e8); // Soft beige background that's gentle on the eyes
        BeigeEarth.NormalText = FColor(0xff4a4a46); // Warm dark gray for comfortable reading
        BeigeEarth.Keywords = FColor(0xff876c99); // Muted purple for subtle emphasis
        BeigeEarth.Operators = FColor(0xffcb7f5c); // Terracotta orange for warm contrast
        BeigeEarth.Strings = FColor(0xff7d9867); // Sage green for natural feel
        BeigeEarth.Numbers = FColor(0xffb87d4b); // Warm brown for earthen touch
        BeigeEarth.Comments = FColor(0xff998e7d); // Light brown gray for subtle comments
        BeigeEarth.Preprocessor = FColor(0xffa65d57); // Clay red for preprocessor directives
        BeigeEarth.Parentheses = FColor(0xff6a8a8a); // Slate blue-gray for brackets
        BeigeEarth.CurlyBraces = FColor(0xff8f7355); // Wooden brown for braces
        BeigeEarth.SquareBrackets = FColor(0xff739187); // Forest green for brackets
        Themes.Add("Beige Earth", BeigeEarth); // Natural earth-toned theme
        
        FN2CCodeEditorColors BeigeEarthDark;
        BeigeEarthDark.Background = FColor(0xff1e1a17); // Deep dark brown background
        BeigeEarthDark.NormalText = FColor(0xffd5cec5); // Warm light gray for comfortable reading
        BeigeEarthDark.Keywords = FColor(0xffb391c7); // Deep lavender for natural emphasis
        BeigeEarthDark.Operators = FColor(0xffe6946a); // Warm copper orange
        BeigeEarthDark.Strings = FColor(0xff9ab87b); // Deep sage green
        BeigeEarthDark.Numbers = FColor(0xffd4956b); // Rich clay brown
        BeigeEarthDark.Comments = FColor(0xff7a7068); // Muted stone gray
        BeigeEarthDark.Preprocessor = FColor(0xffc27171); // Deep terracotta red
        BeigeEarthDark.Parentheses = FColor(0xff8ba7a7); // Deep slate blue-gray
        BeigeEarthDark.CurlyBraces = FColor(0xffb39370); // Rich wooden brown
        BeigeEarthDark.SquareBrackets = FColor(0xff8fb4a3); // Deep forest green
        Themes.Add("Beige Earth Dark", BeigeEarthDark); // Dark earth-toned theme

        FN2CCodeEditorColors CitrusDelight;
        CitrusDelight.Background = FColor(0xff242424); // Dark charcoal for reduced eye strain
        CitrusDelight.NormalText = FColor(0xfff0e6d2); // Warm off-white for comfortable reading
        CitrusDelight.Keywords = FColor(0xffff9933); // Bright orange for emphasis
        CitrusDelight.Operators = FColor(0xffFFB84D); // Soft tangerine for subtle operations
        CitrusDelight.Strings = FColor(0xffFFD700); // Golden yellow for string literals
        CitrusDelight.Numbers = FColor(0xffFFCC00); // Amber yellow for numerical values
        CitrusDelight.Comments = FColor(0xff98C379); // Lime green for natural comment flow
        CitrusDelight.Preprocessor = FColor(0xffFF6B33); // Bright citrus orange for directives
        CitrusDelight.Parentheses = FColor(0xffE6B800); // Muted gold for grouping
        CitrusDelight.CurlyBraces = FColor(0xffFF8533); // Warm orange for block definition
        CitrusDelight.SquareBrackets = FColor(0xffB4E33D); // Light lime for array access
        Themes.Add("Citrus Delight", CitrusDelight); // Fresh and energizing citrus theme

        FN2CCodeEditorColors CuppaJoe;
        CuppaJoe.Background = FColor(0xff231812); // Deep espresso brown background
        CuppaJoe.NormalText = FColor(0xffdecbb7); // Creamy latte color for main text
        CuppaJoe.Keywords = FColor(0xffd4915d); // Warm caramel brown for emphasis
        CuppaJoe.Operators = FColor(0xffb87349); // Roasted coffee bean brown
        CuppaJoe.Strings = FColor(0xffc69c6d); // Cappuccino foam brown
        CuppaJoe.Numbers = FColor(0xffa65d57); // Reddish mocha brown
        CuppaJoe.Comments = FColor(0xff8b6147); // Muted coffee ground brown
        CuppaJoe.Preprocessor = FColor(0xffe6a972); // Light toffee brown
        CuppaJoe.Parentheses = FColor(0xffbe8c63); // Smooth latte brown
        CuppaJoe.CurlyBraces = FColor(0xffd49f7c); // Cinnamon brown
        CuppaJoe.SquareBrackets = FColor(0xffcca182); // Hazelnut brown
        Themes.Add("Cuppa Joe", CuppaJoe); // Coffee-inspired brown theme

        FN2CCodeEditorColors CyberNight;
        CyberNight.Background = FColor(0xff0a0b14); // Deep night sky blue-black
        CyberNight.NormalText = FColor(0xffb4e1ff); // Soft blue-white for easy reading
        CyberNight.Keywords = FColor(0xff00ffdd); // Cyber teal for emphasis
        CyberNight.Operators = FColor(0xff9d61ff); // Neon purple for visual pop
        CyberNight.Strings = FColor(0xffff7b9c); // Neon pink for string content
        CyberNight.Numbers = FColor(0xffff9b3c); // Warm neon orange for numerical values
        CyberNight.Comments = FColor(0xff4a5a7d); // Muted blue for less emphasis
        CyberNight.Preprocessor = FColor(0xff00aaff); // Bright blue for directives
        CyberNight.Parentheses = FColor(0xff36d5ff); // Light blue for grouping
        CyberNight.CurlyBraces = FColor(0xff7d52ff); // Purple for block definition
        CyberNight.SquareBrackets = FColor(0xff00cc9a); // Seafoam green for arrays
        Themes.Add("Cyber Night", CyberNight); // Modern cyberpunk theme

        FN2CCodeEditorColors ForbiddenForest;
        ForbiddenForest.Background = FColor(0xff1a1f1a); // Deep forest shadow background
        ForbiddenForest.NormalText = FColor(0xffbec5b2); // Soft sage text for readability
        ForbiddenForest.Keywords = FColor(0xff9b7bb4); // Mystical purple highlights
        ForbiddenForest.Operators = FColor(0xffe8a84d); // Warm amber like fireflies
        ForbiddenForest.Strings = FColor(0xff7ea364); // Mossy forest green
        ForbiddenForest.Numbers = FColor(0xff5c8dd6); // Magical blue glow
        ForbiddenForest.Comments = FColor(0xff667766); // Shadowy forest green
        ForbiddenForest.Preprocessor = FColor(0xffcf4f4f); // Deep forest flower red
        ForbiddenForest.Parentheses = FColor(0xff49b3cc); // Ethereal cyan glow
        ForbiddenForest.CurlyBraces = FColor(0xffe8a84d); // Matching amber with operators
        ForbiddenForest.SquareBrackets = FColor(0xff98c379); // Fresh leaf green
        Themes.Add("Forbidden Forest", ForbiddenForest); // A mystical forest theme
        
        FN2CCodeEditorColors NightSkyTheme;
        NightSkyTheme.Background = FColor(0xff0f1117); // Deep night sky blue-black
        NightSkyTheme.NormalText = FColor(0xffd8d9ff); // Soft starlight
        NightSkyTheme.Keywords = FColor(0xff9d7cd8); // Cosmic purple
        NightSkyTheme.Operators = FColor(0xff8aa2ff); // Bright star blue
        NightSkyTheme.Strings = FColor(0xffb4c2ff); // Pale nebula blue
        NightSkyTheme.Numbers = FColor(0xff7aa2f7); // Clear night sky blue
        NightSkyTheme.Comments = FColor(0xff6c7bba); // Distant star blue
        NightSkyTheme.Preprocessor = FColor(0xffa48cdb); // Deep purple
        NightSkyTheme.Parentheses = FColor(0xff89ddff); // Bright blue
        NightSkyTheme.CurlyBraces = FColor(0xff9d8cff); // Soft purple
        NightSkyTheme.SquareBrackets = FColor(0xff7dcfff); // Light blue
        Themes.Add("Night Sky", NightSkyTheme); // Celestial blues and purples theme

        FN2CCodeEditorColors MidnightByte;
        MidnightByte.Background = FColor(0xff0a0d14); // Deep space black with slight blue tint
        MidnightByte.NormalText = FColor(0xffe1e9f7); // Soft blue-white for comfortable reading
        MidnightByte.Keywords = FColor(0xff00ccff); // Electric blue for emphasis
        MidnightByte.Operators = FColor(0xff7b68ee); // Medium slate blue for subtle contrast
        MidnightByte.Strings = FColor(0xff36f1cd); // Cyan-mint for data representation
        MidnightByte.Numbers = FColor(0xff7df3e1); // Light cyan for numerical values
        MidnightByte.Comments = FColor(0xff4a5a78); // Muted blue-grey for less emphasis
        MidnightByte.Preprocessor = FColor(0xff9d60ff); // Bright purple for directives
        MidnightByte.Parentheses = FColor(0xff00a2ff); // Bright blue for grouping
        MidnightByte.CurlyBraces = FColor(0xff0088cc); // Darker blue for code blocks
        MidnightByte.SquareBrackets = FColor(0xff00d5ff); // Lightest blue for arrays
        Themes.Add("Midnight Byte", MidnightByte); // Cyberpunk-inspired dark theme
        
        FN2CCodeEditorColors PixelPhosphor;
        PixelPhosphor.Background = FColor(0xff1a1c1a); // Deep charcoal with slight green tint
        PixelPhosphor.NormalText = FColor(0xffb4e0b4); // Soft phosphor green-white
        PixelPhosphor.Keywords = FColor(0xff00ff00); // Classic terminal green
        PixelPhosphor.Operators = FColor(0xff00d7d7); // Bright cyan for contrast
        PixelPhosphor.Strings = FColor(0xffffb054); // Warm amber for string literals
        PixelPhosphor.Numbers = FColor(0xffff8c00); // Dark amber for numerics
        PixelPhosphor.Comments = FColor(0xff4a634a); // Muted green for less emphasis
        PixelPhosphor.Preprocessor = FColor(0xff00ff9c); // Bright mint green
        PixelPhosphor.Parentheses = FColor(0xff20c20e); // Medium phosphor green
        PixelPhosphor.CurlyBraces = FColor(0xff2ecc71); // Emerald green
        PixelPhosphor.SquareBrackets = FColor(0xff00fa9a); // Medium spring green
        Themes.Add("Pixel Phosphor", PixelPhosphor); // Retro-inspired phosphor theme

        FN2CCodeEditorColors NightShift;
        NightShift.Background = FColor(0xff1a1614); // Warm dark charcoal with red undertone
        NightShift.NormalText = FColor(0xfff0d4c0); // Soft warm peach for comfortable reading
        NightShift.Keywords = FColor(0xffff7f66); // Coral red for emphasis
        NightShift.Operators = FColor(0xffe67e43); // Warm orange for good visibility
        NightShift.Strings = FColor(0xffd4a86c); // Amber for string literals
        NightShift.Numbers = FColor(0xffcf8466); // Terra cotta for numerics
        NightShift.Comments = FColor(0xff8a7b73); // Warm gray for unobtrusive comments
        NightShift.Preprocessor = FColor(0xffff6b55); // Bright coral for preprocessor directives
        NightShift.Parentheses = FColor(0xffd98d6a); // Light copper for parentheses
        NightShift.CurlyBraces = FColor(0xffe67d3e); // Dark orange for curly braces
        NightShift.SquareBrackets = FColor(0xffc17f59); // Bronze for square brackets
        Themes.Add("Night Shift", NightShift); // Warm color palette with no cool tones reduced eye strain
        
        FN2CCodeEditorColors StealthTheme;
        StealthTheme.Background = FColor(0xff151515); // Keeping the deep charcoal background
        StealthTheme.NormalText = FColor(0xffd8d8d8); // Significantly brighter for better readability
        StealthTheme.Keywords = FColor(0xffe5e5e5); // Brightest element for maximum emphasis
        StealthTheme.Operators = FColor(0xffd0d0d0); // Bright enough to stand out clearly
        StealthTheme.Strings = FColor(0xffb8b8b8); // Medium-bright gray for clear string visibility
        StealthTheme.Numbers = FColor(0xffc5c5c5); // Distinct from strings but not overpowering
        StealthTheme.Comments = FColor(0xff808080); // Darker but still clearly visible
        StealthTheme.Preprocessor = FColor(0xffd5d5d5); // Very visible for important directives
        StealthTheme.Parentheses = FColor(0xffc8c8c8); // Bright structural elements
        StealthTheme.CurlyBraces = FColor(0xffcecece); // Slightly brighter than parentheses
        StealthTheme.SquareBrackets = FColor(0xffc8c8c8); // Matching parentheses brightness
        Themes.Add("Stealth", StealthTheme); // High-contrast monochrome theme

        FN2CCodeEditorColors MonoWhiteTheme;
        MonoWhiteTheme.Background = FColor(0xfff0f0f0); // Soft white background to reduce eye strain
        MonoWhiteTheme.NormalText = FColor(0xff272727); // Dark gray for comfortable reading
        MonoWhiteTheme.Keywords = FColor(0xff1a1a1a); // Darkest shade for emphasis
        MonoWhiteTheme.Operators = FColor(0xff2f2f2f); // Dark but slightly lighter than keywords
        MonoWhiteTheme.Strings = FColor(0xff474747); // Medium dark gray for string visibility
        MonoWhiteTheme.Numbers = FColor(0xff3a3a3a); // Distinct from strings but still prominent
        MonoWhiteTheme.Comments = FColor(0xff7f7f7f); // Lighter gray for less emphasis
        MonoWhiteTheme.Preprocessor = FColor(0xff2a2a2a); // Very dark for important directives
        MonoWhiteTheme.Parentheses = FColor(0xff373737); // Dark structural elements
        MonoWhiteTheme.CurlyBraces = FColor(0xff313131); // Slightly darker than parentheses
        MonoWhiteTheme.SquareBrackets = FColor(0xff373737); // Matching parentheses darkness
        Themes.Add("Mono White", MonoWhiteTheme); // Clean, professional light theme

        FN2CCodeEditorColors StudioBlue;
        StudioBlue.Background = FColor(0xfff8f9fc); // Soft white with slight blue tint for reduced eye strain
        StudioBlue.NormalText = FColor(0xff1f1f1f); // Dark gray for main text - softer than pure black
        StudioBlue.Keywords = FColor(0xff0000ff); // Classic Visual Studio blue for keywords
        StudioBlue.Operators = FColor(0xff000000); // Near-black for operators to maintain clarity
        StudioBlue.Strings = FColor(0xffa31515); // Traditional VS dark red for string literals
        StudioBlue.Numbers = FColor(0xff098658); // Teal green for numeric values
        StudioBlue.Comments = FColor(0xff008000); // Classic VS green for comments
        StudioBlue.Preprocessor = FColor(0xff800080); // Purple for preprocessor directives
        StudioBlue.Parentheses = FColor(0xff1f1f1f); // Dark gray matching normal text
        StudioBlue.CurlyBraces = FColor(0xff1f1f1f); // Dark gray matching normal text
        StudioBlue.SquareBrackets = FColor(0xff1f1f1f); // Dark gray matching normal text
        Themes.Add("Studio Blue", StudioBlue); // Classic Visual Studio inspired theme

        FN2CCodeEditorColors Crisp;
        Crisp.Background = FColor(0xfff5f5f5); // Soft white background for reduced eye strain
        Crisp.NormalText = FColor(0xff2f3542); // Dark gray for comfortable reading
        Crisp.Keywords = FColor(0xff7d6b9e); // Muted purple for professional look
        Crisp.Operators = FColor(0xffb15a3c); // Rust color for clear operator visibility
        Crisp.Strings = FColor(0xffcd8945); // Warm orange for string literals
        Crisp.Numbers = FColor(0xff3f7cac); // Steel blue for numerical values
        Crisp.Comments = FColor(0xff8b8b8b); // Medium gray for subtle comments
        Crisp.Preprocessor = FColor(0xffcb4b16); // Bright orange for preprocessor directives
        Crisp.Parentheses = FColor(0xff456789); // Navy blue for parentheses
        Crisp.CurlyBraces = FColor(0xff6a8759); // Forest green for curly braces
        Crisp.SquareBrackets = FColor(0xff6c8caf); // Light blue for square brackets
        Themes.Add("Crisp", Crisp); // Professional light theme for studio environments
        
    }
};


// Questions? Check out the Docs: github.com/protospatial/NodeToCode/wiki
UCLASS(Config = NodeToCode, DefaultConfig, meta = (Category = "Node to Code", DisplayName = "Node to Code"))
class NODETOCODE_API UN2CSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UN2CSettings();

    // Begin UDeveloperSettings Interface
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
    virtual FText GetSectionText() const override;
    // End UDeveloperSettings Interface

    /** Selected LLM provider */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Provider")
    EN2CLLMProvider Provider = EN2CLLMProvider::Anthropic;

    /** Reference to user secrets containing API keys */
    UPROPERTY(Transient)
    mutable UN2CUserSecrets* UserSecrets;

    /** Anthropic Model Selection - Sonnet 4 recommended*/
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Anthropic")
    EN2CAnthropicModel AnthropicModel = EN2CAnthropicModel::Claude4_Sonnet;
    
    /** Anthropic API Key - Stored separately in user secrets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | Anthropic",
        meta = (DisplayName = "API Key"))
    FString Anthropic_API_Key_UI;

    /** OpenAI Model Selection - o3-mini recommended for impressive results for a great price, o1 recommended for most thorough results (but quite expensive) */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | OpenAI")
    EN2COpenAIModel OpenAI_Model = EN2COpenAIModel::GPT_o4_mini;

    /** OpenAI API Key - Stored separately in user secrets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | OpenAI",
        meta = (DisplayName = "API Key"))
    FString OpenAI_API_Key_UI;

    /** Gemini Model Selection - 2.5 Flash recommended for best price-performance, 2.5 Pro for most capability */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Gemini")
    EN2CGeminiModel Gemini_Model = EN2CGeminiModel::Gemini_2_5_Flash;

    /** OpenAI API Key - Stored separately in user secrets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | Gemini",
        meta = (DisplayName = "API Key"))
    FString Gemini_API_Key_UI;

    /** DeepSeek Model Selection - R1 recommended for most accurate results */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | DeepSeek")
    EN2CDeepSeekModel DeepSeekModel = EN2CDeepSeekModel::DeepSeek_R1;
    
    /** DeepSeek API Key - Stored separately in user secrets */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Node to Code | LLM Services | DeepSeek",
        meta = (DisplayName = "API Key"))
    FString DeepSeek_API_Key_UI;

    /** Ollama configuration */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Ollama")
    FN2COllamaConfig OllamaConfig;
    
    /** Ollama Model Selection */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Ollama",
        meta=(DisplayName="Model Name"))
    FString OllamaModel = "qwen3:32b";
    
    /** LM Studio Model Selection */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | LM Studio",
        meta=(DisplayName="Model Name"))
    FString LMStudioModel = "qwen3-32b";
    
    /** LM Studio Endpoint - Default is localhost:1234 */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | LM Studio",
        meta=(DisplayName="Server Endpoint"))
    FString LMStudioEndpoint = "http://localhost:1234";
    
    /** LM Studio Prepended Model Command - Text to prepend to user messages (e.g., '/no_think' to disable thinking for reasoning models, or other model-specific commands). This text will appear at the start of each user message. */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | LM Studio",
        meta=(DisplayName="Prepended Model Command", 
              ToolTip="Text to prepend to user messages (e.g., '/no_think' to disable thinking for reasoning models, or other model-specific commands). This text will appear on first line of each user message."))
    FString LMStudioPrependedModelCommand = "";
    
    /** OpenAI Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | OpenAI", DisplayName = "OpenAI Model Pricing")
    TMap<EN2COpenAIModel, FN2COpenAIPricing> OpenAIModelPricing;

    /** Anthropic Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | Anthropic")
    TMap<EN2CAnthropicModel, FN2CAnthropicPricing> AnthropicModelPricing;

    /** Gemini Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | Gemini")
    TMap<EN2CGeminiModel, FN2CGeminiPricing> GeminiModelPricing;

    /** DeepSeek Model Pricing */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | LLM Services | Pricing | DeepSeek")
    TMap<EN2CDeepSeekModel, FN2CDeepSeekPricing> DeepSeekModelPricing;
    
    /** Target programming language for translation */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation", 
        meta=(DisplayName="Target Language"))
    EN2CCodeLanguage TargetLanguage = EN2CCodeLanguage::Cpp;

    /** Maximum depth for nested graph translation (0 = No nested translation). This setting can greatly impact costs and context window utilization, so be mindful! */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation",
        meta=(DisplayName="Max Translation Depth", ClampMin="0", ClampMax="5", UIMin="0", UIMax="5"))
    int32 TranslationDepth = 0;

    /** Include Blueprint variables in serialization output */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation",
        meta=(DisplayName="Include Variables"))
    bool bIncludeVariables = true;
    
    /** Minimum severity level for logging */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Logging")
    EN2CLogSeverity MinSeverity = EN2CLogSeverity::Info;
    
    /** Get the API key for the selected provider */
    FString GetActiveApiKey() const;

    /** Get the model for the selected provider */
    FString GetActiveModel() const;

    /** Get the minimum severity level for logging */
    EN2CLogSeverity GetMinLogSeverity() const { return MinSeverity; }

    /** Style themes for C++ code */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming | Language Specific Themes",
        meta=(DisplayName="C++ Themes"))
    FN2CCodeEditorThemes CPPThemes;

    /** Style themes for Python code */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming | Language Specific Themes",
        meta=(DisplayName="Python Themes"))
    FN2CCodeEditorThemes PythonThemes;

    /** Style themes for JavaScript code */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming | Language Specific Themes",
        meta=(DisplayName="JavaScript Themes"))
    FN2CCodeEditorThemes JavaScriptThemes;

    /** Style themes for C# code */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming | Language Specific Themes",
        meta=(DisplayName="C# Themes"))
    FN2CCodeEditorThemes CSharpThemes;

    /** Style themes for Swift code */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming | Language Specific Themes",
        meta=(DisplayName="Swift Themes"))
    FN2CCodeEditorThemes SwiftThemes;

    /** Style themes for Pseudocode */
    UPROPERTY(Config, EditAnywhere, Category = "Node to Code | Theming | Language Specific Themes",
        meta=(DisplayName="Pseudocode Themes"))
    FN2CCodeEditorThemes PseudocodeThemes;
    
    /** Get theme colors for a specific language and theme name */
    const FN2CCodeEditorColors* GetThemeColors(EN2CCodeLanguage Language, const FName& ThemeName) const;

    // Begin UObject Interface
    virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    // End UObject Interface

    /** Check if a property is a color property in our editor colors structs */
    bool IsColorProperty(const FProperty* Property) const;

    /** Get the current model's input cost */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services | Pricing")
    float GetCurrentInputCost() const
    {
        switch (Provider)
        {
            case EN2CLLMProvider::OpenAI:
                if (const FN2COpenAIPricing* Pricing = OpenAIModelPricing.Find(OpenAI_Model))
                {
                    return Pricing->InputCost;
                }
                return FN2CLLMModelUtils::GetOpenAIPricing(OpenAI_Model).InputCost;
            case EN2CLLMProvider::Anthropic:
                if (const FN2CAnthropicPricing* Pricing = AnthropicModelPricing.Find(AnthropicModel))
                {
                    return Pricing->InputCost;
                }
                return FN2CLLMModelUtils::GetAnthropicPricing(AnthropicModel).InputCost;
            case EN2CLLMProvider::DeepSeek:
                if (const FN2CDeepSeekPricing* Pricing = DeepSeekModelPricing.Find(DeepSeekModel))
                {
                    return Pricing->InputCost;
                }
                return FN2CLLMModelUtils::GetDeepSeekPricing(DeepSeekModel).InputCost;
            case EN2CLLMProvider::Ollama:
            case EN2CLLMProvider::LMStudio:
                return 0.0f; // Local models are free
            default:
                return 0.0f;
        }
    }

    /** Get the current model's output cost */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code | LLM Services | Pricing")
    float GetCurrentOutputCost() const
    {
        switch (Provider)
        {
            case EN2CLLMProvider::OpenAI:
                if (const FN2COpenAIPricing* Pricing = OpenAIModelPricing.Find(OpenAI_Model))
                {
                    return Pricing->OutputCost;
                }
                return FN2CLLMModelUtils::GetOpenAIPricing(OpenAI_Model).OutputCost;
            case EN2CLLMProvider::Anthropic:
                if (const FN2CAnthropicPricing* Pricing = AnthropicModelPricing.Find(AnthropicModel))
                {
                    return Pricing->OutputCost;
                }
                return FN2CLLMModelUtils::GetAnthropicPricing(AnthropicModel).OutputCost;
            case EN2CLLMProvider::DeepSeek:
                if (const FN2CDeepSeekPricing* Pricing = DeepSeekModelPricing.Find(DeepSeekModel))
                {
                    return Pricing->OutputCost;
                }
                return FN2CLLMModelUtils::GetDeepSeekPricing(DeepSeekModel).OutputCost;
            case EN2CLLMProvider::Ollama:
            case EN2CLLMProvider::LMStudio:
                return 0.0f; // Local models are free
            default:
                return 0.0f;
        }
    }

    /** Calculate and store token estimate for reference files */
    int32 GetReferenceFilesTokenEstimate() const
    {
        int32 TotalTokens = 0;
        for (const FFilePath& Path : ReferenceSourceFilePaths)
        {
            if (FPaths::FileExists(Path.FilePath))
            {
                FString Content;
                if (FFileHelper::LoadFileToString(Content, *Path.FilePath))
                {
                    // Estimate tokens by dividing character count by 4
                    TotalTokens += FMath::CeilToInt(Content.Len() / 4.0f);
                }
            }
        }
        return TotalTokens;
    }


    /** Estimated token count from reference files */
    UPROPERTY(Config, VisibleAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation",
        meta = (DisplayName = "Estimated Reference File Tokens"))
    int32 EstimatedReferenceTokens = 0;

    /** Source files to provide as context to the LLM */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation", 
        meta = (DisplayName = "Reference Source Files",
               FilePathFilter = "C++ Files (*.h;*.cpp)|*.h;*.cpp",
               ToolTip="Source files to include as context in LLM prompts"))
    TArray<FFilePath> ReferenceSourceFilePaths;
    
    /** Custom output directory for translations */
    UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Node to Code | Code Generation",
        meta = (DisplayName = "Custom Translation Output Directory",
               ToolTip="If set, translations will be saved to this directory instead of the default location in Saved/NodeToCode/Translations"))
    FDirectoryPath CustomTranslationOutputDirectory;
    
    /** Validate all reference source file paths */
    void ValidateReferenceSourcePaths();

    /** Get the plugin settings */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Node to Code", 
        meta = (DisplayName = "Get Node to Code Settings"))
    static UN2CSettings* GetN2CSettings() 
    {
        return GetMutableDefault<UN2CSettings>();
    }

    /** Copy text to system clipboard */                                                                                                                                                                      
    UFUNCTION(BlueprintCallable, Category = "Node to Code | Utilities",                                                                                                                                       
        meta = (DisplayName = "Copy To Clipboard"))                                                                                                                                                           
    static void CopyToClipboard(const FString& Text);

private:

    /** Keep track of the last edited property */
    FProperty* LastEditedProperty;

    void InitializePricing();
};
