//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "IDiskExtent.h"

/*
 * fake implementation of a IDiskExtent class for test purpose
 * use callbacks in the test code to manage data that will be provided to clients of the IDiskExtent interface
 */
class DiskExtentTest : public Orc::IDiskExtent
{
public:
    typedef std::function<HRESULT(LARGE_INTEGER)> SeekCallBack;
    typedef std::function<HRESULT(PVOID, DWORD)> ReadCallBack;

    DiskExtentTest(const std::wstring& name, SeekCallBack* seekCallBack, ReadCallBack* readCallBack);

    // from IDiskExtend
    virtual HRESULT Open(DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlags);
    virtual HRESULT Seek(LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER pliNewFilePointer, DWORD dwFrom);
    virtual HRESULT Read(PVOID lpBuf, DWORD dwCount, PDWORD pdwBytesRead);
    virtual void Close();

    virtual const std::wstring& GetName() const { return m_Name; }
    virtual ULONGLONG GetStartOffset() const { return 0; }
    virtual ULONGLONG GetSeekOffset() const { return 0; }
    virtual ULONGLONG GetLength() const { return 0; }
    virtual ULONG GetLogicalSectorSize() const { return 512; }
    virtual HANDLE GetHandle() const { return INVALID_HANDLE_VALUE; };

    virtual ~DiskExtentTest();

private:
    std::wstring m_Name;
    SeekCallBack* m_SeekCallBack;
    ReadCallBack* m_ReadCallBack;
};