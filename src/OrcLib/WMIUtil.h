//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <atlcomcli.h>
#include <comdef.h>
#include <Wbemidl.h>

#pragma managed(push, off)

namespace Orc {

class LogFileWriter;

class ORCLIB_API WMIUtil
{

private:
    logger _L_;
    CComPtr<IWbemLocator> m_pLocator;
    CComPtr<IWbemServices> m_pServices;

public:
    WMIUtil(logger pLog)
        : _L_(std::move(pLog)) {};

    HRESULT Initialize();

    HRESULT WMICreateProcess(
        LPCWSTR szCurrentDirectory,
        LPCWSTR szCommandLine,
        DWORD dwCreationFlags,
        DWORD dwPriority,
        DWORD& dwStatus);

    HRESULT WMIEnumPhysicalMedia(std::vector<std::wstring>& physicaldrives);

    ~WMIUtil();
};

}  // namespace Orc

#pragma managed(pop)
