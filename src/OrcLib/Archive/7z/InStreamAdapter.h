//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
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

class InStreamAdapter
    : public IInStream
    , public IStreamGetSize
    , public CMyUnknownImp
{
public:
    InStreamAdapter(std::shared_ptr<ByteStream> outByteStream, bool readErrorIsNotFailure)
        : m_stream(std::move(outByteStream))
        , m_readErrorIsNotFailure(readErrorIsNotFailure)
    {
    }

    MY_UNKNOWN_IMP2(IInStream, IStreamGetSize)

    // ISequentialInStream
    STDMETHOD(Read)(void* data, UInt32 size, UInt32* processedSize) override;

    // IInStream
    STDMETHOD(Seek)(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override;

    // IStreamGetSize
    STDMETHOD(GetSize)(UInt64* size) override;

private:
    std::shared_ptr<ByteStream> m_stream;
    bool m_readErrorIsNotFailure;
};

}  // namespace Archive
}  // namespace Orc
