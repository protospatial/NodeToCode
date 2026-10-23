### User Interface → Top Navbar Overview

The top navbar provides quick access to essential features and navigation options.

## A. Translations Browser (Left)

A powerful tool for managing multiple translations:

1. **Browser Panel Toggle**
    - Click the "Translations" button to slide in the browser panel
    - Shows all translated graphs in current session
    - Organizes translations in order from the top level graph to most nested graphs
2. **Translation Browser Features**
    
    - Search/filter translations
    - Sort by name or timestamp
    - Group by Blueprint source
    - Quick navigation between translations
    
> [!NOTE]
> The translations browser does not currently retain your translation history after closing the Editor window.

3. **Cost Estimation**
    - Displays estimated cost for entire translation depending on the provider/model being used
    - Displays the estimated cost of the translation in US Dollars
    - Updates in real-time with new translations
    - Configurable pricing based on provider settings via the Plugin settings

<p align="center">
    <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_NavBarUICallouts.png" alt="Node to Code navbar diagram" width="1250">
</p>

## B. Implementation / Declaration Tabs (Center)

Central navigation for C++ translations:

1. **Implementation Tab**
    - Shows the main implementation code (.cpp content) such as function definitions

2. **Declaration Tab**
    - Shows header content (.h file)
    - Class declarations and definitions
    - UFUNCTION and UPROPERTY macros
    - Property and function declarations
    
> [!NOTE]
> The Declaration tab is only visible for C++ translations. Other
> languages show only the Implementation tab.
    

## C. Implementation Notes Button (Center-Right)

Access additional context and guidance through the Implementation Notes panel. See the full [Implementation Notes](Implementation-Notes) documentation for detailed usage information.

1. **Notes Panel Toggle**
    - Shows/hides the implementation notes panel
    - Slides in from the bottom of the window
    - Click the Notes button again or the - button to minimize

2. **Notes Content Overview**
    - Integration requirements and setup steps
    - Best practices and optimization tips
    - Blueprint compatibility notes
    - Performance considerations

## D. Styling Button (Far Right)

Customize the editor appearance:

1. **Theme Selection**
    - Choose from a variety of default themes
    - Custom theme support via the Plugin settings

2. **Font Controls**
    - Adjust font size (8-18pt)
    
> [!NOTE]
> Font size and theme are currently not remembered after closing the Node to Code Editor.    

The top navbar is designed for efficient navigation and customization of your translation viewing experience. Each element is carefully positioned for quick access while maintaining a clean, uncluttered interface that lets you focus on the translated code.
