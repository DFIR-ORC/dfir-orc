//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "TaskUtils.h"

#include <fdi.h>

#include <agents.h>
#include <string>
#include <memory>
#include <regex>

#include "StreamMessage.h"
#include "StreamAgent.h"

class TASKUTILS_API ConcurrentCabDecompress
{
public:
    class ExtractedItem
    {
    public:
        std::wstring Matched;
        std::wstring Name;
        std::shared_ptr<StreamAgent> ResultAgent;

        ExtractedItem(
            const std::wstring& aMatched,
            const std::wstring& aName,
            const std::shared_ptr<StreamAgent>& anAgent)
            : Matched(aMatched)
            , Name(aName)
            , ResultAgent(anAgent)
        {
        }
    };

protected:
    StreamMessage::UnboundedMessageBuffer m_requestbuffer;
    StreamMessage::UnboundedMessageBuffer m_resultbuffer;

    DWORD m_dwXOR;

    std::wstring m_strExtractRootDir;

    std::function<std::shared_ptr<StreamAgent>(
        LogFileWriter* pW,
        std::wstring aName,
        StreamMessage::ISource& source,
        StreamMessage::ITarget& target)>
        m_MakeAgent;

    LogFileWriter* m_pW;
    HRESULT GetHRESULTFromERF(__in ERF& erf);

    static FNALLOC(static_FDIAlloc);
    static FNFREE(static_FDIFree);
    static FNOPEN(static_FDIOpen);
    static FNREAD(static_FDIRead);
    static FNWRITE(static_FDIWrite);
    static FNCLOSE(static_FDIClose);
    static FNSEEK(static_FDISeek);
    static FNFDINOTIFY(static_FDINotify);
    FNFDINOTIFY(FDINotify);

    std::vector<std::wstring> m_ListToExtract;
    std::vector<ExtractedItem> m_ListExtracted;

public:
    ConcurrentCabDecompress(LogFileWriter* pW);
    ~ConcurrentCabDecompress();

    bool IsRegularArchiveFile(const WCHAR* szCabFileName);

    HRESULT ExtractFromCab(
        __in PCWSTR pwzCabFilePath,
        std::function<std::shared_ptr<StreamAgent>(
            LogFileWriter* pW,
            std::wstring aName,
            StreamMessage::ISource& source,
            StreamMessage::ITarget& target)> MakeAgent,
        __in const std::vector<std::wstring>& ListToExtract,
        __inout std::vector<ExtractedItem>& ListExtracted,
        __in_opt const DWORD dwXOR = 0);

    HRESULT ExtractFromCab(
        __in PCWSTR pwzCabFilePath,
        std::function<std::shared_ptr<StreamAgent>(
            LogFileWriter* pW,
            std::wstring aName,
            StreamMessage::ISource& source,
            StreamMessage::ITarget& target)> MakeAgent,
        __inout std::vector<ExtractedItem>& ListExtracted,
        __in_opt const DWORD dwXOR = 0);

    HRESULT ExtractFromCab(
        __in PCWSTR pwzCabFilePath,
        __in PCWSTR pwzExtractRootDir,
        __inout std::vector<ExtractedItem>& ListExtracted,
        __in_opt const DWORD dwXOR = 0);

    HRESULT ExtractFromCab(
        __in PCWSTR pwzCabFilePath,
        __in PCWSTR pwzExtractRootDir,
        __in const std::vector<std::wstring>& ListToExtract,
        __inout std::vector<ExtractedItem>& ListExtracted,
        __in_opt const DWORD dwXOR = 0);
};
