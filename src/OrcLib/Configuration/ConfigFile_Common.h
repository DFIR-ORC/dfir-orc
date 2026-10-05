//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
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
HRESULT output(ConfigItem& parent, DWORD dwIndex, std::wstring_view elementName = L"output");

// DOWNLOAD
HRESULT download(ConfigItem& parent, DWORD dwIndex, std::wstring_view elementName = L"download");

// UPLOAD
// Registers upload attributes directly on 'item' (no child node wrapping), so Wolf::upload() can put them on
// its repeatable <upload> node list, one entry per upload fallback.
HRESULT upload_attributes(ConfigItem& item);
// The shared schema tail: proxy attributes (22-26), follow_redirects (27), AWS SigV4 (28-33) and the
// retry policy (34-35). Registered by BOTH upload() and Wolf::upload() after their
// <rest> list (21); factored here so the two upload schemas cannot drift (a missing registration
// silently drops every proxy setting at parse time).
HRESULT upload_proxy_attributes(ConfigItem& item);
HRESULT upload(ConfigItem& parent, DWORD dwIndex, std::wstring_view elementName = L"upload");
// The <upload> NODELIST form (a fallback chain), as opposed to upload()'s single node. Lives here
// rather than in OrcCommand so both the Wolf schema and <destination> register the identical
// attribute set, <header> list, <rest> steps and proxy attributes from one place.
HRESULT upload_list(ConfigItem& parent, DWORD dwIndex, std::wstring_view elementName = L"upload");
// The <uploads> block: several <destination> entries, each one an <upload> fallback chain.
HRESULT uploads(ConfigItem& parent, DWORD dwIndex);
HRESULT rest_step(ConfigItem& parent, DWORD dwIndex);

// SAMPLE content attribute
HRESULT content(ConfigItem& parent, DWORD dwIndex);

// SAMPLE node
HRESULT sample(ConfigItem& parent, DWORD dwIndex);

// SAMPLES node
HRESULT samples(ConfigItem& parent, DWORD dwIndex);

// LOCATION item
HRESULT location(ConfigItem& parent, DWORD dwIndex, ConfigItem::ConfigItemFlags flags);
HRESULT location(ConfigItem& parent, DWORD dwIndex);
HRESULT optional_location(ConfigItem& parent, DWORD dwIndex);

HRESULT knownlocations(ConfigItem& parent, DWORD dwIndex);

// Logging configuration
HRESULT logging(ConfigItem& parent, DWORD dwIndex);

// registry
HRESULT registry_find(ConfigItem& parent, DWORD dwIndex);
HRESULT hive(ConfigItem& parent, DWORD dwIndex);

// file_find magic
HRESULT ntfs_criterias(ConfigItem& parent, DWORD dwIndex);
HRESULT ntfs_find(ConfigItem& parent, DWORD dwIndex);
HRESULT ntfs_exclude(ConfigItem& parent, DWORD dwIndex);

// yara scanner
HRESULT yara(ConfigItem& parent, DWORD dwIndex);

HRESULT templates(ConfigItem& parent, DWORD dwIndex);

HRESULT template_(ConfigItem& parent, DWORD dwIndex);

//<OutputSQL disposition="createnew" >
//  <ConnectionString>Data Source=localhost;Initial Catalog=test;Integrated Security=SSPI;</ConnectionString>
//  <Table key="Registry">Registry</Table>
//</OutputSQL>

HRESULT sql_table(ConfigItem& parent, DWORD dwIndex);
HRESULT sql_outputsql(ConfigItem& parent, DWORD dwIndex);

//<Columns>
//  <Column Name="ComputerName"         Description="Computer Name" Type="String" MaxLen="15"/>
//  <Column Name="LastModificationDate" Description="Last Mod Date" Type="TimeStamp" />
//  <Column Name="FirstBytes"           Description="FirstBytes"    Type="String" MaxLen="40" />
//</Columns>

HRESULT sch_column_common(ConfigItem& item);

HRESULT sch_column_boolean(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_uint8(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_int8(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_uint16(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_int16(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_uint32(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_int32(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_uint64(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_int64(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_timestamp(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_utf8(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_utf16(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_binary(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_guid(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_xml(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_enum(ConfigItem& parent, DWORD dwIndex);
HRESULT sch_column_flags(ConfigItem& parent, DWORD dwIndex);

HRESULT sch_table(ConfigItem& parent, DWORD dwIndex);
HRESULT sqlschema(ConfigItem& item);

HRESULT regfind_template(ConfigItem& item);

}  // namespace Orc::Config::Common
#pragma managed(pop)
