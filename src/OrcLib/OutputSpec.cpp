//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OutputSpec.h"

#include <filesystem>
#include <regex>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/tokenizer.hpp>
#include <fmt/format.h>
#include <fmt/xchar.h>

#include "Configuration/ConfigItem.h"
#include "ParameterCheck.h"
#include "CaseInsensitive.h"
#include "SystemDetails.h"
#include "Archive.h"
#include "Utils/WinApi.h"
#include "Utils/Uri.h"
#include "Text/Guid.h"

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;

namespace {

bool HasValue(const ConfigItem& item, DWORD dwIndex)
{
    if (item.SubItems.size() <= dwIndex)
    {
        return false;
    }

    if (!item.SubItems[dwIndex])
    {
        return false;
    }

    return true;
}

}  // namespace

namespace Orc {

bool Orc::OutputSpec::IsPattern(const std::wstring& strPattern)
{
    if (strPattern.find(L"{ComputerName}") != wstring::npos)
        return true;
    if (strPattern.find(L"{FullComputerName}") != wstring::npos)
        return true;
    if (strPattern.find(L"{SystemType}") != wstring::npos)
        return true;
    if (strPattern.find(L"{TimeStamp}") != wstring::npos)
        return true;
    if (strPattern.find(L"{Name}") != wstring::npos)
        return true;
    if (strPattern.find(L"{FileName}") != wstring::npos)
        return true;
    if (strPattern.find(L"{DirectoryName}") != wstring::npos)
        return true;
    return false;
}

HRESULT
OutputSpec::ApplyPattern(const std::wstring& strPattern, const std::wstring& strName, std::wstring& strFileName)
{
    wstring strComputerName;
    SystemDetails::GetOrcComputerName(strComputerName);

    wstring strFullComputerName;
    SystemDetails::GetOrcFullComputerName(strFullComputerName);

    wstring strTimeStamp;
    SystemDetails::GetTimeStamp(strTimeStamp);

    wstring strSystemType;
    SystemDetails::GetOrcSystemType(strSystemType);

    wstring strRunId(Orc::ToStringW(SystemDetails::GetOrcRunId()));

    strFileName = fmt::vformat(
        fmt::wstring_view(strPattern),
        fmt::make_wformat_args(
            fmt::arg(L"Name", strName),
            fmt::arg(L"FileName", strName),
            fmt::arg(L"DirectoryName", strName),
            fmt::arg(L"ComputerName", strComputerName),
            fmt::arg(L"FullComputerName", strFullComputerName),
            fmt::arg(L"TimeStamp", strTimeStamp),
            fmt::arg(L"SystemType", strSystemType),
            fmt::arg(L"RunId", strRunId)));

    return S_OK;
}

bool OutputSpec::IsDirectory() const
{
    return HasFlag(Type, Kind::Directory);
};

bool OutputSpec::IsFile() const
{
    return HasAnyFlag(
        Type,
        Kind::File | Kind::TableFile | Kind::StructuredFile | Kind::Archive | Kind::CSV | Kind::TSV | Kind::Parquet
            | Kind::ORC | Kind::XML | Kind::JSON);
}

// the same but without archive
bool OutputSpec::IsRegularFile() const
{
    return HasAnyFlag(
        Type,
        Kind::File | Kind::TableFile | Kind::StructuredFile | Kind::CSV | Kind::TSV | Kind::Parquet | Kind::ORC
            | Kind::XML | Kind::JSON);
}

bool OutputSpec::IsTableFile() const
{
    return HasAnyFlag(Type, Kind::TableFile | Kind::CSV | Kind::TSV | Kind::Parquet | Kind::ORC);
}

bool OutputSpec::IsStructuredFile() const
{
    return HasAnyFlag(Type, Kind::StructuredFile | Kind::XML | Kind::JSON);
}

bool OutputSpec::IsArchive() const
{
    return HasFlag(Type, Kind::Archive);
}

std::filesystem::path OutputSpec::Resolve(const std::wstring& path)
{
    fs::path output;

    if (IsPattern(path))
    {
        wstring patternApplied;
        if (auto hr = ApplyPattern(path, L""s, patternApplied); SUCCEEDED(hr))
        {
            output = patternApplied;
        }
        else
        {
            output = path;
        }
    }
    else
    {
        output = path;
    }

    WCHAR szExpanded[ORC_MAX_PATH] = {0};
    ExpandEnvironmentStrings(output.c_str(), szExpanded, ORC_MAX_PATH);
    if (wcscmp(output.c_str(), szExpanded) != 0)
    {
        output.assign(szExpanded);
    }

    return output;
}

HRESULT OutputSpec::Configure(
    OutputSpec::Kind supported,
    const std::wstring& strInputString,
    std::optional<std::filesystem::path> parent)
{
    HRESULT hr = E_FAIL;

    Type = OutputSpec::Kind::None;

    if (IsPattern(strInputString))
    {
        Pattern = strInputString;
    }

    // Now on with regular file paths
    fs::path outPath = Resolve(strInputString);

    if (parent.has_value())
    {
        outPath = parent.value() / outPath.filename();
        Path = outPath;
    }
    else
    {
        Path = outPath;
    }

    FileName = outPath.filename().wstring();

    auto extension = outPath.extension();

    if (HasFlag(supported, OutputSpec::Kind::TableFile))
    {
        if (equalCaseInsensitive(extension.c_str(), L".csv"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV);
            szSeparator = L",";
            szQuote = L"\"";
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
        else if (equalCaseInsensitive(extension.c_str(), L".tsv"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV);
            szSeparator = L"\t";
            szQuote = L"";
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
        else if (equalCaseInsensitive(extension.c_str(), L".parquet"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
        else if (equalCaseInsensitive(extension.c_str(), L".orc"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (HasFlag(supported, OutputSpec::Kind::StructuredFile))
    {
        if (equalCaseInsensitive(extension.c_str(), L".xml"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (HasFlag(supported, OutputSpec::Kind::StructuredFile))
    {
        if (equalCaseInsensitive(extension.c_str(), L".json"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (HasFlag(supported, OutputSpec::Kind::Archive))
    {
        auto fmt = OrcArchive::GetArchiveFormat(extension.c_str());
        if (fmt != ArchiveFormat::Unknown)
        {
            Type = OutputSpec::Kind::Archive;
            ArchiveFormat = fmt;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }

    if (HasFlag(supported, OutputSpec::Kind::Directory) && !outPath.empty())
    {
        // Guess this is a directory path if either there is no extension or no file output is supported
        if (wcslen(extension.c_str()) == 0
            || (!HasFlag(supported, OutputSpec::Kind::File) && !HasFlag(supported, OutputSpec::Kind::StructuredFile)
                && !HasFlag(supported, OutputSpec::Kind::TableFile)))
        {
            // Output without extension could very well be a dir
            if (SUCCEEDED(VerifyDirectoryExists(outPath.c_str())))
            {
                Type = OutputSpec::Kind::Directory;
                Path = outPath.wstring();
                creationStatus = Status::Existing;
                return S_OK;
            }
            else
            {
                Type = OutputSpec::Kind::Directory;
                creationStatus = Status::CreatedNew;
                return Orc::GetOutputDir(outPath.c_str(), Path, true);
            }
        }
    }

    if (HasFlag(supported, OutputSpec::Kind::File))
    {
        Type = OutputSpec::Kind::File;
        return Orc::GetOutputFile(outPath.c_str(), Path, true);
    }
    return S_FALSE;
}

HRESULT
OutputSpec::Configure(OutputSpec::Kind supported, const ConfigItem& item, std::optional<std::filesystem::path> parent)
{
    HRESULT hr = E_FAIL;

    if (!item)
        return S_OK;

    Type = OutputSpec::Kind::None;

    ArchiveFormat = ArchiveFormat::Unknown;
    if (!item.empty())
    {
        if (FAILED(hr = Configure(supported, item.c_str())))
        {
            Log::Error(L"An error occured when evaluating output item {}", item.c_str());
            return hr;
        }
        if (::HasValue(item, CONFIG_OUTPUT_FORMAT))
        {
            ArchiveFormat = OrcArchive::GetArchiveFormat(item.SubItems[CONFIG_OUTPUT_FORMAT]);
        }
    }

    OutputEncoding = OutputSpec::Encoding::UTF8;
    if (::HasValue(item, CONFIG_OUTPUT_ENCODING))
    {
        if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_ENCODING].c_str(), L"utf8"sv))
        {
            OutputEncoding = OutputSpec::Encoding::UTF8;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_ENCODING].c_str(), L"utf16"sv))
        {
            OutputEncoding = OutputSpec::Encoding::UTF16;
        }
        else
        {
            Log::Error(
                L"Invalid encoding for outputdir in config file: {}", item.SubItems[CONFIG_OUTPUT_ENCODING].c_str());
            return E_INVALIDARG;
        }
    }

    if (::HasValue(item, CONFIG_OUTPUT_COMPRESSION))
    {
        Compression = item.SubItems[CONFIG_OUTPUT_COMPRESSION];
    }

    if (::HasValue(item, CONFIG_OUTPUT_PASSWORD))
    {
        Password = item.SubItems[CONFIG_OUTPUT_PASSWORD];
    }
    return S_OK;
}

HRESULT OutputSpec::Upload::Configure(const ConfigItem& item)
{
    std::error_code ec;

    if (::HasValue(item, CONFIG_UPLOAD_METHOD))
    {
        if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_METHOD], L"BITS"sv))
        {
            Method = OutputSpec::UploadMethod::BITS;
        }
        else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_METHOD], L"filecopy"sv))
        {
            Method = OutputSpec::UploadMethod::FileCopy;
        }
        else
            Method = OutputSpec::UploadMethod::NoUpload;

        if (::HasValue(item, CONFIG_UPLOAD_JOBNAME))
        {
            JobName = item.SubItems[CONFIG_UPLOAD_JOBNAME];
        }

        std::optional<std::wstring> configUploadServer;
        if (item.SubItems[CONFIG_UPLOAD_SERVER])
        {
            configUploadServer = item.SubItems[CONFIG_UPLOAD_SERVER];
        }

        std::optional<std::wstring> configUploadRootPath;
        if (item.SubItems[CONFIG_UPLOAD_ROOTPATH])
        {
            configUploadRootPath = item.SubItems[CONFIG_UPLOAD_ROOTPATH];
        }

        std::optional<std::wstring> configUploadUserName;
        if (item.SubItems[CONFIG_UPLOAD_USER])
        {
            configUploadUserName = item.SubItems[CONFIG_UPLOAD_USER];
        }

        std::optional<std::wstring> configUploadPassword;
        if (item.SubItems[CONFIG_UPLOAD_PASSWORD])
        {
            configUploadPassword = item.SubItems[CONFIG_UPLOAD_PASSWORD];
        }

        if (::HasValue(item, CONFIG_UPLOAD_URI))
        {
            if (configUploadServer || configUploadRootPath || configUploadUserName || configUploadPassword)
            {
                Log::Error(
                    "Invalid configuration: 'uri' cannot be used along with 'server','path','username','password' "
                    "attributes");
                return E_INVALIDARG;
            }

            std::wstring configUploadUri = ExpandEnvironmentStringsApi(item[CONFIG_UPLOAD_URI].c_str(), ec);
            if (ec)
            {
                Log::Warn("Failed to expand environment variables in uri [{}]", ec);

                // There could be no environment variable and this is still better than nothing...
                configUploadUri = item.SubItems[CONFIG_UPLOAD_URI];
            }

            Uri uri(configUploadUri, ec);
            if (ec)
            {
                Log::Error(L"Failed to parse uri '{}' [{}]", item.SubItems[CONFIG_UPLOAD_URI].c_str(), ec);
                return E_INVALIDARG;
            }

            if (uri.Host())
            {
                configUploadServer = uri.Scheme().value_or(L"file") + L"://" + *uri.Host();
            }

            if (uri.Path())
            {
                configUploadRootPath = *uri.Path();
            }

            if (uri.UserName())
            {
                configUploadUserName = *uri.UserName();
            }

            if (uri.Password())
            {
                configUploadPassword = *uri.Password();
            }
        }
        if (configUploadServer)
        {
            std::wstring server = ExpandEnvironmentStringsApi(configUploadServer->c_str(), ec);

            static std::wregex r(L"(http|https|file):(//|\\\\)(.*)", std::regex_constants::icase);
            std::wsmatch s;

            if (std::regex_match(server, s, r))
            {
                if (equalCaseInsensitive(s[1].str().c_str(), L"http"sv))
                {
                    bitsMode = BITSMode::HTTP;
                }
                else if (equalCaseInsensitive(s[1].str(), L"https"sv))
                {
                    bitsMode = BITSMode::HTTPS;
                }
                else if (equalCaseInsensitive(s[1].str(), L"file"sv))
                {
                    bitsMode = BITSMode::SMB;
                }
                else
                {
                    return E_INVALIDARG;
                }
                ServerName = s[3].str();
            }
            else
            {
                return E_INVALIDARG;
            }
        }
        if (configUploadRootPath)
        {
            std::wstring root = ExpandEnvironmentStringsApi(configUploadRootPath->c_str(), ec);
            if (ec)
            {
                Log::Warn("Failed to expand environment variables [{}]", ec);

                // There could be no environment variable and this is still better than nothing...
                root = item.SubItems[CONFIG_UPLOAD_ROOTPATH];
            }

            std::replace_copy(
                std::begin(root),
                std::end(root),
                back_inserter(RootPath),
                bitsMode == BITSMode::SMB ? L'/' : L'\\',
                bitsMode == BITSMode::SMB ? L'\\' : L'/');

            if (RootPath.empty())
            {
                RootPath = bitsMode == BITSMode::SMB ? L"\\" : L"/";
            }
            else if (RootPath.front() != L'\\' && RootPath.front() != L'/')
            {
                RootPath.insert(0, 1, bitsMode == BITSMode::SMB ? L'\\' : L'/');
            }
        }

        if (configUploadUserName)
        {
            UserName = *configUploadUserName;
        }

        if (configUploadPassword)
        {
            Password = *configUploadPassword;
        }

        Operation = OutputSpec::UploadOperation::Copy;
        if (::HasValue(item, CONFIG_UPLOAD_OPERATION))
        {
            if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_OPERATION], L"Copy"sv))
            {
                Operation = OutputSpec::UploadOperation::Copy;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_OPERATION], L"Move"sv))
            {
                Operation = OutputSpec::UploadOperation::Move;
            }
        }

        Mode = OutputSpec::UploadMode::Synchronous;
        if (::HasValue(item, CONFIG_UPLOAD_MODE))
        {
            if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_MODE], L"sync"sv))
            {
                Mode = OutputSpec::UploadMode::Synchronous;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_MODE], L"async"sv))
            {
                Mode = OutputSpec::UploadMode::Asynchronous;
            }
        }

        AuthScheme = OutputSpec::UploadAuthScheme::Negotiate;
        if (::HasValue(item, CONFIG_UPLOAD_AUTHSCHEME))
        {
            if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Anonymous"sv))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Basic"sv))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Basic;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"NTLM"sv))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::NTLM;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Kerberos"sv))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Kerberos;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Negotiate"sv))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Negotiate;
            }
        }

        if (::HasValue(item, CONFIG_UPLOAD_FILTER_INC))
        {
            boost::split(
                FilterInclude, (const std::wstring&)item.SubItems[CONFIG_UPLOAD_FILTER_INC], boost::is_any_of(L",;"));
        }

        if (::HasValue(item, CONFIG_UPLOAD_FILTER_EXC))
        {
            boost::split(
                FilterExclude, (const std::wstring&)item.SubItems[CONFIG_UPLOAD_FILTER_INC], boost::is_any_of(L",;"));
        }
    }
    return S_OK;
}

bool Orc::OutputSpec::Upload::IsFileUploaded(const std::wstring& file_name)
{
    if (!FilterInclude.empty())
    {
        auto bMatched = false;
        for (const auto& filter : FilterInclude)
        {
            if (PathMatchSpecW(file_name.c_str(), filter.c_str()))
            {
                bMatched = true;
                break;
            }
        }
        if (!bMatched)
            return false;
    }

    for (const auto& filter : FilterExclude)
    {
        if (PathMatchSpecW(file_name.c_str(), filter.c_str()))
            return false;
    }

    return true;
}

OutputSpec::Disposition OutputSpec::ToDisposition(Orc::FileDisposition disposition)
{
    switch (disposition)
    {
        case FileDisposition::Append:
            return OutputSpec::Disposition::Append;
        case FileDisposition::CreateNew:
            return OutputSpec::Disposition::CreateNew;
        case FileDisposition::Truncate:
            return OutputSpec::Disposition::Truncate;
        default:
            assert(nullptr);
            return OutputSpec::Disposition::Append;
    }
}

OutputSpec::Encoding OutputSpec::ToEncoding(Text::Encoding encoding)
{
    switch (encoding)
    {
        case Text::Encoding::Utf8:
            return OutputSpec::Encoding::UTF8;
        case Text::Encoding::Utf16:
            return OutputSpec::Encoding::UTF16;
        default:
            assert(nullptr);
            return OutputSpec::Encoding::kUnknown;
    }
}

Orc::FileDisposition ToFileDisposition(OutputSpec::Disposition disposition)
{
    switch (disposition)
    {
        case OutputSpec::Disposition::Append:
            return FileDisposition::Append;
        case OutputSpec::Disposition::CreateNew:
            return FileDisposition::CreateNew;
        case OutputSpec::Disposition::Truncate:
            return FileDisposition::Truncate;
        default: {
            assert(nullptr);
            return FileDisposition::Unknown;
        }
    }
}

Text::Encoding ToEncoding(OutputSpec::Encoding encoding)
{
    switch (encoding)
    {
        case OutputSpec::Encoding::UTF8:
            return Text::Encoding::Utf8;
        case OutputSpec::Encoding::UTF16:
            return Text::Encoding::Utf16;
        default: {
            assert(nullptr);
            return Text::Encoding::Unknown;
        }
    }
}

}  // namespace Orc
