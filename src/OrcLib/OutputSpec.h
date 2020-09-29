//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"
#include "ArchiveFormat.h"

#include "TableOutput.h"
#include "Utils/EnumFlags.h"

#include <string>
#include <filesystem>

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

class ORCLIB_API OutputSpec
{
public:
    enum class UploadAuthScheme : char
    {
        Anonymous = 0,
        Basic,
        NTLM,
        Kerberos,
        Negotiate
    };

    static std::wstring ToString(UploadAuthScheme method);

    enum class UploadMethod : char
    {
        NoUpload = 0,
        FileCopy,
        BITS
    };

    static std::wstring ToString(UploadMethod method);

    enum BITSMode : char
    {
        NoMode,
        SMB,
        HTTP,
        HTTPS
    };

    enum class UploadOperation : char
    {
        NoOp = 0,
        Copy,
        Move
    };

    static std::wstring ToString(UploadOperation method);

    enum class UploadMode : char
    {
        Synchronous = 0,
        Asynchronous
    };

    static std::wstring ToString(UploadMode mode);

    class ORCLIB_API Upload
    {
    public:
        UploadMethod Method;
        UploadOperation Operation;
        UploadMode Mode;
        BITSMode bitsMode;
        UploadAuthScheme AuthScheme;

        std::wstring ServerName;
        std::wstring RootPath;
        std::wstring UserName;
        std::wstring Password;
        std::wstring JobName;

        std::vector<std::wstring> FilterInclude;
        std::vector<std::wstring> FilterExclude;

        Upload()
            : Method(UploadMethod::NoUpload)
            , Operation(UploadOperation::NoOp) {};

        HRESULT Configure(const ConfigItem& item);

        bool IsFileUploaded(const std::wstring& file_name);
    };

    enum class Kind
    {
        None = 0,
        TableFile = 1 << 0,
        StructuredFile = 1 << 1,
        File = 1 << 2,
        Archive = 1 << 3,
        Directory = 1 << 4,
        SQL = 1 << 5,
        Pipe = 1 << 6,
        CSV = 1 << 7,
        TSV = 1 << 8,
        Parquet = 1 << 9,
        XML = 1 << 10,
        JSON = 1 << 11,
        ORC = 1 << 12
    };

    static std::wstring ToString(Kind kind);

    enum Disposition
    {
        Truncate = 0,
        CreateNew,
        Append
    };

    enum Status
    {
        StatusUnknown,
        Existing,
        CreatedNew
    };

    Kind Type = Kind::None;
    Kind supportedTypes;

    std::wstring Pattern;
    std::wstring FileName;
    std::wstring Path;

    Disposition Disposition = Disposition::Append;
    Status CreationStatus = StatusUnknown;

    std::wstring ConnectionString;
    std::wstring TableName;
    std::wstring TableKey;

    TableOutput::Schema Schema;

    ArchiveFormat ArchiveFormat = ArchiveFormat::Unknown;
    std::wstring Compression;
    std::wstring Password;

    std::shared_ptr<Upload> UploadOutput;

public:
    enum class Encoding
    {
        kUnknown = 0,
        UTF8,
        UTF16
    };

    static std::wstring ToString(Encoding encoding);

    Encoding OutputEncoding = Encoding::UTF8;
    LPCWSTR szSeparator = L",";
    LPCWSTR szQuote = L"\"";

    OutputSpec() noexcept = default;
    OutputSpec(OutputSpec&&) noexcept = default;
    OutputSpec(const OutputSpec&) = default;
    OutputSpec& operator=(const OutputSpec&) = default;
    OutputSpec& operator=(OutputSpec&&) noexcept = default;

    bool IsDirectory() const;
    bool IsFile() const;
    bool IsRegularFile() const;
    bool IsTableFile() const;
    bool IsStructuredFile() const;
    bool IsArchive() const;

    static bool IsPattern(const std::wstring& pattern);

    static HRESULT ApplyPattern(const std::wstring& pattern, const std::wstring& name, std::wstring& fileName);

    HRESULT Configure(
        OutputSpec::Kind supportedTypes,
        const std::wstring& inputString,
        std::optional<std::filesystem::path> parent = std::nullopt);

    HRESULT
    Configure(const std::wstring& inputString, std::optional<std::filesystem::path> parent = std::nullopt)
    {
        return Configure(supportedTypes, inputString, std::move(parent));
    };

    HRESULT Configure(
        OutputSpec::Kind supportedTypes,
        const ConfigItem& item,
        std::optional<std::filesystem::path> parent = std::nullopt);
    HRESULT
    Configure(const ConfigItem& item, std::optional<std::filesystem::path> parent = std::nullopt)
    {
        return Configure(supportedTypes, item, std::move(parent));
    };
};

ENABLE_BITMASK_OPERATORS(OutputSpec::Kind);

}  // namespace Orc

#pragma managed(pop)
