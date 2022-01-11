//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "Archive/7z/ArchiveUpdateCallback.h"

#include "Archive/7z/InStreamAdapter.h"
#include "Utils/Result.h"
#include "ByteStream.h"

using namespace Orc::Archive;
using namespace Orc;

namespace {

FILETIME NowTimestamp()
{
    SYSTEMTIME st;
    GetSystemTime(&st);

    FILETIME ft;
    BOOL ret = SystemTimeToFileTime(&st, &ft);
    if (ret == FALSE)
    {
        Log::Error("Failed to convert SystemTime to FileTime [{}]", LastWin32Error());
        return {0};
    }

    return ft;
}

}  // namespace

namespace Orc {
namespace Archive {

ArchiveUpdateCallback::ArchiveUpdateCallback(Items items, std::wstring password, uint64_t numberOfInputArchiveItems)
    : m_newItems(std::move(items))
    , m_password(std::move(password))
    , m_numberOfInputArchiveItems(numberOfInputArchiveItems)
{
}

ArchiveUpdateCallback::~ArchiveUpdateCallback()
{
    std::error_code ec;
    for (auto& item : m_newItems)
    {
        if (item->CompressedCb())
        {
            item->CompressedCb()(ec);
        }
    }
}

bool ArchiveUpdateCallback::IsNewItem(UInt32 index) const
{
    return index >= m_numberOfInputArchiveItems;
}

size_t ArchiveUpdateCallback::ToNewItemIndex(UInt32 index) const
{
    _ASSERT(IsNewItem(index));
    const auto newItemIndex = index - m_numberOfInputArchiveItems;
    _ASSERT(newItemIndex < m_newItems.size());
    return newItemIndex;
}

Item* ArchiveUpdateCallback::GetItem(UInt32 index)
{
    if (IsNewItem(index) == false)
    {
        return {};
    }

    const auto newItemIndex = index - m_numberOfInputArchiveItems;
    if (newItemIndex > m_newItems.size())
    {
        return {};
    }

    return m_newItems[newItemIndex].get();
}

const Item* ArchiveUpdateCallback::GetItem(UInt32 index) const
{
    if (IsNewItem(index) == false)
    {
        return {};
    }

    const auto newItemIndex = index - m_numberOfInputArchiveItems;
    if (newItemIndex > m_newItems.size())
    {
        return {};
    }

    return m_newItems[newItemIndex].get();
}

std::unique_ptr<Item> ArchiveUpdateCallback::TakeItem(UInt32 index)
{
    if (IsNewItem(index) == false)
    {
        return {};
    }

    const auto newItemIndex = ToNewItemIndex(index);
    if (newItemIndex > m_newItems.size())
    {
        return {};
    }

    return std::move(m_newItems[newItemIndex]);
    m_newItems[newItemIndex] = {};
}

STDMETHODIMP
ArchiveUpdateCallback::GetUpdateItemInfo(UInt32 index, Int32* pNewData, Int32* pNewProperties, UInt32* pIndexInArchive)
{
    if (IsNewItem(index))
    {
        if (pNewData)
        {
            *pNewData = 1;
        }

        if (pNewProperties)
        {
            *pNewProperties = 1;
        }

        if (pIndexInArchive)
        {
            *pIndexInArchive = -1;
        }
    }
    else
    {
        if (pNewData)
        {
            *pNewData = 0;
        }

        if (pNewProperties)
        {
            *pNewProperties = 0;
        }

        if (pIndexInArchive)
        {
            *pIndexInArchive = index;
        }
    }

    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value)
{
    const auto item = GetItem(index);
    if (item == nullptr)
    {
        return E_FAIL;
    }

    NWindows::NCOM::CPropVariant prop;
    switch (propID)
    {
        case kpidIsAnti:
            prop = false;
            break;

        case kpidTimeType:
            prop = NFileTimeType::kWindows;
            break;

        case kpidPath:
            prop = item->NameInArchive().c_str();
            break;

        case kpidIsDir:
            prop = false;
            break;

        case kpidSize:
            prop = item->Size();
            break;

        case kpidAttrib:
            prop = (UInt32)FILE_ATTRIBUTE_NORMAL;
            break;

        case kpidCTime:
        case kpidMTime:
        case kpidATime: {
            prop = ::NowTimestamp();
            break;
        }
        default:
            Log::Trace("7z: unhandled property id: {}", propID);
            break;
    }

    prop.Detach(value);
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream** pInStream)
{
    if (pInStream == nullptr)
    {
        return E_POINTER;
    }

    auto item = GetItem(index)->Stream();
    if (item->GetSize() == 0)
    {
        // 7z expects NULL streams for empty files
        *pInStream = NULL;
        return S_OK;
    }

    CComQIPtr<ISequentialInStream, &IID_ISequentialInStream> stream(new InStreamAdapter(item));
    *pInStream = stream.Detach();

    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::SetOperationResult(Int32 operationResult)
{
    //
    // NOTE: It seems there is no way to find the item matching the operationResult call... This could be known by
    // patching the 7zip library which has the item index before making the call. It could be also possible to use the
    // 'SetOperationResult2' callback which seems to be a work in progress.
    //
    // I guess it is unsafe to make the assumption that index will match last 'GetStream' index value. Particularly with
    // multiple threads.
    //

    if (operationResult != NArchive::NUpdate::NOperationResult::kOK)
    {
        Log::Error("Failure code for operation result");
        m_ec.assign(E_FAIL, std::system_category());
    }

    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::CryptoGetTextPassword2(Int32* pPasswordIsDefined, BSTR* pPassword)
{
    //
    // The allocation made by 'SysAllocString' will be free by the 7zip library (it will be wrapped into a CMyComBSTR).
    //

    if (m_password.empty())
    {
        if (pPasswordIsDefined)
        {
            *pPasswordIsDefined = 0;
        }

        if (pPassword)
        {
            *pPassword = SysAllocString(L"");
        }
    }
    else
    {
        if (pPasswordIsDefined)
        {
            *pPasswordIsDefined = 1;
        }

        if (pPassword)
        {
            *pPassword = SysAllocString(m_password.c_str());
        }
    }

    if (pPassword && *pPassword == NULL)
    {
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

}  // namespace Archive
}  // namespace Orc
