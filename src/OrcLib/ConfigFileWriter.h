//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "ConfigFile.h"

#include "XmlLiteExtension.h"

#pragma managed(push, off)

struct IXmlWriter;

namespace Orc {

class ORCLIB_API ConfigFileWriter : public ConfigFile
{
private:
    std::shared_ptr<XmlLiteExtension> m_xmllite;
    HRESULT WriteConfig(const CComPtr<IXmlWriter>& pWriter, const ConfigItem& config);

public:
    ConfigFileWriter(logger pLog)
        : ConfigFile(std::move(pLog))
    {
    }

    HRESULT WriteConfig(const std::wstring& strConfigFile, const WCHAR* szDescription, const ConfigItem& config);
    HRESULT
    WriteConfig(const std::shared_ptr<ByteStream>& streamConfig, const WCHAR* szDescription, const ConfigItem& config);

    ~ConfigFileWriter(void);
};

}  // namespace Orc
#pragma managed(pop)
