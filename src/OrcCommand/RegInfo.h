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

// Characters banned from output (break csv file...)
// end the table with a null character
constexpr WCHAR OutputBadChars[] = {'\n', '"', '\0'};

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

    static HRESULT BindColumns(
        const logger& pLog,
        RegInfoType columns,
        const std::vector<TableOutput::Column>& sqlcolumns,
        const std::shared_ptr<TableOutput::IWriter>& pWriter);

public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        Configuration(const logger& pLog)
            : regFindConfig(pLog)
            , m_HiveQuery(pLog)
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

    FILETIME CollectionDate;
    std::wstring ComputerName;

    RegInfoType GetRegInfoType(LPCWSTR Params);

    auto GetRegInfoWriter(const OutputSpec& outFile) { return TableOutput::GetWriter(_L_, outFile); }
    std::shared_ptr<TableOutput::IWriter> GetRegInfoWriter(const OutputSpec& outFile, const std::wstring& strSuffix)
    {
        WCHAR szOutputFile[MAX_PATH];
        StringCchPrintf(szOutputFile, MAX_PATH, L"RegInfo_%s.csv", strSuffix.c_str());

        return TableOutput::GetWriter(_L_, szOutputFile, outFile);
    }

    HRESULT WriteCompName(ITableOutput& output);
    HRESULT WriteSearchDescription(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& Term);
    HRESULT WriteKeyInformation(ITableOutput& output, RegFind::Match::KeyNameMatch& Match);
    HRESULT WriteValueInformation(
        const logger& pLog,
        ITableOutput& output,
        RegFind::Match::ValueNameMatch& Match,
        std::wstring& FileNamePrefix,
        size_t CvsMaxsize);
    HRESULT WriteTermName(ITableOutput& output, const std::shared_ptr<RegFind::SearchTerm>& Term);

public:
    static LPCWSTR ToolName() { return L"RegInfo"; }
    static LPCWSTR ToolDescription() { return L"RegInfo - Registry related data enumeration"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#REGINFO_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#REGINFO_SQLSCHEMA"; }

    Main(logger pLog)
        : UtilitiesMain(pLog)
        , config(pLog) {};

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
