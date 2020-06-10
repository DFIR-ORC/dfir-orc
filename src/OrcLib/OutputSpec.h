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

#include <string>
#include <filesystem>

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

class ORCLIB_API OutputSpec
{
public:
    enum UploadAuthScheme : char
    {
        Anonymous = 0,
        Basic,
        NTLM,
        Kerberos,
        Negotiate
    };

    enum UploadMethod : char
    {
        NoUpload = 0,
        FileCopy,
        BITS
    };

    enum BITSMode : char
    {
        NoMode,
        SMB,
        HTTP,
        HTTPS
    };

    enum UploadOperation : char
    {
        NoOp = 0,
        Copy,
        Move
    };

    enum UploadMode : char
    {
        Synchronous = 0,
        Asynchronous
    };

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
            : Method(NoUpload)
            , Operation(NoOp) {};

        HRESULT Configure(const logger& pLog, const ConfigItem& item);

        bool IsFileUploaded(const logger& pLog, const std::wstring& file_name);
    };

    enum Kind
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
    typedef enum _Encoding
    {
        Undetermined = 0,
        UTF8,
        UTF16
    } Encoding;

    Encoding OutputEncoding = Encoding::UTF8;
    LPCWSTR szSeparator = L",";
    LPCWSTR szQuote = L"\"";
    DWORD XOR = 0L;

    OutputSpec() noexcept = default;
    OutputSpec(OutputSpec&&) noexcept = default;
    OutputSpec(const OutputSpec&) = default;
    OutputSpec& operator=(const OutputSpec&) = default;
    OutputSpec& operator=(OutputSpec&&) noexcept = default;


    bool IsDirectory() {
        return Type & Kind::Directory;
    };

    bool IsFile() {
        return Type & Kind::File
            || Type & Kind::TableFile
            || Type & Kind::StructuredFile
            || Type & Kind::Archive
            || Type & Kind::CSV
            || Type & Kind::TSV
            || Type & Kind::Parquet
            || Type & Kind::ORC
            || Type & Kind::XML
            || Type & Kind::JSON;
    }

    bool IsRegularFile() // the same but without archive
    {
        return Type & Kind::File || Type & Kind::TableFile || Type & Kind::StructuredFile
            || Type & Kind::CSV || Type & Kind::TSV || Type & Kind::Parquet || Type & Kind::ORC || Type & Kind::XML
            || Type & Kind::JSON;
    }

    bool IsTableFile() 
    {
        return Type & Kind::TableFile || Type & Kind::CSV
            || Type & Kind::TSV || Type & Kind::Parquet || Type & Kind::ORC;
    }

    bool IsStructuredFile() {
        return Type & Kind::StructuredFile || Type & Kind::XML || Type & Kind::JSON;
    }

    bool IsArchive()
    {
        return Type & Kind::Archive;
    }

    static bool IsPattern(const std::wstring& strPattern);

    static HRESULT
    ApplyPattern(const std::wstring& strPattern, const std::wstring& strName, std::wstring& strFileName);

    HRESULT Configure(
        const logger& pLog,
        OutputSpec::Kind supportedTypes,
        const std::wstring& strInputString,
        std::optional<std::filesystem::path> parent = std::nullopt);
    HRESULT
    Configure(
        const logger& pLog,
        const std::wstring& strInputString,
        std::optional<std::filesystem::path> parent = std::nullopt)
    {
        return Configure(pLog, supportedTypes, strInputString, std::move(parent));
    };

    HRESULT Configure(const logger& pLog, OutputSpec::Kind supportedTypes, const ConfigItem& item, std::optional<std::filesystem::path> parent = std::nullopt);
    HRESULT
    Configure(const logger& pLog, const ConfigItem& item, std::optional<std::filesystem::path> parent = std::nullopt)
    {
        return Configure(pLog, supportedTypes, item, std::move(parent));
    };
};

}  // namespace Orc

#pragma managed(pop)
