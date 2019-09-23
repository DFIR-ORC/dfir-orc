//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "CircularStream.h"

#include "Temporary.h"

#include "LogFileWriter.h"

// CircularStream impl...

CircularStream::CircularStream(LogFileWriter* pW)
    : ByteStream(pW)
{
    m_EventStreamIsComplete = CreateEvent(NULL, TRUE, FALSE, NULL);
    m_ullReadPointer = 0LL;
    m_ullReadCounter = 0LL;
    m_ullWriteCounter = 0LL;
    InitializeCriticalSection(&m_cs);
}

HRESULT CircularStream::OpenForReadWrite(DWORD dwInitialSize, DWORD dwMaximum, DWORD dwTimeOut)
{
    m_dwTimeOut = dwTimeOut;
    return m_Store.InitializeStorage(dwInitialSize, dwMaximum);
}

HRESULT CircularStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    while (1)
    {
        if (m_Store.SizeOfCursor() == 0)
        {
            hr = WaitForMoreDataOrEndOfFile();

            if (hr == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
            {
                LOG(*m_pW, (L"ERROR: Read on the stream did time out\r\n"));
                *pcbBytesRead = -1;
                return hr;
            }
            else if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
            {
                LOG(*m_pW, (L"ERROR: Read got cancelled\r\n"));
                *pcbBytesRead = -1;
                return hr;
            }
        }

        if (m_Store.SizeOfCursor() > 0)
        {
            // all is fine, there is data available from the stream.

            EnterCriticalSection(&m_cs);

            ULONGLONG ToRead = min(m_Store.SizeOfCursor(), cbBytes);

            CopyMemory(pReadBuffer, m_Store.Cursor(), (size_t)ToRead);

            m_Store.ForwardCursorBy((DWORD)ToRead);
            m_Store.SetNewBottom(m_Store.Cursor());

            VERBOSE(*m_pW, (L"Read %ld bytes from the stream\r\n", ToRead));

            if (m_Store.SizeOfCursor() > 0)
            {
                VERBOSE(*m_pW, (L"Data is still available from the stream\r\n", ToRead));
                SetEvent(m_EventDataAvailable);
            }
            else
            {
                VERBOSE(*m_pW, (L"NO Data is available from the stream\r\n", ToRead));
                ResetEvent(m_EventDataAvailable);
            }

            m_ullReadPointer += ToRead;
            m_ullReadCounter += ToRead;
            *pcbBytesRead = ToRead;

            LeaveCriticalSection(&m_cs);
            return S_OK;
        }
        else
        {
            // consider is EOF is reached...
            if (WaitForSingleObject(m_EventStreamIsComplete, 0) == WAIT_OBJECT_0)
            {
                VERBOSE(
                    *m_pW,
                    (L"Read returns the end of the stream (bytes read %I64d)(bytes written %I64d)\r\n",
                     m_ullReadCounter,
                     m_ullWriteCounter));
                *pcbBytesRead = 0;
                return S_OK;
            }
        }
    }
    return S_OK;
}

HRESULT CircularStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (cbBytesToWrite >= m_Store.GetMaximumSize())
    {
        LOG(*m_pW,
            (L"ERROR: Storage is too small (%d bytes) to handle write request of %d bytes\r\n",
             m_Store.GetMaximumSize(),
             cbBytesToWrite));
        return E_OUTOFMEMORY;
    }
    else if (cbBytesToWrite >= m_Store.GetAvailableSize())
    {
        // wait&check if we freed some space
        SleepEx(500, TRUE);
        if (cbBytesToWrite >= m_Store.GetAvailableSize())
        {
            LOG(*m_pW,
                (L"ERROR: Available Storage is too small (%d bytes) to handle write request of %d bytes\r\n",
                 m_Store.GetAvailableSize(),
                 cbBytesToWrite));
            return E_OUTOFMEMORY;
        }
    }

    EnterCriticalSection(&m_cs);

    if (FAILED(hr = m_Store.CheckSpareRoomAndLock((DWORD)cbBytesToWrite)))
    {
        LeaveCriticalSection(&m_cs);
        *pcbBytesWritten = -1;
        return hr;
    }

    VERBOSE(*m_pW, (L"Writing %ld bytes\r\n", cbBytesToWrite));

    CopyMemory(m_Store.EndOfCursor(), pWriteBuffer, (size_t)cbBytesToWrite);
    m_ullWriteCounter += cbBytesToWrite;

    if (FAILED(hr = m_Store.ForwardEndOfCursorBy((DWORD)cbBytesToWrite)))
    {
        LeaveCriticalSection(&m_cs);
        *pcbBytesWritten = -1;
        return hr;
    }

    if (FAILED(hr = m_Store.Unlock()))
    {
        LeaveCriticalSection(&m_cs);
        *pcbBytesWritten = -1;
        return hr;
    }

    LeaveCriticalSection(&m_cs);

    VERBOSE(*m_pW, (L"Added %ld bytes to the stream\r\n", cbBytesToWrite));

    SetEvent(m_EventDataAvailable);

    return S_OK;
}

HRESULT
CircularStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    HRESULT hr = E_FAIL;

    return S_OK;
}

STDMETHODIMP CircularStream::SetSize(ULONG64 ullSize)
{
    return E_NOTIMPL;
}

ULONG64 CircularStream::GetSize()
{
    HRESULT hr = E_FAIL;

    return S_OK;
}

HRESULT CircularStream::Close()
{
    m_ullReadPointer = 0;
    return S_OK;
}

CircularStream::~CircularStream()
{
    Close();
    DeleteCriticalSection(&m_cs);
}
