//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Text/Guid.h"

namespace {

template <typename CharT>
void ToGuid(std::basic_string_view<CharT> input, GUID& guid, std::error_code& ec)
{
    if (input.empty())
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    size_t first = 0;
    if (input[0] == '{')
    {
        first = 1;
    }

    char msb = 0;
    bool isByteComplete = false;
    size_t offset = 0;
    std::array<char, sizeof(GUID)> buffer;

    for (size_t i = first; i < input.size(); ++i)
    {
        char nibble;

        if (input[i] == '}' && input[0] == '{')
        {
            break;
        }

        if (offset >= sizeof(GUID))
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return;
        }

        if (input[i] == '-')
        {
            continue;
        }

        if ('0' <= input[i] && input[i] <= '9')
        {
            nibble = input[i] - '0';
        }
        else if ('a' <= input[i] && input[i] <= 'f')
        {
            nibble = input[i] - 'a' + 10;
        }
        else if ('A' <= input[i] && input[i] <= 'F')
        {
            nibble = input[i] - 'A' + 10;
        }
        else
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return;
        }

        if (isByteComplete)
        {
            buffer[offset++] = msb + nibble;
        }
        else
        {
            msb = nibble << 4;
        }

        isByteComplete = !isByteComplete;
    }

    std::string_view view(buffer.data(), buffer.size());

    std::reverse_copy(
        std::cbegin(view), std::cbegin(view) + sizeof(guid.Data1), reinterpret_cast<uint8_t*>(&guid.Data1));
    view.remove_prefix(sizeof(guid.Data1));

    std::reverse_copy(
        std::cbegin(view), std::cbegin(view) + sizeof(guid.Data2), reinterpret_cast<uint8_t*>(&guid.Data2));
    view.remove_prefix(sizeof(guid.Data2));

    std::reverse_copy(
        std::cbegin(view), std::cbegin(view) + sizeof(guid.Data3), reinterpret_cast<uint8_t*>(&guid.Data3));
    view.remove_prefix(sizeof(guid.Data3));

    std::copy(std::cbegin(view), std::cend(view), guid.Data4);
}

}  // namespace

namespace Orc {

//const int kGuidStringLength = sizeof(GUID) * 2 + 4;

void ToGuid(std::string_view input, GUID& guid, std::error_code& ec)
{
    ::ToGuid(input, guid, ec);
}

void ToGuid(std::wstring_view input, GUID& guid, std::error_code& ec)
{
    ::ToGuid(input, guid, ec);
}

std::string ToString(const GUID& guid)
{
    std::string s;
    ToString(guid, std::back_inserter(s));
    return s;
}

std::wstring ToStringW(const GUID& guid)
{
    std::wstring s;
    ToString(guid, std::back_inserter(s));
    return s;
}

}  // namespace Orc
