//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "NtAlgorithm.h"

#include <map>

#include <windows.h>
#include <winnt.h>

namespace {

constexpr std::string_view kUnknown = "<Unknown>";
constexpr std::string_view kNone = "none";
constexpr std::string_view kDefault = "default";
constexpr std::string_view kLznt1 = "lznt1";
constexpr std::string_view kXpress = "xpress";
constexpr std::string_view kXpressHuff = "xpress_huffman";

}  // namespace

namespace Orc {

std::string_view ToString(NtAlgorithm algorithm)
{
    switch (algorithm)
    {
        case NtAlgorithm::kUnknown:
            return kUnknown;
        case NtAlgorithm::kNone:
            return kNone;
        case NtAlgorithm::kDefault:
            return kDefault;
        case NtAlgorithm::kLznt1:
            return kLznt1;
        case NtAlgorithm::kXpress:
            return kXpress;
        case NtAlgorithm::kXpressHuffman:
            return kXpressHuff;
    }

    return kUnknown;
}

NtAlgorithm ToNtAlgorithm(const std::string& algorithm, std::error_code& ec)
{
    const std::map<std::string_view, NtAlgorithm> map = {
        {kNone, NtAlgorithm::kNone},
        {kDefault, NtAlgorithm::kDefault},
        {kLznt1, NtAlgorithm::kLznt1},
        {kXpress, NtAlgorithm::kXpress},
        {kXpressHuff, NtAlgorithm::kXpressHuffman}};

    auto it = map.find(algorithm);
    if (it == std::cend(map))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return NtAlgorithm::kUnknown;
    }

    return it->second;
}

}  // namespace Orc
