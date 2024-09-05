//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "VolumeReaderTest.h"

using namespace Orc;
using namespace Orc::Test;

VolumeReaderTest::VolumeReaderTest(SeekCallBack* seekCallBack, ReadCallBack* readCallBack)
    : VolumeReader(L"\\")
    , m_SeekCallBack(seekCallBack)
    , m_ReadCallBack(readCallBack)
{
}

VolumeReaderTest::~VolumeReaderTest() {}

HRESULT VolumeReaderTest::Seek(ULONGLONG offset)
{
    if (m_SeekCallBack != nullptr)
        return (*m_SeekCallBack)(offset);

    return S_OK;
}

uint64_t VolumeReaderTest::Position() const
{
    return -1;
}

HRESULT VolumeReaderTest::Read(ULONGLONG offset, CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    data.SetCount(static_cast<size_t>(ullBytesToRead));

    if (m_ReadCallBack != nullptr)
        return (*m_ReadCallBack)(offset, data, ullBytesToRead, ullBytesRead);

    return S_OK;
}

HRESULT VolumeReaderTest::Read(CBinaryBuffer& data, ULONGLONG ullBytesToRead, ULONGLONG& ullBytesRead)
{
    data.SetCount(static_cast<size_t>(ullBytesToRead));

    if (m_ReadCallBack != nullptr)
        return (*m_ReadCallBack)(0, data, ullBytesToRead, ullBytesRead);

    return S_OK;
}

std::shared_ptr<VolumeReader> VolumeReaderTest::ReOpen(DWORD dwDesiredAccess, DWORD dwShareMode, DWORD dwFlags)
{
    return DuplicateReader();
}

std::shared_ptr<VolumeReader> VolumeReaderTest::DuplicateReader()
{
    return std::make_shared<VolumeReaderTest>(m_SeekCallBack, m_ReadCallBack);
}
