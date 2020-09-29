//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#ifdef ORCSQL_EXPORTS
#    define ORCSQL_API __declspec(dllexport)
#elif ORCSQL_IMPORTS
#    define ORCSQL_API __declspec(dllimport)
#else  // STATIC linking
#    define ORCSQL_API
#endif

#include <memory>

#include "TableOutputWriter.h"

namespace Orc {

ORCSQL_API std::shared_ptr<TableOutput::IConnection> ConnectionFactory(const Orc::logger& pLog);
ORCSQL_API std::shared_ptr<TableOutput::IConnectWriter> ConnectTableFactory(const Orc::logger& pLog);
}  // namespace Orc
