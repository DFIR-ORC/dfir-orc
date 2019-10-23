//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "MFTOffline.h"
#include "LogFileWriter.h"

#include "OfflineMFTReader.h"

using namespace Orc;

MFTOffline::MFTOffline(logger pLog, std::shared_ptr<OfflineMFTReader>& volReader)
    : m_pVolReader(volReader)
    , _L_(std::move(pLog))
{
    m_RootUSN = $ROOT_FILE_REFERENCE_NUMBER;
}

MFTOffline::~MFTOffline() {}

HRESULT MFTOffline::Initialize()
{
    m_pFetchReader = std::dynamic_pointer_cast<OfflineMFTReader>(m_pVolReader->ReOpen(
        FILE_READ_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE | FILE_SHARE_WRITE, FILE_RANDOM_ACCESS));

    return S_OK;
}

HRESULT MFTOffline::EnumMFTRecord(MFTUtils::EnumMFTRecordCall pCallBack)
{
    HRESULT hr = E_FAIL;

    if (pCallBack == NULL)
        return E_POINTER;

    LARGE_INTEGER Start = {0};
    LARGE_INTEGER End = {0};

    GetFileSizeEx(m_pVolReader->GetHandle(), &End);

    if (INVALID_SET_FILE_POINTER
        == SetFilePointer(m_pVolReader->GetHandle(), Start.LowPart, &Start.HighPart, FILE_BEGIN))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log::Error(_L_, hr, L"Could not seek to offset %d in MFT file\r\n", Start.LowPart);
        return hr;
    }

    CBinaryBuffer buffer;
    if (!buffer.SetCount(m_pVolReader->GetBytesPerFRS()))
        return E_OUTOFMEMORY;

    ZeroMemory(buffer.GetData(), m_pVolReader->GetBytesPerFRS());

    ULONGLONG ullCurrentIndex = 0;
    ULONGLONG ullCurrentMftIndex = 0;
    ULONGLONG ullLastIndex = (End.QuadPart - Start.QuadPart) / m_pVolReader->GetBytesPerFRS();

    while (ullCurrentIndex < ullLastIndex)
    {
        DWORD dwBytesRead = 0;

        if (!ReadFile(m_pVolReader->GetHandle(), buffer.GetData(), m_pVolReader->GetBytesPerFRS(), &dwBytesRead, NULL))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            log::Error(_L_, hr, L"Could not read in MFT file\r\n");
            return hr;
        }

        if (dwBytesRead != m_pVolReader->GetBytesPerFRS())
        {
            log::Verbose(_L_, L"Reached end of offline MFT\r\n");
            return S_OK;
        }

        PFILE_RECORD_SEGMENT_HEADER pHeader = (PFILE_RECORD_SEGMENT_HEADER)buffer.GetData();

        if ((pHeader->MultiSectorHeader.Signature[0] != 'F') || (pHeader->MultiSectorHeader.Signature[1] != 'I')
            || (pHeader->MultiSectorHeader.Signature[2] != 'L') || (pHeader->MultiSectorHeader.Signature[3] != 'E'))
        {
            log::Verbose(
                _L_,
                L"Skipping... MultiSectorHeader.Signature is not FILE - \"%c%c%c%c\".\r\n",
                pHeader->MultiSectorHeader.Signature[0],
                pHeader->MultiSectorHeader.Signature[1],
                pHeader->MultiSectorHeader.Signature[2],
                pHeader->MultiSectorHeader.Signature[3]);
            continue;
        }

        if (FAILED(hr = pCallBack(ullCurrentMftIndex, buffer)))
        {
            if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
            {
                log::Verbose(_L_, L"INFO: stopping enumeration\r\n");
                return hr;
            }
            log::Verbose(_L_, L"WARNING: Add Record Callback failed\r\n");
        }
        ullCurrentIndex++;
        ullCurrentMftIndex++;
    }

    return S_OK;
}

HRESULT MFTOffline::FetchMFTRecord(std::vector<MFT_SEGMENT_REFERENCE>& frn, MFTUtils::EnumMFTRecordCall pCallBack)
{
    HRESULT hr = E_FAIL;

    if (pCallBack == nullptr)
        return E_POINTER;
    if (frn.empty())
        return S_OK;

    std::sort(
        begin(frn), end(frn), [](const MFT_SEGMENT_REFERENCE& leftRFN, const MFT_SEGMENT_REFERENCE& rigthRFN) -> bool {
            if (leftRFN.SegmentNumberHighPart != rigthRFN.SegmentNumberHighPart)
                return leftRFN.SegmentNumberHighPart < rigthRFN.SegmentNumberHighPart;
            return leftRFN.SegmentNumberLowPart < rigthRFN.SegmentNumberLowPart;
        });

    ULONGLONG ullCurrentIndex = 0;
    // go through each extent of mft and search for the files

    ULONGLONG ullCorrection = 0LL;

    ULONG ulBytesPerFRS = m_pVolReader->GetBytesPerFRS();

    CBinaryBuffer localReadBuffer(true);

    if (!localReadBuffer.CheckCount(ulBytesPerFRS))
        return E_OUTOFMEMORY;

    for (const auto& idx : frn)
    {
        LARGE_INTEGER Index;

        Index.LowPart = idx.SegmentNumberLowPart;
        Index.HighPart = idx.SegmentNumberHighPart;

        Index.QuadPart *= ulBytesPerFRS;

        if (INVALID_SET_FILE_POINTER
            == SetFilePointer(m_pFetchReader->GetHandle(), Index.LowPart, &Index.HighPart, FILE_BEGIN))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            log::Error(_L_, hr, L"Could not seek to offset %I64d in MFT file\r\n", Index.QuadPart);
            continue;
        }

        DWORD dwBytesRead = 0LL;
        if (!ReadFile(
                m_pFetchReader->GetHandle(),
                localReadBuffer.GetData(),
                m_pFetchReader->GetBytesPerFRS(),
                &dwBytesRead,
                NULL))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            log::Error(_L_, hr, L"Could not read %d bytes in MFT file\r\n", m_pFetchReader->GetBytesPerFRS());
            continue;
        }

        PFILE_RECORD_SEGMENT_HEADER pHeader = (PFILE_RECORD_SEGMENT_HEADER)localReadBuffer.GetData();

        if ((pHeader->MultiSectorHeader.Signature[0] != 'F') || (pHeader->MultiSectorHeader.Signature[1] != 'I')
            || (pHeader->MultiSectorHeader.Signature[2] != 'L') || (pHeader->MultiSectorHeader.Signature[3] != 'E'))
        {
            log::Verbose(
                _L_,
                L"Skipping... MultiSectorHeader.Signature is not FILE - \"%c%c%c%c\".\r\n",
                pHeader->MultiSectorHeader.Signature[0],
                pHeader->MultiSectorHeader.Signature[1],
                pHeader->MultiSectorHeader.Signature[2],
                pHeader->MultiSectorHeader.Signature[3]);
            continue;
        }

        MFT_SEGMENT_REFERENCE read_record_frn = {0};
        read_record_frn.SegmentNumberHighPart = pHeader->SegmentNumberHighPart;
        read_record_frn.SegmentNumberLowPart = pHeader->SegmentNumberLowPart;
        read_record_frn.SequenceNumber = pHeader->SequenceNumber;

        if (NtfsSegmentNumber(&read_record_frn) != NtfsSegmentNumber(&idx))
        {
            log::Verbose(
                _L_,
                L"Skipping... %I64X does not match the expected %I64X\r\n",
                NtfsSegmentNumber(&read_record_frn),
                NtfsSegmentNumber(&idx));
            continue;
        }
        if (read_record_frn.SequenceNumber != idx.SequenceNumber)
        {
            log::Verbose(
                _L_,
                L"Skipping... Sequence numbed %d does not match the expected %d\r\n",
                read_record_frn.SequenceNumber,
                idx.SequenceNumber);
            continue;
        }

        MFTUtils::SafeMFTSegmentNumber safeFRN = (ULONGLONG)Index.QuadPart;
        if (FAILED(hr = pCallBack(safeFRN, localReadBuffer)))
        {
            if (hr == E_OUTOFMEMORY)
            {
                log::Error(_L_, hr, L"Add Record Callback failed, not enough memory to continue\r\n");
                return hr;
            }
            else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
            {
                log::Verbose(_L_, L"Add Record Callback asks for enumeration to stop...\r\n");
                return hr;
            }
            log::Verbose(_L_, L"WARNING: Add Record Callback failed\r\n");
        }
    }

    return S_OK;
}

ULONG MFTOffline::GetMFTRecordCount() const
{
    LARGE_INTEGER FileSize;

    FileSize.LowPart = GetFileSize(m_pVolReader->GetHandle(), (LPDWORD)&FileSize.HighPart);

    if (FileSize.QuadPart % m_pVolReader->GetBytesPerFRS())
    {
        log::Verbose(_L_, L"Weird, MFT size is not a multiple of records...");
    }

    return FileSize.LowPart / m_pVolReader->GetBytesPerFRS();
}

MFTUtils::SafeMFTSegmentNumber MFTOffline::GetUSNRoot() const
{
    return m_RootUSN;
}
