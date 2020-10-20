//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OutputSpec.h"

#include "ConfigItem.h"
#include "ParameterCheck.h"
#include "CaseInsensitive.h"
#include "SystemDetails.h"

#include "Archive.h"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/tokenizer.hpp>

#include <filesystem>
#include <regex>

#include <fmt/format.h>

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

std::wstring OutputSpec::ToString(OutputSpec::UploadAuthScheme scheme)
{
    switch (scheme)
    {
        case OutputSpec::UploadAuthScheme::Anonymous:
            return L"Anonymous";
        case OutputSpec::UploadAuthScheme::Basic:
            return L"Basic";
        case OutputSpec::UploadAuthScheme::Kerberos:
            return L"Kerberos";
        case OutputSpec::UploadAuthScheme::Negotiate:
            return L"Negotiate";
        case OutputSpec::UploadAuthScheme::NTLM:
            return L"NTLM";
    }

    return L"Unknown";
}

std::wstring OutputSpec::ToString(OutputSpec::UploadMethod method)
{
    switch (method)
    {
        case OutputSpec::UploadMethod::BITS:
            return L"Background Intelligent Transfer Service (BITS)";
        case OutputSpec::UploadMethod::FileCopy:
            return L"File copy";
        case OutputSpec::UploadMethod::NoUpload:
            return L"No upload";
    }

    return L"Unknown";
}

std::wstring OutputSpec::ToString(OutputSpec::UploadOperation operation)
{
    switch (operation)
    {
        case OutputSpec::UploadOperation::Copy:
            return L"Copy file";
        case OutputSpec::UploadOperation::Move:
            return L"Move file";
        case OutputSpec::UploadOperation::NoOp:
            return L"No operation";
    }

    return L"Unknown";
}

std::wstring OutputSpec::ToString(UploadMode mode)
{
    switch (mode)
    {
        case OutputSpec::UploadMode::Asynchronous:
            return L"Asynchronous";
        case OutputSpec::UploadMode::Synchronous:
            return L"Synchronous";
    }

    return L"Unknown";
}

std::wstring OutputSpec::ToString(OutputSpec::Kind kind)
{
    switch (kind)
    {
        case OutputSpec::Kind::Archive:
            return L"archive";
        case OutputSpec::Kind::CSV:
            return L"csv";
        case OutputSpec::Kind::Directory:
            return L"directory";
        case OutputSpec::Kind::File:
            return L"file";
        case OutputSpec::Kind::JSON:
            return L"json";
        case OutputSpec::Kind::None:
            return L"none";
        case OutputSpec::Kind::ORC:
            return L"ORC";
        case OutputSpec::Kind::Parquet:
            return L"parquet";
        case OutputSpec::Kind::Pipe:
            return L"pipe";
        case OutputSpec::Kind::SQL:
            return L"sql";
        case OutputSpec::Kind::StructuredFile:
            return L"structured file";
        case OutputSpec::Kind::TableFile:
            return L"table file";
        case OutputSpec::Kind::TSV:
            return L"tsv";
        case OutputSpec::Kind::XML:
            return L"xml";
    }

    return L"Unknown";
}

std::wstring OutputSpec::ToString(OutputSpec::Encoding encoding)
{
    switch (encoding)
    {
        case OutputSpec::Encoding::UTF8:
            return L"utf-8";
        case OutputSpec::Encoding::UTF16:
            return L"utf-16";
    }

    return L"Unknown";
}

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
    SystemDetails::GetSystemType(strSystemType);

    strFileName = fmt::format(
        strPattern,
        fmt::arg(L"Name", strName),
        fmt::arg(L"FileName", strName),
        fmt::arg(L"DirectoryName", strName),
        fmt::arg(L"ComputerName", strComputerName),
        fmt::arg(L"FullComputerName", strFullComputerName),
        fmt::arg(L"TimeStamp", strTimeStamp),
        fmt::arg(L"SystemType", strSystemType));

    return S_OK;
}

bool OutputSpec::IsDirectory() const
{
    return Type & Kind::Directory;
};

bool OutputSpec::IsFile() const
{
    return Type & Kind::File || Type & Kind::TableFile || Type & Kind::StructuredFile || Type & Kind::Archive
        || Type & Kind::CSV || Type & Kind::TSV || Type & Kind::Parquet || Type & Kind::ORC || Type & Kind::XML
        || Type & Kind::JSON;
}

// the same but without archive
bool OutputSpec::IsRegularFile() const
{
    return Type & Kind::File || Type & Kind::TableFile || Type & Kind::StructuredFile || Type & Kind::CSV
        || Type & Kind::TSV || Type & Kind::Parquet || Type & Kind::ORC || Type & Kind::XML || Type & Kind::JSON;
}

bool OutputSpec::IsTableFile() const
{
    return Type & Kind::TableFile || Type & Kind::CSV || Type & Kind::TSV || Type & Kind::Parquet || Type & Kind::ORC;
}

bool OutputSpec::IsStructuredFile() const
{
    return Type & Kind::StructuredFile || Type & Kind::XML || Type & Kind::JSON;
}

bool OutputSpec::IsArchive() const
{
    return Type & Kind::Archive;
}

HRESULT OutputSpec::Configure(
    OutputSpec::Kind supported,
    const std::wstring& strInputString,
    std::optional<std::filesystem::path> parent)
{
    HRESULT hr = E_FAIL;

    Type = OutputSpec::Kind::None;

    if (OutputSpec::Kind::SQL & supported)  // Getting the SQL stuff out of the door asap
    {
        static std::wregex reConnectionString(LR"RAW(^(([\w\s]+=[\w\s{}.]+;?)+)#([\w]+)$)RAW");

        std::wcmatch matches;
        if (std::regex_match(strInputString.c_str(), matches, reConnectionString))
        {
            if (matches.size() == 4)
            {
                Type = OutputSpec::Kind::SQL;
                ConnectionString = matches[1].str();
                TableName = matches[3].str();
                return S_OK;
            }
        }
    }

    // Now on with regular file paths
    fs::path outPath;

    if (IsPattern(strInputString))
    {
        Pattern = strInputString;

        wstring patternApplied;
        if (auto hr = ApplyPattern(strInputString, L""s, patternApplied); SUCCEEDED(hr))
            outPath = patternApplied;
        else
            outPath = strInputString;
    }
    else
        outPath = strInputString;

    WCHAR szExpanded[MAX_PATH] = {0};
    ExpandEnvironmentStrings(outPath.c_str(), szExpanded, MAX_PATH);
    if (wcscmp(outPath.c_str(), szExpanded) != 0)
        outPath.assign(szExpanded);

    if (parent.has_value())
    {
        outPath = parent.value() / outPath.filename();
        Path = outPath;
    }
    else
        Path = outPath;

    FileName = outPath.filename().wstring();

    auto extension = outPath.extension();

    if (OutputSpec::Kind::TableFile & supported)
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
    if (OutputSpec::Kind::StructuredFile & supported)
    {
        if (equalCaseInsensitive(extension.c_str(), L".xml"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (OutputSpec::Kind::StructuredFile & supported)
    {
        if (equalCaseInsensitive(extension.c_str(), L".json"sv))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }
    if (OutputSpec::Kind::Archive & supported)
    {
        auto fmt = Archive::GetArchiveFormat(extension.c_str());
        if (fmt != ArchiveFormat::Unknown)
        {
            Type = OutputSpec::Kind::Archive;
            ArchiveFormat = fmt;
            return Orc::GetOutputFile(outPath.c_str(), Path, true);
        }
    }

    if (OutputSpec::Kind::Directory & supported && wcslen(extension.c_str()) == 0L && !outPath.empty())
    {
        // Output without extension could very well be a dir
        if (SUCCEEDED(VerifyDirectoryExists(outPath.c_str())))
        {
            Type = OutputSpec::Kind::Directory;
            Path = outPath.wstring();
            CreationStatus = Existing;
            return S_OK;
        }
        else
        {
            Type = OutputSpec::Kind::Directory;
            CreationStatus = CreatedNew;
            return Orc::GetOutputDir(outPath.c_str(), Path, true);
        }
    }

    if (OutputSpec::Kind::File & supported)
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

    if (supported & static_cast<OutputSpec::Kind>(OutputSpec::Kind::SQL))
    {
        bool bDone = false;
        if (::HasValue(item, CONFIG_OUTPUT_CONNECTION))
        {
            bool bDone = false;
            if (::HasValue(item, CONFIG_OUTPUT_CONNECTION))
            {
                Type = OutputSpec::Kind::SQL;
                ConnectionString = item.SubItems[CONFIG_OUTPUT_CONNECTION];
                bDone = true;
            }
            if (::HasValue(item, CONFIG_OUTPUT_TABLE))
            {
                TableName = item.SubItems[CONFIG_OUTPUT_TABLE];
            }
            if (::HasValue(item, CONFIG_OUTPUT_KEY))
            {
                TableKey = item.SubItems[CONFIG_OUTPUT_KEY];
            }
            if (::HasValue(item, CONFIG_OUTPUT_DISPOSITION))
            {
                if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"createnew"sv)
                    || equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"create_new"sv))
                    Disposition = Disposition::CreateNew;
                else if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"truncate"sv))
                    Disposition = Disposition::Truncate;
                else if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"append"sv))
                    Disposition = Disposition::Append;
                else
                {
                    spdlog::warn(
                        L"Invalid disposition \"{}\", defaulting to append", item[CONFIG_OUTPUT_DISPOSITION].c_str());
                    Disposition = Disposition::Append;
                }
            }
        }
        if (::HasValue(item, CONFIG_OUTPUT_TABLE))
        {
            TableName = item.SubItems[CONFIG_OUTPUT_TABLE];
        }
        if (::HasValue(item, CONFIG_OUTPUT_KEY))
        {
            TableKey = item.SubItems[CONFIG_OUTPUT_KEY];
        }
        if (::HasValue(item, CONFIG_OUTPUT_DISPOSITION))
        {
            if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"createnew"sv)
                || equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"create_new"sv))
                Disposition = Disposition::CreateNew;
            else if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"truncate"sv))
                Disposition = Disposition::Truncate;
            else if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"append"sv))
                Disposition = Disposition::Append;
            else
            {
                spdlog::warn(
                    L"Invalid disposition \"{}\", defaulting to append", item[CONFIG_OUTPUT_DISPOSITION].c_str());
                Disposition = Disposition::Append;
            }
        }
    }

    ArchiveFormat = ArchiveFormat::Unknown;
    if (!item.empty())
    {
        if (FAILED(hr = Configure(supported, item.c_str())))
        {
            spdlog::error(L"An error occured when evaluating output item {}", item.c_str());
            return hr;
        }
        if (::HasValue(item, CONFIG_OUTPUT_FORMAT))
        {
            ArchiveFormat = Archive::GetArchiveFormat(item.SubItems[CONFIG_OUTPUT_FORMAT]);
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
            spdlog::error(L"Invalid encoding for outputdir in config file: {}", item.SubItems[CONFIG_OUTPUT_ENCODING]);
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

        if (::HasValue(item, CONFIG_UPLOAD_SERVER))
        {
            static std::wregex r(L"(http|https|file):(//|\\\\)(.*)", std::regex_constants::icase);

            std::wsmatch s;

            if (std::regex_match((const wstring&)item.SubItems[CONFIG_UPLOAD_SERVER], s, r))
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
        if (::HasValue(item, CONFIG_UPLOAD_ROOTPATH))
        {
            const std::wstring_view root = item.SubItems[CONFIG_UPLOAD_ROOTPATH];
            std::replace_copy(
                begin(root),
                end(root),
                back_inserter(RootPath),
                bitsMode == BITSMode::SMB ? L'/' : L'\\',
                bitsMode == BITSMode::SMB ? L'\\' : L'/');

            if (RootPath.empty())
            {
                RootPath = bitsMode == BITSMode::SMB ? L"\\" : L"/";
            }
            else if (RootPath.front() != L'\\' || RootPath.front() != L'/')
            {
                RootPath.insert(0, 1, bitsMode == BITSMode::SMB ? L'\\' : L'/');
            }
        }
        if (::HasValue(item, CONFIG_UPLOAD_USER))
        {
            UserName = item.SubItems[CONFIG_UPLOAD_USER];
        }
        if (::HasValue(item, CONFIG_UPLOAD_PASSWORD))
        {
            Password = item.SubItems[CONFIG_UPLOAD_PASSWORD];
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
