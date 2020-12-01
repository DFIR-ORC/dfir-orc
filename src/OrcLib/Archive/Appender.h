//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <filesystem>
#include <memory>

#include <TemporaryStream.h>

#include "Archive/IArchive.h"
#include "Archive/CompressionLevel.h"
#include "Archive/Item.h"
#include "WinApiHelper.h"

namespace Orc {
namespace Archive {

//
// Appender: Add new items to existing archives for archiver that does not support this feature natively.
//
template <typename T>
class Appender
{
public:
    using Items = IArchive::Items;
    using CompletionHandler = IArchive::CompletionHandler;

    static std::unique_ptr<Appender>
    Create(T&& archiver, std::filesystem::path output, size_t bufferSize, std::error_code& ec)
    {
        const bool kDontReleaseOnClose = false;

        std::unique_ptr<TemporaryStream> tempStreams[2];
        for (int i = 0; i < 2; ++i)
        {
            tempStreams[i] = std::make_unique<TemporaryStream>();

            const auto tempPath = GetTempPathApi(ec);
            if (ec)
            {
                Log::Error("Failed to get temporary path [{}]", ec);
                return {};
            }

            HRESULT hr = tempStreams[i]->Open(tempPath.c_str(), L"", bufferSize, kDontReleaseOnClose);
            if (FAILED(hr))
            {
                ec.assign(hr, std::system_category());
                Log::Error(L"Failed to open temporary path {} [{}]", tempPath, ec);
                return {};
            }
        }

        auto appender = std::make_unique<Appender<T>>(
            std::move(archiver), std::move(output), std::move(tempStreams[0]), std::move(tempStreams[1]));

        return appender;
    }

    Appender(
        T&& archiver,
        std::filesystem::path output,
        std::unique_ptr<TemporaryStream> tempStream1,
        std::unique_ptr<TemporaryStream> tempStream2)
        : m_archiver(std::move(archiver))
        , m_output(std::move(output))
        , m_tempStreams({std::move(tempStream1), std::move(tempStream2)})
        , m_srcStreamIndex(0)
        , m_isFirstFlush(true)
        , m_compressionLevel(m_archiver.CompressionLevel())
    {
        // Put some low compression until the final call from 'Close' method
        std::error_code ec;
        m_archiver.SetCompressionLevel(CompressionLevel::kFastest, ec);
        if (ec)
        {
            // TODO: add log
        }
    }

    void Add(std::unique_ptr<Item> item) { m_archiver.Add(std::move(item)); }

    void Add(Items items) { m_archiver.Add(std::move(items)); }

    void Flush(std::error_code& ec)
    {
        if (m_archiver.AddedItems().size() == 0 && m_isFirstFlush)
        {
            return;
        }

        if (m_isFirstFlush)
        {
            m_isFirstFlush = false;
            m_archiver.Compress(m_tempStreams[0], {}, ec);
            return;
        }

        auto& srcStream = m_tempStreams[m_srcStreamIndex];
        auto& dstStream = m_tempStreams[(m_srcStreamIndex + 1) % 2];

        m_archiver.Compress(dstStream, srcStream, ec);
        if (ec)
        {
            Log::Error("Failed to compress stream [{}]", ec);
            return;
        }

        HRESULT hr = srcStream->SetFilePointer(0, FILE_BEGIN, nullptr);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Error("Failed to seek source stream [{}]", ec);
            return;
        }

        hr = srcStream->SetSize(0);
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Error("Failed to resize source stream [{}]", ec);
            return;
        }

        m_srcStreamIndex = (m_srcStreamIndex + 1) % 2;
    }

    void Close(std::error_code& ec)
    {
        m_archiver.SetCompressionLevel(m_compressionLevel, ec);
        if (ec)
        {
            return;
        }

        Flush(ec);
        if (ec)
        {
            Log::Error("Failed to flush stream [{}]", ec);
            return;
        }

        auto& tempStream = m_tempStreams[m_srcStreamIndex];
        HRESULT hr = tempStream->MoveTo(m_output.c_str());
        if (FAILED(hr))
        {
            ec.assign(hr, std::system_category());
            Log::Error(L"Failed to move stream to {} [{}]", m_output, ec);
            return;
        }
    }

    const IArchive::Items& AddedItems() const { return m_archiver.AddedItems(); };

private:
    T m_archiver;
    const std::filesystem::path m_output;
    const std::array<std::shared_ptr<TemporaryStream>, 2> m_tempStreams;
    uint8_t m_srcStreamIndex;
    bool m_isFirstFlush;
    CompressionLevel m_compressionLevel;
};

}  // namespace Archive
}  // namespace Orc
