#pragma once

#include <algorithm>
#include <string>
#include <cwctype>

namespace Orc {

inline bool EndsWith(const std::wstring& string, const std::wstring& substring) noexcept
{
    if (substring.size() > string.size())
    {
        return false;
    }

    return std::equal(substring.rbegin(), substring.rend(), string.rbegin());
}

inline bool EndsWithNoCase(const std::wstring& string, const std::wstring& substring) noexcept
{
    if (substring.size() > string.size())
        return false;
    return std::equal(substring.rbegin(), substring.rend(), string.rbegin(), [](wchar_t a, wchar_t b) {
        return std::towlower(a) == std::towlower(b);
    });
}

}  // namespace Orc
