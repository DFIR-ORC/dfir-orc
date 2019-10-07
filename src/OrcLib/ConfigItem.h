//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <vector>
#include <string>

#include "ParameterCheck.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API ConfigItem
{
public:  // TYPES
    enum ConfigItemType : char
    {
        NODE = 0,
        NODELIST = 1,
        ATTRIBUTE = 1 << 1
    };

    enum ConfigItemFlags : char
    {
        OPTION = 0,
        MANDATORY = 1,
        TRIMMED = 1 << 1,
        ALL = 0x0F
    };

    enum ConfigItemStatus : char
    {
        MISSING = 0,
        PRESENT = 1
    };

    typedef HRESULT (*AdderFunction)(ConfigItem& parent, DWORD dwIndex);
    typedef HRESULT (*NamedAdderFunction)(ConfigItem& parent, DWORD dwIndex, LPCWSTR szName);
    typedef HRESULT (*InitFunction)(ConfigItem& item);

public:  // PROPERTIES
    ConfigItemType Type;
    ConfigItemFlags Flags;
    ConfigItemStatus Status;
    std::wstring strName;
    DWORD dwIndex;
    DWORD dwOrderIndex;
    std::vector<ConfigItem> SubItems;
    std::vector<ConfigItem> NodeList;

    std::wstring strData;

public:  // METHODS
    ConfigItem()
    {
        Type = ConfigItem::NODE;
        Flags = ConfigItem::OPTION;
        Status = ConfigItem::MISSING;
        dwIndex = 0;
        dwOrderIndex = 0;
    }

    ConfigItem(LPWSTR name, DWORD index, ConfigItemType type, ConfigItemFlags flags)
        : Type(type)
        , Flags(flags)
        , dwIndex(index)
        , strName(name)
        , Status(MISSING) {};
    ConfigItem(const ConfigItem& other) noexcept;

    ConfigItem(ConfigItem&& other) noexcept
    {
        Type = other.Type;
        Flags = other.Flags;
        Status = other.Status;
        std::swap(strName, other.strName);
        dwIndex = other.dwIndex;
        dwOrderIndex = other.dwOrderIndex;
        std::swap(SubItems, other.SubItems);
        std::swap(strData, other.strData);
        std::swap(NodeList, other.NodeList);
    }

    ConfigItem& operator=(const ConfigItem& item) noexcept;
    ConfigItem& operator=(ConfigItem&& other) noexcept
    {
        Type = other.Type;
        Flags = other.Flags;
        Status = other.Status;
        std::swap(strName, other.strName);
        dwIndex = other.dwIndex;
        dwOrderIndex = other.dwOrderIndex;
        std::swap(SubItems, other.SubItems);
        std::swap(strData, other.strData);
        std::swap(NodeList, other.NodeList);
        return *this;
    };

    ConfigItem& operator[](DWORD dwIdx) { return SubItems[dwIdx]; }
    const ConfigItem& operator[](DWORD dwIdx) const { return SubItems[dwIdx]; }

    bool operator!() const
    {
        if (Status != ConfigItemStatus::PRESENT)
            return true;
        return false;
    }

    explicit operator std::wstring() const { return strData; }

    LPCWSTR c_str() const { return strData.c_str(); }

    explicit operator DWORD() const;
    explicit operator DWORD32() const;
    explicit operator DWORD64() const;

    explicit operator bool() const
    {
        if (Status == ConfigItemStatus::PRESENT)
            return true;
        return false;
    }

    operator std::wstring_view() const { return std::wstring_view(strData); }
    operator const std::wstring&() const { return strData; }

    ConfigItem& Item(LPCWSTR szIndex);
    const ConfigItem& Item(LPCWSTR szIndex) const;

    HRESULT AddAttribute(LPCWSTR szAttr, DWORD index, ConfigItemFlags flags);
    HRESULT AddAttribute(LPCWSTR szAttr, DWORD index, ConfigItemFlags flags, const logger& pLog);

    HRESULT AddChildNode(LPCWSTR szName, DWORD index, ConfigItemFlags flags);
    HRESULT AddChildNode(LPCWSTR szName, DWORD index, ConfigItemFlags flags, const logger& pLog);

    HRESULT AddChildNodeList(LPCWSTR szName, DWORD index, ConfigItemFlags flags);
    HRESULT AddChildNodeList(LPCWSTR szName, DWORD index, ConfigItemFlags flags, const logger& pLog);
    HRESULT AddChild(const ConfigItem& item);
    HRESULT AddChild(const ConfigItem& item, const logger& pLog);

    HRESULT AddChild(ConfigItem&& item);
    HRESULT AddChild(ConfigItem&& item, const logger& pLog);

    HRESULT AddChild(AdderFunction adder, DWORD dwIdx) { return adder(*this, dwIdx); }
    HRESULT AddChild(AdderFunction adder, DWORD dwIdx, const logger& pLog);

    HRESULT AddChild(LPCWSTR szName, NamedAdderFunction adder, DWORD dwIdx) { return adder(*this, dwIdx, szName); }
    HRESULT AddChild(LPCWSTR szName, NamedAdderFunction adder, DWORD dwIdx, const logger& pLog);
};

}  // namespace Orc

// Logging indexes
constexpr auto CONFIG_LOGFILE = 0U;
constexpr auto CONFIG_DEBUG = 1U;
constexpr auto CONFIG_VERBOSE = 2U;
constexpr auto CONFIG_NOCONSOLE = 3U;

constexpr auto CONFIG_XORPATTERN = 0U;
constexpr auto CONFIG_CSVENCODING = 1U;

// Output
constexpr auto CONFIG_OUTPUT_FORMAT = 0U;
constexpr auto CONFIG_OUTPUT_COMPRESSION = 1U;
constexpr auto CONFIG_OUTPUT_XORPATTERN = 2U;
constexpr auto CONFIG_OUTPUT_ENCODING = 3U;
constexpr auto CONFIG_OUTPUT_CONNECTION = 4U;
constexpr auto CONFIG_OUTPUT_TABLE = 5U;
constexpr auto CONFIG_OUTPUT_KEY = 6U;
constexpr auto CONFIG_OUTPUT_DISPOSITION = 7U;
constexpr auto CONFIG_OUTPUT_PASSWORD = 8U;

// UPLOAD
constexpr auto CONFIG_UPLOAD_METHOD = 0U;
constexpr auto CONFIG_UPLOAD_SERVER = 1U;
constexpr auto CONFIG_UPLOAD_ROOTPATH = 2U;
constexpr auto CONFIG_UPLOAD_JOBNAME = 3U;
constexpr auto CONFIG_UPLOAD_USER = 4U;
constexpr auto CONFIG_UPLOAD_PASSWORD = 5U;
constexpr auto CONFIG_UPLOAD_OPERATION = 6U;
constexpr auto CONFIG_UPLOAD_MODE = 7U;
constexpr auto CONFIG_UPLOAD_AUTHSCHEME = 8U;
constexpr auto CONFIG_UPLOAD_FILTER_EXC = 9U;
constexpr auto CONFIG_UPLOAD_FILTER_INC = 10U;

// DOWNLOAD
constexpr auto CONFIG_DOWNLOAD_METHOD = 0U;
constexpr auto CONFIG_DOWNLOAD_SERVER = 1U;
constexpr auto CONFIG_DOWNLOAD_ROOTPATH = 2U;
constexpr auto CONFIG_DOWNLOAD_JOBNAME = 3U;
constexpr auto CONFIG_DOWNLOAD_COMMAND = 4U;
constexpr auto CONFIG_DOWNLOAD_FILE = 5U;

constexpr auto CONFIG_DOWNLOAD_FILE_NAME = 0U;
constexpr auto CONFIG_DOWNLOAD_FILE_LOCALPATH = 1U;
constexpr auto CONFIG_DOWNLOAD_FILE_DELETE = 2U;

// REGFIND
constexpr auto CONFIG_REGFIND_KEY = 0L;
constexpr auto CONFIG_REGFIND_KEY_REGEX = 1L;
constexpr auto CONFIG_REGFIND_PATH = 2L;
constexpr auto CONFIG_REGFIND_PATH_REGEX = 3L;
constexpr auto CONFIG_REGFIND_VALUE = 4L;
constexpr auto CONFIG_REGFIND_VALUE_REGEX = 5L;
constexpr auto CONFIG_REGFIND_VALUE_TYPE = 6L;
constexpr auto CONFIG_REGFIND_DATA = 7L;
constexpr auto CONFIG_REGFIND_DATA_HEX = 8L;
constexpr auto CONFIG_REGFIND_DATA_REGEX = 9L;
constexpr auto CONFIG_REGFIND_DATA_SIZE = 10L;
constexpr auto CONFIG_REGFIND_DATA_SIZE_GT = 11L;
constexpr auto CONFIG_REGFIND_DATA_SIZE_GE = 12L;
constexpr auto CONFIG_REGFIND_DATA_SIZE_LT = 13L;
constexpr auto CONFIG_REGFIND_DATA_SIZE_LE = 14L;
constexpr auto CONFIG_REGFIND_DATA_CONTAINS = 15L;
constexpr auto CONFIG_REGFIND_DATA_CONTAINS_HEX = 16L;

constexpr auto CONFIG_FILEFIND_NAME = 0U;
constexpr auto CONFIG_FILEFIND_NAME_MATCH = 1U;
constexpr auto CONFIG_FILEFIND_NAME_REGEX = 2U;
constexpr auto CONFIG_FILEFIND_PATH = 3U;
constexpr auto CONFIG_FILEFIND_PATH_MATCH = 4U;
constexpr auto CONFIG_FILEFIND_PATH_REGEX = 5U;
constexpr auto CONFIG_FILEFIND_SIZE = 6U;
constexpr auto CONFIG_FILEFIND_SIZE_GT = 7U;
constexpr auto CONFIG_FILEFIND_SIZE_GE = 8U;
constexpr auto CONFIG_FILEFIND_SIZE_LT = 9U;
constexpr auto CONFIG_FILEFIND_SIZE_LE = 10U;
constexpr auto CONFIG_FILEFIND_MD5 = 11U;
constexpr auto CONFIG_FILEFIND_SHA1 = 12U;
constexpr auto CONFIG_FILEFIND_SHA256 = 13U;
constexpr auto CONFIG_FILEFIND_HEADER = 14U;
constexpr auto CONFIG_FILEFIND_HEADER_REGEX = 15U;
constexpr auto CONFIG_FILEFIND_HEADER_HEX = 16U;
constexpr auto CONFIG_FILEFIND_HEADER_LENGTH = 17U;
constexpr auto CONFIG_FILEFIND_ADS = 18U;
constexpr auto CONFIG_FILEFIND_ADS_MATCH = 19U;
constexpr auto CONFIG_FILEFIND_ADS_REGEX = 20U;
constexpr auto CONFIG_FILEFIND_EA = 21U;
constexpr auto CONFIG_FILEFIND_EA_MATCH = 22U;
constexpr auto CONFIG_FILEFIND_EA_REGEX = 23U;
constexpr auto CONFIG_FILEFIND_ATTR_NAME = 24U;
constexpr auto CONFIG_FILEFIND_ATTR_MATCH = 25U;
constexpr auto CONFIG_FILEFIND_ATTR_REGEX = 26U;
constexpr auto CONFIG_FILEFIND_ATTR_TYPE = 27U;
constexpr auto CONFIG_FILEFIND_CONTAINS = 28U;
constexpr auto CONFIG_FILEFIND_CONTAINS_HEX = 29U;
constexpr auto CONFIG_FILEFIND_YARA_RULE = 30U;

constexpr auto CONFIG_YARA_SOURCE = 0L;
constexpr auto CONFIG_YARA_BLOCK = 1L;
constexpr auto CONFIG_YARA_OVERLAP = 2L;
constexpr auto CONFIG_YARA_TIMEOUT = 3L;
constexpr auto CONFIG_YARA_SCAN_METHOD = 4L;

constexpr auto CONFIG_TEMPLATE_NAME = 0L;
constexpr auto CONFIG_TEMPLATE_LOCATION = 1L;

constexpr auto CONFIG_HIVE_NAME = 0L;
constexpr auto CONFIG_HIVE_TEMPLATE = 1L;
constexpr auto CONFIG_HIVE_FILEFIND = 2L;
constexpr auto CONFIG_HIVE_FILE = 3L;
constexpr auto CONFIG_HIVE_REGFIND = 4L;

constexpr auto CONFIG_HIVE_REGFIND_TEMPLATE = 0L;

constexpr auto CONFIG_MAXBYTESTOTAL = 0U;
constexpr auto CONFIG_MAXBYTESPERSAMPLE = 1U;
constexpr auto CONFIG_MAXSAMPLECOUNT = 2U;
constexpr auto CONFIG_SAMPLE_CONTENT = 3U;
constexpr auto CONFIG_SAMPLE_EXCLUDE = 4U;
constexpr auto CONFIG_SAMPLE_FILEFIND = 5U;
constexpr auto CONFIG_SAMPLE_NAME = 6U;

constexpr auto CONFIG_SAMPLE = 5U;

constexpr auto CONFIG_COMPUTERNAME = 0U;
constexpr auto CONFIG_BYTESPERSECTOR = 1U;
constexpr auto CONFIG_BYTESPERCLUSTER = 2U;
constexpr auto CONFIG_BYTESPERFRS = 3U;
constexpr auto CONFIG_ORIGINALVOLUME = 4U;
constexpr auto CONFIG_VOLUME_OFFSET = 5U;
constexpr auto CONFIG_VOLUME_SIZE = 6U;
constexpr auto CONFIG_VOLUME_SHADOWS = 7U;
constexpr auto CONFIG_VOLUME_ALTITUDE = 8U;

constexpr auto CONFIG_SQL_TABLEKEY = 0U;

constexpr auto CONFIG_SQL_CONNECTIONSTRING = 0U;
constexpr auto CONFIG_SQL_TABLE = 1U;
constexpr auto CONFIG_SQL_DISPOSITION = 2U;

constexpr auto CONFIG_SCHEMA_COLUMN_NAME = 0U;
constexpr auto CONFIG_SCHEMA_COLUMN_DESCRIPTION = 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_ARTIFACT = 2U;
constexpr auto CONFIG_SCHEMA_COLUMN_NULL = 3U;
constexpr auto CONFIG_SCHEMA_COLUMN_NOTNULL = 4U;
constexpr auto CONFIG_SCHEMA_COLUMN_FMT = 5U;
constexpr auto CONFIG_SCHEMA_COLUMN_COMMON_MAX = 5U;

constexpr auto CONFIG_SCHEMA_COLUMN_UTF8_MAXLEN = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_UTF8_LEN = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 2U;
constexpr auto CONFIG_SCHEMA_COLUMN_UTF16_MAXLEN = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_UTF16_LEN = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 2U;

constexpr auto CONFIG_SCHEMA_COLUMN_BINARY_LEN = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_BINARY_MAXLEN = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 2U;

constexpr auto CONFIG_SCHEMA_COLUMN_XML_MAXLEN = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 1U;

constexpr auto CONFIG_SCHEMA_COLUMN_ENUM_VALUE = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_ENUM_VALUE_INDEX = 0U;

constexpr auto CONFIG_SCHEMA_COLUMN_FLAGS_VALUE = CONFIG_SCHEMA_COLUMN_COMMON_MAX + 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_FLAGS_VALUE_INDEX = 0U;

constexpr auto CONFIG_SCHEMA_TABLEKEY = 0U;

constexpr auto CONFIG_SCHEMA_COLUMN_FIRST_TYPE = 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_BOOL = 1U;
constexpr auto CONFIG_SCHEMA_COLUMN_UINT8 = 2U;
constexpr auto CONFIG_SCHEMA_COLUMN_INT8 = 3U;
constexpr auto CONFIG_SCHEMA_COLUMN_UINT16 = 4U;
constexpr auto CONFIG_SCHEMA_COLUMN_INT16 = 5U;
constexpr auto CONFIG_SCHEMA_COLUMN_UINT32 = 6U;
constexpr auto CONFIG_SCHEMA_COLUMN_INT32 = 7U;
constexpr auto CONFIG_SCHEMA_COLUMN_UINT64 = 8U;
constexpr auto CONFIG_SCHEMA_COLUMN_INT64 = 9U;
constexpr auto CONFIG_SCHEMA_COLUMN_TIMESTAMP = 10U;
constexpr auto CONFIG_SCHEMA_COLUMN_UTF8 = 11U;
constexpr auto CONFIG_SCHEMA_COLUMN_UTF16 = 12U;
constexpr auto CONFIG_SCHEMA_COLUMN_BINARY = 13U;
constexpr auto CONFIG_SCHEMA_COLUMN_GUID = 14U;
constexpr auto CONFIG_SCHEMA_COLUMN_XML = 15U;
constexpr auto CONFIG_SCHEMA_COLUMN_ENUM = 16U;
constexpr auto CONFIG_SCHEMA_COLUMN_FLAGS = 17U;
constexpr auto CONFIG_SCHEMA_COLUMN_LAST_TYPE = 17U;

constexpr auto CONFIG_SCHEMA_TOOL = 0U;
constexpr auto CONFIG_SCHEMA_TABLE = 1U;

#pragma managed(pop)
