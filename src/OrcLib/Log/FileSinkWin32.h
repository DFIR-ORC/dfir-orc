//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2026 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include <memory>
#include <cassert>
#include <iostream>
#include <filesystem>

#include <spdlog/sinks/base_sink.h>

#include "Log/Log.h"
#include "Log/MemorySink.h"
#include "FileDisposition.h"
#include "Utils/Guard.h"
#include "Utils/WinApi.h"

#include "Text/Fmt/std_filesystem.h"
#include "Text/Fmt/std_error_code.h"

namespace Orc {
namespace Log {

//
// FileSinkWin32 will cache some logs into a MemorySink until the log file is opened
//
// BEWARE: Synchronization is made by the base_sink code but only for overriden methods. For other methods it needs to
// be done manually like it is the case for Open.
//
template <typename Mutex>
class FileSinkWin32 final : public spdlog::sinks::base_sink<Mutex>
{
public:
    // No need of mutexes since synchronization will be made on 'Mutex'
    using MemorySink = MemorySink<std::vector<uint8_t>, spdlog::details::null_mutex>;
    const size_t kMemorySinkSize = 128000;

    FileSinkWin32()
        : m_memorySink(std::make_unique<MemorySink>(kMemorySinkSize))
    {
        spdlog::sinks::base_sink<Mutex>::set_level(spdlog::level::trace);
    }

    void Open(const std::filesystem::path& path, FileDisposition disposition, std::error_code& ec)
    {
        DWORD dispositionWin32;
        switch (disposition)
        {
            case FileDisposition::Append:
                dispositionWin32 = OPEN_ALWAYS;
                break;
            case FileDisposition::Truncate:
                dispositionWin32 = CREATE_ALWAYS;
                break;
            case FileDisposition::CreateNew:
                dispositionWin32 = CREATE_ALWAYS;  // this is the expected behavior
                break;
            default:
                ec = std::make_error_code(std::errc::invalid_argument);
                return;
        }

        ec = Open(path, dispositionWin32);
    }

    [[nodiscard]] std::error_code Open(const std::filesystem::path& path, DWORD disposition)
    {
        std::lock_guard<Mutex> lock(spdlog::sinks::base_sink<Mutex>::mutex_);

        // Open can only be called once
        if (m_memorySink == nullptr)
        {
            return std::make_error_code(std::errc::device_or_resource_busy);
        }

        auto handle = CreateFileApi(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            disposition,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr);
        if (!handle)
        {
            std::wcerr << L"Failed SetFilePointerEx on " << path << L" " << handle.error() << std::endl;
            return handle.error();
        }

        if (disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS)
        {
            const LARGE_INTEGER zero {};
            if (!::SetFilePointerEx(handle->value(), zero, nullptr, FILE_END))
            {
                DWORD err = GetLastError();
                std::wcerr << L"Failed SetFilePointerEx on " << path << L" " << err << std::endl;
                return Win32Error(err);
            }
        }

        m_path = path;
        m_handle = std::move(*handle);

        const auto& buffer = m_memorySink->buffer();
        if (!buffer.empty())
        {
            DWORD written = 0;
            const BOOL ok =
                ::WriteFile(m_handle.value(), buffer.data(), static_cast<DWORD>(buffer.size()), &written, nullptr);

            if (!ok || written != static_cast<DWORD>(buffer.size()))
            {
                DWORD err = GetLastError();
                std::wcerr << L"Failed WriteFile on " << path << L" " << err << std::endl;
                return Win32Error(err);
            }
        }

        m_memorySink.reset();
        return {};
    }

    ~FileSinkWin32() override {}

    inline bool IsOpen() const { return m_handle.value() != INVALID_HANDLE_VALUE; }
    inline void Close()
    {
        Flush();
        m_handle = {};
    }

    std::optional<std::filesystem::path> OutputPath() const { return m_path; }

protected:
    void set_pattern_(const std::string& pattern) override
    {
        set_formatter_(std::make_unique<spdlog::pattern_formatter>(pattern));
    }

    void set_formatter_(std::unique_ptr<spdlog::formatter> formatter) override
    {
        if (m_memorySink)
        {
            m_memorySink->set_formatter(formatter->clone());
        }

        spdlog::sinks::base_sink<Mutex>::set_formatter_(std::move(formatter));
    }

    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        if (m_memorySink)
        {
            m_memorySink->log(msg);
            return;
        }

        if (!IsOpen())
        {
            return;  // handle has been close
        }

        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

        DWORD written = 0;
        if (!::WriteFile(m_handle.value(), formatted.data(), static_cast<DWORD>(formatted.size()), &written, nullptr))
        {
            std::wcerr << L"Failed WriteFile on " << m_path.value_or(L"<null>") << L" " << GetLastError() << std::endl;
            return;
        }
    }

    void flush_() override
    {
        if (m_memorySink)
        {
            return;
        }

        ::FlushFileBuffers(m_handle.value());
    }

private:
    Guard::FileHandle m_handle;
    std::unique_ptr<MemorySink> m_memorySink;
    std::optional<std::filesystem::path> m_path;
};

}  // namespace Log
}  // namespace Orc
