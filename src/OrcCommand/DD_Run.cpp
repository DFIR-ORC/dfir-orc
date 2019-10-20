//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "FileStream.h"
#include "CryptoHashStream.h"

#include "SystemDetails.h"
#include "LogFileWriter.h"
#include "TableOutputWriter.h"

#include "DD.h"

#include <chrono>

using namespace Orc;
using namespace Orc::Command::DD;

HRESULT Main::Run()
{
    std::shared_ptr<ByteStream> input_stream;
    std::shared_ptr<FileStream> input_file_stream = std::make_shared<FileStream>(_L_);

    if (auto hr = loc_set.EnumerateLocations(); FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to enumerate locations\r\n");
        return hr;
    }

    //std::vector<std::shared_ptr<Location>> addedLocs;
    //if (auto hr = loc_set.AddLocation(config.strIF.c_str(), addedLocs); FAILED(hr))
    //{
    //    log::Error(_L_, hr, L"Failed to enumerate locations\r\n");
    //    return hr;
    //}

    if (auto hr = input_file_stream->OpenFile(
                config.strIF.c_str(),
                FILE_READ_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL,
                OPEN_EXISTING,
                FILE_FLAG_SEQUENTIAL_SCAN,
        NULL); FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to open %s to read data\r\n", config.strIF.c_str());
        return hr;
    }

    ULONGLONG ullMaxBytes = input_file_stream->GetSize();
    ULONGLONG ullTotalBytes = 0LL;

    if (config.Count.QuadPart == 0LL)
    {
        ullTotalBytes = ullMaxBytes;
    }
    else
    {
        ullTotalBytes = std::min<ULONGLONG>(
            config.Count.QuadPart * config.BlockSize.QuadPart,
            ullMaxBytes - (config.Skip.QuadPart * config.BlockSize.QuadPart));
    }

    if (config.Hash != CryptoHashStream::Algorithm::Undefined)
    {
        auto hash_stream = std::make_shared<CryptoHashStream>(_L_);

        if (auto hr = hash_stream->OpenToRead(config.Hash, input_file_stream); FAILED(hr))
        {
            log::Error(_L_, hr, L"Failed to open hash stream for input\r\n");
            return hr;
        }
        input_stream = hash_stream;
    }
    else
    {
        input_stream = input_file_stream;
    }

    std::vector<std::pair<std::wstring, std::shared_ptr<ByteStream>>> output_streams;
    bool bValidOutput = false;
    for (const auto& out : config.OF)
    {
        std::shared_ptr<ByteStream> out_stream;

        auto out_file_stream = std::make_shared<FileStream>(_L_);

        if (auto hr = out_file_stream->OpenFile(out.c_str(), GENERIC_WRITE, 0L, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); FAILED(hr))
        {
            log::Warning(_L_, hr, L"Failed to open %s to write data\r\n", config.strIF.c_str());
            out_stream = out_file_stream = nullptr;
        }
        else
        {
            if (config.Hash != CryptoHashStream::Algorithm::Undefined)
            {
                auto hash_stream = std::make_shared<CryptoHashStream>(_L_);

                if (auto hr = hash_stream->OpenToWrite(config.Hash, out_file_stream); FAILED(hr))
                {
                    log::Error(_L_, hr, L"Failed to open hash stream for input\r\n");
                    return hr;
                }
                out_stream = hash_stream;
            }
            else
            {
                out_stream = out_file_stream;
            }
            bValidOutput = true;
        }

        output_streams.push_back(std::make_pair(out, out_stream));
    }

    if (!bValidOutput)
    {
        log::Error(_L_, E_INVALIDARG, L"None of the supplied output can be opened, nowhere to write to\r\n");
        return E_INVALIDARG;
    }

    ULONGLONG ullCurrentCursor = 0LLU;

    if (config.Skip.QuadPart > 0LL)
    {
        if (auto hr = input_stream->SetFilePointer(
                    config.BlockSize.QuadPart * config.Skip.QuadPart, FILE_BEGIN, &ullCurrentCursor); FAILED(hr))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to skip %I64d bytes from input stream %s\r\n",
                config.BlockSize.QuadPart * config.Skip.QuadPart,
                config.strIF.c_str());
            return hr;
        }
    }
    if (config.Seek.QuadPart > 0LL)
    {
        for (const auto& out : output_streams)
        {
            if (out.second != nullptr)
            {
                if (config.NoTrunc)
                {
                    if (auto hr = out.second->SetFilePointer(
                            config.BlockSize.QuadPart * config.Seek.QuadPart, FILE_BEGIN, nullptr); FAILED(hr))
                    {
                        log::Warning(
                            _L_,
                            hr,
                            L"Failed to seek %I64d bytes in output stream %s\r\n",
                            config.BlockSize.QuadPart * config.Seek.QuadPart,
                            out.first.c_str());
                    }
                }
                else
                {
                    if (auto hr = out.second->SetSize(config.BlockSize.QuadPart * config.Seek.QuadPart); FAILED(hr))
                    {
                        log::Warning(
                            _L_,
                            hr,
                            L"Failed to truncate %I64d bytes in output stream %s\r\n",
                            config.BlockSize.QuadPart * config.Seek.QuadPart,
                            out.first.c_str());
                    }
                }
            }
        }
    }

    CBinaryBuffer buffer(true);
    buffer.SetCount(config.BlockSize.LowPart);

    auto ullBlockCount = 0LLU;
    auto ullProgressBytes = 0LLU;
    auto ullAbsoluteOffset = config.Skip.QuadPart;
    SHORT Progress = 0;

    auto start = std::chrono::system_clock::now();

    FILETIME theStartTime;
    GetSystemTimeAsFileTime(&theStartTime);

    while (1)
    {
        auto blockStart = std::chrono::system_clock::now();

        ULONGLONG ullRead = 0LL;
        if (auto hr = input_stream->Read(buffer.GetData(), buffer.GetCount(), &ullRead); FAILED(hr))
        {
            if (config.NoError)
            {
                ZeroMemory(buffer.GetData(), buffer.GetCount());
                ullRead = config.BlockSize.QuadPart;
                if (auto hr = input_file_stream->SetFilePointer(
                        config.BlockSize.QuadPart, FILE_CURRENT, NULL); FAILED(hr))
                {
                    log::Error(
                        _L_,
                        hr,
                        L"\nFailed to seek to %I64d bytes offset after error\r\n",
                        buffer.GetCount(),
                        config.strIF.c_str(),
                        ullAbsoluteOffset);
                    break;
                }
            }
            else
            {
                log::Error(
                    _L_,
                    hr,
                    L"\nFailed to read %I64d bytes from input stream %s (absolute offset %I64d)\r\n",
                    buffer.GetCount(),
                    config.strIF.c_str(),
                    ullAbsoluteOffset);
                break;
            }
        }

        if (ullRead == 0LL)
        {
            // Done reading
            log::Verbose(_L_, L"Done reading from input stream\r\n");
            break;
        }
        else
        {
            ullCurrentCursor += ullRead;
            auto ullNewCursor = 0LLU;
            if (auto hr = input_stream->SetFilePointer(ullCurrentCursor, FILE_BEGIN, &ullNewCursor); FAILED(hr))
            {
                log::Error(
                        _L_,
                        hr,
                        L"\nFailed to seek to %I64d offset\r\n",
                        ullCurrentCursor);
            }
            assert(ullNewCursor == ullCurrentCursor);
        }

        for (const auto& output : output_streams)
        {
            ULONGLONG ullWritten = 0LL;
            auto hr = E_FAIL;
            if (output.second != nullptr
                && FAILED(hr = output.second->Write(buffer.GetData(), ullRead, &ullWritten)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Failed to write %I64d bytes to output stream %s\r\n",
                    buffer.GetCount(),
                    output.first.c_str());
            }
        }

        auto nowEnd = std::chrono::system_clock::now();
        std::chrono::nanoseconds blockDuration(nowEnd - blockStart);
        std::chrono::nanoseconds totalDuration(nowEnd - start);

        ullBlockCount++;
        ullProgressBytes += ullRead;
        ullAbsoluteOffset += ullRead;

        double dblTXnow = (((double)config.BlockSize.QuadPart) / blockDuration.count()) * 1000;
        double dblTXaverage = (((double)(config.BlockSize.QuadPart * ullBlockCount)) / totalDuration.count()) * 1000;

        WCHAR szProgress[10];
        if (ullTotalBytes > 0)
        {
            swprintf_s(szProgress, 10, L"%2.0f%% : ", ((double)ullProgressBytes / ullTotalBytes) * 100);
        }
        else
            szProgress[0] = L'\0';

        log::Progress(
            _L_,
            L"%s%I64d blocks of %I64d bytes copied (%I64d Mbytes) (now:%.2f MB/sec, average:%.2f MB/sec)\r",
            szProgress,
            ullBlockCount,
            std::min(config.BlockSize.QuadPart, ullRead),
            ullProgressBytes / (1024 * 1024),
            dblTXnow,
            dblTXaverage);

        if (config.Count.QuadPart > 0LL && ullBlockCount >= config.Count.QuadPart)
        {
            log::Verbose(_L_, L"Read accounted blocks from input stream\r\n");
            break;
        }
    }

    if (auto hr = input_stream->Close(); FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to close input stream %s\r\n", buffer.GetCount(), config.strIF.c_str());
        return hr;
    }

    for (const auto& output : output_streams)
    {
        auto hr = E_FAIL;
        if (output.second != nullptr && FAILED(hr = output.second->Close()))
        {
            log::Error(_L_, hr, L"Failed to close input stream %s\r\n", buffer.GetCount(), output.first.c_str());
        }
    }

    FILETIME theFinishTime;
    GetSystemTimeAsFileTime(&theFinishTime);

    const auto writer = TableOutput::GetWriter(_L_, config.output);

    if (writer != nullptr)
    {
        auto& output = writer->GetTableOutput();

        auto input_hashstream = std::dynamic_pointer_cast<CryptoHashStream>(ByteStream::GetHashStream(input_stream));
        CBinaryBuffer inMD5, inSHA1, inSHA256;
        if (input_hashstream)
        {
            input_hashstream->GetMD5(inMD5);
            input_hashstream->GetSHA1(inSHA1);
            input_hashstream->GetSHA256(inSHA256);
        }

        for (const auto& out : output_streams)
        {
            SystemDetails::WriteComputerName(output);
            output.WriteString(config.strIF.c_str());
            output.WriteString(out.first.c_str());
            output.WriteInteger(config.BlockSize.QuadPart);
            output.WriteInteger(config.Skip.QuadPart);
            output.WriteInteger(config.Seek.QuadPart);
            output.WriteInteger(config.Count.QuadPart);

            output.WriteFileTime(theStartTime);

            output.WriteFileTime(theFinishTime);

            output.WriteInteger(ullTotalBytes);

            if (inMD5.GetCount() > 0)
                output.WriteBytes(inMD5);
            else
                output.WriteNothing();

            if (inSHA1.GetCount() > 0)
                output.WriteBytes(inSHA1);
            else
                output.WriteNothing();

            if (inSHA256.GetCount() > 0)
                output.WriteBytes(inSHA256);
            else
                output.WriteNothing();

            auto hashstream = std::dynamic_pointer_cast<CryptoHashStream>(ByteStream::GetHashStream(out.second));
            if (hashstream)
            {
                CBinaryBuffer MD5, SHA1, SHA256;
                hashstream->GetMD5(MD5);
                hashstream->GetSHA1(SHA1);
                hashstream->GetSHA256(SHA256);

                output.WriteBytes(MD5);
                output.WriteBytes(SHA1);
                output.WriteBytes(SHA256);
            }
            else
            {
                output.WriteNothing();
                output.WriteNothing();
                output.WriteNothing();
            }
            output.WriteEndOfLine();
        }
        writer->Close();
    }

    log::Info(_L_, L"\nDone!\r\n");
    return S_OK;
}
