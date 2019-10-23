//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "ConfigFile.h"

#include "ConfigFile_Common.h"

using namespace Orc;

// OUTPUT
HRESULT Orc::Config::Common::output(ConfigItem& parent, DWORD dwIndex, const WCHAR* szEltName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(szEltName, dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"format", CONFIG_OUTPUT_FORMAT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(L"compression", CONFIG_OUTPUT_COMPRESSION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"XOR", CONFIG_OUTPUT_XORPATTERN, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"encoding", CONFIG_OUTPUT_ENCODING, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"connectionstring", CONFIG_OUTPUT_CONNECTION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"table", CONFIG_OUTPUT_TABLE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"key", CONFIG_OUTPUT_KEY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(L"disposition", CONFIG_OUTPUT_DISPOSITION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"password", CONFIG_OUTPUT_PASSWORD, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// UPLOAD
HRESULT download_file(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"file", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", CONFIG_DOWNLOAD_FILE_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"localpath", CONFIG_DOWNLOAD_FILE_LOCALPATH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"delete", CONFIG_DOWNLOAD_FILE_DELETE, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}
ORCLIB_API HRESULT Orc::Config::Common::download(ConfigItem& parent, DWORD dwIndex, const WCHAR* szEltName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(szEltName, dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"method", CONFIG_DOWNLOAD_METHOD, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"server", CONFIG_DOWNLOAD_SERVER, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"path", CONFIG_DOWNLOAD_ROOTPATH, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"job", CONFIG_DOWNLOAD_JOBNAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"command", CONFIG_DOWNLOAD_COMMAND, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = download_file(parent.SubItems[dwIndex], CONFIG_DOWNLOAD_FILE)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::upload(ConfigItem& parent, DWORD dwIndex, const WCHAR* szEltName)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(szEltName, dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"method", CONFIG_UPLOAD_METHOD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"server", CONFIG_UPLOAD_SERVER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"path", CONFIG_UPLOAD_ROOTPATH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"job", CONFIG_UPLOAD_JOBNAME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"user", CONFIG_UPLOAD_USER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"password", CONFIG_UPLOAD_PASSWORD, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"operation", CONFIG_UPLOAD_OPERATION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"mode", CONFIG_UPLOAD_MODE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"authscheme", CONFIG_UPLOAD_AUTHSCHEME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"exclude", CONFIG_UPLOAD_FILTER_EXC, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"include", CONFIG_UPLOAD_FILTER_INC, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::ntfs_criterias(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", CONFIG_FILEFIND_NAME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name_match", CONFIG_FILEFIND_NAME_MATCH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name_regex", CONFIG_FILEFIND_NAME_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"path", CONFIG_FILEFIND_PATH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"path_match", CONFIG_FILEFIND_PATH_MATCH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"path_regex", CONFIG_FILEFIND_PATH_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"size", CONFIG_FILEFIND_SIZE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"size_gt", CONFIG_FILEFIND_SIZE_GT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"size_ge", CONFIG_FILEFIND_SIZE_GE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"size_lt", CONFIG_FILEFIND_SIZE_LT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"size_le", CONFIG_FILEFIND_SIZE_LE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"md5", CONFIG_FILEFIND_MD5, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"sha1", CONFIG_FILEFIND_SHA1, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"sha256", CONFIG_FILEFIND_SHA256, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"header", CONFIG_FILEFIND_HEADER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"header_regex", CONFIG_FILEFIND_HEADER_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"header_hex", CONFIG_FILEFIND_HEADER_HEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"header_length", CONFIG_FILEFIND_HEADER_LENGTH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ads", CONFIG_FILEFIND_ADS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ads_match", CONFIG_FILEFIND_ADS_MATCH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ads_regex", CONFIG_FILEFIND_ADS_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ea", CONFIG_FILEFIND_EA, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ea_match", CONFIG_FILEFIND_EA_MATCH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"ea_regex", CONFIG_FILEFIND_EA_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"attr_name", CONFIG_FILEFIND_ATTR_NAME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"attr_match", CONFIG_FILEFIND_ATTR_MATCH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"attr_regex", CONFIG_FILEFIND_ATTR_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"attr_type", CONFIG_FILEFIND_ATTR_TYPE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"contains", CONFIG_FILEFIND_CONTAINS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"contains_hex", CONFIG_FILEFIND_CONTAINS_HEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"yara_rule", CONFIG_FILEFIND_YARA_RULE, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// FileFind magic
HRESULT Orc::Config::Common::ntfs_find(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"ntfs_find", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = Orc::Config::Common::ntfs_criterias(parent, dwIndex)))
        return hr;
    return S_OK;
};

// FileFind magic
HRESULT Orc::Config::Common::ntfs_exclude(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"ntfs_exclude", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = ntfs_criterias(parent, dwIndex)))
        return hr;
    return S_OK;
};

// FileFind magic
HRESULT Orc::Config::Common::yara(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"yara", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"source", CONFIG_YARA_SOURCE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"block", CONFIG_YARA_BLOCK, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"overlap", CONFIG_YARA_OVERLAP, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"timeout", CONFIG_YARA_TIMEOUT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"scan_method", CONFIG_YARA_SCAN_METHOD, ConfigItem::OPTION)))
        return hr;
    return S_OK;
};

HRESULT Orc::Config::Common::registry_find(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"registry_find", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"key", CONFIG_REGFIND_KEY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"key_regex", CONFIG_REGFIND_KEY_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"key_path", CONFIG_REGFIND_PATH, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"key_path_regex", CONFIG_REGFIND_PATH_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"value", CONFIG_REGFIND_VALUE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(L"value_regex", CONFIG_REGFIND_VALUE_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(L"value_type", CONFIG_REGFIND_VALUE_TYPE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"data", CONFIG_REGFIND_DATA, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"data_hex", CONFIG_REGFIND_DATA_HEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(L"data_regex", CONFIG_REGFIND_DATA_REGEX, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent.SubItems[dwIndex].AddAttribute(L"data_size", CONFIG_REGFIND_DATA_SIZE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"data_size_gt", CONFIG_REGFIND_DATA_SIZE_GT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"data_size_ge", CONFIG_REGFIND_DATA_SIZE_GE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"data_size_lt", CONFIG_REGFIND_DATA_SIZE_LT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"data_size_le", CONFIG_REGFIND_DATA_SIZE_LE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"data_contains", CONFIG_REGFIND_DATA_CONTAINS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent.SubItems[dwIndex].AddAttribute(
                L"data_contains_hex", CONFIG_REGFIND_DATA_CONTAINS_HEX, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::template_(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"template", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", CONFIG_TEMPLATE_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"location", CONFIG_TEMPLATE_LOCATION, ConfigItem::MANDATORY)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::templates(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"templates", dwIndex, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::hive(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"hive", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", CONFIG_HIVE_NAME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = template_(parent[dwIndex], CONFIG_HIVE_TEMPLATE)))
        return hr;
    if (FAILED(hr = ntfs_find(parent[dwIndex], CONFIG_HIVE_FILEFIND)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChildNodeList(L"filename", CONFIG_HIVE_FILE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = registry_find(parent[dwIndex], CONFIG_HIVE_REGFIND)))
        return hr;
    return S_OK;
}

// SAMPLE content attribute node
HRESULT Orc::Config::Common::content(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddAttribute(L"content", dwIndex, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// SAMPLE node
HRESULT Orc::Config::Common::sample(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"sample", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"MaxTotalBytes", CONFIG_MAXBYTESTOTAL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"MaxPerSampleBytes", CONFIG_MAXBYTESPERSAMPLE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"MaxSampleCount", CONFIG_MAXSAMPLECOUNT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(content, CONFIG_SAMPLE_CONTENT)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(ntfs_exclude, CONFIG_SAMPLE_EXCLUDE)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(ntfs_find, CONFIG_SAMPLE_FILEFIND)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"name", CONFIG_SAMPLE_NAME, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// SAMPLES node
HRESULT Orc::Config::Common::samples(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"samples", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"MaxTotalBytes", CONFIG_MAXBYTESTOTAL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"MaxPerSampleBytes", CONFIG_MAXBYTESPERSAMPLE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"MaxSampleCount", CONFIG_MAXSAMPLECOUNT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(content, CONFIG_SAMPLE_CONTENT)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(ntfs_exclude, CONFIG_SAMPLE_EXCLUDE)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sample, CONFIG_SAMPLE)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::location(ConfigItem& parent, DWORD dwIndex, ConfigItem::ConfigItemFlags flags)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"location", dwIndex, flags)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"computer", CONFIG_COMPUTERNAME, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"bytespersector", CONFIG_BYTESPERSECTOR, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"bytespercluster", CONFIG_BYTESPERCLUSTER, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"bytesperfrs", CONFIG_BYTESPERFRS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"offset", CONFIG_VOLUME_OFFSET, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"length", CONFIG_VOLUME_SIZE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"shadows", CONFIG_VOLUME_SHADOWS, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"altitude", CONFIG_VOLUME_ALTITUDE, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

// LOCATION item
HRESULT Orc::Config::Common::location(ConfigItem& parent, DWORD dwIndex)
{
    return location(parent, dwIndex, ConfigItem::MANDATORY);
}

HRESULT Orc::Config::Common::optional_location(ConfigItem& parent, DWORD dwIndex)
{
    return location(parent, dwIndex, ConfigItem::OPTION);
}

HRESULT Orc::Config::Common::knownlocations(ConfigItem& parent, DWORD dwIndex)
{
    return parent.AddChildNode(L"knownlocations", dwIndex, ConfigItem::OPTION);
}

// Logging configuration
HRESULT Orc::Config::Common::logging(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"logging", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"file", CONFIG_LOGFILE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"debug", CONFIG_DEBUG, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"verbose", CONFIG_VERBOSE, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"noconsole", CONFIG_NOCONSOLE, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

//<OutputSQL disposition="createnew" >
//  <ConnectionString>Data Source=localhost;Initial Catalog=test;Integrated Security=SSPI;</ConnectionString>
//  <Table key="Registry">Registry</Table>
//</OutputSQL>

HRESULT Orc::Config::Common::sql_table(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"table", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"key", CONFIG_SQL_TABLEKEY, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sql_outputsql(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNode(L"outputsql", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex].AddChildNode(L"connectionstring", CONFIG_SQL_CONNECTIONSTRING, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sql_table, CONFIG_SQL_TABLE)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"disposition", CONFIG_SQL_DISPOSITION, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_common(ConfigItem& item)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = item.AddAttribute(L"name", CONFIG_SCHEMA_COLUMN_NAME, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"description", CONFIG_SCHEMA_COLUMN_DESCRIPTION, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"artifact", CONFIG_SCHEMA_COLUMN_ARTIFACT, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"allows_null", CONFIG_SCHEMA_COLUMN_NULL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"not_null", CONFIG_SCHEMA_COLUMN_NOTNULL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddAttribute(L"fmt", CONFIG_SCHEMA_COLUMN_FMT, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_boolean(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"bool", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_uint8(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"uint8", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_int8(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"int8", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_uint16(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"uint16", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_int16(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"int16", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_uint32(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"uint32", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}
HRESULT Orc::Config::Common::sch_column_int32(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"int32", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_uint64(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"uint64", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_int64(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"int64", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_timestamp(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"timestamp", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_utf8(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"utf8", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"maxlen", CONFIG_SCHEMA_COLUMN_UTF8_MAXLEN, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"len", CONFIG_SCHEMA_COLUMN_UTF8_LEN, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_utf16(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"utf16", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"maxlen", CONFIG_SCHEMA_COLUMN_UTF16_MAXLEN, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"len", CONFIG_SCHEMA_COLUMN_UTF16_LEN, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_binary(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"binary", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"len", CONFIG_SCHEMA_COLUMN_BINARY_LEN, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"maxlen", CONFIG_SCHEMA_COLUMN_BINARY_MAXLEN, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_guid(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"guid", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_xml(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"xml", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"maxlen", CONFIG_SCHEMA_COLUMN_XML_MAXLEN, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_enum(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"enum", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChildNodeList(L"value", CONFIG_SCHEMA_COLUMN_ENUM_VALUE, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][CONFIG_SCHEMA_COLUMN_ENUM_VALUE].AddAttribute(
                L"index", CONFIG_SCHEMA_COLUMN_ENUM_VALUE_INDEX, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_column_flags(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"flags", dwIndex, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = sch_column_common(parent[dwIndex])))
        return hr;
    if (FAILED(
            hr = parent[dwIndex].AddChildNodeList(L"value", CONFIG_SCHEMA_COLUMN_FLAGS_VALUE, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(
            hr = parent[dwIndex][CONFIG_SCHEMA_COLUMN_FLAGS_VALUE].AddAttribute(
                L"index", CONFIG_SCHEMA_COLUMN_FLAGS_VALUE_INDEX, ConfigItem::OPTION)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sch_table(ConfigItem& parent, DWORD dwIndex)
{
    HRESULT hr = E_FAIL;
    if (FAILED(hr = parent.AddChildNodeList(L"table", dwIndex, ConfigItem::MANDATORY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddAttribute(L"key", CONFIG_SCHEMA_TABLEKEY, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_boolean, CONFIG_SCHEMA_COLUMN_BOOL)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_uint8, CONFIG_SCHEMA_COLUMN_UINT8)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_int8, CONFIG_SCHEMA_COLUMN_INT8)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_uint16, CONFIG_SCHEMA_COLUMN_UINT16)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_int16, CONFIG_SCHEMA_COLUMN_INT16)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_uint32, CONFIG_SCHEMA_COLUMN_UINT32)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_int32, CONFIG_SCHEMA_COLUMN_INT32)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_uint64, CONFIG_SCHEMA_COLUMN_UINT64)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_int64, CONFIG_SCHEMA_COLUMN_INT64)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_timestamp, CONFIG_SCHEMA_COLUMN_TIMESTAMP)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_utf8, CONFIG_SCHEMA_COLUMN_UTF8)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_utf16, CONFIG_SCHEMA_COLUMN_UTF16)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_binary, CONFIG_SCHEMA_COLUMN_BINARY)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_guid, CONFIG_SCHEMA_COLUMN_GUID)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_xml, CONFIG_SCHEMA_COLUMN_XML)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_enum, CONFIG_SCHEMA_COLUMN_ENUM)))
        return hr;
    if (FAILED(hr = parent[dwIndex].AddChild(sch_column_flags, CONFIG_SCHEMA_COLUMN_FLAGS)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::sqlschema(ConfigItem& item)
{
    HRESULT hr = E_FAIL;
    item.strName = L"sqlschema";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddAttribute(L"tool", CONFIG_SCHEMA_TOOL, ConfigItem::OPTION)))
        return hr;
    if (FAILED(hr = item.AddChild(sch_table, CONFIG_SCHEMA_TABLE)))
        return hr;
    return S_OK;
}

HRESULT Orc::Config::Common::regfind_template(ConfigItem& item)
{
    HRESULT hr = E_FAIL;

    item.strName = L"reginfo_template";
    item.Flags = ConfigItem::MANDATORY;
    item.dwIndex = 0L;
    item.Status = ConfigItem::MISSING;

    if (FAILED(hr = item.AddChild(registry_find, CONFIG_HIVE_REGFIND_TEMPLATE)))
        return hr;

    return S_OK;
}
