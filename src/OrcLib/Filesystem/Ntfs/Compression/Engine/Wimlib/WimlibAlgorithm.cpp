//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "WimlibAlgorithm.h"

#include <map>

#include <windows.h>
#include <winnt.h>

namespace {

constexpr std::string_view kUnknown = "<Unknown>";
constexpr std::string_view kNone = "none";
constexpr std::string_view kXpress = "xpress";
constexpr std::string_view kLzx = "lzx";
constexpr std::string_view kLzms = "lzms";

}  // namespace

namespace Orc {

std::string_view ToString(WimlibAlgorithm algorithm)
{
    switch (algorithm)
    {
        case WimlibAlgorithm::kUnknown:
            return kUnknown;
        case WimlibAlgorithm::kNone:
            return kXpress;
        case WimlibAlgorithm::kXpress:
            return kXpress;
        case WimlibAlgorithm::kLzx:
            return kLzx;
        case WimlibAlgorithm::kLzms:
            return kLzms;
    }

    return kUnknown;
}

WimlibAlgorithm ToWimlibAlgorithm(const std::string& algorithm, std::error_code& ec)
{
    const std::map<std::string_view, WimlibAlgorithm> map = {
        {kNone, WimlibAlgorithm::kNone},
        {kXpress, WimlibAlgorithm::kXpress},
        {kLzx, WimlibAlgorithm::kLzx},
        {kLzx, WimlibAlgorithm::kLzms}};

    auto it = map.find(algorithm);
    if (it == std::cend(map))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return WimlibAlgorithm::kUnknown;
    }

    return it->second;
}

}  // namespace Orc
