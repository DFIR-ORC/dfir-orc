//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
// OrcParquet.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

#include "OrcParquet.h"

#include "ParquetWriter.h"

using namespace Orc;

std::shared_ptr<TableOutput::IStreamWriter>
StreamTableFactory(const logger& pLog, std::unique_ptr<TableOutput::Options>&& options)
{
#pragma comment(linker, "/export:StreamTableFactory=" __FUNCDNAME__)

    std::unique_ptr<TableOutput::Parquet::Options> pParquetOpt(
        dynamic_cast<TableOutput::Parquet::Options*>(options.release()));

    return Orc::TableOutput::Parquet::Writer::MakeNew(pLog, std::move(pParquetOpt));
}
