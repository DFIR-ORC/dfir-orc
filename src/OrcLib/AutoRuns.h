//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <vector>

#include "ConfigFileReader.h"

#pragma managed(push, off)

namespace Orc {

struct AutoRunItem
{
    const WCHAR* Location;
    const WCHAR* Name;
    const WCHAR* Enabled;
    const WCHAR* LaunchString;
    const WCHAR* Description;
    const WCHAR* Company;
    const WCHAR* Signer;
    const WCHAR* Version;
    const WCHAR* ImagePath;
    const WCHAR* MD5;
    const WCHAR* SHA1;
    const WCHAR* SHA256;
    const WCHAR* PESHA1;
    const WCHAR* PESHA256;
    const WCHAR* ImpHash;
    bool IsVerified;
};

using AutoRunsEnumItemCallback = void(const AutoRunItem& item, LPVOID pContext);

using AutoRunsVector = std::vector<AutoRunItem>;

class CBinaryBuffer;

class ORCLIB_API AutoRuns
{

    ConfigItem m_AutoRuns;

    ConfigFileReader m_Reader;

    std::shared_ptr<ByteStream> m_xmlDataStream;

public:
    AutoRuns();

    HRESULT LoadAutoRunsXml(const WCHAR* szXmlFile);
    HRESULT LoadAutoRunsXml(const CBinaryBuffer& szXmlData);

    std::shared_ptr<ByteStream> GetXmlDataStream() const { return m_xmlDataStream; }

    HRESULT PrintAutoRuns();

    HRESULT EnumItems(AutoRunsEnumItemCallback pCallBack, LPVOID pContext);
    HRESULT GetAutoRuns(AutoRunsVector& autoruns);
    HRESULT GetAutoRunFiles(std::vector<std::wstring>& Modules);

    ~AutoRuns(void);
};

}  // namespace Orc
#pragma managed(pop)
