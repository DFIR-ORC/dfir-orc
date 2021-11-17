//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "UtilitiesMain.h"

#include <string>
#include <vector>
#include <optional>

#include "OrcCommand.h"
#include "Configuration/ConfigFileReader.h"
#include "ConfigFile_FastFind.h"
#include "Location.h"
#include "TableOutput.h"
#include "StructuredOutput.h"
#include "FileFind.h"
#include "RegFind.h"
#include "ObjectDirectory.h"
#include "FileDirectory.h"
#include "OutputSpec.h"
#include "CryptoHashStream.h"

#pragma managed(push, off)

namespace Orc {
class YaraConfig;
}

namespace Orc::Command::FastFind {

class FileSystemSpec
{

public:
    FileSystemSpec()
        : Locations()
        , Files(
              true,
              CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1
                  | CryptoHashStream::Algorithm::SHA256) {};

    LocationSet Locations;
    FileFind Files;
    boost::logic::tribool bAddShadows = boost::logic::indeterminate;
    std::optional<LocationSet::ShadowFilters> m_shadows;
    std::optional<LocationSet::PathExcludes> m_excludes;
};

class RegistrySpec
{

public:
    RegistrySpec()
        : Locations()
        , Files() {};

    RegistrySpec(RegistrySpec&& other) noexcept = default;

    LocationSet Locations;
    FileFind Files;
    std::vector<RegFind> RegistryFind;
    boost::logic::tribool bAddShadows = boost::logic::indeterminate;
    std::optional<LocationSet::ShadowFilters> m_shadows;
    std::optional<LocationSet::PathExcludes> m_excludes;
};

class ObjectSpec
{
public:
    typedef enum _MatchType
    {
        Invalid = 0,
        Exact,
        Match,
        Regex
    } MatchType;

    class ObjectItem
    {
    public:
        ObjectDirectory::ObjectType ObjType;

        MatchType name_typeofMatch;
        std::wstring strName;
        std::unique_ptr<std::wregex> name_regexp;

        MatchType path_typeofMatch;
        std::wstring strPath;
        std::unique_ptr<std::wregex> path_regexp;

        ObjectItem()
        {
            name_typeofMatch = MatchType::Invalid;
            path_typeofMatch = MatchType::Invalid;
            ObjType = ObjectDirectory::ObjectType::Invalid;
        }

        ObjectItem(ObjectItem&& other)
        {
            std::swap(ObjType, other.ObjType);
            std::swap(name_typeofMatch, other.name_typeofMatch);
            std::swap(strName, other.strName);
            std::swap(name_regexp, other.name_regexp);
            std::swap(path_typeofMatch, other.path_typeofMatch);
            std::swap(strPath, other.strPath);
            std::swap(path_regexp, other.path_regexp);
        }
        ObjectItem(const ObjectItem& other)
        {
            ObjType = other.ObjType;

            name_typeofMatch = other.name_typeofMatch;
            strName = other.strName;
            if (name_typeofMatch == MatchType::Regex)
                name_regexp = std::make_unique<std::wregex>(strName, std::regex_constants::icase);

            path_typeofMatch = other.path_typeofMatch;
            strPath = other.strPath;
            if (path_typeofMatch == MatchType::Regex && !strPath.empty())
                path_regexp = std::make_unique<std::wregex>(strPath, std::regex_constants::icase);
        }
        const std::wstring Description() const
        {
            std::wstring descr;
            switch (ObjType)
            {
                case ObjectDirectory::ObjectType::ALPCPort:
                    descr = L"ALPCPort";
                    break;
                case ObjectDirectory::ObjectType::Callback:
                    descr = L"Callback";
                    break;
                case ObjectDirectory::ObjectType::Device:
                    descr = L"Device";
                    break;
                case ObjectDirectory::ObjectType::Directory:
                    descr = L"Directory";
                    break;
                case ObjectDirectory::ObjectType::Driver:
                    descr = L"Driver";
                    break;
                case ObjectDirectory::ObjectType::Event:
                    descr = L"Event";
                    break;
                case ObjectDirectory::ObjectType::File:
                    descr = L"File";
                    break;
                case ObjectDirectory::ObjectType::FilterConnectionPort:
                    descr = L"FilterConnectionPort";
                    break;
                case ObjectDirectory::ObjectType::Job:
                    descr = L"Job";
                    break;
                case ObjectDirectory::ObjectType::Key:
                    descr = L"Key";
                    break;
                case ObjectDirectory::ObjectType::KeyedEvent:
                    descr = L"KeyedEvent";
                    break;
                case ObjectDirectory::ObjectType::Mutant:
                    descr = L"Mutant";
                    break;
                case ObjectDirectory::ObjectType::Section:
                    descr = L"Section";
                    break;
                case ObjectDirectory::ObjectType::Semaphore:
                    descr = L"Semaphore";
                    break;
                case ObjectDirectory::ObjectType::Session:
                    descr = L"Session";
                    break;
                case ObjectDirectory::ObjectType::SymbolicLink:
                    descr = L"SymbolicLink";
                    break;
                case ObjectDirectory::ObjectType::Type:
                    descr = L"Type";
                    break;
                case ObjectDirectory::ObjectType::WindowStations:
                    descr = L"WindowStations";
                    break;
                default:
                    descr = L"<unknown>";
                    break;
            }
            if (!strName.empty() || name_regexp)
            {
                switch (name_typeofMatch)
                {
                    case MatchType::Exact:
                        descr += L"'name is ";
                        break;
                    case MatchType::Match:
                        descr += L"'name matches ";
                        break;
                    case MatchType::Regex:
                        descr += L"'name matches regex ";
                        // descr += str
                        break;
                }
                descr += strName;
            }
            if (!strPath.empty() || path_regexp)
            {
                switch (name_typeofMatch)
                {
                    case MatchType::Exact:
                        descr += L"'path is ";
                        break;
                    case MatchType::Match:
                        descr += L"'path matches ";
                        break;
                    case MatchType::Regex:
                        descr += L"'path matches regex ";
                        break;
                }
                descr += strPath;
            }
            return descr;
        }
    };

    std::vector<ObjectItem> Items;
};

const wchar_t kToolName[] = L"FastFind";

class ORCUTILS_API Main : public UtilitiesMain
{
public:
    class ORCUTILS_API Configuration : UtilitiesMain::Configuration
    {
        friend class Main;

    public:
        std::wstring strNames;
        std::wstring strVersion;

        OutputSpec outFileSystem;
        OutputSpec outRegsitry;
        OutputSpec outObject;

        OutputSpec outStructured;

        bool bAll = false;
        WCHAR Volume = 0;

        bool bSkipDeleted = true;

        FileSystemSpec FileSystem;
        RegistrySpec Registry;
        ObjectSpec Object;

        std::wstring YaraSource;
        std::unique_ptr<YaraConfig> Yara;

        Configuration()
            : FileSystem()
            , Registry()
        {
        }
    };

private:
    Configuration config;

    FILETIME CollectionDate;
    std::wstring ComputerName;

    std::shared_ptr<ITableWriter> pFileSystemTableOutput;
    std::shared_ptr<ITableWriter> pRegistryTableOutput;
    std::shared_ptr<ITableWriter> pObjectTableOutput;
    std::shared_ptr<IStructuredWriter> pStructuredOutput;

    std::vector<std::wstring> ObjectDirs;
    std::vector<std::wstring> FileDirs;

    HRESULT LogObjectMatch(
        const ObjectSpec::ObjectItem& spec,
        const ObjectDirectory::ObjectInstance& obj,
        LPCWSTR szElement = nullptr);
    HRESULT LogObjectMatch(
        const ObjectSpec::ObjectItem& spec,
        const FileDirectory::FileInstance& file,
        LPCWSTR szElement = nullptr);

    HRESULT RunFileSystem();
    HRESULT RunRegistry();
    HRESULT RunObject();

    HRESULT RegFlushKeys();

public:
    static LPCWSTR ToolName() { return kToolName; }
    static LPCWSTR ToolDescription() { return L"IOC Finder"; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return L"res:#FASTFIND_CONFIG"; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder() { return nullptr; }
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return nullptr; }

    static LPCWSTR DefaultSchema() { return L"res:#FASTFIND_SQLSCHEMA"; }

    Main()
        : UtilitiesMain()
        , config() {

          };

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem);
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem)
    {
        return S_OK;
    };  // No Local Configuration support

    HRESULT GetConfigurationFromArgcArgv(int argc, LPCWSTR argv[]);

    HRESULT GetSchemaFromConfig(const ConfigItem& schemaitem);

    HRESULT CheckConfiguration();

    void PrintUsage();
    void PrintParameters();

    HRESULT Run();

    void PrintFooter();
};
}  // namespace Orc::Command::FastFind
