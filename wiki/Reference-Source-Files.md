### Plugin Settings → Reference Source Files
***

Reference source files provide essential context to improve translation quality by showing the LLM your project's coding patterns and conventions.

## Configuration

1. Under `Code Generation`, locate `Reference Source Files`
2. Click the + button to add .h/.cpp files using a file browser dialog
3. Select relevant source files that provide context for your translations

    <p align="left">
        <img src="https://github.com/protospatial/NodeToCode/blob/main/assets/Image_NodeToCode_CodeGeneration.png" alt="Node to Code code generation settings" width="500">
    </p>

## Selection Guidelines

### Primary References
- Base classes being extended
- Interfaces being implemented
- Common utility functions
- Shared type definitions

### Supporting References
- Related system implementations
- Similar pattern examples
- Project-specific conventions
- Custom macros or helpers

## Best Practices

### File Selection
- Include minimal, focused examples
- Prioritize clean, well-documented code
- Select files with similar functionality
- Include project-specific patterns

> [!NOTE]
> Quality over quantity - choose the most relevant files that
> demonstrate your desired coding patterns or a specific class you
> want the translated codes to be integrated into.

### Maintenance
- Regularly update reference files
- Remove outdated examples
- Add new patterns as needed
- Monitor [token usage impact](Translation-Depth)
