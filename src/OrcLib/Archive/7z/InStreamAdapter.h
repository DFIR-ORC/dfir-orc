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

    Z7_IFACES_IMP_UNK_2(IInStream, IStreamGetSize)

    // ISequentialInStream
    STDMETHOD(Read)(void* data, UInt32 size, UInt32* processedSize) override;

private:
    std::shared_ptr<ByteStream> m_stream;
    bool m_readErrorIsNotFailure;
};

}  // namespace Archive
}  // namespace Orc
