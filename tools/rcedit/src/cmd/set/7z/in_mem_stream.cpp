//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "in_mem_stream.h"

#include <algorithm>

namespace rcedit {

InMemStream::InMemStream( const std::string_view& buffer )
    : m_buffer( buffer )
    , m_pos( 0 )
{
}

STDMETHODIMP InMemStream::Read(
    void* pOutput,
    UInt32 outputSize,
    UInt32* processedSize )
{
    if( processedSize == nullptr || pOutput == nullptr ) {
        return E_FAIL;
    }

    if( outputSize == 0 || m_pos >= m_buffer.size() ) {
        *processedSize = 0;
        return S_OK;
    }

    const auto maxBuffer =
        std::distance( std::cbegin( m_buffer ) + m_pos, std::cend( m_buffer ) );

    const auto maxCopy =
        std::min( static_cast< ptrdiff_t >( outputSize ), maxBuffer );

    std::memcpy( pOutput, m_buffer.data() + m_pos, maxCopy );

    *processedSize = static_cast< UInt32 >( maxCopy );
    m_pos += *processedSize;

    return S_OK;
}

STDMETHODIMP InMemStream::Seek(
    Int64 offset,
    UInt32 seekOrigin,
    UInt64* newPosition )
{
    switch( seekOrigin ) {
        case STREAM_SEEK_SET:
            break;
        case STREAM_SEEK_CUR:
            offset += m_pos;
            break;
        case STREAM_SEEK_END:
            offset += m_buffer.size() - 1 + offset;
            break;
        default:
            return STG_E_INVALIDFUNCTION;
    }

    if( offset < 0 ) {
        return HRESULT_WIN32_ERROR_NEGATIVE_SEEK;
    }

    m_pos = static_cast< size_t >( offset );
    if( newPosition ) {
        *newPosition = m_pos;
    }

    return S_OK;
}

}  // namespace rcedit
