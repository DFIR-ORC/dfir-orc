//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OrcException.h"

#include "WideAnsi.h"

#include "fmt/format.h"

#include "Log/Log.h"

using namespace Orc;

Orc::Exception::Exception(std::wstring descr)
    : Description(std::move(descr))
{
}

Orc::Exception::Exception(Orc::Severity status, std::wstring descr)
    : Description(std::move(descr))
    , Severity(status)
{
}

HRESULT Exception::PrintMessage() const
{
    Log::Error(L"Exception Occured: {}", Description);
    return S_OK;
}

char const* Exception::what() const
{
    using namespace std::string_literals;

    if (What.has_value())
        return What.value().c_str();

    auto [hr, str] = Orc::WideToAnsi(fmt::format(L"Exception: {}", Description));
    if (SUCCEEDED(hr))
    {
        What.emplace(std::move(str));
        return What.value().c_str();
    }

    return "Exception raised without further description";
}

Exception::~Exception() {}
