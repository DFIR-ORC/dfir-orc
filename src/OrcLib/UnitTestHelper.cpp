//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "LogFileWriter.h"

#include "SystemDetails.h"

#ifdef ORC_BUILD_CHARACORE
#    include "ChakraExtension.h"
#endif

#include "YaraStaticExtension.h"

#include <boost/scope_exit.hpp>

#include <filesystem>
// Headers for CppUnitTest
#include "CppUnitTest.h"

#include "UnittestHelper.h"

using namespace std;
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace fs = std::filesystem;

using namespace Orc;
using namespace Orc::Test;

void UnitTestHelper::InitLogFileWriter(const logger& pLog)
{
    pLog->SetConsoleLog(false);
    pLog->SetDebugLog(false);
    pLog->SetVerboseLog(false);

    pLog->SetLogCallback([this](const WCHAR* szMsg, DWORD dwSize, DWORD& dwWritten) -> HRESULT {
        auto write_msg = [](std::wstring& acc, const WCHAR* szMsg) {
            if (acc.empty())
                Logger::WriteMessage(szMsg);
            else
            {
                acc.append(szMsg);
                Logger::WriteMessage(acc.c_str());
                acc.resize(0);
            }
        };

        if (dwSize >= 2 && szMsg[dwSize - 1] == L'\n' && szMsg[dwSize - 2] == L'\r')
        {
            const_cast<WCHAR*>(szMsg)[dwSize - 2] = L'\0';
            write_msg(m_strAccumulator, szMsg);
            const_cast<WCHAR*>(szMsg)[dwSize - 2] = L'\r';
        }
        else if (dwSize >= 2 && szMsg[dwSize - 1] == L'\r' && szMsg[dwSize - 2] == L'\n')
        {
            const_cast<WCHAR*>(szMsg)[dwSize - 2] = L'\0';
            write_msg(m_strAccumulator, szMsg);
            const_cast<WCHAR*>(szMsg)[dwSize - 2] = L'\n';
        }
        else if (dwSize >= 1 && szMsg[dwSize - 1] == L'\r')
        {
            const_cast<WCHAR*>(szMsg)[dwSize - 1] = L'\0';
            write_msg(m_strAccumulator, szMsg);
            const_cast<WCHAR*>(szMsg)[dwSize - 1] = L'\r';
        }
        else if (dwSize >= 1 && szMsg[dwSize - 1] == L'\n')
        {
            const_cast<WCHAR*>(szMsg)[dwSize - 1] = L'\0';
            write_msg(m_strAccumulator, szMsg);
            const_cast<WCHAR*>(szMsg)[dwSize - 2] = L'\n';
        }
        else
        {
            m_strAccumulator.append(szMsg);
        }
        return S_OK;
    });
}

void UnitTestHelper::FinalizeLogFileWriter(const logger& pLog)
{
    pLog->SetLogCallback(nullptr);
    pLog->SetConsoleLog(false);
    pLog->SetDebugLog(false);
}

std::wstring UnitTestHelper::GetDirectoryName(const std::wstring& apath)
{
    fs::path spath = apath;

    if (spath.has_parent_path())
        return spath.parent_path().wstring();
    else
        return L"";
}

HRESULT UnitTestHelper::ExtractArchive(
    const logger& pLog,
    ArchiveFormat format,
    ArchiveExtract::MakeArchiveStream makeArchiveStream,
    const ArchiveExtract::ItemShouldBeExtractedCallback pShouldBeExtracted,
    ArchiveExtract::MakeOutputStream MakeWriteAbleStream,
    Archive::ArchiveCallback archiveCallback)
{
    HRESULT hr = E_FAIL;

    std::shared_ptr<ArchiveExtract> extractor = ArchiveExtract::MakeExtractor(format, pLog, false);
    if (!extractor)
    {
        log::Error(pLog, E_FAIL, L"Failed to create extractor\r\n");
        return E_FAIL;
    }

    extractor->SetCallback(archiveCallback);

    if (FAILED(extractor->Extract(makeArchiveStream, pShouldBeExtracted, MakeWriteAbleStream)))
    {
        log::Error(pLog, hr, L"Failed to extract archive\r\n");
        return hr;
    }

    return S_OK;
}
