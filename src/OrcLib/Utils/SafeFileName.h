//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//

#pragma once

#include <algorithm>
#include <cwctype>
#include <iterator>
#include <string>
#include <string_view>

namespace Orc {

namespace Detail {

inline bool IsForbiddenFileNameCharacter(wchar_t c)
{
    // ':' would create an alternate data stream instead of a file, '*' and '?' are wildcards
    constexpr std::wstring_view kForbidden = LR"(\/:*?"<>|)";

    if (c >= 0x01 && c <= 0x1f)
    {
        return true;
    }

    return kForbidden.find(c) != std::wstring_view::npos;
}

inline bool IsDosDeviceName(std::wstring_view name)
{
    // A DOS device is matched on the stem alone: 'CON', 'CON.txt' and 'CON .txt' all reach the device
    auto stem = name.substr(0, name.find(L'.'));

    const auto last = stem.find_last_not_of(L' ');
    if (last == std::wstring_view::npos)
    {
        return false;
    }

    stem = stem.substr(0, last + 1);

    if (stem.size() < 3 || stem.size() > 4)
    {
        return false;
    }

    std::wstring upper;
    std::transform(std::cbegin(stem), std::cend(stem), std::back_inserter(upper), [](wchar_t c) {
        return static_cast<wchar_t>(std::towupper(c));
    });

    if (upper == L"CON" || upper == L"PRN" || upper == L"AUX" || upper == L"NUL")
    {
        return true;
    }

    if (upper.size() != 4 || upper[3] < L'1' || upper[3] > L'9')
    {
        return false;
    }

    return upper.compare(0, 3, L"COM") == 0 || upper.compare(0, 3, L"LPT") == 0;
}

}  // namespace Detail

// Make an attacker controlled name usable as a single Win32 path component.
//
// Returns an empty string when nothing usable remains ('.', '..', '...', ' ', a name made only of forbidden
// characters): such an item must be skipped by the caller, as any fallback name would silently collide.
inline std::wstring SanitizeFileName(std::wstring_view name)
{
    std::wstring sanitized;
    sanitized.reserve(name.size());

    std::transform(std::cbegin(name), std::cend(name), std::back_inserter(sanitized), [](wchar_t c) {
        return Detail::IsForbiddenFileNameCharacter(c) ? L'_' : c;
    });

    // Win32 strips trailing dots and spaces: keeping them would alias another name of the same directory and
    // make the recorded path disagree with the file system
    const auto last = sanitized.find_last_not_of(L". ");
    if (last == std::wstring::npos)
    {
        return {};
    }

    sanitized.resize(last + 1);

    if (Detail::IsDosDeviceName(sanitized))
    {
        sanitized.insert(sanitized.begin(), L'_');
    }

    return sanitized;
}

}  // namespace Orc
