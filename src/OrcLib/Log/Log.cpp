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

SpdlogLogger::Ptr DefaultFacility()
{
    auto logger = DefaultLogger();
    assert(logger);
    return logger->Get(Log::Facility::kDefault);
}

}  // namespace Log
}  // namespace Orc
