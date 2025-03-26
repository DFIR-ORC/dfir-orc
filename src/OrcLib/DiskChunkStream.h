//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once
#include "ByteStream.h"
#include "DiskExtent.h"

#pragma managed(push, off)

namespace Orc {

class DiskChunkStream : public ByteStream
{
public:
    // Name of a device that identify the disk. Also the name used when writing results in output.
    std::wstring m_DiskName;

    // Name of a disk device interface to used when reading the disk. By default, equal to m_DiskName.
    std::wstring m_DiskInterface;

    // m_offset is the disk offset where the chunk start
    ULONG64 m_offset;

    // m_size is chunk size
    ULONG64 m_size;

    // m_chunkPointer is the current offset inside the disk chunk (0 <= m_chunkPointer <= m_size)
    ULONG64 m_chunkPointer = 0;

    // m_diskReader is used to read the disk by opening the disk interface named m_DiskInterface
    std::shared_ptr<CDiskExtent> m_diskReader;

    // The raw reads made with m_diskReader must be aligned on a sector boundary.
    // Because of this, it is sometimes necessary to
    ULONGLONG m_deltaFromDiskToChunkPointer = 0;

    bool m_isOpen = false;

    std::wstring m_description;

    DiskChunkStream(
        std::wstring diskName,
        ULONGLONG offset,
        DWORD size,
        std::wstring description,
        std::wstring diskInterface = L"")
        : ByteStream()
        , m_DiskName(diskName)
        , m_offset(offset)
        , m_size(size)
        , m_description(description)
    {
        if (diskInterface.empty())
        {
            m_DiskInterface = diskName;
        }
        else
        {
            m_DiskInterface = diskInterface;
        }

        m_diskReader.reset(new CDiskExtent(m_DiskInterface));

        Open();
        SetFilePointer(0, FILE_BEGIN, NULL);
    }

    ~DiskChunkStream();

    void Accept(ByteStreamVisitor& visitor) override { return visitor.Visit(*this); };

    std::wstring getSampleName();

    STDMETHOD(Open)();

    // Implement ByteStream interface
    STDMETHOD(IsOpen)();
    STDMETHOD(CanRead)();
    STDMETHOD(CanWrite)();
    STDMETHOD(CanSeek)();

    STDMETHOD(Read_)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write_)
    (__in_bcount(cbBytes) const PVOID pBuffer, __in ULONGLONG cbBytes, __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer);

    STDMETHOD_(ULONG64, GetSize)();
    STDMETHOD(SetSize)(ULONG64);

    STDMETHOD(Close)();

    // Legacy attributes
    void setSectorBySectorMode(boolean mode) {};
    ULONGLONG m_readingTime = 0;
};

}  // namespace Orc

#pragma managed(pop)
