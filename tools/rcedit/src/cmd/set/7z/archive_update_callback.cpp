//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#include "archive_update_callback.h"

#include <cassert>
#include <filesystem>
#include <system_error>

#include "in_mem_stream.h"

namespace fs = std::filesystem;

namespace rcedit {

guard::ComPtr< ArchiveUpdateCallback > ArchiveUpdateCallback::Create(
    const fs::path& inputPath,
    std::error_code& ec )
{
    guard::ComPtr< ArchiveUpdateCallback > updateCb(
        new ArchiveUpdateCallback() );

    updateCb->Init( inputPath, ec );
    if( ec ) {
        return nullptr;
    }

    return updateCb;
}

guard::ComPtr< ArchiveUpdateCallback > ArchiveUpdateCallback::Create(
    std::string_view content,
    const std::wstring& filename,
    std::error_code& ec )
{
    guard::ComPtr< ArchiveUpdateCallback > updateCb(
        new ArchiveUpdateCallback() );

    updateCb->Init( content, filename, ec );
    if( ec ) {
        return nullptr;
    }

    return updateCb;
}

void ArchiveUpdateCallback::Init(
    const fs::path& inputPath,
    std::error_code& ec )
{
    if( fs::is_directory( inputPath ) ) {
        ec.assign( ERROR_INVALID_PARAMETER, std::system_category() );
        return;
    }

    if( m_fileInfo.Find( inputPath.c_str() ) == false ) {
        ec.assign( ERROR_FILE_NOT_FOUND, std::system_category() );
        return;
    }

    // 7z expects NULL streams for empty files
    if( m_fileInfo.Size == 0 ) {
        m_inStream = NULL;
        return;
    }

    CInFileStream* inFileStream = new CInFileStream;
    m_inStream = inFileStream;

    if( inFileStream->Open( inputPath.wstring().c_str() ) == false ) {
        m_ec.assign( GetLastError(), std::system_category() );
        return;
    }
}

void ArchiveUpdateCallback::Init(
    std::string_view content,
    const std::wstring& filename,
    std::error_code& ec )
{
    m_fileInfo.Size = content.size();
    m_fileInfo.Name = filename.c_str();

    SYSTEMTIME now;
    GetSystemTime( &now );
    if( SystemTimeToFileTime( &now, &m_fileInfo.ATime ) == FALSE ) {
        ec.assign( GetLastError(), std::system_category() );
        return;
    }

    m_fileInfo.CTime = m_fileInfo.MTime = m_fileInfo.ATime;
    m_fileInfo.Attrib = FILE_ATTRIBUTE_NORMAL;

    // 7z expects NULL streams for empty files
    if( m_fileInfo.Size == 0 ) {
        m_inStream = NULL;
        return;
    }

    m_inStream = new InMemStream( content );
}

STDMETHODIMP ArchiveUpdateCallback::GetUpdateItemInfo(
    UInt32 /* index */,
    Int32* newData,
    Int32* newProperties,
    UInt32* indexInArchive )
{
    if( newData ) {
        *newData = BoolToInt( true );
    }

    if( newProperties ) {
        *newProperties = BoolToInt( true );
    }

    if( indexInArchive ) {
        *indexInArchive = (UInt32)(Int32)-1;
    }

    return S_OK;
}

FILETIME TimetToFileTime( time_t time )
{
    FILETIME ft;
    LONGLONG ll = Int32x32To64( time, 10000000 ) + 116444736000000000;
    ft.dwLowDateTime = (DWORD)ll;
    ft.dwHighDateTime = ll >> 32;
    return ft;
};

STDMETHODIMP ArchiveUpdateCallback::GetProperty(
    UInt32 index,
    PROPID propID,
    PROPVARIANT* value )
{
    NWindows::NCOM::CPropVariant prop;

    // TODO: could be something strange here
    if( propID == kpidIsAnti ) {
        prop = false;
        prop.Detach( value );
        return S_OK;
    }

    switch( propID ) {
        case kpidPath:
            prop = m_fileInfo.Name;
            break;
        case kpidIsDir:
            prop = m_fileInfo.IsDir();
            break;
        case kpidSize:
            prop = m_fileInfo.Size;
            break;
        case kpidAttrib:
            prop = static_cast< UINT32 >( m_fileInfo.Attrib );
            break;
        case kpidCTime:
            prop = m_fileInfo.CTime;
            break;
        case kpidMTime:
            prop = m_fileInfo.MTime;
            break;
        case kpidATime:
            prop = m_fileInfo.ATime;
            break;
    }

    prop.Detach( value );
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::GetStream(
    UInt32 index,
    ISequentialInStream** inStream )
{
    assert( index == 0 );

    if( m_inStream ) {
        *inStream = m_inStream.Detach();
    }
    else {
        // 7z expects NULL streams for empty files
        *inStream = NULL;
    }

    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallback::SetOperationResult(
    Int32 /* operationResult */ )
{
    return S_OK;
}

}  // namespace rcedit
