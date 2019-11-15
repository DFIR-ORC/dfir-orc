#pragma once

#include <algorithm>
#include <string>

namespace Orc {

inline bool EndsWith(const std::wstring& string, const std::wstring& substring) noexcept
{
    if (substring.size() > string.size())
    {
        return false;
    }

    return std::equal(substring.rbegin(), substring.rend(), string.rbegin());
}

}  // namespace Orc
