//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#ifdef ORCPARQUET_EXPORTS
#    define ORCPARQUET_API __declspec(dllexport)
#elif ORCPARQUET_IMPORTS
#    define ORCPARQUET_API __declspec(dllimport)
#else  // STATIC linking
#    define ORCPARQUET_API
#endif

#include <memory>

#include "TableOutputWriter.h"

namespace Orc {

ORCPARQUET_API std::shared_ptr<TableOutput::IStreamWriter> StreamTableFactory();
}  // namespace Orc
