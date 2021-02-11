//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "DownloadTask.h"

#include "CaseInsensitive.h"
#include "BITSDownloadTask.h"
#include "FileCopyDownloadTask.h"

#include "ParameterCheck.h"

#include "Configuration/ConfigItem.h"

#include <regex>
#include <sstream>

using namespace std;

using namespace Orc;

std::shared_ptr<DownloadTask> DownloadTask::GetTaskFromConfig(const ConfigItem& item)
{
    if (item.strName.compare(L"download") != 0)
        return nullptr;

    std::shared_ptr<DownloadTask> retval;

    std::wstring strJobName;

    if (item[CONFIG_DOWNLOAD_JOBNAME])
    {
        strJobName = item[CONFIG_DOWNLOAD_JOBNAME];
    }

    if (item[CONFIG_DOWNLOAD_METHOD])
    {
        if (equalCaseInsensitive(item[CONFIG_DOWNLOAD_METHOD], L"bits"))
        {
            retval = std::make_shared<BITSDownloadTask>(strJobName.c_str());
        }
        else if (equalCaseInsensitive(item[CONFIG_DOWNLOAD_METHOD], L"filecopy"))
        {
            retval = std::make_shared<FileCopyDownloadTask>(strJobName.c_str());
        }
        else
        {
            Log::Error(L"Invalid download method '{}'", item[CONFIG_DOWNLOAD_METHOD].c_str());
            return nullptr;
        }
    }

    if (retval == nullptr)
        return nullptr;

    if (item[CONFIG_DOWNLOAD_SERVER])
    {
        static std::wregex r(L"(http|https|file):(//|\\\\)(.*)", std::regex_constants::icase);

        std::wsmatch s;

        if (std::regex_match((const std::wstring&)item.SubItems[CONFIG_DOWNLOAD_SERVER], s, r))
        {
            if (!_wcsicmp(s[1].str().c_str(), L"http"))
            {
                retval->m_Protocol = BITSProtocol::BITS_HTTP;
            }
            else if (!_wcsicmp(s[1].str().c_str(), L"https"))
            {
                retval->m_Protocol = BITSProtocol::BITS_HTTPS;
            }
            else if (!_wcsicmp(s[1].str().c_str(), L"file"))
            {
                retval->m_Protocol = BITSProtocol::BITS_SMB;
            }
            else
            {
                return nullptr;
            }
            retval->m_strServer = s[3].str();
        }
        else
        {
            return nullptr;
        }
    }

    if (item.SubItems[CONFIG_DOWNLOAD_ROOTPATH])
    {
        auto rootpath = (std::wstring_view)item.SubItems[CONFIG_DOWNLOAD_ROOTPATH];
        std::replace_copy(
            begin(rootpath),
            end(rootpath),
            back_inserter(retval->m_strPath),
            retval->m_Protocol == BITSProtocol::BITS_SMB ? L'/' : L'\\',
            retval->m_Protocol == BITSProtocol::BITS_SMB ? L'\\' : L'/');
        if (retval->m_strPath.empty())
        {
            retval->m_strPath = retval->m_Protocol == BITSProtocol::BITS_SMB ? L"\\" : L"/";
        }
        else if (retval->m_strPath.front() != L'\\' || retval->m_strPath.front() != L'/')
        {
            retval->m_strPath.insert(0, 1, retval->m_Protocol == BITSProtocol::BITS_SMB ? L'\\' : L'/');
        }
    }

    if (item[CONFIG_DOWNLOAD_COMMAND])
    {
        retval->m_strCmd = item[CONFIG_DOWNLOAD_COMMAND];
    }

    if (item[CONFIG_DOWNLOAD_FILE])
    {
        for (const auto& file : item[CONFIG_DOWNLOAD_FILE].NodeList)
        {
            HRESULT hr = E_FAIL;
            std::wstring strName, strLocalName;
            bool bDelete = false;

            strName = file[CONFIG_DOWNLOAD_FILE_NAME];
            if (FAILED(hr = GetOutputFile(file[CONFIG_DOWNLOAD_FILE_LOCALPATH].c_str(), strLocalName, true)))
            {
                Log::Error(
                    L"Error while computing local file name for download task: '{}' [{}]",
                    file[CONFIG_DOWNLOAD_FILE_LOCALPATH].c_str(),
                    SystemError(hr));
            }

            if (file[CONFIG_DOWNLOAD_FILE_DELETE])
            {
                if (!equalCaseInsensitive(file[CONFIG_DOWNLOAD_FILE_DELETE], L"no"))
                {
                    bDelete = true;
                }
            }

            retval->AddFile(strName, strLocalName, bDelete);
        }
    }

    return retval;
}

HRESULT DownloadTask::AddFile(const std::wstring& strRemotePath, const std::wstring& strLocalPath, const bool bDelete)
{
    m_files.emplace_back(strRemotePath, strLocalPath, bDelete);
    return S_OK;
}
