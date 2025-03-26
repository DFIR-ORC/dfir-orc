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

    MY_UNKNOWN_IMP2(ISequentialOutStream, IOutStream)

    // ISequentialOutStream
    STDMETHOD(Write)(const void* data, UInt32 size, UInt32* processedSize) override;

    // IOutStream
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override;
    STDMETHOD(SetSize)(UInt64 newSize) override;

private:
    std::shared_ptr<ByteStream> m_stream;
};

}  // namespace Archive
}  // namespace Orc
