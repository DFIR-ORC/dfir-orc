//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//

#include "Location.h"

#include "FileFind.h"

#include "UtilitiesMain.h"

#include "TableOutputWriter.h"

#include "OrcCommand.h"

#include "RegFindConfig.h"
#include "RegFind.h"
#include "HiveQuery.h"

#include <utility>

#pragma managed(push, off)

// max size of data to dump into csv
constexpr auto REGINFO_CSV_DEFALUT_LIMIT = (512);

namespace Orc {

namespace Command::RegInfo {

class ORCUTILS_API Main : public UtilitiesMain
{

private:
    typedef enum _RegInfoType
        : DWORDLONG
    {

        REGINFO_NONE = 0,

        REGINFO_COMPUTERNAME = (1LL),

        REGINFO_TERMNAME = (1LL << 1),
        REGINFO_TERMDESCRIPTION = (1LL << 2),

        REGINFO_LASTMODDATE = (1LL << 3),

        REGINFO_KEYNAME = (1LL << 4),
        REGINFO_KEYTREE = (1LL << 5),

        REGINFO_VALUENAME = (1LL << 6),
        REGINFO_VALUETYPE = (1LL << 7),
        REGINFO_VALUESIZE = (1LL << 8),
        REGINFO_VALUEFLAG = (1LL << 9),
        REGINFO_VALUE = (1LL << 10),
        REGINFO_VALUEDUMPFILE = (1LL << 11),

        REGINFO_TIMELINE = (REGINFO_LASTMODDATE | REGINFO_KEYNAME),
        REGINFO_VALUEINFORMATION =
            (REGINFO_VALUENAME | REGINFO_VALUETYPE | REGINFO_VALUE | REGINFO_VALUEFLAG | REGINFO_VALUESIZE
             | REGINFO_VALUEDUMPFILE),

        REGINFO_ALL = (unsigned long long)-1

    } RegInfoType;

    typedef enum _RegInfoValueFlag
        : DWORDLONG
    {
        VALUE_NOTINHIVE,
        VALUE_DUMPFILE,
        VALUE_PRESENT,
        VALUE_HASBADCHAR
    } RegInfoValueFlag;

    class RegInfoDescription
    {
    public:
        RegInfoType Type;
        LPCWSTR ColumnName;
        LPCWSTR Description;
    };

    static RegInfoDescription _InfoDescription[];
    static RegInfoDescription _AliasDescription[];

public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration()
            : regFindConfig()
            , m_HiveQuery()
        {
            Information = static_cast<RegInfoType>(
                REGINFO_LASTMODDATE | REGINFO_TERMNAME | REGINFO_TERMDESCRIPTION | REGINFO_KEYNAME | REGINFO_KEYTREE
                | REGINFO_VALUENAME | REGINFO_VALUETYPE);
        };

        RegFindConfig regFindConfig;
        HiveQuery m_HiveQuery;
        RegInfoType Information;

        OutputSpec Output;
        size_t CsvValueLengthLimit;
        std::wstring strComputerName;
    };

private:
    Configuration config;

    FILETIME m_collectionDate;

    RegInfoType GetRegInfoType(LPCWSTR Params);

    auto GetRegInfoWriter(const OutputSpec& outFile) { return TableOutput::GetWriter(outFile); }
    std::shared_ptr<TableOutput::IWriter> GetRegInfoWriter(const OutputSpec& outFile, const std::wstring& strSuffix)
    {
        WCHAR szOutputFile[MAX_PATH];
        StringCchPrintf(szOutputFile, MAX_PATH, L"RegInfo_%s.csv", strSuffix.c_str());

        return TableOutput::GetWriter(szOutputFile, outFile);
    }

    HRESULT WriteComputerName(ITableOutput& output);
    HRESULT WriteSearchDescription(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& Term);
    HRESULT WriteKeyInformation(ITableOutput& output, const RegFind::Match::KeyNameMatch& Match);
    HRESULT WriteValueInformation(
        ITableOutput& output,
        const RegFind::Match::ValueNameMatch& Match,
        const std::wstring& FileNamePrefix,
        size_t CvsMaxsize);
    HRESULT WriteTermName(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& Term);

public:
    static LPCWSTR ToolName() { return L"RegInfo"; }
    static LPCWSTR ToolDescription() { return L"Registry related data enumeration"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#REGINFO_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#REGINFO_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
        , config() {};

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetSchemaFromConfig(const ConfigItem& configitem);

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();
};
}  // namespace Command::RegInfo
}  // namespace Orc

#pragma managed(pop)
