//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "ShadowCopyVolumeReader.h"

#include "BinaryBuffer.h"
#include "Utils/BufferSpan.h"

namespace Orc {

HRESULT ShadowCopyVolumeReader::LoadDiskProperties()
{
    HRESULT hr = E_FAIL;

    if (IsReady())
    {
        return S_OK;
    }

    std::error_code ec;
    m_stream = std::make_unique<Ntfs::ShadowCopy::ShadowCopyStream>(
        std::make_shared<VolumeStreamReader>(m_Shadow.parentVolume), m_Shadow.guid, ec);
    if (ec)
    {
        m_stream.reset();
        return E_FAIL;
    }

    CBinaryBuffer buffer;
    buffer.SetCount(sizeof(PackedGenBootSector));

    m_stream->Seek(SeekDirection::kBegin, 0, ec);
    if (ec)
    {
        Log::Debug("Failed to seek to boot sector [{}]", ec);
        return ToHRESULT(ec);
    }

    m_stream->Read({buffer.GetData(), buffer.GetCount()}, ec);
    if (ec)
    {
        Log::Debug("Failed to read boot sector [{}]", ec);
        return ToHRESULT(ec);
    }

    hr = VolumeReader::ParseBootSector(buffer);
    if (FAILED(hr))
    {
        Log::Debug("Failed to parse boot sector [{}]", SystemError(hr));
        return hr;
    }

    m_bReadyForEnumeration = TRUE;

    return S_OK;
}

HRESULT ShadowCopyVolumeReader::Seek(ULONGLONG offset)
{
    std::error_code ec;

    m_stream->Seek(SeekDirection::kBegin, offset, ec);
    if (ec)
    {
        return ToHRESULT(ec);
    }

    return S_OK;
}

HRESULT
ShadowCopyVolumeReader::Read(ULONGLONG offset, CBinaryBuffer& buffer, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    Log::Trace("VSS: read (offset: {:#016x}, length: {})", offset, ullBytesToRead);

    HRESULT hr = Seek(offset);
    if (FAILED(hr))
    {
        return hr;
    }

    return Read(buffer, ullBytesToRead, ullBytesRead);
}

HRESULT ShadowCopyVolumeReader::Read(CBinaryBuffer& buffer, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    std::error_code ec;
    if (buffer.OwnsBuffer())
    {
        buffer.SetCount(ullBytesToRead);
    }

    ullBytesRead = m_stream->Read(BufferSpan(buffer.GetData(), ullBytesToRead), ec);
    if (ec)
    {
        return ToHRESULT(ec);
    }

    return S_OK;
}

std::shared_ptr<VolumeReader> ShadowCopyVolumeReader::ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags)
{
    m_Shadow.parentVolume = m_Shadow.parentVolume->ReOpen(dwDesiredAccess, dwShareMode, dwFlags);
    return shared_from_this();
}

}  // namespace Orc
