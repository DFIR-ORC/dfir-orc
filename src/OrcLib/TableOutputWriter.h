//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>
#include <filesystem>

#include <ObjIdl.h>

#include "TableOutput.h"
#include "OutputSpec.h"

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;
class OutputSpec;
class ConfigItem;

class ByteStream;

namespace TableOutput {

class IConnection
{
public:
    STDMETHOD(Connect)(const std::wstring& strConnString) PURE;
    STDMETHOD_(bool, IsConnected)() const PURE;
    STDMETHOD(Disconnect)() PURE;

    STDMETHOD_(bool, IsTablePresent)(const std::wstring& strTableName) PURE;
    STDMETHOD(DropTable)(const std::wstring& strTableName) PURE;
    STDMETHOD(TruncateTable)(const std::wstring& strTableName) PURE;
    STDMETHOD(CreateTable)
    (const std::wstring& strTableName, const Schema& columns, bool bCompress = false, bool bTABLOCK = true) PURE;

    STDMETHOD(ExecuteStatement)(const std::wstring& strStatement) PURE;
};

class IConnectWriter;
class IStreamWriter;

class IWriter : public IOutput
{
public:
    STDMETHOD(SetSchema)(const Schema& columns) PURE;

    STDMETHOD(Flush)() PURE;
    STDMETHOD(Close)() PURE;
};

namespace CSV {

constexpr auto WRITE_BUFFER = (0x100000);

using namespace std::string_literals;
struct Options : Orc::TableOutput::Options
{
    OutputSpec::Encoding Encoding = OutputSpec::Encoding::UTF8;
    DWORD dwBufferSize = WRITE_BUFFER;
    bool bBOM = true;
    std::wstring Delimiter = L","s;
    std::wstring StringDelimiter = L"\""s;
    std::wstring EndOfLine = L"\r\n"s;
    std::wstring BoolChars = L"YN"s;
};
}  // namespace CSV

namespace Parquet {
struct Options : Orc::TableOutput::Options
{
    std::optional<DWORD> BatchSize;
};
}  // namespace Parquet

namespace Sql {
struct Options : Orc::TableOutput::Options
{
};
}  // namespace Sql

namespace ApacheOrc {
struct Options : Orc::TableOutput::Options
{
    std::optional<DWORD> BatchSize;
    std::optional<std::pair<std::wstring, std::vector<BYTE>>> TimeZone;
};
}  // namespace ApacheOrc

[[nodiscard]] std::shared_ptr<IWriter> GetWriter(const OutputSpec& out);
[[nodiscard]] std::shared_ptr<IWriter> GetWriter(LPCWSTR szFileName, const OutputSpec& out);

[[nodiscard]] std::shared_ptr<IStreamWriter> GetCSVWriter(std::unique_ptr<Options> options);
[[nodiscard]] std::shared_ptr<IStreamWriter> GetParquetWriter(std::unique_ptr<Options> options);
[[nodiscard]] std::shared_ptr<IStreamWriter> GetApacheOrcWriter(std::unique_ptr<Options> options);

class IStreamWriter : public IWriter
{
public:
    STDMETHOD(WriteToFile)(const std::filesystem::path& path) PURE;
    STDMETHOD(WriteToFile)(const WCHAR* szFileName) PURE;
    STDMETHOD(WriteToStream)(const std::shared_ptr<ByteStream>& pStream, bool bCloseStream = true) PURE;

    virtual std::shared_ptr<ByteStream> GetStream() const PURE;
};

class BoundColumn;
using BoundRecord = std::vector<BoundColumn>;

class IConnectWriter : public IWriter
{
public:
    STDMETHOD(AddColumn)
    (DWORD dwColId,
     const std::wstring& strName,
     ColumnType type,
     std::optional<DWORD> dwMaxLen = std::nullopt,
     std::optional<DWORD> dwLen = std::nullopt) PURE;

    STDMETHOD(SetConnection)(const std::shared_ptr<IConnection>& pConnection) PURE;

    STDMETHOD(BindColumns)(LPCWSTR szTable) PURE;
    STDMETHOD(NextColumn)() PURE;

    STDMETHOD(ClearTable)(LPCWSTR szTableName) PURE;

    virtual const BoundRecord& GetColumnDefinitions() const PURE;
    virtual BoundRecord& GetColumnDefinitions() PURE;
};

class Writer : public Orc::OutputWriter
{
protected:
    Schema m_Schema;

public:
};

Schema GetColumnsFromConfig(LPCWSTR szTableName, const ConfigItem& item);
}  // namespace TableOutput
}  // namespace Orc

#pragma managed(pop)
