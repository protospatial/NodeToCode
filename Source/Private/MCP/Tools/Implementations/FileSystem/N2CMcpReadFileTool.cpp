// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "N2CMcpReadFileTool.h"
#include "MCP/Tools/N2CMcpToolRegistry.h"
#include "Utils/N2CLogger.h"
#include "Utils/N2CPathUtils.h"
#include "Utils/N2CContentTypeUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Templates/UniquePtr.h"

// Auto-register this tool
REGISTER_MCP_TOOL(FN2CMcpReadFileTool)

namespace N2CReadFile
{
    /** Bytes read from disk per step */
    constexpr int64 ChunkBytes = 64 * 1024;

    /** How far into a file the tool scans for lines (to reach an offset, or to count totalLines) */
    constexpr int64 MaxScanBytes = 64 * 1024 * 1024;

    /** A NUL byte this close to the start of a UTF-8 file marks it as binary */
    constexpr int64 BinarySniffBytes = 8 * 1024;

    enum class EEncoding : uint8 { Utf8, Utf8Bom, Utf16LE, Utf16BE };

    const TCHAR* EncodingName(EEncoding Encoding)
    {
        switch (Encoding)
        {
            case EEncoding::Utf8Bom: return TEXT("utf-8-bom");
            case EEncoding::Utf16LE: return TEXT("utf-16le");
            case EEncoding::Utf16BE: return TEXT("utf-16be");
            default: return TEXT("utf-8");
        }
    }

    /** True when Data[0, Len) is well-formed UTF-8 (no overlong forms, surrogates or code points above U+10FFFF) */
    bool IsValidUtf8(const uint8* Data, int32 Len)
    {
        int32 I = 0;
        while (I < Len)
        {
            const uint8 B = Data[I];
            if (B < 0x80)
            {
                ++I;
                continue;
            }
            int32 Count = 0;
            uint32 CodePoint = 0;
            uint32 Min = 0;
            if ((B & 0xE0) == 0xC0) { Count = 1; CodePoint = B & 0x1F; Min = 0x80; }
            else if ((B & 0xF0) == 0xE0) { Count = 2; CodePoint = B & 0x0F; Min = 0x800; }
            else if ((B & 0xF8) == 0xF0) { Count = 3; CodePoint = B & 0x07; Min = 0x10000; }
            else { return false; }
            if (I + Count >= Len)
            {
                return false;
            }
            for (int32 K = 1; K <= Count; ++K)
            {
                const uint8 C = Data[I + K];
                if ((C & 0xC0) != 0x80)
                {
                    return false;
                }
                CodePoint = (CodePoint << 6) | (C & 0x3F);
            }
            if (CodePoint < Min || CodePoint > 0x10FFFF || (CodePoint >= 0xD800 && CodePoint <= 0xDFFF))
            {
                return false;
            }
            I += Count + 1;
        }
        return true;
    }

    /** The longest prefix of Data no longer than MaxLen that does not split a UTF-8 sequence */
    int32 Utf8PrefixLength(const uint8* Data, int32 Len, int32 MaxLen)
    {
        if (Len <= MaxLen)
        {
            return Len;
        }
        int32 Cut = MaxLen;
        while (Cut > 0 && (Data[Cut] & 0xC0) == 0x80)
        {
            --Cut;
        }
        return Cut;
    }

    void AppendUtf8(TArray<uint8>& Out, uint32 CodePoint)
    {
        if (CodePoint < 0x80)
        {
            Out.Add(static_cast<uint8>(CodePoint));
        }
        else if (CodePoint < 0x800)
        {
            Out.Add(static_cast<uint8>(0xC0 | (CodePoint >> 6)));
            Out.Add(static_cast<uint8>(0x80 | (CodePoint & 0x3F)));
        }
        else if (CodePoint < 0x10000)
        {
            Out.Add(static_cast<uint8>(0xE0 | (CodePoint >> 12)));
            Out.Add(static_cast<uint8>(0x80 | ((CodePoint >> 6) & 0x3F)));
            Out.Add(static_cast<uint8>(0x80 | (CodePoint & 0x3F)));
        }
        else
        {
            Out.Add(static_cast<uint8>(0xF0 | (CodePoint >> 18)));
            Out.Add(static_cast<uint8>(0x80 | ((CodePoint >> 12) & 0x3F)));
            Out.Add(static_cast<uint8>(0x80 | ((CodePoint >> 6) & 0x3F)));
            Out.Add(static_cast<uint8>(0x80 | (CodePoint & 0x3F)));
        }
    }

    /** Turns UTF-16 bytes into UTF-8, carrying an odd byte or a lone high surrogate over to the next chunk */
    struct FUtf16Decoder
    {
        bool bBigEndian = false;
        TArray<uint8> Carry;
        uint32 HighSurrogate = 0;
        bool bInvalid = false;

        void Decode(const uint8* Data, int32 Len, TArray<uint8>& Out)
        {
            TArray<uint8> Bytes = MoveTemp(Carry);
            Carry.Reset();
            Bytes.Append(Data, Len);
            const int32 Even = Bytes.Num() & ~1;
            for (int32 I = 0; I < Even; I += 2)
            {
                const uint32 Unit = bBigEndian ? ((Bytes[I] << 8) | Bytes[I + 1]) : (Bytes[I] | (Bytes[I + 1] << 8));
                if (HighSurrogate != 0)
                {
                    if (Unit >= 0xDC00 && Unit <= 0xDFFF)
                    {
                        AppendUtf8(Out, 0x10000 + ((HighSurrogate - 0xD800) << 10) + (Unit - 0xDC00));
                        HighSurrogate = 0;
                        continue;
                    }
                    bInvalid = true;
                    HighSurrogate = 0;
                }
                if (Unit >= 0xD800 && Unit <= 0xDBFF)
                {
                    HighSurrogate = Unit;
                }
                else if ((Unit >= 0xDC00 && Unit <= 0xDFFF) || Unit == 0)
                {
                    bInvalid = true; // a lone low surrogate, or a NUL: not text
                }
                else
                {
                    AppendUtf8(Out, Unit);
                }
            }
            if (Even < Bytes.Num())
            {
                Carry.Add(Bytes.Last());
            }
        }

        bool Finish() const { return !bInvalid && HighSurrogate == 0 && Carry.Num() == 0; }
    };

    /** The extensions read-file serves (lower case, no dot). A file with no extension (README, LICENSE) is read too. */
    const TSet<FString>& TextExtensions()
    {
        static const TSet<FString> Extensions = {
            // text and data
            TEXT("txt"), TEXT("log"), TEXT("md"), TEXT("markdown"), TEXT("rst"), TEXT("json"), TEXT("jsonl"), TEXT("ndjson"),
            TEXT("csv"), TEXT("tsv"), TEXT("xml"), TEXT("yaml"), TEXT("yml"), TEXT("toml"), TEXT("html"), TEXT("htm"), TEXT("css"),
            // configuration
            TEXT("ini"), TEXT("cfg"), TEXT("conf"), TEXT("config"), TEXT("properties"), TEXT("editorconfig"), TEXT("gitignore"),
            TEXT("gitattributes"), TEXT("clang-format"), TEXT("p4ignore"),
            // Unreal descriptors and shaders
            TEXT("uproject"), TEXT("uplugin"), TEXT("uprojectdirs"), TEXT("usf"), TEXT("ush"), TEXT("hlsl"), TEXT("glsl"),
            // source (the translation targets among them)
            TEXT("h"), TEXT("hh"), TEXT("hpp"), TEXT("hxx"), TEXT("inl"), TEXT("ipp"), TEXT("c"), TEXT("cc"), TEXT("cpp"), TEXT("cxx"),
            TEXT("cs"), TEXT("py"), TEXT("pyi"), TEXT("js"), TEXT("mjs"), TEXT("cjs"), TEXT("ts"), TEXT("tsx"), TEXT("jsx"),
            TEXT("swift"), TEXT("java"), TEXT("kt"), TEXT("lua"), TEXT("m"), TEXT("mm"),
            // scripts and build files
            TEXT("bat"), TEXT("cmd"), TEXT("ps1"), TEXT("psm1"), TEXT("sh"), TEXT("bash"), TEXT("cmake"), TEXT("mk"),
            TEXT("diff"), TEXT("patch")
        };
        return Extensions;
    }

    FString AllowedExtensionsText()
    {
        TArray<FString> Sorted = TextExtensions().Array();
        Sorted.Sort();
        for (FString& Extension : Sorted)
        {
            Extension = TEXT(".") + Extension;
        }
        return FString::Join(Sorted, TEXT(" "));
    }

    /**
     * An optional whole-number argument: absent gives Default; a number or a numeric string must be a whole number
     * of at least Min. False with OutError otherwise.
     */
    bool ReadOptionalInt(const TSharedPtr<FJsonObject>& Arguments, const TCHAR* Name, int64 Default, int64 Min, int64& OutValue, FString& OutError)
    {
        OutValue = Default;
        const TSharedPtr<FJsonValue> Value = Arguments.IsValid() ? Arguments->TryGetField(Name) : nullptr;
        if (!Value.IsValid() || Value->IsNull())
        {
            return true;
        }
        double Number = 0.0;
        FString Text;
        if (Value->TryGetString(Text))
        {
            Text.TrimStartAndEndInline();
            if (!Text.IsNumeric())
            {
                OutError = FString::Printf(TEXT("%s must be a whole number, got \"%s\""), Name, *Text);
                return false;
            }
            Number = FCString::Atod(*Text);
        }
        else if (!Value->TryGetNumber(Number))
        {
            OutError = FString::Printf(TEXT("%s must be a whole number"), Name);
            return false;
        }
        if (Number != FMath::FloorToDouble(Number) || Number < static_cast<double>(Min) || Number > 9.0e15)
        {
            OutError = FString::Printf(TEXT("%s must be a whole number of at least %lld"), Name, Min);
            return false;
        }
        OutValue = static_cast<int64>(Number);
        return true;
    }

    /** What ReadLines gives back */
    struct FReadResult
    {
        TArray<uint8> Utf8;          // the returned text
        int64 FirstLine = 0;         // first returned line (1-based), 0 when none
        int64 LastLine = 0;
        int64 LinesReturned = 0;
        int64 NextOffset = 0;        // offset for the next part, 0 when nothing follows
        int64 TotalLines = -1;       // -1 when the scan stopped before the end of the file
        bool bTruncated = false;     // the content stopped at the byte cap
        bool bLineCut = false;       // a single line was longer than the cap; only its start was returned
        EEncoding Encoding = EEncoding::Utf8;
    };

    /**
     * Streams a file and keeps lines [Offset, Offset + Limit) up to MaxBytes of UTF-8. Never holds more than
     * MaxBytes + one chunk of the file in memory, and stops scanning at MaxScanBytes. False with OutError when the
     * file cannot be read, is binary, or the offset lies beyond the scanned part.
     */
    bool ReadLines(const FString& Path, int64 Offset, int64 Limit, int64 MaxBytes, int64& OutFileSize, FReadResult& Result, FString& OutError)
    {
        TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path, FILEREAD_AllowWrite));
        if (!Reader)
        {
            OutError = TEXT("Failed to open the file");
            return false;
        }
        OutFileSize = Reader->TotalSize();

        FUtf16Decoder Utf16;
        TArray<uint8> Chunk;
        TArray<uint8> Decoded;
        int64 Position = 0;
        int64 LineNumber = 1;          // the line the next byte belongs to
        int64 LineStartInOut = 0;      // where the current line starts in Result.Utf8 when it is being kept
        bool bLineOpen = false;        // some byte of LineNumber has been seen
        bool bCollectDone = false;     // stopped keeping lines; only counting from here on
        bool bFirstChunk = true;

        auto StopAt = [&](int64 Next)
        {
            bCollectDone = true;
            Result.NextOffset = Next;
        };

        // Feeds UTF-8 bytes through the line splitter
        auto Consume = [&](const uint8* Data, int32 Len)
        {
            for (int32 I = 0; I < Len; ++I)
            {
                const uint8 Byte = Data[I];
                if (!bLineOpen)
                {
                    bLineOpen = true;
                    if (!bCollectDone && LineNumber >= Offset)
                    {
                        if (Limit > 0 && Result.LinesReturned >= Limit)
                        {
                            StopAt(LineNumber);
                        }
                        else
                        {
                            LineStartInOut = Result.Utf8.Num();
                        }
                    }
                }

                if (!bCollectDone && LineNumber >= Offset)
                {
                    Result.Utf8.Add(Byte);
                    if (Result.Utf8.Num() > MaxBytes)
                    {
                        Result.bTruncated = true;
                        if (Result.LinesReturned == 0)
                        {
                            // One line longer than the cap: return its start, cut on a character boundary
                            const int32 Cut = Utf8PrefixLength(Result.Utf8.GetData(), Result.Utf8.Num(), static_cast<int32>(MaxBytes));
                            Result.Utf8.SetNum(Cut);
                            Result.FirstLine = LineNumber;
                            Result.LastLine = LineNumber;
                            Result.LinesReturned = 1;
                            Result.bLineCut = true;
                            StopAt(LineNumber + 1); // provisional: confirmed below only when a next line exists
                        }
                        else
                        {
                            Result.Utf8.SetNum(static_cast<int32>(LineStartInOut));
                            StopAt(LineNumber);
                        }
                    }
                }

                if (Byte == '\n')
                {
                    if (!bCollectDone && LineNumber >= Offset)
                    {
                        if (Result.LinesReturned == 0)
                        {
                            Result.FirstLine = LineNumber;
                        }
                        Result.LastLine = LineNumber;
                        ++Result.LinesReturned;
                    }
                    ++LineNumber;
                    bLineOpen = false;
                }
            }
        };

        while (Position < OutFileSize)
        {
            const int64 Want = FMath::Min(ChunkBytes, OutFileSize - Position);
            Chunk.SetNumUninitialized(static_cast<int32>(Want));
            Reader->Serialize(Chunk.GetData(), Want);
            if (Reader->IsError())
            {
                OutError = TEXT("Failed to read the file");
                return false;
            }
            Position += Want;

            const uint8* Data = Chunk.GetData();
            int32 Len = Chunk.Num();
            if (bFirstChunk)
            {
                bFirstChunk = false;
                if (Len >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF)
                {
                    Result.Encoding = EEncoding::Utf8Bom;
                    Data += 3;
                    Len -= 3;
                }
                else if (Len >= 2 && Data[0] == 0xFF && Data[1] == 0xFE)
                {
                    Result.Encoding = EEncoding::Utf16LE;
                    Data += 2;
                    Len -= 2;
                }
                else if (Len >= 2 && Data[0] == 0xFE && Data[1] == 0xFF)
                {
                    Result.Encoding = EEncoding::Utf16BE;
                    Utf16.bBigEndian = true;
                    Data += 2;
                    Len -= 2;
                }
                if (Result.Encoding == EEncoding::Utf8 || Result.Encoding == EEncoding::Utf8Bom)
                {
                    const int32 Sniff = static_cast<int32>(FMath::Min<int64>(Len, BinarySniffBytes));
                    for (int32 I = 0; I < Sniff; ++I)
                    {
                        if (Data[I] == 0)
                        {
                            OutError = TEXT("the file looks binary (it has a NUL byte in its first 8 KB)");
                            return false;
                        }
                    }
                }
            }

            if (Result.Encoding == EEncoding::Utf16LE || Result.Encoding == EEncoding::Utf16BE)
            {
                Decoded.Reset();
                Utf16.Decode(Data, Len, Decoded);
                if (Utf16.bInvalid)
                {
                    OutError = TEXT("the file is not valid UTF-16 text (it has a NUL or an unpaired surrogate)");
                    return false;
                }
                Consume(Decoded.GetData(), Decoded.Num());
            }
            else
            {
                Consume(Data, Len);
            }

            if (Position >= MaxScanBytes)
            {
                break; // a bounded scan: past this, totalLines is left out and an offset further on is refused
            }
        }

        const bool bReachedEnd = Position >= OutFileSize;
        if (bReachedEnd && (Result.Encoding == EEncoding::Utf16LE || Result.Encoding == EEncoding::Utf16BE) && !Utf16.Finish())
        {
            OutError = TEXT("the file is not valid UTF-16 text (it ends inside a character)");
            return false;
        }

        // A last line without a newline
        if (bLineOpen)
        {
            if (!bCollectDone && LineNumber >= Offset)
            {
                if (Result.LinesReturned == 0)
                {
                    Result.FirstLine = LineNumber;
                }
                Result.LastLine = LineNumber;
                ++Result.LinesReturned;
            }
            ++LineNumber;
        }

        if (bReachedEnd)
        {
            Result.TotalLines = LineNumber - 1;
            if (Result.NextOffset > Result.TotalLines)
            {
                Result.NextOffset = 0; // the cut line was the last one
            }
        }
        else if (!bCollectDone)
        {
            if (Result.LinesReturned == 0)
            {
                OutError = FString::Printf(TEXT("offset %lld is beyond the first %lld MB of the file, which is as far as read-file scans"),
                    Offset, MaxScanBytes / (1024 * 1024));
                return false;
            }
            Result.NextOffset = Result.LastLine + 1;
        }

        if (!IsValidUtf8(Result.Utf8.GetData(), Result.Utf8.Num()))
        {
            OutError = TEXT("the file is not valid UTF-8 text");
            return false;
        }
        return true;
    }
}

FMcpToolDefinition FN2CMcpReadFileTool::GetDefinition() const
{
    FMcpToolDefinition Definition(
        TEXT("read-file"),
        TEXT("Reads a text file in the Unreal Engine project, or in the translation output directory when that is outside the project. "
             "relativePath is relative to the project folder; an absolute path is accepted only inside those two folders. "
             "Only text files are read (source, config, JSON, CSV, Markdown, logs, ...): other types such as .uasset, .uexp, .png or .dll, "
             "and files with NUL bytes or invalid UTF-8, are refused. "
             "At most 500 KB of text comes back per call, in whole lines: truncated is true when the cap cut the read short, "
             "and nextOffset is the offset that continues it. offset and limit read a line range. "
             "Credential files (secrets.json, .env, Saved/NodeToCode/User, .git) are never readable."),
        N2CMcpToolsets::FileSystem
    );

    TMap<FString, FString> Properties;
    Properties.Add(TEXT("relativePath"), TEXT("string"));
    Properties.Add(TEXT("offset"), TEXT("integer"));
    Properties.Add(TEXT("limit"), TEXT("integer"));

    TMap<FString, FString> Descriptions;
    Descriptions.Add(TEXT("relativePath"), TEXT("The file, relative to the project folder (for example \"Config/DefaultEngine.ini\"); an absolute path must be inside the project or the translation output directory"));
    Descriptions.Add(TEXT("offset"), TEXT("Optional. The line number to start at, 1-based (default 1)"));
    Descriptions.Add(TEXT("limit"), TEXT("Optional. The most lines to return (default: as many as fit in 500 KB)"));

    TArray<FString> Required;
    Required.Add(TEXT("relativePath"));

    Definition.InputSchema = BuildInputSchema(Properties, Required, Descriptions);
    const TSharedPtr<FJsonObject>* SchemaProperties = nullptr;
    if (Definition.InputSchema.IsValid() && Definition.InputSchema->TryGetObjectField(TEXT("properties"), SchemaProperties))
    {
        const TSharedPtr<FJsonObject>* Offset = nullptr;
        if ((*SchemaProperties)->TryGetObjectField(TEXT("offset"), Offset))
        {
            (*Offset)->SetNumberField(TEXT("minimum"), 1);
        }
        const TSharedPtr<FJsonObject>* Limit = nullptr;
        if ((*SchemaProperties)->TryGetObjectField(TEXT("limit"), Limit))
        {
            (*Limit)->SetNumberField(TEXT("minimum"), 1);
        }
    }

    // Read-only: reads editor or project state and changes nothing
    SetAnnotations(Definition, FMcpToolAnnotations::ReadOnly());

    return Definition;
}

FMcpToolCallResult FN2CMcpReadFileTool::Execute(const TSharedPtr<FJsonObject>& Arguments)
{
    FString RelativePath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("relativePath"), RelativePath))
    {
        return FMcpToolCallResult::CreateErrorResult(TEXT("Missing required parameter: relativePath"));
    }

    int64 Offset = 1;
    int64 Limit = 0;
    FString ArgumentError;
    if (!N2CReadFile::ReadOptionalInt(Arguments, TEXT("offset"), 1, 0, Offset, ArgumentError)
        || !N2CReadFile::ReadOptionalInt(Arguments, TEXT("limit"), 0, 1, Limit, ArgumentError))
    {
        return FMcpToolCallResult::CreateErrorResult(ArgumentError);
    }
    Offset = FMath::Max<int64>(Offset, 1); // 0 reads from the first line too

    FN2CLogger::Get().Log(
        FString::Printf(TEXT("Executing read-file tool with relativePath: %s"), *RelativePath),
        EN2CLogSeverity::Debug
    );

    return ExecuteOnGameThread([RelativePath, Offset, Limit]() -> FMcpToolCallResult
    {
        // Part 1: Path. The project folder and the translation output directory are the roots; the jail, the
        // absolute-path rule and the credential deny-list are in FN2CPathUtils::ResolveAgentReadPath.
        FString RequestedPath;
        FString PathError;
        if (!FN2CPathUtils::ResolveAgentReadPath(RelativePath, RequestedPath, PathError))
        {
            return FMcpToolCallResult::CreateErrorResult(PathError);
        }

        if (FPaths::DirectoryExists(RequestedPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("%s is a directory; use read-path to list it"), *RelativePath));
        }
        if (!FPaths::FileExists(RequestedPath))
        {
            return FMcpToolCallResult::CreateErrorResult(
                FString::Printf(TEXT("File does not exist: %s"), *RelativePath));
        }

        // Part 2: Type. Only text extensions (or none) are read; content that turns out binary is refused below.
        const FString Extension = FPaths::GetExtension(RequestedPath).ToLower();
        if (!Extension.IsEmpty() && !N2CReadFile::TextExtensions().Contains(Extension))
        {
            return FMcpToolCallResult::CreateErrorResult(FString::Printf(
                TEXT("Unsupported file type '.%s': read-file reads text files only (%s, or a file with no extension)"),
                *Extension, *N2CReadFile::AllowedExtensionsText()));
        }

        // Part 3: Bounded read. At most MaxContentBytes of whole lines, never the whole file.
        N2CReadFile::FReadResult Read;
        int64 FileSize = 0;
        FString ReadError;
        if (!N2CReadFile::ReadLines(RequestedPath, Offset, Limit, MaxContentBytes, FileSize, Read, ReadError))
        {
            const bool bNotText = ReadError.StartsWith(TEXT("the file"));
            if (!bNotText)
            {
                FN2CLogger::Get().LogError(FString::Printf(TEXT("read-file failed on %s: %s"), *RequestedPath, *ReadError));
            }
            return FMcpToolCallResult::CreateErrorResult(bNotText
                ? FString::Printf(TEXT("Unsupported content: %s: %s; read-file reads text files only"), *RelativePath, *ReadError)
                : FString::Printf(TEXT("%s: %s"), *RelativePath, *ReadError));
        }

        FString FileContent;
        if (Read.Utf8.Num() > 0)
        {
            const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Read.Utf8.GetData()), Read.Utf8.Num());
            FileContent = FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
        }

        // Part 4: Response. Paths are project-relative (absolute only for a translation root outside the project).
        const FFileStatData FileStats = IFileManager::Get().GetStatData(*RequestedPath);
        TSharedPtr<FJsonObject> ResponseObject = MakeShared<FJsonObject>();
        ResponseObject->SetStringField(TEXT("path"), FN2CPathUtils::MakeAgentDisplayPath(RequestedPath));
        ResponseObject->SetStringField(TEXT("content"), FileContent);
        ResponseObject->SetNumberField(TEXT("size"), FileSize);
        ResponseObject->SetNumberField(TEXT("contentBytes"), Read.Utf8.Num());
        ResponseObject->SetStringField(TEXT("contentType"), FN2CContentTypeUtils::GetContentTypeFromExtension(Extension));
        ResponseObject->SetStringField(TEXT("extension"), Extension);
        ResponseObject->SetStringField(TEXT("encoding"), N2CReadFile::EncodingName(Read.Encoding));
        ResponseObject->SetStringField(TEXT("lastModified"), FileStats.ModificationTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
        ResponseObject->SetNumberField(TEXT("startLine"), Read.FirstLine);
        ResponseObject->SetNumberField(TEXT("endLine"), Read.LastLine);
        ResponseObject->SetNumberField(TEXT("lineCount"), Read.LinesReturned);
        if (Read.TotalLines >= 0)
        {
            ResponseObject->SetNumberField(TEXT("totalLines"), Read.TotalLines);
        }
        ResponseObject->SetBoolField(TEXT("truncated"), Read.bTruncated);
        if (Read.NextOffset > 0)
        {
            ResponseObject->SetNumberField(TEXT("nextOffset"), Read.NextOffset);
        }
        if (Read.bLineCut)
        {
            ResponseObject->SetStringField(TEXT("note"), FString::Printf(
                TEXT("Line %lld is longer than %lld bytes: only its start is returned"), Read.FirstLine, MaxContentBytes));
        }
        ResponseObject->SetBoolField(TEXT("success"), true);

        FN2CLogger::Get().Log(
            FString::Printf(TEXT("Read file: %s (lines %lld-%lld, %d of %lld bytes%s)"),
                *RelativePath, Read.FirstLine, Read.LastLine, Read.Utf8.Num(), FileSize, Read.bTruncated ? TEXT(", truncated") : TEXT("")),
            EN2CLogSeverity::Info
        );

        FString OutputString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
        FJsonSerializer::Serialize(ResponseObject.ToSharedRef(), Writer);
        return FMcpToolCallResult::CreateTextResult(OutputString);
    });
}
