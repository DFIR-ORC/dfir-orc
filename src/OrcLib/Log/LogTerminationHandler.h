//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//
#pragma once

#include "Robustness.h"

#include "Log/Log.h"

namespace Orc {

class LogTerminationHandler : public TerminationHandler
{
public:
    LogTerminationHandler()
        : TerminationHandler(L"Flush logs", ROBUSTNESS_LOG)
    {
    }

    inline HRESULT operator()()
    {
        auto& logger = Orc::Log::DefaultLogger();
        if (logger)
        {
            logger->Flush();
        }

        return S_OK;
    }
};

}  // namespace Orc
