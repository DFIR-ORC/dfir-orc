//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <memory>
#include <system_error>
#include <functional>

namespace Orc {

class ByteStream;

namespace Archive {

class Item
{
public:
    using CompressedCallback = std::function<void(const std::error_code&)>;

    Item(std::shared_ptr<ByteStream> stream, std::wstring nameInArchive, CompressedCallback cb = {});

    const std::wstring& NameInArchive() const;
    std::shared_ptr<ByteStream>& Stream();
    size_t Size() const;
    const CompressedCallback& CompressedCb() const;

private:
    std::shared_ptr<ByteStream> m_stream;
    std::wstring m_nameInArchive;
    CompressedCallback m_compressedCb;
};

}  // namespace Archive
}  // namespace Orc
