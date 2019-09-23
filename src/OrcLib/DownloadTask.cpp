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

#include "ConfigItem.h"

#include <regex>
#include <sstream>

using namespace std;

using namespace Orc;

std::shared_ptr<DownloadTask> DownloadTask::GetTaskFromConfig(const logger& pLog, const ConfigItem& item)
{
    if (item.strName.compare(L"download") != 0)
        return nullptr;

    std::shared_ptr<DownloadTask> retval;

    std::wstring strJobName;

    if (item[CONFIG_DOWNLOAD_JOBNAME].Status & ConfigItem::PRESENT)
    {
        strJobName = item[CONFIG_DOWNLOAD_JOBNAME].strData;
    }

    if (item[CONFIG_DOWNLOAD_METHOD].Status & ConfigItem::PRESENT)
    {
        if (equalCaseInsensitive(item[CONFIG_DOWNLOAD_METHOD].strData, L"bits"))
        {
            retval = std::make_shared<BITSDownloadTask>(pLog, strJobName.c_str());
        }
        else if (equalCaseInsensitive(item[CONFIG_DOWNLOAD_METHOD].strData, L"filecopy"))
        {
            retval = std::make_shared<FileCopyDownloadTask>(pLog, strJobName.c_str());
        }
        else
        {
            log::Error(
                pLog,
                E_INVALIDARG,
                L"Invalid download method \"%s\"\r\n",
                item[CONFIG_DOWNLOAD_METHOD].strData.c_str());
            return nullptr;
        }
    }

    if (retval == nullptr)
        return nullptr;

    if (item[CONFIG_DOWNLOAD_SERVER].Status & ConfigItem::PRESENT)
    {
        static std::wregex r(L"(http|https|file):(//|\\\\)(.*)", std::regex_constants::icase);

        std::wsmatch s;

        if (std::regex_match(item.SubItems[CONFIG_DOWNLOAD_SERVER].strData, s, r))
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

    if (item.SubItems[CONFIG_DOWNLOAD_ROOTPATH].Status == ConfigItem::PRESENT)
    {
        std::replace_copy(
            begin(item.SubItems[CONFIG_DOWNLOAD_ROOTPATH].strData),
            end(item.SubItems[CONFIG_DOWNLOAD_ROOTPATH].strData),
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

    if (item[CONFIG_DOWNLOAD_COMMAND].Status & ConfigItem::PRESENT)
    {
        retval->m_strCmd = item[CONFIG_DOWNLOAD_COMMAND].strData;
    }

    if (item[CONFIG_DOWNLOAD_FILE].Status & ConfigItem::PRESENT)
    {
        for (const auto& file : item[CONFIG_DOWNLOAD_FILE].NodeList)
        {
            HRESULT hr = E_FAIL;
            std::wstring strName, strLocalName;
            bool bDelete = false;

            strName = file[CONFIG_DOWNLOAD_FILE_NAME].strData;
            if (FAILED(hr = GetOutputFile(file[CONFIG_DOWNLOAD_FILE_LOCALPATH].strData.c_str(), strLocalName, true)))
            {
                log::Error(
                    pLog,
                    hr,
                    L"Error while computing local file name for download task (%s)\r\n",
                    file[CONFIG_DOWNLOAD_FILE_LOCALPATH].strData);
            }

            if (file[CONFIG_DOWNLOAD_FILE_DELETE].Status & ConfigItem::PRESENT)
            {
                if (!equalCaseInsensitive(file[CONFIG_DOWNLOAD_FILE_DELETE].strData, L"no"))
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
