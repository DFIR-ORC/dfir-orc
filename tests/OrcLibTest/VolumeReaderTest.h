//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "VolumeReader.h"

namespace Orc {

namespace Test {
/*
 * fake implementation of a VolumeReader class for test purpose
 * use callbacks in the test code to manage data that will be provided to clients of the VolumeReader interface
 */
class VolumeReaderTest : public VolumeReader
{
public:
    typedef std::function<HRESULT(ULONGLONG)> SeekCallBack;
    typedef std::function<
        HRESULT(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)>
        ReadCallBack;

    VolumeReaderTest(SeekCallBack* seekCallBack, ReadCallBack* readCallBack);
    virtual ~VolumeReaderTest();

    virtual const WCHAR* ShortVolumeName() { return L"Test"; }

    virtual HRESULT LoadDiskProperties() { return S_OK; }
    virtual HANDLE GetDevice() { return INVALID_HANDLE_VALUE; }

    virtual HRESULT Seek(ULONGLONG offset);
    virtual HRESULT Read(ULONGLONG offset, Orc::CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead);
    virtual HRESULT Read(CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead);

    virtual std::shared_ptr<VolumeReader> ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags);

private:
    virtual std::shared_ptr<VolumeReader> DuplicateReader();

    SeekCallBack* m_SeekCallBack;
    ReadCallBack* m_ReadCallBack;
};
}  // namespace Test
}  // namespace Orc
