//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "stdafx.h"

#include "Archive/Item.h"

#include "ByteStream.h"

using namespace Orc::Archive;
using namespace Orc;

Item::Item(std::shared_ptr<ByteStream> stream, std::wstring_view nameInArchive, CompressedCallback cb)
    : m_stream(std::move(stream))
    , m_nameInArchive(std::move(nameInArchive))
    , m_compressedCb(std::move(cb))
{
}

const std::wstring& Item::NameInArchive() const
{
    return m_nameInArchive;
}

std::shared_ptr<ByteStream>& Item::Stream()
{
    return m_stream;
}

uint64_t Item::Size() const
{
    if (m_stream == nullptr)
    {
        return 0;
    }

    return m_stream->GetSize();
}

const Item::CompressedCallback& Item::CompressedCb() const
{
    return m_compressedCb;
}
