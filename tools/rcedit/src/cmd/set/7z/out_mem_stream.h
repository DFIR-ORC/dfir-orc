//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <cstdint>
#include <vector>

#include <7zip/7zip.h>
#include <7zip/CPP/Common/MyCom.h>
#include <7zip/IStream.h>

namespace rcedit {

class OutMemStream
    : public IOutStream
    , public CMyUnknownImp
{
public:
    const size_t kMaxBufferSize = -1;

    MY_UNKNOWN_IMP2( ISequentialOutStream, IOutStream )

    explicit OutMemStream( std::vector< uint8_t >& buffer );
    STDMETHOD( Write )( const void* data, UInt32 size, UInt32* processedSize );
    STDMETHOD( Seek )( Int64 offset, UInt32 seekOrigin, UInt64* newPosition );
    STDMETHOD( SetSize )( UInt64 newSize );

private:
    std::vector< uint8_t >& m_buffer;
    size_t m_pos;
};

}  // namespace rcedit
