//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//
#pragma once

#include <string_view>

namespace Orc {

class Uri
{
public:
    Uri(std::wstring uri, std::error_code& ec);

    const std::optional<std::wstring>& Scheme() const { return m_scheme; }
    const std::optional<std::wstring>& Authority() const { return m_authority; }
    const std::optional<std::wstring>& Host() const { return Authority(); }
    const std::optional<std::wstring>& Path() const { return m_path; }
    const std::optional<std::wstring>& Query() const { return m_query; }
    const std::optional<std::wstring>& Fragment() const { return m_fragment; }
    const std::optional<std::wstring>& UserName() const { return m_userName; }
    const std::optional<std::wstring>& Password() const { return m_password; }
    const std::optional<std::wstring>& Port() const { return m_port; }

private:
    std::optional<std::wstring> m_scheme;
    std::optional<std::wstring> m_authority;
    std::optional<std::wstring> m_path;
    std::optional<std::wstring> m_query;
    std::optional<std::wstring> m_fragment;
    std::optional<std::wstring> m_userName;
    std::optional<std::wstring> m_password;
    std::optional<std::wstring> m_port;
};

}  // namespace Orc
