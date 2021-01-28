//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ConfigFile.h"

#pragma managed(push, off)

namespace Orc::Config::Common {

// A few frequently used patterns

// OUTPUT
HRESULT ORCLIB_API output(ConfigItem& parent, DWORD dwIndex, std::wstring_view elementName = L"output");

// DOWNLOAD
ORCLIB_API HRESULT download(ConfigItem& parent, DWORD dwIndex, std::wstring_view elementName = L"download");

// UPLOAD
ORCLIB_API HRESULT upload(ConfigItem& parent, DWORD dwIndex, std::wstring_view elementName = L"upload");

// SAMPLE content attribute
ORCLIB_API HRESULT content(ConfigItem& parent, DWORD dwIndex);

// SAMPLE node
ORCLIB_API HRESULT sample(ConfigItem& parent, DWORD dwIndex);

// SAMPLES node
ORCLIB_API HRESULT samples(ConfigItem& parent, DWORD dwIndex);

// LOCATION item
ORCLIB_API HRESULT location(ConfigItem& parent, DWORD dwIndex, ConfigItem::ConfigItemFlags flags);
ORCLIB_API HRESULT location(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT optional_location(ConfigItem& parent, DWORD dwIndex);

ORCLIB_API HRESULT knownlocations(ConfigItem& parent, DWORD dwIndex);

// Logging configuration
ORCLIB_API HRESULT logging(ConfigItem& parent, DWORD dwIndex);

// registry
ORCLIB_API HRESULT registry_find(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT hive(ConfigItem& parent, DWORD dwIndex);

// file_find magic
ORCLIB_API HRESULT ntfs_criterias(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT ntfs_find(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT ntfs_exclude(ConfigItem& parent, DWORD dwIndex);

// yara scanner
ORCLIB_API HRESULT yara(ConfigItem& parent, DWORD dwIndex);

ORCLIB_API HRESULT templates(ConfigItem& parent, DWORD dwIndex);

ORCLIB_API HRESULT template_(ConfigItem& parent, DWORD dwIndex);

//<OutputSQL disposition="createnew" >
//  <ConnectionString>Data Source=localhost;Initial Catalog=test;Integrated Security=SSPI;</ConnectionString>
//  <Table key="Registry">Registry</Table>
//</OutputSQL>

ORCLIB_API HRESULT sql_table(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sql_outputsql(ConfigItem& parent, DWORD dwIndex);

//<Columns>
//  <Column Name="ComputerName"         Description="Computer Name" Type="String" MaxLen="15"/>
//  <Column Name="LastModificationDate" Description="Last Mod Date" Type="TimeStamp" />
//  <Column Name="FirstBytes"           Description="FirstBytes"    Type="String" MaxLen="40" />
//</Columns>

ORCLIB_API HRESULT sch_column_common(ConfigItem& item);

ORCLIB_API HRESULT sch_column_boolean(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_uint8(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_int8(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_uint16(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_int16(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_uint32(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_int32(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_uint64(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_int64(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_timestamp(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_utf8(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_utf16(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_binary(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_guid(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_xml(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_enum(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sch_column_flags(ConfigItem& parent, DWORD dwIndex);

ORCLIB_API HRESULT sch_table(ConfigItem& parent, DWORD dwIndex);
ORCLIB_API HRESULT sqlschema(ConfigItem& item);

ORCLIB_API HRESULT regfind_template(ConfigItem& item);

}  // namespace Orc::Config::Common
#pragma managed(pop)
