//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "Location.h"

#include "ConfigItem.h"

#include "ParameterCheck.h"

#include <vector>
#include <string>

#include <winternl.h>

#pragma managed(push, off)

struct IXmlReader;

namespace Orc {

class ConfigFileReader;

class ORCLIB_API ConfigFile
{
private:
protected:
    HANDLE m_hHeap;
    logger _L_;

    HRESULT TrimString(struct _UNICODE_STRING* pString);
    HRESULT TrimString(std::wstring& string);

    HRESULT SetData(ConfigItem& config, const UNICODE_STRING& String);
    HRESULT SetData(ConfigItem& config, const std::wstring& String);

public:
    ConfigFile(logger pLog);

    static HRESULT LookupAndReadConfiguration(
        const logger& pLog,
        int argc,
        LPCWSTR argv[],
        ConfigFileReader& r,
        LPCWSTR szConfigOpt,
        LPCWSTR szDefaultConfigResource,
        LPCWSTR szReferenceConfigResource,
        LPCWSTR szCompanionExtension,
        ConfigItem& Config);
    static HRESULT ConfigureLogging(const ConfigItem& item, const logger& pLog);

    static HRESULT CheckConfig(const ConfigItem& config, const logger& pLog = nullptr);
    static HRESULT PrintConfig(const ConfigItem& config, DWORD dwIndent = 0, const logger& pLog = nullptr);
    static HRESULT DestroyConfig(ConfigItem& config, bool bDeleteSubItems = false);

    HRESULT SetOutputSpec(ConfigItem& item, const OutputSpec& outputSpec);

    HRESULT
    GetOutputDir(const ConfigItem& item, std::wstring& outputDir, OutputSpec::Encoding& anEncoding);
    HRESULT SetOutputDir(
        ConfigItem& item,
        const std::wstring& outputDir,
        OutputSpec::Encoding anEncoding = OutputSpec::Encoding::UTF8);

    HRESULT GetOutputFile(const ConfigItem& item, std::wstring& outputFile, OutputSpec::Encoding& anEncoding);
    HRESULT SetOutputFile(
        ConfigItem& item,
        const std::wstring& outputFile,
        OutputSpec::Encoding anEncoding = OutputSpec::Encoding::UTF8);

    HRESULT GetInputFile(const ConfigItem& item, std::wstring& inputFile);
    HRESULT SetInputFile(ConfigItem& item, const std::wstring& inputFile);

    HRESULT GetSQLConnectionString(const ConfigItem& item, std::wstring& strConnectionString);
    HRESULT GetSQLTableName(const ConfigItem& item, const LPWSTR szTableKey, std::wstring& strTableName);

    HRESULT AddSampleName(ConfigItem& item, const std::wstring& sampleName);
    HRESULT AddSamplePath(ConfigItem& item, const std::wstring& samplePath);

    HRESULT GetSampleNames(const ConfigItem& item, std::vector<std::wstring>& sampleNames);

    HRESULT SetLocations(ConfigItem& item, const std::vector<std::shared_ptr<Location>>& locs);

    ~ConfigFile()
    {
        if (m_hHeap)
            HeapDestroy(m_hHeap);
        m_hHeap = NULL;
    };
};

}  // namespace Orc

#pragma managed(pop)
