//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"
#include "ByteStream.h"

#include "Temporary.h"

#include "LogFileWriter.h"

#include "CryptoHashStream.h"
#include "XORStream.h"
#include "OutputSpec.h"

#include "FileStream.h"
#include "PipeStream.h"

#include "InByteStreamWrapper.h"
#include "IStreamWrapper.h"
#include "ISequentialStreamWrapper.h"

using namespace std;

using namespace Orc;

ByteStream::~ByteStream() {}

/*
    CByteStream::CopyTo

    Copies the current stream to another byte stream
*/
HRESULT ByteStream::CopyTo(__in const std::shared_ptr<ByteStream>& pOutStream, __out_opt PULONGLONG pcbBytesWritten)
{
    if (pOutStream == nullptr)
        return E_POINTER;
    return CopyTo(*pOutStream, DEFAULT_READ_SIZE, pcbBytesWritten);
}

HRESULT ByteStream::CopyTo(__in ByteStream& outStream, __out_opt PULONGLONG pcbBytesWritten)
{
    return CopyTo(outStream, DEFAULT_READ_SIZE, pcbBytesWritten);
}

HRESULT ByteStream::CopyTo(
    __in const std::shared_ptr<ByteStream>& pOutStream,
    const ULONGLONG ullChunk,
    __out_opt PULONGLONG pcbBytesWritten)
{
    if (pOutStream == nullptr)
        return E_POINTER;
    return CopyTo(*pOutStream, ullChunk, pcbBytesWritten);
}

HRESULT ByteStream::CopyTo(__in ByteStream& outStream, const ULONGLONG ullChunk, __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;
    ULONG64 qwBytesCopied = 0;
    ULONGLONG ullBytesRead = 0;
    ULONGLONG ullBytesWritten = 0;
    CBinaryBuffer buffer(true);

    buffer.SetCount(static_cast<size_t>(ullChunk));

    if (pcbBytesWritten)
        *pcbBytesWritten = 0LL;

    // Reset the source stream's file pointer to the begining

    if (FAILED(hr = SetFilePointer(0, SEEK_SET, NULL)))
        return hr;

    while (qwBytesCopied < GetSize())
    {
        if (FAILED(hr = Read(buffer.GetData(), buffer.GetCount(), &ullBytesRead)))
            return hr;

        if (ullBytesRead == 0LL)
            break;  // When read returns 0 bytes read, we have reached the end of the file

        if (FAILED(hr = outStream.Write(buffer.GetData(), ullBytesRead, &ullBytesWritten)))
            return hr;

        _ASSERT(ullBytesRead == ullBytesWritten);
        qwBytesCopied += ullBytesRead;

        if (pcbBytesWritten)
            *pcbBytesWritten = qwBytesCopied;
    }

    return S_OK;
}

std::shared_ptr<ByteStream> ByteStream::_GetHashStream()
{
    return nullptr;
}
std::shared_ptr<ByteStream> ByteStream::_GetXORStream()
{
    return nullptr;
}

std::shared_ptr<ByteStream> Orc::ByteStream::GetStream(const logger& pLog, const OutputSpec& output)
{
    auto hr = E_FAIL;
    switch (output.supportedTypes)
    {
        case OutputSpec::Kind::File:
        {
            auto retval = std::make_shared<FileStream>(pLog);

            switch (output.Disposition)
            {
                case OutputSpec::Disposition::Append:
                    if (FAILED(
                            hr = retval->OpenFile(
                                output.Path.c_str(),
                                GENERIC_WRITE,
                                0L,
                                NULL,
                                OPEN_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL)))
                    {
                        log::Error(pLog, hr, L"Failed to open stream for path %s\r\n", output.Path.c_str());
                        return nullptr;
                    }
                    if (FAILED(hr = retval->SetFilePointer(0L, FILE_END, NULL)))
                    {
                        log::Error(
                            pLog, hr, L"Failed to move to the end of the file for path %s\r\n", output.Path.c_str());
                        return nullptr;
                    }
                    return retval;
                case OutputSpec::Disposition::CreateNew:
                    if (FAILED(
                            hr = retval->OpenFile(
                                output.Path.c_str(), GENERIC_WRITE, 0L, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL)))
                    {
                        log::Error(pLog, hr, L"Failed to open stream for path %s\r\n", output.Path.c_str());
                        return nullptr;
                    }
                    return retval;
                case OutputSpec::Disposition::Truncate:
                    if (FAILED(
                            hr = retval->OpenFile(
                                output.Path.c_str(),
                                GENERIC_WRITE,
                                0L,
                                NULL,
                                CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL)))
                    {
                        log::Error(pLog, hr, L"Failed to open stream for path %s\r\n", output.Path.c_str());
                        return nullptr;
                    }
                    return retval;
                default:
                    return nullptr;
            }
        }
        case OutputSpec::Kind::Pipe:
        {
            auto retval = std::make_shared<PipeStream>(pLog);
            if (FAILED(hr = retval->CreatePipe()))
            {
                log::Error(pLog, hr, L"Failed to create pipe\r\n");
                return nullptr;
            }
            return retval;
        }
        default:
            return nullptr;
    }
}

std::shared_ptr<ByteStream> ByteStream::GetHashStream(const std::shared_ptr<ByteStream>& aStream)
{
    std::shared_ptr<CryptoHashStream> hs = std::dynamic_pointer_cast<CryptoHashStream>(aStream);

    if (hs)
        return hs;
    else
        return aStream->_GetHashStream();
}

std::shared_ptr<ByteStream> ByteStream::GetXORStream(const std::shared_ptr<ByteStream>& aStream)
{
    std::shared_ptr<XORStream> hs = std::dynamic_pointer_cast<XORStream>(aStream);

    if (hs)
        return hs;
    else
        return aStream->_GetXORStream();
}

HRESULT ByteStream::Get_IInStream(const std::shared_ptr<ByteStream>& aStream, ::IInStream** pInStream)
{
    CComPtr<IInStream> wrapper = new InByteStreamWrapper(aStream);
    if (wrapper == nullptr)
        return E_OUTOFMEMORY;
    wrapper.CopyTo(pInStream);
    return S_OK;
}

HRESULT ByteStream::Get_IStream(const std::shared_ptr<ByteStream>& aStream, ::IStream** pStream)
{
    CComPtr<IStream> wrapper = new IStreamWrapper(aStream);

    if (wrapper == nullptr)
        return E_OUTOFMEMORY;
    wrapper.CopyTo(pStream);
    return S_OK;
}

HRESULT
ByteStream::Get_ISequentialStream(const std::shared_ptr<ByteStream>& aStream, ISequentialStream** pSequentialStream)
{
    CComPtr<ISequentialStream> wrapper = new ISequentialStreamWrapper(aStream);
    if (wrapper == nullptr)
        return E_OUTOFMEMORY;
    wrapper.CopyTo(pSequentialStream);
    return S_OK;
}
