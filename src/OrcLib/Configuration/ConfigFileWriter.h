//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ConfigFile.h"

#include "XmlLiteExtension.h"

#pragma managed(push, off)

struct IXmlWriter;

namespace Orc {

class ConfigFileWriter : public ConfigFile
{
public:
    ConfigFileWriter()
        : ConfigFile()
    {
    }
    static HRESULT WriteConfigNode(const CComPtr<IXmlWriter>& pWriter, const ConfigItem& config);

    HRESULT WriteConfig(const std::wstring& strConfigFile, const WCHAR* szDescription, const ConfigItem& config);
    HRESULT
    WriteConfig(const std::shared_ptr<ByteStream>& streamConfig, const WCHAR* szDescription, const ConfigItem& config);

    ~ConfigFileWriter(void);

private:
    std::shared_ptr<XmlLiteExtension> m_xmllite;
};

}  // namespace Orc
#pragma managed(pop)
