//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2022 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "Utils/Guid.h"

namespace Orc {

template void ToGuid(std::basic_string_view<char> input, GUID& guid, std::error_code& ec);
template void ToGuid(std::basic_string_view<wchar_t> input, GUID& guid, std::error_code& ec);

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
