//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "7zip/CPP/7zip/IStream.h"
#include "7zip/CPP/Common/MyCom.h"

#include <memory>

namespace Orc {

class ByteStream;

namespace Archive {

class OutStreamAdapter
    : public IOutStream
    , public CMyUnknownImp
{
public:
    OutStreamAdapter(std::shared_ptr<ByteStream> outByteStream)
        : m_stream(std::move(outByteStream))
    {
    }

    Z7_IFACES_IMP_UNK_2(ISequentialOutStream, IOutStream)

private:
    std::shared_ptr<ByteStream> m_stream;
};

}  // namespace Archive
}  // namespace Orc
