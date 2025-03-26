//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//            Yann SALAUN
//
#pragma once

#include "OrcLib.h"

#include "Windows.h"
#include <string>

#pragma managed(push, off)

namespace Orc {

class IDiskExtent
{
public:
    virtual const std::wstring& GetName() const PURE;
    virtual ULONGLONG GetStartOffset() const PURE;
    virtual ULONGLONG GetSeekOffset() const PURE;
    virtual ULONGLONG GetLength() const PURE;
    virtual ULONG GetLogicalSectorSize() const PURE;
    virtual HANDLE GetHandle() const PURE;

    virtual HRESULT Open(DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlags) PURE;
    virtual HRESULT Seek(LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER pliNewFilePointer, DWORD dwFrom) PURE;
    virtual HRESULT Read(PVOID lpBuf, DWORD dwCount, PDWORD pdwBytesRead) PURE;
    virtual void Close() PURE;

    virtual ~IDiskExtent() {}

};  // IDiskExtent

}  // namespace Orc

#pragma managed(pop)
