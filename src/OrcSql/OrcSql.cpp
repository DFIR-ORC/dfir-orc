//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
// OrcSql.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

#include "OrcSql.h"

#include "SqlConnection.h"
#include "SqlWriter.h"

using namespace Orc;

std::shared_ptr<TableOutput::IConnection> ConnectionFactory(const logger& pLog)
{
#pragma comment(linker, "/export:ConnectionFactory=" __FUNCDNAME__)
    return std::make_shared<TableOutput::Sql::Connection>(pLog);
}

std::shared_ptr<TableOutput::IConnectWriter> ConnectTableFactory(const logger& pLog)
{
#pragma comment(linker, "/export:ConnectTableFactory=" __FUNCDNAME__)
    return std::make_shared<TableOutput::Sql::Writer>(pLog);
}
