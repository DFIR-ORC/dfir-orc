//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "WofAlgorithm.h"

#include <map>

#include <windows.h>

using namespace Orc::Ntfs;
using namespace Orc;

namespace {

constexpr std::string_view kUnknown = "<Unknown>";
constexpr std::string_view kXpress4k = "xpress4k";
constexpr std::string_view kXpress8k = "xpress8k";
constexpr std::string_view kXpress16k = "xpress16k";
constexpr std::string_view kLzx = "lzx";

WofAlgorithm ToWofAlgorithmXpress(uint64_t chunkSize, std::error_code& ec)
{
    if (chunkSize == 4096)
    {
        return WofAlgorithm::kXpress4k;
    }
    else if (chunkSize == 8192)
    {
        return WofAlgorithm::kXpress8k;
    }
    else if (chunkSize == 16384)
    {
        return WofAlgorithm::kXpress16k;
    }
    else
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return WofAlgorithm::kUnknown;
    }
}

}  // namespace

namespace Orc {
namespace Ntfs {

std::string_view ToString(WofAlgorithm algorithm)
{
    switch (algorithm)
    {
        case WofAlgorithm::kUnknown:
            return kUnknown;
        case WofAlgorithm::kXpress4k:
            return kXpress4k;
        case WofAlgorithm::kXpress8k:
            return kXpress8k;
        case WofAlgorithm::kXpress16k:
            return kXpress16k;
        case WofAlgorithm::kLzx:
            return kLzx;
    }

    return kUnknown;
}

WofAlgorithm ToWofAlgorithm(const std::string& algorithm, std::error_code& ec)
{
    const std::map<std::string_view, WofAlgorithm> map = {
        {kXpress4k, WofAlgorithm::kXpress4k},
        {kXpress8k, WofAlgorithm::kXpress8k},
        {kXpress16k, WofAlgorithm::kXpress16k},
        {kLzx, WofAlgorithm::kLzx}};

    auto it = map.find(algorithm);
    if (it == std::cend(map))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return WofAlgorithm::kUnknown;
    }

    return it->second;
}

NtAlgorithm ToNtAlgorithm(WofAlgorithm algorithm)
{
    switch (algorithm)
    {
        case WofAlgorithm::kUnknown:
            return NtAlgorithm::kUnknown;
        case WofAlgorithm::kXpress4k:
            return NtAlgorithm::kXpressHuffman;
        case WofAlgorithm::kXpress8k:
            return NtAlgorithm::kXpressHuffman;
        case WofAlgorithm::kXpress16k:
            return NtAlgorithm::kXpressHuffman;
        case WofAlgorithm::kLzx:
            return NtAlgorithm::kUnknown;
    }

    return NtAlgorithm::kUnknown;
}

WofAlgorithm ToWofAlgorithm(NtAlgorithm algorithm, uint64_t chunkSize, std::error_code& ec)
{
    switch (algorithm)
    {
        case NtAlgorithm::kUnknown:
            return WofAlgorithm::kUnknown;
        case NtAlgorithm::kLznt1:
            ec = std::make_error_code(std::errc::invalid_argument);
            return WofAlgorithm::kUnknown;
        case NtAlgorithm::kXpress:
        case NtAlgorithm::kXpressHuffman:
            return ToWofAlgorithmXpress(chunkSize, ec);
    }

    return WofAlgorithm::kUnknown;
}

WofAlgorithm ToWofAlgorithm(WimlibAlgorithm algorithm, uint64_t chunkSize, std::error_code& ec)
{
    switch (algorithm)
    {
        case WimlibAlgorithm::kUnknown:
            return WofAlgorithm::kUnknown;
        case WimlibAlgorithm::kXpress:
            return ToWofAlgorithmXpress(chunkSize, ec);
        case WimlibAlgorithm::kLzx:
            return WofAlgorithm::kLzx;
    }

    ec = std::make_error_code(std::errc::invalid_argument);
    return WofAlgorithm::kUnknown;
}

WofAlgorithm ToWofAlgorithm(uint32_t algorithm, std::error_code& ec)
{
    switch (static_cast<WofAlgorithm>(algorithm))
    {
        case WofAlgorithm::kUnknown:
        case WofAlgorithm::kXpress4k:
        case WofAlgorithm::kXpress8k:
        case WofAlgorithm::kXpress16k:
        case WofAlgorithm::kLzx:
            return static_cast<WofAlgorithm>(algorithm);
    }

    ec = std::make_error_code(std::errc::invalid_argument);
    return WofAlgorithm::kUnknown;
}

}  // namespace Ntfs
}  // namespace Orc
