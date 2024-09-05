//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "TaskUtils.h"

#include <memory>

#include <agents.h>

#include "StreamMessage.h"

#include "MemoryAgent.h"

class ResourceAgent : public MemoryAgent
{
private:
    HMODULE m_hModule;
    HGLOBAL m_hFileResource;
    HRSRC m_hResource;

protected:
    HRESULT Close();

public:
    ResourceAgent(
        LogFileWriter* pW,
        StreamMessage::ISource& source,
        StreamMessage::ITarget& target,
        Concurrency::ITarget<CBinaryBuffer>* result = nullptr)
        : MemoryAgent(pW, source, target, result)
    {
        m_hFileResource = NULL;
        m_hModule = NULL;
        m_hResource = NULL;
    }

    HRESULT IsOpen()
    {
        if (m_hResource)
            return S_OK;
        else
            return S_FALSE;
    };

    // HRESULT OpenForReadOnly(__in const std::wstring& strModuleName, __in WORD resourceID);
    // HRESULT OpenForReadOnly(__in const std::wstring& strModuleName, __in const std::wstring& strName);

    HRESULT OpenForReadOnly(const HMODULE hModule, const HRSRC hRes);

    ~ResourceAgent(void);
};
