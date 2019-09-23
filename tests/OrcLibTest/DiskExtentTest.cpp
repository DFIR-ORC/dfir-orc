//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "DiskExtentTest.h"

using namespace Orc;
using namespace Orc::Test;

DiskExtentTest::DiskExtentTest(const std::wstring& name, SeekCallBack* seekCallBack, ReadCallBack* readCallBack)
    : m_Name(name)
    , m_SeekCallBack(seekCallBack)
    , m_ReadCallBack(readCallBack)
{
}

DiskExtentTest::~DiskExtentTest() {}

HRESULT DiskExtentTest::Open(DWORD dwShareMode, DWORD dwCreationDisposition, DWORD dwFlags)  // useless here
{
    return S_OK;
}

HRESULT DiskExtentTest::Read(PVOID lpBuf, DWORD dwCount, PDWORD pdwBytesRead)
{
    if (m_ReadCallBack != nullptr)
        return (*m_ReadCallBack)(lpBuf, dwCount);

    return S_OK;
}

HRESULT DiskExtentTest::Seek(LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER pliNewFilePointer, DWORD dwFrom)
{
    if (m_SeekCallBack != nullptr)
        return (*m_SeekCallBack)(liDistanceToMove);

    return S_OK;
}

void DiskExtentTest::Close()  // useless here
{
}
