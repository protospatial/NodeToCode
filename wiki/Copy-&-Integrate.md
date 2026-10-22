### Translation Workflow → Copy & Integrate
***

## Copying the Generated Code

To use the translated code:

1. Select the [appropriate tab](Top-Navbar-Overview#b-implementation--declaration-tabs-center)
2. Click the "Copy" button in the bottom-right corner of the [code editor](Code-Editor)
3. The code is now in your clipboard, ready to paste into your IDE

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_CopyButton.png" alt="Node to Code header" width="250">
    </p>

## Integration Steps

When integrating the translated code:

1. **Create New Files**
    - Make a new header file (.h) for declarations
    - Create a new source file (.cpp) for implementations
    - Follow your project's file naming conventions
2. **Paste and Review**
    - Paste the copied code into your project source file/IDE of choice
    - Check include statements and dependencies
    - Verify namespace usage matches your project
    - Review class names and inheritance
3. **Manual Refinement**
    
    ```cpp
    // Example of common adjustments needed:
    
    // Before (generated):
    UFUNCTION(BlueprintCallable)
    void ProcessData(const TArray<FString>& Data);
    
    // After (refined):
    UFUNCTION(BlueprintCallable, Category = "MyGame|DataProcessing")
    void ProcessData(const TArray<FString>& Data);
    
    ```
    
4. **Compilation Check**
    - Attempt to compile the new code
    - Address any compiler errors
    - Fix include paths if needed
    - Resolve any missing dependencies

## Best Practices for Integration

- Keep the original Blueprint until the code is fully tested
- Document any manual changes made to the generated code
- Test the C++ implementation against the Blueprint behavior
- Consider creating unit tests for the new code
- Update any dependent systems to use the new C++ functions

> [!NOTE]
> The generated code is a starting point. Always review and refine it to match your project's coding standards and requirements.

Remember that Node to Code accelerates the translation process but doesn't replace the need for proper code review and testing. Use the generated code as a foundation, then apply your expertise, critical thinking, and/or further research to ensure it meets your project's quality standards.