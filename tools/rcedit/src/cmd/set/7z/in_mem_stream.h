//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <cstdint>
#include <string_view>

#include <7zip/7zip.h>
#include <7zip/CPP/Common/MyCom.h>
#include <7zip/IStream.h>

namespace rcedit {

class InMemStream
    : public IInStream
    , public CMyUnknownImp
{
public:
    const size_t kMaxBufferSize = -1;

    MY_UNKNOWN_IMP2( ISequentialInStream, IInStream )

    explicit InMemStream( std::string_view buffer );
    STDMETHOD( Read )( void* data, UInt32 size, UInt32* processedSize );
    STDMETHOD( Seek )( Int64 offset, UInt32 seekOrigin, UInt64* newPosition );

private:
    std::string_view m_buffer;
    size_t m_pos;
};

}  // namespace rcedit
