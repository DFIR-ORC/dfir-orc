//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <string>
#include <filesystem>

#include "OrcLib.h"
#include "ArchiveFormat.h"
#include "TableOutput.h"

#include "OutputSpecTypes.h"
#include "FileDisposition.h"
#include "Text/Encoding.h"
#include "Text/Fmt/OutputSpecTypes.h"

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

class OutputSpec
{
public:
    using UploadAuthScheme = OutputSpecTypes::UploadAuthScheme;
    using UploadMethod = OutputSpecTypes::UploadMethod;
    using BITSMode = OutputSpecTypes::BITSMode;
    using UploadOperation = OutputSpecTypes::UploadOperation;
    using UploadMode = OutputSpecTypes::UploadMode;
    using Kind = OutputSpecTypes::Kind;
    using Disposition = OutputSpecTypes::Disposition;
    using Status = OutputSpecTypes::Status;
    using Encoding = OutputSpecTypes::Encoding;

    class Upload
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

        std::optional<bool> bitsDeleteSmbShare;

        std::vector<std::wstring> FilterInclude;
        std::vector<std::wstring> FilterExclude;

        Upload()
            : Method(UploadMethod::NoUpload)
            , Operation(UploadOperation::NoOp)
            , bitsDeleteSmbShare(false)
        {
        }

        HRESULT Configure(const ConfigItem& item);

        bool IsFileUploaded(const std::wstring& file_name);
    };

    Kind Type = Kind::None;
    Kind supportedTypes;

    std::wstring Pattern;
    std::wstring FileName;
    std::wstring Path;

    Disposition disposition = Disposition::Append;
    Status creationStatus = Status::StatusUnknown;

    std::wstring ConnectionString;
    std::wstring TableName;
    std::wstring TableKey;

    TableOutput::Schema Schema;

    ArchiveFormat ArchiveFormat = ArchiveFormat::Unknown;
    std::wstring Compression;
    std::wstring Password;

    std::shared_ptr<Upload> UploadOutput;

public:
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

    static OutputSpec::Disposition ToDisposition(Orc::FileDisposition disposition);

    static OutputSpec::Encoding ToEncoding(Text::Encoding);

    static std::filesystem::path Resolve(const std::wstring& path);

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

Orc::FileDisposition ToFileDisposition(OutputSpec::Disposition disposition);

Text::Encoding ToEncoding(OutputSpec::Encoding encoding);

}  // namespace Orc

#pragma managed(pop)
