//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "OrcException.h"

#include "LogFileWriter.h"
#include "WideAnsi.h"

#include "fmt/format.h"

using namespace Orc;

Exception::Exception(std::wstring descr)
    : Description(std::move(descr))
{
}

Exception::Exception(ExceptionSeverity status, std::wstring descr)
    : Description(std::move(descr))
    , Severity(status)
{
}

HRESULT Exception::PrintMessage(const Orc::logger& pLog) const
{
    Orc::log::Error(pLog, E_FAIL, L"Exception Occured: %s\r\n", Description.c_str());
    return S_OK;
}

char const* Exception::what() const
{
    using namespace std::string_literals;

    if (What.has_value())
        return What.value().c_str();

    auto [hr, str] = Orc::WideToAnsi(nullptr, fmt::format(L"Exception: {}", Description));
    if (SUCCEEDED(hr))
        What.emplace(std::move(str));

    return What.value_or("Exception raised without further desciption"s).c_str();
}

Exception::~Exception() {}
