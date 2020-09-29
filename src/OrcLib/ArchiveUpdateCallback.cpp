//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <7zip/7zip.h>

#include "7zip/Archive/IArchive.h"
#include "7zip/ICoder.h"
#include "7zip/IPassword.h"

#include "PropVariant.h"

#include "InByteStreamWrapper.h"
#include "CryptoHashStream.h"
#include "PipeStream.h"
#include "MemoryStream.h"
#include "ByteStreamVisitor.h"

#include "ArchiveUpdateCallback.h"

using namespace std;
using namespace lib7z;

using namespace Orc;

namespace {

class ArchiveItemStreamState : public Orc::ByteStreamVisitor
{
public:
    void Visit(PipeStream& stream) override { m_isPipeStream = true; }

    void Visit(MemoryStream& stream) override { m_isMemStream = true; }

    bool IsPipeStream() const { return m_isPipeStream; }
    bool IsMemoryStream() const { return m_isMemStream; }

private:
    bool m_isPipeStream = false;
    bool m_isMemStream = false;
};

}  // namespace

ArchiveUpdateCallback::ArchiveUpdateCallback(
    Archive::ArchiveItems& items,
    Archive::ArchiveIndexes& indexes,
    bool bFinal,
    Archive::ArchiveCallback pCallback,
    const std::wstring& pwd)
    : m_refCount(0)
    , m_Items(items)
    , m_curIndex(0L)
    , m_curIndexInArchive((UInt32)indexes.size())
    , m_Callback(pCallback)
    , m_Indexes(indexes)
    , m_Password(pwd)
    , m_bFinal(bFinal)
{
    for (size_t i = 0; i < m_Items.size(); ++i)
    {
        if (m_Items[i].Stream == nullptr)
        {
            continue;
        }

        ArchiveItemStreamState itemState;
        m_Items[i].Stream->Accept(itemState);
        if (itemState.IsPipeStream())
        {
            m_pipeStreamIndexes.push_back(i);
        }
        else if (itemState.IsMemoryStream())
        {
            m_memoryStreamIndexes.push_back(i);
        }
    }
}

ArchiveUpdateCallback::~ArchiveUpdateCallback() {}

size_t ArchiveUpdateCallback::DeviseNextBestAddition()
{
    for (auto it = std::begin(m_pipeStreamIndexes); it != std::end(m_pipeStreamIndexes); ++it)
    {
        const auto& item = m_Items[*it];
        if (item.currentStatus != Archive::ArchiveItem::Status::Waiting)
        {
            continue;
        }

        const auto pipeStream = std::static_pointer_cast<PipeStream>(item.Stream);
        if (!pipeStream->DataIsAvailable())
        {
            continue;
        }

        const auto index = *it;
        m_pipeStreamIndexes.erase(it);
        return index;
    }

    for (auto it = std::begin(m_memoryStreamIndexes); it != std::end(m_memoryStreamIndexes); ++it)
    {
        if (m_bFinal)
        {
            const auto index = *it;
            m_memoryStreamIndexes.erase(it);
            return index;
        }

        const auto& item = m_Items[*it];
        if (item.currentStatus != Archive::ArchiveItem::Status::Waiting)
        {
            continue;
        }

        const auto memoryStream = std::static_pointer_cast<MemoryStream>(item.Stream);
        if (memoryStream->GetSize() == 0)
        {
            continue;
        }

        const auto index = *it;
        m_memoryStreamIndexes.erase(it);
        return index;
    }

    return (size_t)-1;
}

STDMETHODIMP ArchiveUpdateCallback::QueryInterface(REFIID iid, void** ppvObject)
{
    if (iid == __uuidof(IUnknown))
    {
        *ppvObject = reinterpret_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_IArchiveUpdateCallback)
    {
        *ppvObject = static_cast<IArchiveUpdateCallback*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_ICryptoGetTextPassword2)
    {
        *ppvObject = static_cast<ICryptoGetTextPassword2*>(this);
        AddRef();
        return S_OK;
    }

    if (iid == IID_ICompressProgressInfo)
    {
        *ppvObject = static_cast<ICompressProgressInfo*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ArchiveUpdateCallback::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_refCount));
}

STDMETHODIMP_(ULONG) ArchiveUpdateCallback::Release()
{
    ULONG res = static_cast<ULONG>(InterlockedDecrement(&m_refCount));
    if (res == 0)
    {
        delete this;
    }
    return res;
}

STDMETHODIMP ArchiveUpdateCallback::SetTotal(UInt64)
{
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::SetCompleted(const UInt64*)
{
    return S_OK;
}

STDMETHODIMP
ArchiveUpdateCallback::GetUpdateItemInfo(UInt32 index, Int32* newData, Int32* newProperties, UInt32* indexInArchive)
{
    // Setting info for Create mode (vs. Append mode).

    if (m_Indexes.find(index) != end(m_Indexes))
    {
        if (newData != NULL)
            *newData = 0;
        if (newProperties != NULL)
            *newProperties = 0;
        if (indexInArchive != NULL)
            *indexInArchive = index;  // TODO: UInt32.Max
    }
    else
    {
        if (newData != NULL)
            *newData = 1;
        if (newProperties != NULL)
            *newProperties = 1;
        if (indexInArchive != NULL)
            *indexInArchive = (UInt32)-1;  // TODO: UInt32.Max
    }
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value)
{
    concurrency::critical_section::scoped_lock sl(m_cs);

    CPropVariant prop;

    if (propID == kpidIsAnti)
    {
        prop = false;
        prop.Detach(value);
        return S_OK;
    }

    SYSTEMTIME st;
    GetSystemTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);

    if (index >= m_Items.size())
        return E_INVALIDARG;

    switch (propID)
    {
        case kpidTimeType:
            prop = NFileTimeType::kWindows;
            prop.Detach(value);
            return S_OK;
        default:
            break;
    }

    auto it = m_Indexes.find(index);
    if (it != end(m_Indexes))
    {
        // item is already "in the pipe"
        switch (propID)
        {
            case kpidPath:
                prop = m_Items[it->second].NameInArchive.c_str();
                break;
            case kpidIsDir:
                prop = false;
                break;
            case kpidSize:
                if (m_Items[it->second].Stream != nullptr)
                    prop = m_Items[it->second].Stream->GetSize();
                else
                    prop = m_Items[it->second].Size;

                if (prop.ulVal == 0L)
                    prop.ulVal = (ULONG)-1;

                break;
            case kpidAttrib:
                prop = (UInt32)FILE_ATTRIBUTE_NORMAL;
                break;
            case kpidCTime:
                prop = ft;
                break;
            case kpidATime:
                prop = ft;
                break;
            case kpidMTime:
                prop = ft;
                break;
        }
        m_Items[it->second].currentStatus = Archive::ArchiveItem::Status::Selected;
        prop.Detach(value);
    }
    else
    {
        if (m_Items[index].currentStatus == Archive::ArchiveItem::Status::Waiting)
        {
            // Item is just the "next" in line selected  by 7zip
            // we need to devise if we have a "better" one to add
            auto suggested = DeviseNextBestAddition();

            if (suggested != (size_t)-1 && suggested != index)
            {
                std::swap(m_Items[index], m_Items[suggested]);
            }
        }

        switch (propID)
        {
            case kpidPath:
                prop = m_Items[index].NameInArchive.c_str();
                break;
            case kpidIsDir:
                prop = false;
                break;
            case kpidSize:
                if (m_Items[index].Stream != nullptr)
                    prop = m_Items[index].Stream->GetSize();
                else
                    prop = m_Items[index].Size;

                if (prop.ulVal == 0L)
                    prop.ulVal = (ULONG)-1;

                break;
            case kpidAttrib:
                prop = (UInt32)FILE_ATTRIBUTE_NORMAL;
                break;
            case kpidCTime:
                prop = ft;
                break;
            case kpidATime:
                prop = ft;
                break;
            case kpidMTime:
                prop = ft;
                break;
        }
        m_Items[index].currentStatus = Archive::ArchiveItem::Status::Selected;
        prop.Detach(value);
    }
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream** inStream)
{
    concurrency::critical_section::scoped_lock sl(m_cs);

    if (inStream == nullptr)
        return E_POINTER;

    if (index >= m_Items.size())
        return E_INVALIDARG;

    if (m_Items[index].Stream == nullptr)
    {
        return E_POINTER;
    }

    CComQIPtr<ISequentialInStream, &IID_ISequentialInStream> fileStream =
        new InByteStreamWrapper(m_Items[index].Stream);

    if (fileStream == nullptr)
        return E_OUTOFMEMORY;

    *inStream = fileStream.Detach();

    m_Items[index].currentStatus = Archive::ArchiveItem::Status::Processing;

    m_curIndex = index;
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::SetOperationResult(Int32 operationResult)
{
    concurrency::critical_section::scoped_lock sl(m_cs);

    Archive::ArchiveItem& item = m_Items[m_curIndex];
    if (operationResult != NArchive::NUpdate::NOperationResult::kOK)
    {
        HRESULT hr = E_FAIL;
        spdlog::error(L"Failed operation on: '{}' (code: {:#x})", m_Items[m_curIndex].NameInArchive, operationResult);
        if (item.m_archivedCallback)
        {
            item.m_archivedCallback(hr);
            return hr;
        }
    }

    spdlog::debug(L"Archive of '{}' succeed", m_Items[m_curIndex].NameInArchive);

    item.Index = m_curIndexInArchive;
    m_Indexes[m_curIndexInArchive] = m_curIndex;
    m_curIndexInArchive++;

    m_Items[m_curIndex].currentStatus = Archive::ArchiveItem::Status::Done;

    if (item.m_archivedCallback)
    {
        item.m_archivedCallback(S_OK);
    }

    if (m_Callback)
    {
        m_Callback(item);
    }

    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::CryptoGetTextPassword2(Int32* passwordIsDefined, BSTR* password)
{
    if (m_Password.empty())
    {
        *passwordIsDefined = 0;
        *password = SysAllocString(L"");
    }
    else
    {
        *passwordIsDefined = 1;
        *password = SysAllocString(m_Password.c_str());
    }
    return *password != 0 ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP ArchiveUpdateCallback::SetRatioInfo(const UInt64* inSize, const UInt64* outSize)
{
    DBG_UNREFERENCED_PARAMETER(inSize);
    DBG_UNREFERENCED_PARAMETER(outSize);
    return S_OK;
}
