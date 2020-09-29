//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "BinaryBuffer.h"
#include "ConfigFile.h"

#include "XmlLiteExtension.h"

#include <atlbase.h>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ConfigFileReader : public ConfigFile
{
private:
    std::shared_ptr<XmlLiteExtension> m_xmllite;
    bool m_bLogComments;

    HRESULT NavigateConfigAttributes(const CComPtr<IXmlReader>& pReader, ConfigItem& config);
    HRESULT NavigateConfigNodeList(const CComPtr<IXmlReader>& pReader, ConfigItem& config);
    HRESULT NavigateConfigNode(const CComPtr<IXmlReader>& pReader, ConfigItem& config);

public:
    ConfigFileReader(bool bLogComment = true);

    HRESULT ReadConfig(const WCHAR* pCfgFile, ConfigItem& config, LPCWSTR szEncodingHint = NULL);
    HRESULT
    ReadConfig(const std::shared_ptr<ByteStream>& streamConfig, ConfigItem& config, LPCWSTR szEncodingHint = NULL);

    ~ConfigFileReader(void);
};

}  // namespace Orc

#pragma managed(pop)
