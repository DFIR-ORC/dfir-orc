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

#include "Archive.h"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/tokenizer.hpp>

#include <filesystem>
#include <regex>

using namespace std;
namespace fs = std::filesystem;

using namespace Orc;

HRESULT OutputSpec::Configure(const logger& pLog, OutputSpec::Kind supported, const WCHAR* szInputString)
{
    HRESULT hr = E_FAIL;

    Type = OutputSpec::Kind::None;
    WCHAR szExpanded[MAX_PATH] = {0};

    ExpandEnvironmentStrings(szInputString, szExpanded, MAX_PATH);

    if (OutputSpec::Kind::SQL & supported)
    {
        static std::wregex reConnectionString(L"^(([a-zA-Z0-9_\\s]+=[a-zA-Z0-9\\s{}.]+;)+)#([a-zA-Z0-9]+)$");

        std::wcmatch matches;
        if (std::regex_match(szInputString, matches, reConnectionString))
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

    WCHAR szExtension[MAX_PATH] = {0};

    if (FAILED(hr = GetExtensionForFile(szExpanded, szExtension, MAX_PATH)))
    {
        log::Error(pLog, hr, L"Failed to extract extension from %s\r\n", szExpanded);
        return hr;
    }

    if (OutputSpec::Kind::TableFile & supported)
    {
        if (equalCaseInsensitive(szExtension, L".csv"))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::CSV);
            szSeparator = L",";
            szQuote = L"\"";
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(szExpanded, Path, true);
        }
        else if (equalCaseInsensitive(szExtension, L".tsv"))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::TSV);
            szSeparator = L"\t";
            szQuote = L"";
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(szExpanded, Path, true);
        }
        else if (equalCaseInsensitive(szExtension, L".parquet"))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::Parquet);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(szExpanded, Path, true);
        }
        else if (equalCaseInsensitive(szExtension, L".orc"))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::TableFile | OutputSpec::Kind::ORC);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(szExpanded, Path, true);
        }
    }
    if (OutputSpec::Kind::StructuredFile & supported)
    {
        if (equalCaseInsensitive(szExtension, L".xml"))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(szExpanded, Path, true);
        }
    }
    if (OutputSpec::Kind::StructuredFile & supported)
    {
        if (equalCaseInsensitive(szExtension, L".json"))
        {
            Type = static_cast<OutputSpec::Kind>(OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON);
            ArchiveFormat = ArchiveFormat::Unknown;
            return Orc::GetOutputFile(szExpanded, Path, true);
        }
    }
    if (OutputSpec::Kind::Archive & supported)
    {
        auto fmt = Archive::GetArchiveFormat(szExtension);
        if (fmt != ArchiveFormat::Unknown)
        {
            Type = OutputSpec::Kind::Archive;
            ArchiveFormat = fmt;
            return Orc::GetOutputFile(szExpanded, Path, true);
        }
    }

    if (OutputSpec::Kind::Directory & supported && wcslen(szExtension) == 0L && wcslen(szExpanded) > 0)
    {
        // Output without extension could very well be a dir
        if (SUCCEEDED(VerifyDirectoryExists(szExpanded)))
        {
            Type = OutputSpec::Kind::Directory;
            Path = szExpanded;
            CreationStatus = Existing;
            return S_OK;
        }
        else
        {
            Type = OutputSpec::Kind::Directory;
            CreationStatus = CreatedNew;
            return Orc::GetOutputDir(szExpanded, Path, true);
        }
    }

    if (OutputSpec::Kind::File & supported)
    {
        Type = OutputSpec::Kind::File;
        return Orc::GetOutputFile(szExpanded, Path, true);
    }
    return S_FALSE;
}

HRESULT OutputSpec::Configure(const logger& pLog, OutputSpec::Kind supported, const ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    if (item)
    {
        Type = OutputSpec::Kind::None;

        if (supported & static_cast<OutputSpec::Kind>(OutputSpec::Kind::SQL))
        {
            bool bDone = false;
            if (item.SubItems[CONFIG_OUTPUT_CONNECTION])
            {
                Type = OutputSpec::Kind::SQL;
                ConnectionString = item.SubItems[CONFIG_OUTPUT_CONNECTION];
                bDone = true;
            }
            if (item.SubItems[CONFIG_OUTPUT_TABLE])
            {
                TableName = item.SubItems[CONFIG_OUTPUT_TABLE];
            }
            if (item.SubItems[CONFIG_OUTPUT_KEY])
            {
                TableKey = item.SubItems[CONFIG_OUTPUT_KEY];
            }
            if (item[CONFIG_OUTPUT_DISPOSITION])
            {
                if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"createnew")
                    || equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"create_new"))
                    Disposition = Disposition::CreateNew;
                else if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"truncate"))
                    Disposition = Disposition::Truncate;
                else if (equalCaseInsensitive(item[CONFIG_OUTPUT_DISPOSITION], L"append"))
                    Disposition = Disposition::Append;
                else
                {
                    log::Warning(
                        pLog,
                        E_INVALIDARG,
                        L"Invalid disposition \"%s\", defaulting to append\r\n",
                        item[CONFIG_OUTPUT_DISPOSITION].c_str());
                    Disposition = Disposition::Append;
                }
            }
        }

        ArchiveFormat = ArchiveFormat::Unknown;
        if (!item.strData.empty())
        {
            if (FAILED(hr = Configure(pLog, supported, item.c_str())))
            {
                log::Error(pLog, hr, L"An error occured when evaluating output item %s\r\n", item.c_str());
                return hr;
            }
            if (item.SubItems[CONFIG_OUTPUT_FORMAT])
            {
                ArchiveFormat = Archive::GetArchiveFormat(item.SubItems[CONFIG_OUTPUT_FORMAT]);
            }
        }

        XOR = 0L;
        if (item.SubItems[CONFIG_OUTPUT_XORPATTERN])
        {
            if (FAILED(hr = GetIntegerFromHexaString(item.SubItems[CONFIG_OUTPUT_XORPATTERN].c_str(), XOR)))
            {
                log::Error(
                    pLog,
                    hr,
                    L"Invalid XOR pattern for outputdir in config file: %s\r\n",
                    item.SubItems[CONFIG_OUTPUT_XORPATTERN].strData.c_str());
                return hr;
            }
        }

        OutputEncoding = OutputSpec::Encoding::UTF8;
        if (item.SubItems[CONFIG_OUTPUT_ENCODING])
        {
            if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_ENCODING].c_str(), L"utf8"))
            {
                OutputEncoding = OutputSpec::Encoding::UTF8;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_OUTPUT_ENCODING].c_str(), L"utf16"))
            {
                OutputEncoding = OutputSpec::Encoding::UTF16;
            }
            else
            {
                log::Error(
                    pLog,
                    E_INVALIDARG,
                    L"Invalid encoding for outputdir in config file: %s\r\n",
                    item.SubItems[CONFIG_OUTPUT_ENCODING].strData.c_str());
                return E_INVALIDARG;
            }
        }

        if (item.SubItems[CONFIG_OUTPUT_COMPRESSION])
        {
            Compression = item.SubItems[CONFIG_OUTPUT_COMPRESSION];
        }

        if (item.SubItems[CONFIG_OUTPUT_PASSWORD])
        {
            Password = item.SubItems[CONFIG_OUTPUT_PASSWORD];
        }
    }
    return S_OK;
}

HRESULT OutputSpec::Upload::Configure(const logger& pLog, const ConfigItem& item)
{
    if (item.SubItems[CONFIG_UPLOAD_METHOD])
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

        if (item.SubItems[CONFIG_UPLOAD_JOBNAME])
        {
            JobName = item.SubItems[CONFIG_UPLOAD_JOBNAME];
        }
        if (item.SubItems[CONFIG_UPLOAD_SERVER])
        {
            static std::wregex r(L"(http|https|file):(//|\\\\)(.*)", std::regex_constants::icase);

            std::wsmatch s;

            if (std::regex_match(item.SubItems[CONFIG_UPLOAD_SERVER].strData, s, r))
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
        if (item.SubItems[CONFIG_UPLOAD_ROOTPATH])
        {
            std::replace_copy(
                begin(item.SubItems[CONFIG_UPLOAD_ROOTPATH].strData),
                end(item.SubItems[CONFIG_UPLOAD_ROOTPATH].strData),
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
        if (item.SubItems[CONFIG_UPLOAD_USER])
        {
            UserName = item.SubItems[CONFIG_UPLOAD_USER];
        }
        if (item.SubItems[CONFIG_UPLOAD_PASSWORD])
        {
            Password = item.SubItems[CONFIG_UPLOAD_PASSWORD];
        }

        Operation = OutputSpec::UploadOperation::Copy;
        if (item.SubItems[CONFIG_UPLOAD_OPERATION])
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
        if (item.SubItems[CONFIG_UPLOAD_MODE])
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
        if (item.SubItems[CONFIG_UPLOAD_AUTHSCHEME])
        {
            if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Anonymous"sv))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Anonymous;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Basic"sv))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Basic;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"NTLM"))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::NTLM;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Kerberos"))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Kerberos;
            }
            else if (equalCaseInsensitive(item.SubItems[CONFIG_UPLOAD_AUTHSCHEME], L"Negotiate"))
            {
                AuthScheme = OutputSpec::UploadAuthScheme::Negotiate;
            }
        }

        if (item.SubItems[CONFIG_UPLOAD_FILTER_INC])
        {
            boost::split(
                FilterInclude, (const std::wstring&)item.SubItems[CONFIG_UPLOAD_FILTER_INC], boost::is_any_of(L",;"));
        }
        if (item.SubItems[CONFIG_UPLOAD_FILTER_EXC])
        {
            boost::split(
                FilterExclude, (const std::wstring&)item.SubItems[CONFIG_UPLOAD_FILTER_INC], boost::is_any_of(L",;"));
        }
    }
    return S_OK;
}

bool Orc::OutputSpec::Upload::IsFileUploaded(const logger& pLog, const std::wstring& file_name)
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
