//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "OutputWriter.h"
#include "Flags.h"
#include "Buffer.h"

#include <optional>
#include <string_view>

#ifndef __cplusplus_cli
#    include <fmt/format.h>
#endif

#pragma managed(push, off)

namespace Orc {

class CBinaryBuffer;

namespace TableOutput {

struct Options : public Orc::OutputOptions
{
};

enum ColumnType
{
    Nothing,
    BoolType,
    UInt8Type,
    Int8Type,
    UInt16Type,
    Int16Type,
    UInt32Type,
    Int32Type,
    UInt64Type,
    Int64Type,
    TimeStampType,
    UTF16Type,
    UTF8Type,
    BinaryType,
    FixedBinaryType,
    GUIDType,
    EnumType,
    XMLType,
    FlagsType
};

class EnumValue
{
public:
    DWORD Index;
    std::wstring strValue;

    EnumValue(const std::wstring& strV, DWORD dwIndex)
        : Index(dwIndex)
        , strValue(strV)
    {
    }
};

class FlagValue
{
public:
    DWORD dwFlag;
    std::wstring strFlag;

    FlagValue(const std::wstring& strV, DWORD dwIndex)
        : dwFlag(dwIndex)
        , strFlag(strV)
    {
    }
};

using namespace std::string_view_literals;

class Column
{
public:
    Column(
        ColumnType type,
        const std::wstring_view& strName,
        const std::optional<std::wstring_view>& strDescr = std::nullopt,
        const std::optional<std::wstring_view>& strFormat = std::nullopt,
        const std::optional<std::wstring_view>& strArtifact = std::nullopt)
        : Type(type)
        , ColumnName(strName)
        , Description(strDescr)
        , Format(strFormat)
        , Artifact(strArtifact) {};
    virtual ~Column() {};

    Column() { ColumnName = L"None"; };

    Column(Column&& other) noexcept
    {
        std::swap(ColumnName, other.ColumnName);
        std::swap(Type, other.Type);
        std::swap(Description, other.Description);
        std::swap(Artifact, other.Artifact);
        std::swap(Format, other.Format);
        std::swap(dwColumnID, other.dwColumnID);
        std::swap(dwMaxLen, other.dwMaxLen);
        std::swap(dwLen, other.dwLen);
        std::swap(bAllowsNullValues, other.bAllowsNullValues);
        std::swap(EnumValues, other.EnumValues);
        std::swap(FlagsValues, other.FlagsValues);
    }

    Column(const Column& other) = default;
    Column& operator=(const Column& other) = default;

    DWORD dwColumnID = 0;
    ColumnType Type = Nothing;
    std::wstring ColumnName;
    std::optional<std::wstring> Description;
    std::optional<std::wstring> Format;
    std::optional<std::wstring> Artifact;

    std::optional<DWORD> dwMaxLen;
    std::optional<DWORD> dwLen;
    bool bAllowsNullValues = true;

    std::optional<std::vector<EnumValue>> EnumValues;
    std::optional<std::vector<FlagValue>> FlagsValues;
};

class Schema
{

public:
    using value_type = std::unique_ptr<Column>;

    Schema() = default;
    Schema(const Schema& other) = default;
    Schema(Schema&& other) noexcept = default;

    Schema(std::initializer_list<Column> columns)
    {
        m_Columns = std::make_shared<std::vector<value_type>>();
        m_Columns->reserve(columns.size());
        DWORD dwColumIdx = 1LU;
        for (auto&& c : columns)
        {
            m_Columns->push_back(std::make_unique<Column>(std::move(c)));
            m_Columns->back()->dwColumnID = dwColumIdx;
            dwColumIdx++;
        }
    }

    const Column& operator[](const std::wstring_view& strName) const
    {
        auto it = std::find_if(std::begin(*m_Columns), std::end(*m_Columns), [&strName](const auto& col) -> bool {
            if (col)
                return col->ColumnName == strName;
            return false;
        });
        if (it == std::end(*m_Columns))
            throw Orc::Exception(
                Fatal, E_INVALIDARG, L"No column named \"%.*s\" in schema", strName.size(), strName.data());
        return *(*it);
    };
    Column& operator[](const std::wstring_view& strName)
    {
        auto it = std::find_if(std::begin(*m_Columns), std::end(*m_Columns), [&strName](const auto& col) -> bool {
            if (col)
                return col->ColumnName == strName;
            return false;
        });
        if (it == std::end(*m_Columns))
            throw Orc::Exception(
                Fatal, E_INVALIDARG, L"No column named \"%.*s\" in schema", strName.size(), strName.data());
        return *(*it);
    };
    const Column& operator[](const size_t colId) const
    {
        if (!m_Columns)
            throw Orc::Exception(Fatal, E_INVALIDARG, L"Schema not initialized");
        if (colId >= m_Columns->size())
            throw Orc::Exception(
                Fatal, E_INVALIDARG, L"No column at index %d (max is %d)", colId, m_Columns->size() - 1);
        const auto& pCol = (*m_Columns)[colId];
        if (!pCol)
            throw Orc::Exception(Fatal, E_INVALIDARG, L"Column at index %d is not initialized", colId);
        return *((*m_Columns)[colId]);
    };
    Column& operator[](const size_t colId)
    {
        if (!m_Columns)
            throw Orc::Exception(Fatal, E_INVALIDARG, L"Schema not initialized");
        if (colId >= m_Columns->size())
            throw Orc::Exception(
                Fatal, E_INVALIDARG, L"No column at index %d (max is %d)", colId, m_Columns->size() - 1);
        const auto& pCol = (*m_Columns)[colId];
        if (!pCol)
            throw Orc::Exception(Fatal, E_INVALIDARG, L"Column at index %d is not initialized", colId);
        return *((*m_Columns)[colId]);
    };

    Schema& operator=(const Schema& other)
    {
        m_Columns = other.m_Columns;
        return *this;
    }

    explicit operator bool() const { return (bool)m_Columns; };

    auto begin() const { return std::begin(*m_Columns); }
    auto end() const { return std::end(*m_Columns); }

    void reserve(size_t capacity)
    {
        if (!m_Columns)
            m_Columns = std::make_shared<std::vector<value_type>>();
        m_Columns->reserve(capacity);
    }

    size_t size() const
    {
        if (m_Columns)
            return m_Columns->size();
        return 0;
    }

    void AddColumn(value_type&& pCol)
    {
        if (!m_Columns)
            m_Columns = std::make_shared<std::vector<value_type>>();
        m_Columns->emplace_back(std::move(pCol));
    }

private:
    std::shared_ptr<std::vector<value_type>> m_Columns;
};

class IOutput
{
public:
    virtual DWORD GetCurrentColumnID() PURE;
    virtual const Column& GetCurrentColumn() PURE;

    STDMETHOD(WriteNothing)() PURE;

    STDMETHOD(WriteString)(const std::wstring& strString) PURE;
    STDMETHOD(WriteString)(const std::wstring_view& strString) PURE;
    STDMETHOD(WriteString)(const WCHAR* szString) PURE;
    STDMETHOD(WriteCharArray)(const WCHAR* szArray, DWORD dwCharCount) PURE;

    STDMETHOD(WriteString)(const std::string& strString) PURE;
    STDMETHOD(WriteString)(const std::string_view& strString) PURE;
    STDMETHOD(WriteString)(const CHAR* szString) PURE;
    STDMETHOD(WriteCharArray)(const CHAR* szArray, DWORD dwCharCount) PURE;

    STDMETHOD(WriteAttributes)(DWORD dwAttibutes) PURE;

    STDMETHOD(WriteFileTime)(FILETIME fileTime) PURE;
    STDMETHOD(WriteFileTime)(LONGLONG fileTime) PURE;
    STDMETHOD(WriteTimeStamp)(time_t tmStamp) PURE;
    STDMETHOD(WriteTimeStamp)(tm tmStamp) PURE;

    STDMETHOD(WriteFileSize)(LARGE_INTEGER fileSize) PURE;
    STDMETHOD(WriteFileSize)(ULONGLONG fileSize) PURE;
    STDMETHOD(WriteFileSize)(DWORD nFileSizeHigh, DWORD nFileSizeLow) PURE;

    STDMETHOD(WriteInteger)(DWORD dwInteger) PURE;
    STDMETHOD(WriteInteger)(LONGLONG dw64Integer) PURE;
    STDMETHOD(WriteInteger)(ULONGLONG dw64Integer) PURE;

    STDMETHOD(WriteBytes)(const BYTE pSHA1[], DWORD dwLen) PURE;
    STDMETHOD(WriteBytes)(const CBinaryBuffer& Buffer) PURE;

    STDMETHOD(WriteBool)(bool bBoolean) PURE;

    STDMETHOD(WriteEnum)(DWORD dwEnum) PURE;
    STDMETHOD(WriteEnum)(DWORD dwEnum, const WCHAR* EnumValues[]) PURE;

    STDMETHOD(WriteFlags)(DWORD dwFlags) PURE;
    STDMETHOD(WriteFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[], WCHAR cSeparator) PURE;

    STDMETHOD(WriteExactFlags)(DWORD dwFlags) PURE;
    STDMETHOD(WriteExactFlags)(DWORD dwFlags, const FlagsDefinition FlagValues[]) PURE;

    STDMETHOD(WriteGUID)(const GUID& guid) PURE;

    STDMETHOD(WriteXML)(const WCHAR* szString) PURE;
    STDMETHOD(WriteXML)(const CHAR* szString) PURE;
    STDMETHOD(WriteXML)(const WCHAR* szArray, DWORD dwCharCount) PURE;
    STDMETHOD(WriteXML)(const CHAR* szArray, DWORD dwCharCount) PURE;

    STDMETHOD(AbandonRow)() PURE;
    STDMETHOD(AbandonColumn)() PURE;

    virtual HRESULT WriteEndOfLine() PURE;

#ifndef __cplusplus_cli

    using wformat_iterator = std::back_insert_iterator<Buffer<WCHAR, MAX_PATH>>;
    using wformat_args = fmt::format_args_t<wformat_iterator, wchar_t>;

    template <typename... Args>
    HRESULT WriteFormated(const std::wstring_view& szFormat, Args&&... args)
    {
        using context = fmt::basic_format_context<wformat_iterator, WCHAR>;
        return WriteFormated_(szFormat, fmt::make_format_args<context>(args...));
    }

    using format_iterator = std::back_insert_iterator<Buffer<CHAR, MAX_PATH>>;
    using format_args = fmt::format_args_t<format_iterator, CHAR>;

    template <typename... Args>
    HRESULT WriteFormated(const std::string_view& szFormat, Args&&... args)
    {
        using context = fmt::basic_format_context<format_iterator, CHAR>;
        return WriteFormated_(szFormat, fmt::make_format_args<context>(args...));
    }

protected:
    STDMETHOD(WriteFormated_)(const std::wstring_view& szFormat, wformat_args args) PURE;
    STDMETHOD(WriteFormated_)(const std::string_view& szFormat, format_args args) PURE;

#endif
};
}  // namespace TableOutput
using ITableOutput = TableOutput::IOutput;
}  // namespace Orc

#pragma managed(pop)
