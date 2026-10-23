### Best Practices → Blueprint Selection
***

## Choosing Blueprints for Translation

When selecting Blueprints to translate, prioritize those that will benefit most from C++ conversion:

1. **Performance-Critical Systems**
    - Frequently executed logic
    - Complex mathematical calculations
    - Heavy data processing
    - Systems with tight timing requirements

2. **Well-Structured Blueprints**
    - Clear execution flow
    - Properly organized nodes
    - Meaningful variable names
    - Documented functionality via comments

3. **Modular Functionality**
    - Self-contained systems
    - Clear input/output relationships
    - Minimal external dependencies
    - Reusable components

## Preparing Blueprints for Translation

Before translation, optimize your Blueprint graphs for complexity considerations:

1. **Clean Up Node Organization**
    
    - Align nodes logically
    - Group related functionality
    - Remove unused variables and nodes
    - Break up overly complex sequences

> [!NOTE]
> While Node to Code can handle messy graphs, clean organization 
> leads to better translations and more maintainable code.

2. **Add Contextual Comments**
    - Document node purpose and behavior
    - Explain complex algorithms
    - Note any special cases or requirements
    - Provide context for future reference

3. **Separate Concerns**
    - Split data-heavy operations from visualization
    - Isolate platform-specific functionality
    - Separate UI logic from core systems 
    - Break large graphs into focused functions (see [Handling Large Graphs](Handling-Large-Graphs#breaking-down-complex-blueprints))
