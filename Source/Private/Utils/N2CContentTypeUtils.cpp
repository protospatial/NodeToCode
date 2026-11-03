// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CContentTypeUtils.h"

FString FN2CContentTypeUtils::GetContentTypeFromExtension(const FString& Extension)
{
    FString LowerExtension = Extension.ToLower();
    
    // Programming languages
    if (LowerExtension == TEXT("cpp") || LowerExtension == TEXT("cc") || LowerExtension == TEXT("cxx"))
        return TEXT("text/x-c++src");
    else if (LowerExtension == TEXT("h") || LowerExtension == TEXT("hpp") || LowerExtension == TEXT("hxx"))
        return TEXT("text/x-c++hdr");
    else if (LowerExtension == TEXT("c"))
        return TEXT("text/x-c");
    else if (LowerExtension == TEXT("py"))
        return TEXT("text/x-python");
    else if (LowerExtension == TEXT("js") || LowerExtension == TEXT("mjs"))
        return TEXT("text/javascript");
    else if (LowerExtension == TEXT("ts"))
        return TEXT("text/typescript");
    else if (LowerExtension == TEXT("cs"))
        return TEXT("text/x-csharp");
    else if (LowerExtension == TEXT("swift"))
        return TEXT("text/x-swift");
    else if (LowerExtension == TEXT("java"))
        return TEXT("text/x-java");
    else if (LowerExtension == TEXT("rs"))
        return TEXT("text/x-rust");
    else if (LowerExtension == TEXT("go"))
        return TEXT("text/x-go");
    else if (LowerExtension == TEXT("rb"))
        return TEXT("text/x-ruby");
    else if (LowerExtension == TEXT("php"))
        return TEXT("text/x-php");
    else if (LowerExtension == TEXT("kt") || LowerExtension == TEXT("kts"))
        return TEXT("text/x-kotlin");
    else if (LowerExtension == TEXT("m") || LowerExtension == TEXT("mm"))
        return TEXT("text/x-objectivec");
    else if (LowerExtension == TEXT("lua"))
        return TEXT("text/x-lua");
    else if (LowerExtension == TEXT("r"))
        return TEXT("text/x-r");
    else if (LowerExtension == TEXT("scala"))
        return TEXT("text/x-scala");
    
    // Markup and data formats
    else if (LowerExtension == TEXT("json"))
        return TEXT("application/json");
    else if (LowerExtension == TEXT("xml"))
        return TEXT("application/xml");
    else if (LowerExtension == TEXT("yaml") || LowerExtension == TEXT("yml"))
        return TEXT("text/yaml");
    else if (LowerExtension == TEXT("md") || LowerExtension == TEXT("markdown"))
        return TEXT("text/markdown");
    else if (LowerExtension == TEXT("html") || LowerExtension == TEXT("htm"))
        return TEXT("text/html");
    else if (LowerExtension == TEXT("css"))
        return TEXT("text/css");
    else if (LowerExtension == TEXT("scss") || LowerExtension == TEXT("sass"))
        return TEXT("text/x-scss");
    else if (LowerExtension == TEXT("less"))
        return TEXT("text/x-less");
    
    // Configuration files
    else if (LowerExtension == TEXT("ini"))
        return TEXT("text/plain");
    else if (LowerExtension == TEXT("toml"))
        return TEXT("text/toml");
    else if (LowerExtension == TEXT("cfg") || LowerExtension == TEXT("conf") || LowerExtension == TEXT("config"))
        return TEXT("text/plain");
    else if (LowerExtension == TEXT("properties"))
        return TEXT("text/x-properties");
    else if (LowerExtension == TEXT("env"))
        return TEXT("text/plain");
    
    // Unreal Engine specific
    else if (LowerExtension == TEXT("uproject"))
        return TEXT("application/json");
    else if (LowerExtension == TEXT("uplugin"))
        return TEXT("application/json");
    else if (LowerExtension == TEXT("uasset"))
        return TEXT("application/octet-stream");
    else if (LowerExtension == TEXT("umap"))
        return TEXT("application/octet-stream");
    else if (LowerExtension == TEXT("ush") || LowerExtension == TEXT("usf"))
        return TEXT("text/x-hlsl");
    
    // Documentation
    else if (LowerExtension == TEXT("txt"))
        return TEXT("text/plain");
    else if (LowerExtension == TEXT("log"))
        return TEXT("text/plain");
    else if (LowerExtension == TEXT("csv"))
        return TEXT("text/csv");
    else if (LowerExtension == TEXT("tsv"))
        return TEXT("text/tab-separated-values");
    else if (LowerExtension == TEXT("rst"))
        return TEXT("text/x-rst");
    else if (LowerExtension == TEXT("tex"))
        return TEXT("text/x-tex");
    
    // Shell scripts
    else if (LowerExtension == TEXT("sh") || LowerExtension == TEXT("bash"))
        return TEXT("text/x-shellscript");
    else if (LowerExtension == TEXT("bat") || LowerExtension == TEXT("cmd"))
        return TEXT("text/x-batch");
    else if (LowerExtension == TEXT("ps1"))
        return TEXT("text/x-powershell");
    else if (LowerExtension == TEXT("zsh"))
        return TEXT("text/x-shellscript");
    else if (LowerExtension == TEXT("fish"))
        return TEXT("text/x-fish");
    
    // Build files
    else if (LowerExtension == TEXT("makefile") || LowerExtension == TEXT("mk"))
        return TEXT("text/x-makefile");
    else if (LowerExtension == TEXT("cmake"))
        return TEXT("text/x-cmake");
    else if (LowerExtension == TEXT("gradle"))
        return TEXT("text/x-gradle");
    
    // Version control
    else if (LowerExtension == TEXT("gitignore") || LowerExtension == TEXT("gitattributes"))
        return TEXT("text/plain");
    else if (LowerExtension == TEXT("diff") || LowerExtension == TEXT("patch"))
        return TEXT("text/x-diff");
    
    // Default
    else
        return TEXT("text/plain");
}

FString FN2CContentTypeUtils::GetFileTypeDescription(const FString& Extension)
{
    FString LowerExtension = Extension.ToLower();
    
    // Programming languages
    if (LowerExtension == TEXT("cpp") || LowerExtension == TEXT("cc") || LowerExtension == TEXT("cxx"))
        return TEXT("C++ Source File");
    else if (LowerExtension == TEXT("h") || LowerExtension == TEXT("hpp") || LowerExtension == TEXT("hxx"))
        return TEXT("C++ Header File");
    else if (LowerExtension == TEXT("c"))
        return TEXT("C Source File");
    else if (LowerExtension == TEXT("py"))
        return TEXT("Python Script");
    else if (LowerExtension == TEXT("js") || LowerExtension == TEXT("mjs"))
        return TEXT("JavaScript File");
    else if (LowerExtension == TEXT("ts"))
        return TEXT("TypeScript File");
    else if (LowerExtension == TEXT("cs"))
        return TEXT("C# Source File");
    else if (LowerExtension == TEXT("swift"))
        return TEXT("Swift Source File");
    else if (LowerExtension == TEXT("java"))
        return TEXT("Java Source File");
    else if (LowerExtension == TEXT("rs"))
        return TEXT("Rust Source File");
    else if (LowerExtension == TEXT("go"))
        return TEXT("Go Source File");
    
    // Markup and data formats
    else if (LowerExtension == TEXT("json"))
        return TEXT("JSON Data");
    else if (LowerExtension == TEXT("xml"))
        return TEXT("XML Document");
    else if (LowerExtension == TEXT("yaml") || LowerExtension == TEXT("yml"))
        return TEXT("YAML Configuration");
    else if (LowerExtension == TEXT("md") || LowerExtension == TEXT("markdown"))
        return TEXT("Markdown Document");
    else if (LowerExtension == TEXT("html") || LowerExtension == TEXT("htm"))
        return TEXT("HTML Document");
    else if (LowerExtension == TEXT("css"))
        return TEXT("CSS Stylesheet");
    
    // Unreal Engine specific
    else if (LowerExtension == TEXT("uproject"))
        return TEXT("Unreal Project File");
    else if (LowerExtension == TEXT("uplugin"))
        return TEXT("Unreal Plugin File");
    else if (LowerExtension == TEXT("uasset"))
        return TEXT("Unreal Asset");
    else if (LowerExtension == TEXT("umap"))
        return TEXT("Unreal Map");
    else if (LowerExtension == TEXT("ush") || LowerExtension == TEXT("usf"))
        return TEXT("Unreal Shader File");
    
    // Configuration files
    else if (LowerExtension == TEXT("ini"))
        return TEXT("Configuration File");
    else if (LowerExtension == TEXT("toml"))
        return TEXT("TOML Configuration");
    else if (LowerExtension == TEXT("cfg") || LowerExtension == TEXT("conf") || LowerExtension == TEXT("config"))
        return TEXT("Configuration File");
    
    // Documentation
    else if (LowerExtension == TEXT("txt"))
        return TEXT("Text File");
    else if (LowerExtension == TEXT("log"))
        return TEXT("Log File");
    else if (LowerExtension == TEXT("csv"))
        return TEXT("CSV Data");
    
    // Shell scripts
    else if (LowerExtension == TEXT("sh") || LowerExtension == TEXT("bash"))
        return TEXT("Shell Script");
    else if (LowerExtension == TEXT("bat") || LowerExtension == TEXT("cmd"))
        return TEXT("Batch Script");
    else if (LowerExtension == TEXT("ps1"))
        return TEXT("PowerShell Script");
    
    // Default
    else
        return TEXT("File");
}