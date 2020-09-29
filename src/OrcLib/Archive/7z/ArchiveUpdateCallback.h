//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#pragma once

#include <system_error>
#include <optional>

#include <7zip/extras.h>

#include "Archive/Item.h"

namespace Orc {

namespace Archive {

class ArchiveUpdateCallback
    : public IArchiveUpdateCallback
    , public ICryptoGetTextPassword2
    , public CMyUnknownImp
{
public:
    using Items = std::vector<std::unique_ptr<Item>>;

    MY_UNKNOWN_IMP2(IArchiveUpdateCallback, ICryptoGetTextPassword2)

    ArchiveUpdateCallback(Items newItems, std::wstring password, uint64_t numberOfInputArchiveItems = 0);
    ~ArchiveUpdateCallback();

    // IProgress
    STDMETHOD(SetTotal)(UInt64 size) override { return S_OK; }
    STDMETHOD(SetCompleted)(const UInt64* completeValue) override { return S_OK; }

    // IUpdateCallback
    STDMETHOD(GetUpdateItemInfo)
    (UInt32 index, Int32* newData, Int32* newProperties, UInt32* indexInArchive) override;
    STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT* value) override;
    STDMETHOD(GetStream)(UInt32 index, ISequentialInStream** inStream) override;
    STDMETHOD(SetOperationResult)(Int32 operationResult) override;

    // ICryptoGetTextPassword2
    STDMETHOD(CryptoGetTextPassword2)(Int32* passwordIsDefined, BSTR* password) override;

    const std::error_code& GetCallbackError() const { return m_ec; }

private:
    bool IsNewItem(UInt32 index) const;
    size_t ToNewItemIndex(UInt32 index) const;
    Item* GetItem(UInt32 index);
    const Item* GetItem(UInt32 index) const;
    std::unique_ptr<Item> TakeItem(UInt32 index);

    Items m_newItems;
    const std::wstring m_password;
    const uint64_t m_numberOfInputArchiveItems;
    std::error_code m_ec;
};

}  // namespace Archive

}  // namespace Orc
