//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "CsvFileReader.h"
#include "BoundTableRecord.h"
#include "TableOutputWriter.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API CsvToSql
{

public:
    CsvToSql(logger pLog);

    HRESULT Initialize(
        std::shared_ptr<TableOutput::CSV::FileReader> pReader,
        std::shared_ptr<TableOutput::IConnectWriter> pSql);

    HRESULT MoveNextLine(TableOutput::CSV::FileReader::Record& record);

    const auto& CSV() const { return m_pReader; }
    const auto& SQL() const { return m_pSqlWriter; }

    ~CsvToSql(void);

private:
    logger _L_;
    std::shared_ptr<TableOutput::CSV::FileReader> m_pReader;
    std::shared_ptr<TableOutput::IConnectWriter> m_pSqlWriter;

    std::vector<DWORD> m_mappings;

    HRESULT MoveColumn(const TableOutput::CSV::FileReader::Column& csv_value, TableOutput::BoundColumn& sql_value);

    HRESULT MoveData(const TableOutput::CSV::FileReader::Record& csv_record, BoundRecord& sql_record);
};

}  // namespace Orc

#pragma managed(pop)
