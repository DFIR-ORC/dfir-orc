//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "JournalingStream.h"

#include "ByteStream.h"
#include "PipeStream.h"
#include "BinaryBuffer.h"

#include <boost/scope_exit.hpp>

using namespace Orc;

struct JRNL_HEADER
{
    CHAR Signature[4];
    DWORD Version = 0x0002;
    JRNL_HEADER()
    {
        Signature[0] = 'J';
        Signature[1] = 'R';
        Signature[2] = 'N';
        Signature[3] = 'L';
    };
};

struct OP_HEADER
{
    CHAR Signature[4];
};

struct OPERATION_HEADER
{
    OP_HEADER OpHeader;
    DWORD dwPadding = 0L;
    ULONGLONG ullPadding = 0LL;

    OPERATION_HEADER()
    {
        OpHeader.Signature[0] = 'I';
        OpHeader.Signature[1] = 'N';
        OpHeader.Signature[2] = 'V';
        OpHeader.Signature[3] = 'L';
    };
};

struct WRITE_HEADER
{
    OP_HEADER OpHeader;
    DWORD dwPadding = 0L;
    ULONGLONG ullLength = 0LL;

    WRITE_HEADER()
    {
        OpHeader.Signature[0] = 'W';
        OpHeader.Signature[1] = 'R';
        OpHeader.Signature[2] = 'I';
        OpHeader.Signature[3] = 'T';
    };
};

enum OP_MOVE_METHOD : DWORD
{
    METHOD_FILE_BEGIN = 0,
    METHOD_FILE_CURRENT = 1,
    METHOD_FILE_END = 2
};

struct SEEK_HEADER
{
    OP_HEADER OpHeader;
    OP_MOVE_METHOD dwMoveMethod = METHOD_FILE_BEGIN;
    ULONGLONG llDistanteToMove = 0LL;

    SEEK_HEADER()
    {
        OpHeader.Signature[0] = 'S';
        OpHeader.Signature[1] = 'E';
        OpHeader.Signature[2] = 'E';
        OpHeader.Signature[3] = 'K';
    };
};

struct CLOSE_HEADER
{
    OP_HEADER OpHeader;
    DWORD dwPadding = 0L;
    ULONGLONG ullPadding = 0LL;

    CLOSE_HEADER()
    {
        OpHeader.Signature[0] = 'C';
        OpHeader.Signature[1] = 'L';
        OpHeader.Signature[2] = 'O';
        OpHeader.Signature[3] = 'S';
    };
};

STDMETHODIMP JournalingStream::Open(const std::shared_ptr<ByteStream>& pChainedStream)
{
    HRESULT hr = E_FAIL;
    if (!pChainedStream)
        return E_POINTER;

    if (pChainedStream->CanWrite() == S_FALSE)
    {
        Log::Error("Chained stream not able to write cannot be used in a journaling stream");
        return E_INVALIDARG;
    }

    m_pChainedStream = pChainedStream;

    JRNL_HEADER Header;

    ULONGLONG ullBytesWritten = 0LL;
    if (FAILED(hr = m_pChainedStream->Write(&Header, sizeof(JRNL_HEADER), &ullBytesWritten)))
    {
        Log::Error("Failed to write journal header to chained stream [{}]", SystemError(hr));
        return hr;
    }

    return S_OK;
}

STDMETHODIMP JournalingStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    DBG_UNREFERENCED_PARAMETER(pReadBuffer);
    DBG_UNREFERENCED_PARAMETER(cbBytes);
    DBG_UNREFERENCED_PARAMETER(pcbBytesRead);

    Log::Error("Cannot read from a journaling stream");

    return HRESULT_FROM_WIN32(ERROR_INVALID_OPERATION);
}

STDMETHODIMP JournalingStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (!m_pChainedStream)
        return E_POINTER;

    CBinaryBuffer buffer;

    if (!buffer.SetCount((size_t)(cbBytesToWrite + sizeof(WRITE_HEADER))))
        return E_OUTOFMEMORY;

    auto& WriteHeader = buffer.Get<WRITE_HEADER>(0);

    static const WRITE_HEADER default_write_header;
    WriteHeader = default_write_header;
    WriteHeader.ullLength = cbBytesToWrite;

    CopyMemory((BYTE*)buffer.GetData() + sizeof(WRITE_HEADER), pWriteBuffer, (size_t)cbBytesToWrite);

    ULONGLONG ullBytesWritten = 0LL;
    if (FAILED(hr = m_pChainedStream->Write(buffer.GetData(), cbBytesToWrite + sizeof(WRITE_HEADER), &ullBytesWritten)))
    {
        Log::Error(L"Failed to write to journal's chained stream [{}]", SystemError(hr));
        return hr;
    }

    if (pcbBytesWritten)
    {
        if (ullBytesWritten > sizeof(WRITE_HEADER))
        {
            *pcbBytesWritten = ullBytesWritten - sizeof(WRITE_HEADER);
            m_ullCurrentPosition += ullBytesWritten - sizeof(WRITE_HEADER);
            if (m_ullCurrentPosition > m_ullStreamSize)
                m_ullStreamSize = m_ullCurrentPosition;
        }
        else
        {
            *pcbBytesWritten = 0LL;
        }
    }
    return S_OK;
}

STDMETHODIMP
JournalingStream::SetFilePointer(__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
{
    HRESULT hr = E_FAIL;

    if (!m_pChainedStream)
        return E_POINTER;

    SEEK_HEADER header;

    header.dwMoveMethod = (OP_MOVE_METHOD)dwMoveMethod;
    header.llDistanteToMove = DistanceToMove;

    ULONGLONG ullBytesWritten = 0LL;
    if (FAILED(hr = m_pChainedStream->Write(&header, sizeof(SEEK_HEADER), &ullBytesWritten)))
    {
        Log::Error(L"Failed to write to journal's chained stream [{}]", SystemError(hr));
        return hr;
    }
    if (ullBytesWritten != sizeof(SEEK_HEADER))
    {
        Log::Warn("Did not write the complete journaled seek operation");
    }

    switch (dwMoveMethod)
    {
        case FILE_BEGIN:
            m_ullCurrentPosition = DistanceToMove;
            break;
        case FILE_CURRENT:
            m_ullCurrentPosition += DistanceToMove;
            break;
        case FILE_END:
            m_ullCurrentPosition = m_ullStreamSize;
            break;
    }

    if (m_ullCurrentPosition > m_ullStreamSize)
        m_ullStreamSize = m_ullCurrentPosition;

    if (pCurrPointer)
    {
        *pCurrPointer = m_ullCurrentPosition;
    }

    return S_OK;
}

STDMETHODIMP_(ULONG64) JournalingStream::GetSize()
{
    return m_ullStreamSize;
}
STDMETHODIMP JournalingStream::SetSize(ULONG64 ullSize)
{
    DBG_UNREFERENCED_PARAMETER(ullSize);
    Log::Warn("SetSize is not implemented in JournalingStream::SetFilePointer");
    return E_NOTIMPL;
}

STDMETHODIMP JournalingStream::Close()
{
    HRESULT hr = E_FAIL;

    CLOSE_HEADER Header;

    ULONGLONG ullBytesWritten = 0LL;
    hr = m_pChainedStream->Write(&Header, sizeof(CLOSE_HEADER), &ullBytesWritten);
    if (FAILED(hr))
    {
        Log::Error(L"Failed to write journal header to chained stream [{}]", SystemError(hr));
        return hr;
    }
    if (FAILED(hr = m_pChainedStream->Close()))
    {
        Log::Error(L"Failed to close journal header chained stream [{}]", SystemError(hr));
        return hr;
    }
    bClosed = true;
    return S_OK;
}

HRESULT JournalingStream::IsStreamJournalized(const std::shared_ptr<ByteStream>& pStream)
{
    HRESULT hr = E_FAIL;

    if (pStream->CanSeek() == S_OK)
    {
        ULONGLONG ullCurrentPosition = 0LL;

        if (FAILED(hr = pStream->SetFilePointer(0LL, FILE_CURRENT, &ullCurrentPosition)))
        {
            Log::Error(L"Failed to save stream's current position [{}]", SystemError(hr));
            return hr;
        }

        BOOST_SCOPE_EXIT(&pStream, &ullCurrentPosition, &hr)
        {
            hr = pStream->SetFilePointer(ullCurrentPosition, FILE_BEGIN, NULL);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to reset stream's position [{}]", SystemError(hr));
            }
        }
        BOOST_SCOPE_EXIT_END

        hr = pStream->SetFilePointer(0LL, FILE_BEGIN, NULL);
        if (FAILED(hr))
        {
            Log::Error("Failed to set stream's current position to stream's beginning [{}]", SystemError(hr));
            return hr;
        }

        CBinaryBuffer buffer;
        if (!buffer.SetCount(sizeof(JRNL_HEADER)))
            return E_POINTER;

        ULONGLONG ullBytesRead = 0LL;
        hr = pStream->Read(buffer.GetData(), sizeof(JRNL_HEADER), &ullBytesRead);
        if (FAILED(hr))
        {
            Log::Error("Failed to read stream's header [{}]", SystemError(hr));
            return hr;
        }

        if (ullBytesRead != sizeof(JRNL_HEADER))
            return S_FALSE;

        JRNL_HEADER default_jrnl_header;

        if (!memcmp(buffer.GetData(), default_jrnl_header.Signature, sizeof(default_jrnl_header.Signature)))
        {
            return S_OK;
        }
    }
    else
    {
        std::shared_ptr<PipeStream> pPipe = std::dynamic_pointer_cast<PipeStream>(pStream);

        if (!pPipe)
        {
            Log::Warn("Unseekable stream (not a pipe), cannot be observed for its format");
        }
        else
        {
            JRNL_HEADER jrnl_header;
            DWORD dwBytesRead = 0L;

            hr = pPipe->Peek(&jrnl_header, sizeof(JRNL_HEADER), &dwBytesRead, NULL);
            if (FAILED(hr))
            {
                Log::Error("Failed to read stream's JRNL header [{}]", SystemError(hr));
                return hr;
            }

            if (dwBytesRead != sizeof(JRNL_HEADER))
                return S_FALSE;

            JRNL_HEADER default_jrnl_header;

            if (!memcmp(jrnl_header.Signature, &default_jrnl_header.Signature, sizeof(default_jrnl_header.Signature))
                && jrnl_header.Version == default_jrnl_header.Version)
            {
                return S_OK;
            }
        }
    }
    return S_FALSE;
}

HRESULT JournalingStream::ReplayJournalStream(
    const std::shared_ptr<ByteStream>& pFromStream,
    const std::shared_ptr<ByteStream>& pToStream)
{
    HRESULT hr = E_FAIL;
    if (!pFromStream || !pToStream)
        return E_POINTER;

    pFromStream->SetFilePointer(0, FILE_BEGIN, NULL);

    JRNL_HEADER jrnl_header, default_jrnl_header;

    ZeroMemory(&jrnl_header, sizeof(jrnl_header));

    ULONGLONG ullBytesRead = 0LL;
    if (FAILED(hr = pFromStream->Read(&jrnl_header, sizeof(jrnl_header), &ullBytesRead)))
    {
        Log::Error(L"Failed to read journal header from FromStream");
        return hr;
    }

    if (memcmp(jrnl_header.Signature, default_jrnl_header.Signature, sizeof(jrnl_header.Signature)))
    {
        Log::Error(
            "Stream signature '{}' does not match expected '{}'", jrnl_header.Signature, default_jrnl_header.Signature);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }

    OPERATION_HEADER OpHeader, default_oper_header;
    ullBytesRead = 0LL;

    _ASSERT(
        sizeof(OPERATION_HEADER) == sizeof(WRITE_HEADER) && sizeof(OPERATION_HEADER) == sizeof(SEEK_HEADER)
        && sizeof(OPERATION_HEADER) == sizeof(CLOSE_HEADER));

    if (FAILED(hr = pFromStream->Read(&OpHeader, sizeof(OpHeader), &ullBytesRead)))
    {
        Log::Error(L"Failed to read operation header from FromStream [{}]", SystemError(hr));
        return hr;
    }

    while (ullBytesRead > 0)
    {
        const DWORD dwOperationSig = *((DWORD*)&OpHeader.OpHeader.Signature);
        switch (dwOperationSig)
        {
            case 0x54495257:

            {
                WRITE_HEADER* pWriteHeader = (WRITE_HEADER*)&OpHeader;

                CBinaryBuffer buffer;

                if (!buffer.SetCount(static_cast<size_t>(pWriteHeader->ullLength)))
                    return E_OUTOFMEMORY;

                ULONGLONG ullAccumulatedBytes = 0LL;
                ULONGLONG ullBytesLeftToRead = pWriteHeader->ullLength;
                while (ullBytesLeftToRead > 0)
                {
                    ullBytesRead = 0LL;
                    if (FAILED(hr = pFromStream->Read(buffer.GetData(), ullBytesLeftToRead, &ullBytesRead)))
                    {
                        Log::Error(L"Failed to read data from FromStream [{}]", SystemError(hr));
                        return hr;
                    }

                    ULONGLONG ullBytesWritten = 0LL;
                    if (FAILED(hr = pToStream->Write(buffer.GetData(), ullBytesRead, &ullBytesWritten)))
                    {
                        Log::Error(L"Failed to write data to ToStream [{}]", SystemError(hr));
                        return hr;
                    }
                    ullAccumulatedBytes += ullBytesRead;
                    ullBytesLeftToRead -= ullBytesRead;
                }
            }

            break;
            case 0x4b454553: {
                SEEK_HEADER* pSeekHeader = (SEEK_HEADER*)&OpHeader;

                ULONG64 ullCurPos = 0LL;
                if (FAILED(
                        hr = pToStream->SetFilePointer(
                            pSeekHeader->llDistanteToMove, pSeekHeader->dwMoveMethod, &ullCurPos)))
                {
                    Log::Error(L"Seek operation failed [{}]", SystemError(hr));
                    return hr;
                }
            }
            break;
            case 0x534f4c43: {
                if (FAILED(hr = pToStream->Close()))
                {
                    Log::Error(L"Close operation failed [{}]", SystemError(hr));
                    return hr;
                }
            }
            break;
            default:
                Log::Error("Invalid operation code {} in operation hreader", OpHeader.OpHeader.Signature);
                break;
        }

        if (FAILED(hr = pFromStream->Read(&OpHeader, sizeof(OpHeader), &ullBytesRead)))
        {
            Log::Error(L"Failed to read journal header from FromStream [{}]", SystemError(hr));
            return hr;
        }
    }
    pFromStream->Close();
    pToStream->Close();
    return S_OK;
}

JournalingStream::~JournalingStream()
{
    if (m_pChainedStream && !bClosed)
    {
        Close();
    }
}
