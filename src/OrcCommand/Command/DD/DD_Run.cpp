//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "FileStream.h"
#include "CryptoHashStream.h"

#include "SystemDetails.h"
#include "TableOutputWriter.h"

#include "DD.h"

#include <chrono>

using namespace Orc;
using namespace Orc::Command::DD;

HRESULT Main::Run()
{
    std::shared_ptr<ByteStream> input_stream;
    std::shared_ptr<FileStream> input_file_stream = std::make_shared<FileStream>();

    HRESULT hr = loc_set.EnumerateLocations();
    if (FAILED(hr))
    {
        Log::Critical(L"Failed to enumerate locations [{}]", SystemError(hr));
        return hr;
    }

    // std::vector<std::shared_ptr<Location>> addedLocs;
    // if (auto hr = loc_set.AddLocation(config.strIF.c_str(), addedLocs); FAILED(hr))
    //{
    //    Log::Error(L"Failed to enumerate locations");
    //    return hr;
    //}

    hr = input_file_stream->OpenFile(
        config.strIF.c_str(),
        FILE_READ_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);

    if (FAILED(hr))
    {
        Log::Critical(L"Failed to open '{}' to read data [{}]", config.strIF, SystemError(hr));
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
        auto hash_stream = std::make_shared<CryptoHashStream>();
        auto hr = hash_stream->OpenToRead(config.Hash, input_file_stream);
        if (FAILED(hr))
        {
            Log::Critical("Failed to open hash stream for input [{}]", SystemError(hr));
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

        auto out_file_stream = std::make_shared<FileStream>();

        if (auto hr = out_file_stream->OpenFile(
                out.c_str(), GENERIC_WRITE, 0L, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            FAILED(hr))
        {
            Log::Warn(L"Failed to open '{}' for write [{}]", config.strIF, SystemError(hr));
            out_stream = out_file_stream = nullptr;
        }
        else
        {
            if (config.Hash != CryptoHashStream::Algorithm::Undefined)
            {
                auto hash_stream = std::make_shared<CryptoHashStream>();

                if (auto hr = hash_stream->OpenToWrite(config.Hash, out_file_stream); FAILED(hr))
                {
                    Log::Error(L"Failed to open hash stream '{}' for input [{}]", out, SystemError(hr));
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
        Log::Error("None of the supplied output can be opened, nowhere to write to");
        return E_INVALIDARG;
    }

    ULONGLONG ullCurrentCursor = 0LLU;

    if (config.Skip.QuadPart > 0LL)
    {
        if (auto hr = input_stream->SetFilePointer(
                config.BlockSize.QuadPart * config.Skip.QuadPart, FILE_BEGIN, &ullCurrentCursor);
            FAILED(hr))
        {
            Log::Error(
                L"Failed to skip {} bytes from input stream '{}' [{}]",
                config.BlockSize.QuadPart * config.Skip.QuadPart,
                config.strIF,
                SystemError(hr));
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
                            config.BlockSize.QuadPart * config.Seek.QuadPart, FILE_BEGIN, nullptr);
                        FAILED(hr))
                    {
                        Log::Warn(
                            L"Failed to seek {} bytes in output stream '{}' [{}]",
                            config.BlockSize.QuadPart * config.Seek.QuadPart,
                            out.first,
                            SystemError(hr));
                    }
                }
                else
                {
                    if (auto hr = out.second->SetSize(config.BlockSize.QuadPart * config.Seek.QuadPart); FAILED(hr))
                    {
                        Log::Warn(
                            L"Failed to truncate {} in bytesoutput stream '{}' [{}]",
                            config.BlockSize.QuadPart * config.Seek.QuadPart,
                            out.first,
                            SystemError(hr));
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
                if (auto hr = input_file_stream->SetFilePointer(config.BlockSize.QuadPart, FILE_CURRENT, NULL);
                    FAILED(hr))
                {
                    Log::Error(
                        L"Failed to seek to {} bytes offset after error with '{}' (absolute offset {})",
                        buffer.GetCount(),
                        config.strIF,
                        ullAbsoluteOffset);
                    break;
                }
            }
            else
            {
                Log::Error(
                    L"Failed to read {} bytes from input stream {} (absolute offset {})",
                    buffer.GetCount(),
                    config.strIF,
                    ullAbsoluteOffset);
                break;
            }
        }

        if (ullRead == 0LL)
        {
            Log::Debug("Done reading from input stream");
            break;
        }
        else
        {
            ullCurrentCursor += ullRead;
            auto ullNewCursor = 0LLU;
            if (auto hr = input_stream->SetFilePointer(ullCurrentCursor, FILE_BEGIN, &ullNewCursor); FAILED(hr))
            {
                Log::Error("Failed to seek to {} offset", ullCurrentCursor);
            }
            assert(ullNewCursor == ullCurrentCursor);
        }

        for (const auto& output : output_streams)
        {
            ULONGLONG ullWritten = 0LL;
            auto hr = E_FAIL;
            if (output.second != nullptr && FAILED(hr = output.second->Write(buffer.GetData(), ullRead, &ullWritten)))
            {
                Log::Error(L"Failed to write {} bytes to output stream '{}'", buffer.GetCount(), output.first);
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

        m_console.Print(
            L"{}% {} blocks of {} bytes copied ({} Mbytes) (now: {} MB/sec, average: {} MB/sec)",
            szProgress,
            ullBlockCount,
            std::min(config.BlockSize.QuadPart, ullRead),
            ullProgressBytes / (1024 * 1024),
            dblTXnow,
            dblTXaverage);

        if (config.Count.QuadPart > 0LL && ullBlockCount >= config.Count.QuadPart)
        {
            Log::Debug("Read accounted blocks from input stream");
            break;
        }
    }

    if (auto hr = input_stream->Close(); FAILED(hr))
    {
        Log::Error(L"Failed to close input stream '{}' [{}]", config.strIF, SystemError(hr));
        return hr;
    }

    for (const auto& output : output_streams)
    {
        auto hr = E_FAIL;
        if (output.second != nullptr && FAILED(hr = output.second->Close()))
        {
            Log::Error(L"Failed to close input stream '{}'", output.first);
        }
    }

    FILETIME theFinishTime;
    GetSystemTimeAsFileTime(&theFinishTime);

    const auto writer = TableOutput::GetWriter(config.output);

    if (writer != nullptr)
    {
        auto& output = *writer;

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

    Log::Info("Done");
    return S_OK;
}
