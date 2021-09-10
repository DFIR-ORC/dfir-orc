//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#include "uri.h"

#include <regex>

//
// Build on RFC's given regex
//
// URI regex from: https://www.ietf.org/rfc/rfc2396.txt
//
// B. Parsing a URI Reference with a Regular Expression
// ...
//
//  ^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?
//   12            3  4          5       6  7        8 9
//
//    $1 = http:
//    $2 = http  [scheme]
//    $3 = //www.ics.uci.edu
//    $4 = www.ics.uci.edu    [authority]
//    $5 = /pub/ietf/uri/    [path]
//    $6 = <undefined>
//    $7 = <undefined>    [query]
//    $8 = #Related
//    $9 = Related    [fragment]
//
// See also: https://cpp-netlib.org/0.9.4/in_depth/uri.html
//           https://regexr.com/65ab9
//

namespace {

std::optional<std::wstring> ToOptional(const std::wssub_match& submatch)
{
    if (submatch.matched)
    {
        return submatch;
    }

    return {};
}

}

namespace Orc {

Uri::Uri(std::wstring uri, std::error_code& ec)
{
    std::wregex regex(L"^(([^\\\\:/?#]+):)?([\\/\\\\]{2}((([^:]+)(:(.*))?@)?([^\\\\/?#:]*))(:([0-9]+))?)?([^?#]*)(\\?([^#]*))?(#(.*))?");
    std::wsmatch matches;

    if (!std::regex_match(uri, matches, regex))
    {
        ec = std::make_error_code(std::errc::invalid_argument);
        return;
    }

    m_scheme = ::ToOptional(matches[2]);
    m_authority = ::ToOptional(matches[9]);
    m_path = ::ToOptional(matches[12]);
    m_query = ::ToOptional(matches[13]);
    m_fragment = ::ToOptional(matches[16]);
    m_userName = ::ToOptional(matches[6]);
    m_password = ::ToOptional(matches[8]);
    m_port = ::ToOptional(matches[11]);
}

}  // namespace Orc
