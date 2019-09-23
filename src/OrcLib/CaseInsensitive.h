//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <functional>

#pragma managed(push, off)

namespace Orc {

inline bool lessCaseInsensitive(const std::wstring_view s1, const std::wstring_view s2)
{
    return std::lexicographical_compare(
        s1.begin(),
        s1.end(),  // source range
        s2.begin(),
        s2.end(),  // dest range
        [](const wchar_t c1, const wchar_t c2) { return tolower(c1) < tolower(c2); });  // comparison
}

inline bool equalCaseInsensitive(const std::wstring_view s1, const std::wstring_view s2)
{
    if (s1.size() != s2.size())
        return false;

    auto p1 = s1.begin();
    auto p2 = s2.begin();

    while (p1 != s1.end() && p2 != s2.end())
    {
        if (tolower(*p1) != tolower(*p2))
            return false;
        ++p1;
        ++p2;
    }
    return true;
}

inline bool equalCaseInsensitive(const std::wstring_view s1, const std::wstring_view s2, size_t cchCount)
{
    if (s1.size() < cchCount || s2.size() < cchCount)
        return false;

    auto p1 = s1.begin();
    auto p2 = s2.begin();
    size_t index = 0;

    while (p1 != s1.end() && p2 != s2.end())
    {
        if (index == cchCount)
            return true;
        if (tolower(*p1) != tolower(*p2))
            return false;
        ++p1;
        ++p2;
        ++index;
    }
    return true;
}

inline bool equalCaseInsensitive(const std::string_view s1, const std::string_view& s2)
{
    if (s1.size() != s2.size())
        return false;

    auto p1 = s1.begin();
    auto p2 = s2.begin();

    while (p1 != s1.end() && p2 != s2.end())
    {
        if (tolower((unsigned char)*p1) != tolower((unsigned char)*p2))
            return false;
        ++p1;
        ++p2;
    }
    return true;
}

inline bool equalCaseInsensitive(const std::string_view s1, const std::string_view s2, size_t cchCount)
{
    if (s1.size() < cchCount || s2.size() < cchCount)
        return false;

    auto p1 = s1.begin();
    auto p2 = s2.begin();
    size_t index = 0;

    while (p1 != s1.end() && p2 != s2.end())
    {
        if (index == cchCount)
            return true;
        if (tolower((unsigned char)*p1) != tolower((unsigned char)*p2))
            return false;
        ++p1;
        ++p2;
    }
    return true;
}

inline size_t hashCaseInsensitive(const std::wstring_view s)
{
    ULONG hash = 0;

    for (unsigned int i = 0; i < s.size(); i++)
        hash = tolower(s[i]) + (hash << 6) + (hash << 16) - hash;
    return hash;
}

inline size_t hashCaseInsensitive(const std::string_view s)
{
    ULONG hash = 0;

    for (unsigned int i = 0; i < s.size(); i++)
        hash = tolower((unsigned char)s[i]) + (hash << 6) + (hash << 16) - hash;

    return hash;
}

struct CaseInsensitive : std::function<bool(const std::shared_ptr<std::wstring>&, const std::shared_ptr<std::wstring>&)>
{
    inline bool operator()(const std::shared_ptr<std::wstring>& s1, const std::shared_ptr<std::wstring>& s2) const
    {
        return std::lexicographical_compare(
            s1->begin(),
            s1->end(),  // source range
            s2->begin(),
            s2->end(),  // dest range
            [](const wchar_t c1, const wchar_t c2) { return tolower(c1) < tolower(c2); });  // comparison
    };
    inline bool operator()(const std::wstring_view s1, const std::wstring_view s2) const
    {
        return std::lexicographical_compare(
            s1.begin(),
            s1.end(),  // source range
            s2.begin(),
            s2.end(),  // dest range
            [](const wchar_t c1, const wchar_t c2) { return tolower(c1) < tolower(c2); });  // comparison
    };
    inline size_t operator()(const std::wstring_view s) const { return hashCaseInsensitive(s); }
};

struct CaseInsensitiveUnordered
    : std::function<bool(const std::shared_ptr<std::wstring>&, const std::shared_ptr<std::wstring>&)>
{
    inline bool operator()(const std::shared_ptr<std::wstring>& s1, const std::shared_ptr<std::wstring>& s2) const
    {
        return equalCaseInsensitive(*s1, *s2);
    };
    inline bool operator()(const std::wstring_view s1, const std::wstring_view s2) const
    {
        return equalCaseInsensitive(s1, s2);
    };
    inline size_t operator()(const std::wstring_view s) const { return hashCaseInsensitive(s); }
};

struct CaseInsensitiveUnorderedAnsi
    : std::function<bool(const std::shared_ptr<std::string>&, const std::shared_ptr<std::string>&)>
{
    inline bool operator()(const std::shared_ptr<std::string>& s1, const std::shared_ptr<std::string>& s2) const
    {
        return equalCaseInsensitive(*s1, *s2);
    };
    inline bool operator()(const std::string_view s1, const std::string_view s2) const
    {
        return equalCaseInsensitive(s1, s2);
    };
    inline size_t operator()(const std::string& s) const { return hashCaseInsensitive(s); }
};
}  // namespace Orc

#pragma managed(pop)
