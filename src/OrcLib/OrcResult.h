//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Buffer.h"

#include <fmt/format.h>

#pragma managed(push, off)

#if defined __has_include
#    if __has_include(<stx/result.h>)
#        include <stx/result.h>

namespace Orc {

struct VoidResult
{
};

template <typename FormatContext>
auto formatMessage(const HRESULT hr, FormatContext& ctx)
{
    Buffer<WCHAR, 2> buffer;
    buffer.reserve(buffer.inner_elts());

    DWORD cchWritten = 0LU;

    do
    {
        cchWritten = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            HRESULT_CODE(hr),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
            buffer.get(),
            buffer.capacity(),
            NULL);

        if (cchWritten == 0)
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            {
                buffer.reserve(buffer.capacity() + MAX_PATH);
                continue;
            }
        }
        break;
    } while (1);

    if (cchWritten == 0)
        return;

    buffer.resize(cchWritten);
    while (cchWritten > 0
           && (buffer[cchWritten - 1] == L'\r' || buffer[cchWritten - 1] == L'\n' || buffer[cchWritten - 1] == L'.'))
    {
        cchWritten--;
    }
    buffer.resize(cchWritten);
    std::copy(buffer.get(), buffer.get() + buffer.size(), ctx.out());
    return;
}


template <typename T = VoidResult>
stx::Result<T, HRESULT> make_hr(HRESULT hr)
{
    if (FAILED(hr))
        return stx::make_err<T>(hr);
    return stx::make_ok<T, HRESULT>({});
}

}  // namespace Orc

template <typename T>
struct fmt::formatter<stx::v1::Result<T, HRESULT>, wchar_t>
{
    bool bMsg = true;
    bool bhr = true;

    constexpr auto parse(wformat_parse_context& ctx)
    {
        using namespace std::string_view_literals;

        auto begin = ctx.begin(), end = ctx.end();
        auto msg = L"msg}"sv;
        auto hr = L"hr}"sv;
        auto hrmsg = L"hrmsg}"sv;
        auto it = ctx.begin();
        if (std::equal(
                begin,
                end,  // source range
                std::begin(msg),
                std::end(msg),  // dest range
                [](const wchar_t c1, const wchar_t c2) { return tolower(c1) == tolower(c2); }))
        {
            bMsg = true;
            bhr = false;
            it += msg.size() - 1;
        }
        else if (std::equal(
                     begin,
                     end,  // source range
                     std::begin(hr),
                     std::end(hr),  // dest range
                     [](const wchar_t c1, const wchar_t c2) { return tolower(c1) == tolower(c2); }))
        {
            bhr = true;
            bMsg = false;
            it += hr.size() - 1;
        }
        else if (std::equal(
                     begin,
                     end,  // source range
                     std::begin(hrmsg),
                     std::end(hrmsg),  // dest range
                     [](const wchar_t c1, const wchar_t c2) { return tolower(c1) == tolower(c2); }))
        {
            bMsg = true;
            bhr = true;
            it += hrmsg.size() - 1;
        }

        if (it != end && *it != L'}')
            throw format_error("invalid fmt format for HRESULT");

        // Return an iterator past the end of the parsed range:
        return it;
    }

    template <typename FormatContext>
    auto format(const stx::v1::Result<T, HRESULT>& result, FormatContext& ctx)
    {
        if (result.is_err())
        {
            if (bhr && bMsg)
            {
                fmt::format_to(ctx.out(), L"{:#x}", (ULONG32)result.err_value());
                ctx.out() = L':';
                ctx.out()++;
                formatMessage(result.err_value(), ctx);
            }
            else if (bMsg)
            {
                formatMessage(result.err_value(), ctx);
            }
            else
                fmt::format_to(ctx.out(), L"{:#x}", (ULONG32)result.err_value());
        }
        else
            fmt::format_to(ctx.out(), L"{}", result.value());
        return ctx.out();
    }
};

template <>
template <typename FormatContext>
auto fmt::formatter<stx::v1::Result<Orc::VoidResult, HRESULT>, wchar_t>::format(
    const stx::v1::Result<Orc::VoidResult, HRESULT>& result,
    FormatContext& ctx)
{
    if (result.is_err())
    {
        if (bMsg)
        {
            if (bhr)
            {
                fmt::format_to(ctx.out(), L"{:#x}", (ULONG32)result.err_value());
                ctx.out() = L':';
                ctx.out()++;
            }
            formatMessage(result.err_value(), ctx);
        }
        else
            fmt::format_to(ctx.out(), L"{:#x}", (ULONG32)result.err_value());
    }
    else
        fmt::format_to(ctx.out(), L"S_OK");
    return ctx.out();
}


#    endif

#else
#    error "Missing STX Result library"
#endif

#pragma managed(pop)
