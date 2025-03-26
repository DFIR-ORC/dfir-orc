//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "out_mem_stream.h"

#include <string_view>

namespace rcedit {

OutMemStream::OutMemStream( std::vector< uint8_t >& buffer )
    : m_buffer( buffer )
    , m_pos( 0 )
{
}

STDMETHODIMP OutMemStream::Write(
    const void* pInput,
    UInt32 inputSize,
    UInt32* pProcessedSize )
{
    if( pProcessedSize == nullptr || pInput == nullptr ) {
        return E_FAIL;
    }

    if( inputSize == 0 ) {
        *pProcessedSize = 0;
        return S_OK;
    }

    std::string_view input( static_cast< const char* >( pInput ), inputSize );

    const auto requiredSize = m_pos + input.size();
    if( m_buffer.size() < requiredSize ) {
        m_buffer.resize( requiredSize );
    }

    std::copy( std::cbegin( input ), std::cend( input ), &m_buffer[ m_pos ] );
    m_pos += input.size();
    *pProcessedSize = static_cast< UInt32 >( input.size() );

    return S_OK;
}

STDMETHODIMP OutMemStream::Seek(
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

STDMETHODIMP OutMemStream::SetSize( UInt64 newSize )
{
    if( newSize >= OutMemStream::kMaxBufferSize ) {
        return HRESULT_FROM_WIN32( ERROR_INVALID_PARAMETER );
    }

    m_buffer.resize( static_cast< size_t >( newSize ) );
    return S_OK;
}

}  // namespace rcedit
