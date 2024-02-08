//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl
//

#include "stdafx.h"

#include "Log.h"

namespace Orc {
namespace Log {

std::shared_ptr<Logger>& DefaultLogger()
{
    static std::shared_ptr<Logger> instance;
    return instance;
}

void SetDefaultLogger(std::shared_ptr<Logger> instance)
{
    auto& pInstance = DefaultLogger();
    pInstance = std::move(instance);
}

const SpdlogLogger::Ptr& Get(Facility id)
{
    static SpdlogLogger::Ptr null_ptr;

    auto& instance = DefaultLogger();
    if (instance)
    {
        return instance->Get(id);
    }

    return null_ptr;
}

}  // namespace Log
}  // namespace Orc
