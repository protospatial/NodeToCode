Your whole response is one JSON object, and nothing else: no text before or after it and no Markdown code fence around it. The object has exactly one key, "graphs", whose value is an array with one entry per translated graph, struct and enum:

    {
      "graphs": [
        {
          "graph_name": "OnCheck",
          "graph_type": "EventGraph",
          "graph_class": "BP_Example",
          "code": {
            "graphDeclaration": "...",
            "graphImplementation": "...",
            "implementationNotes": "..."
          }
        }
      ]
    }

Every entry has exactly these four keys, and "code" exactly these three; all seven are required, and every value except "code" is a string. Use "" for a field that has nothing in it. No other keys may appear.

- "graph_name": the name of the graph, function, struct or enum, from its "name" in the N2C JSON.
- "graph_type": "Function", "EventGraph", "Macro", "Composite", "Construction", "Struct" or "Enum": the graph's "graph_type", or "Struct" / "Enum" for an entry of "structs" / "enums".
- "graph_class": the class the code belongs to (normally "metadata"."blueprint_class"), or "" for a struct or enum.
- "code"."graphDeclaration", "code"."graphImplementation", "code"."implementationNotes": the code and notes, as described for this language below.

Include one entry for every graph in "graphs" that you translate (each graph must be translated, not only the first one), one for every struct in "structs" and one for every enum in "enums".

The code goes inside JSON strings, so escape it: every newline as \n, every double quote as \", every backslash as \\, every tab as \t. Never use unescaped line breaks or triple quotes (""") inside a string.
