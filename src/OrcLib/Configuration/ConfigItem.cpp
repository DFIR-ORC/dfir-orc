//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "StdAfx.h"

#include "OrcException.h"

#include "ConfigFile.h"
#include "ConfigFileReader.h"
#include "ConfigFileWriter.h"

#include "EmbeddedResource.h"
#include "ParameterCheck.h"

#include "Log/Log.h"


using namespace std;

using namespace Orc;

std::shared_ptr<XmlLiteExtension> ConfigItem::m_xmlLite = nullptr;

ConfigItem::ConfigItem(const ConfigItem& other) noexcept
{
    Type = other.Type;
    Flags = other.Flags;
    Status = other.Status;
    strName = other.strName;
    dwIndex = other.dwIndex;
    dwOrderIndex = other.dwOrderIndex;
    SubItems = other.SubItems;
    strData = other.strData;
}

ConfigItem& ConfigItem::operator=(const ConfigItem& other) noexcept
{
    Type = other.Type;
    Flags = other.Flags;
    Status = other.Status;
    strName = other.strName;
    dwIndex = other.dwIndex;
    dwOrderIndex = other.dwOrderIndex;
    SubItems = other.SubItems;
    strData = other.strData;
    return *this;
}

Orc::ConfigItem::operator DWORD() const
{
    LARGE_INTEGER li = {0, 0};
    if (strData._Starts_with(L"0x") || strData._Starts_with(L"0X"))
    {
        if (auto hr = GetIntegerFromHexaString(strData.c_str(), li); FAILED(hr))
        {
            if (auto hr = GetIntegerFromHexaString(strData.c_str(), li.LowPart); FAILED(hr))
                throw Orc::Exception(
                    Severity::Continue,
                    hr,
                    L"{} is not a valid value for this attribute (does not convert to a DWORD)"sv,
                    strData);
        }
    }
    else if (auto hr = GetFileSizeFromArg(strData.c_str(), li); FAILED(hr))
    {
        throw Orc::Exception(
            Severity::Continue,
            hr,
            L"{} is not a valid value for this attribute (does not convert to a DWORD)",
            strData);
    }

    if (li.HighPart != 0)
    {
        throw Orc::Exception(
            Severity::Continue,
            HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            L"{} is not a valid value for this attribute (does not fit a DWORD)"sv,
            strData);
    }
    return li.LowPart;
}

Orc::ConfigItem::operator DWORD32() const
{
    LARGE_INTEGER li = {0, 0};
    if (strData._Starts_with(L"0x") || strData._Starts_with(L"0X"))
    {
        if (auto hr = GetIntegerFromHexaString(strData.c_str(), li); FAILED(hr))
        {
            if (auto hr = GetIntegerFromHexaString(strData.c_str(), li.LowPart); FAILED(hr))
                throw Orc::Exception(
                    Severity::Continue,
                    hr,
                    L"{} is not a valid value for this attribute (does not convert to a DWORD32)"sv,
                    strData);
        }
    }
    else if (auto hr = GetFileSizeFromArg(strData.c_str(), li); FAILED(hr))
    {
        throw Orc::Exception(
            Severity::Continue,
            hr,
            L"{} is not a valid value for this attribute (does not convert to a DWORD32)"sv,
            strData);
    }

    if (li.HighPart != 0)
    {
        throw Orc::Exception(
            Severity::Continue,
            HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            L"{} is not a valid value for this attribute (does not fit a DWORD32)"sv,
            strData);
    }
    return li.LowPart;
}

Orc::ConfigItem::operator DWORD64() const
{
    LARGE_INTEGER li = {0, 0};
    if (strData._Starts_with(L"0x") || strData._Starts_with(L"0X"))
    {
        if (auto hr = GetIntegerFromHexaString(strData.c_str(), li); FAILED(hr))
        {
            if (auto hr = GetIntegerFromHexaString(strData.c_str(), li.LowPart); FAILED(hr))
                throw Orc::Exception(
                    Severity::Continue,
                    hr,
                    L"{} is not a valid value for this attribute (does not convert to a DWORD64)"sv,
                    strData);
        }
    }
    else if (auto hr = GetFileSizeFromArg(strData.c_str(), li); FAILED(hr))
    {
        throw Orc::Exception(
            Severity::Continue,
            hr,
            L"{} is not a valid value for this attribute (does not convert to a DWORD64)"sv,
            strData);
    }

    return li.QuadPart;
}

ConfigItem& ConfigItem::Item(LPCWSTR szIndex)
{
    auto it = std::find_if(
        begin(SubItems), end(SubItems), [szIndex](const ConfigItem& item) { return !item.strName.compare(szIndex); });
    if (it == end(SubItems))
        throw std::runtime_error("Index not found");
    return *it;
}

const ConfigItem& ConfigItem::Item(LPCWSTR szIndex) const
{
    auto it = std::find_if(
        begin(SubItems), end(SubItems), [szIndex](const ConfigItem& item) { return !item.strName.compare(szIndex); });
    if (it == end(SubItems))
        throw std::runtime_error("Index not found");
    return *it;
}

HRESULT ConfigItem::AddAttribute(std::wstring_view name, DWORD index, ConfigItemFlags flags)
{
    if (index != SubItems.size())
    {
        Log::Error(L"Failed to add attribute '{}'", name);
        return HRESULT_FROM_WIN32(ERROR_INVALID_INDEX);
    }

    ConfigItem newitem;
    newitem.Type = ConfigItem::ATTRIBUTE;
    newitem.strName = name;
    newitem.dwIndex = index;
    newitem.Flags = flags;
    newitem.Status = ConfigItem::MISSING;
    SubItems.push_back(std::move(newitem));

    return S_OK;
}

HRESULT ConfigItem::AddAttribute(LPCWSTR szAttr, DWORD index, ConfigItemFlags flags)
{
    return AddAttribute(std::wstring_view(szAttr), index, flags);
}

HRESULT ConfigItem::AddChildNode(std::wstring_view name, DWORD index, ConfigItemFlags flags)
{
    if (index != SubItems.size())
    {
        Log::Error(L"Failed to add child node '{}' at index {}", name, index);
        return HRESULT_FROM_WIN32(ERROR_INVALID_INDEX);
    }
    ConfigItem newitem;
    newitem.Type = ConfigItem::NODE;
    newitem.strName = name;
    newitem.dwIndex = index;
    newitem.Flags = flags;
    newitem.Status = ConfigItem::MISSING;
    SubItems.push_back(std::move(newitem));
    return S_OK;
}

HRESULT ConfigItem::AddChildNode(LPCWSTR szName, DWORD index, ConfigItemFlags flags)
{
    return AddChildNode(std::wstring_view(szName), index, flags);
}

HRESULT ConfigItem::AddChildNodeList(LPCWSTR szName, DWORD index, ConfigItemFlags flags)
{
    if (index != SubItems.size())
    {
        Log::Error(L"Failed to add child node list '{}' at index {}", szName, index);
        return HRESULT_FROM_WIN32(ERROR_INVALID_INDEX);
    }

    ConfigItem newitem;
    newitem.Type = ConfigItem::NODELIST;
    newitem.strName = szName;
    newitem.dwIndex = index;
    newitem.Flags = flags;
    newitem.Status = ConfigItem::MISSING;
    SubItems.push_back(std::move(newitem));
    return S_OK;
}

HRESULT ConfigItem::AddChild(const ConfigItem& item)
{
    if (item.dwIndex != SubItems.size())
    {
        Log::Error(L"Failed to add child '{}' at index {}", item.strName, item.dwIndex);
        return HRESULT_FROM_WIN32(ERROR_INVALID_INDEX);
    }
    SubItems.push_back(item);
    return S_OK;
}

HRESULT ConfigItem::AddChild(ConfigItem&& item)
{
    if (item.dwIndex != SubItems.size())
    {
        Log::Error(L"Failed to add child '{}' at index {}", item.strName, item.dwIndex);
        return HRESULT_FROM_WIN32(ERROR_INVALID_INDEX);
    }
    SubItems.push_back(item);
    return S_OK;
}

HRESULT ConfigItem::AddChild(std::wstring_view name, NamedAdderFunction adder, DWORD dwIdx)
{
    return adder(*this, dwIdx, name);
}

HRESULT ConfigItem::AddChild(LPCWSTR szName, NamedAdderFunction adder, DWORD dwIdx)
{
    return AddChild(std::wstring_view(szName), adder, dwIdx);
}

HRESULT ConfigItem::ToXml(std::wstring& output) const
{
    HRESULT hr = E_FAIL;
    CComPtr<IStream> stream;

    if (!m_xmlLite)
    {
        m_xmlLite = ExtensionLibrary::GetLibrary<XmlLiteExtension>();
        if (!m_xmlLite)
        {
            Log::Error(L"Failed to load XmlLite");
            return E_FAIL;
        }
    }

    CComPtr<IXmlWriter> pWriter;
    if (FAILED(hr = m_xmlLite->CreateXmlWriter(IID_IXmlWriter, (PVOID*)&pWriter, nullptr)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to instantiate XmlWriter [{}]", SystemError(hr));
        return hr;
    }

    auto memstream = std::make_shared<MemoryStream>();
    if (FAILED(hr = ByteStream::Get_IStream(memstream, &stream)))
    {
        return hr;
    }

    if (FAILED(hr = pWriter->SetOutput(stream)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to set output stream [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = ConfigFileWriter::WriteConfigNode(pWriter, *this)))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to write config item node [{}]", SystemError(hr));
        return hr;
    }

    if (FAILED(hr = pWriter->Flush()))
    {
        XmlLiteExtension::LogError(hr, nullptr);
        Log::Error(L"Failed to flush config item [{}]", SystemError(hr));
        return hr;
    }

    auto buffer = memstream->GetConstBuffer();
    std::copy(buffer.cbegin(), buffer.cend(), std::back_inserter(output));
    return S_OK;
}
