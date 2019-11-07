//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"
#include "SqlImportAgent.h"

#include "ImportMessage.h"
#include "ImportNotification.h"

#include "CsvFileReader.h"
#include "CsvToSql.h"
#include "Temporary.h"
#include "TemporaryStream.h"
#include "FileStream.h"
#include "MemoryStream.h"

#include "ConfigFileReader.h"
#include "ConfigFile_Common.h"
#include "EmbeddedResource.h"

#include <boost/scope_exit.hpp>
#include <sstream>

using namespace std;

using namespace Orc;

HRESULT
SqlImportAgent::Initialize(const OutputSpec& importOutput, const OutputSpec& tempOutput, TableDescription& table)
{
    m_wevtapi = ExtensionLibrary::GetLibrary<EvtLibrary>(_L_);
    if (!m_wevtapi)
    {
        log::Error(_L_, E_FAIL, L"Failed to initialize wevtapi extension\r\n");
        return E_FAIL;
    }

    HRESULT hr = E_FAIL;
    m_databaseOutput = importOutput;
    m_tempOutput = tempOutput;

    if (FAILED(hr = InitializeTableColumns(table)))
    {
        log::Error(_L_, hr, L"Failed to initialize table columns for table %s\r\n", table.name.c_str());
        return hr;
    }

    return InitializeTable(table);
}

HRESULT SqlImportAgent::InitializeTableColumns(TableDescription& table)
{
    HRESULT hr = E_FAIL;

    if (!m_TableDefinition.first.Schema.empty())
        return S_OK;  // list of columns is already initialised

    m_TableDefinition.first = table;

    ConfigFileReader r(_L_);

    ConfigItem schemaconfig;
    Orc::Config::Common::sqlschema(schemaconfig);

    if (EmbeddedResource::IsResourceBased(m_TableDefinition.first.Schema))
    {
        CBinaryBuffer buf;
        if (FAILED(hr = EmbeddedResource::ExtractToBuffer(_L_, m_TableDefinition.first.Schema, buf)))
        {
            log::Error(_L_, hr, L"\r\nFailed to load schema \"%s\"\r\n", m_TableDefinition.first.Schema.c_str());
            return hr;
        }

        auto memstream = std::make_shared<MemoryStream>(_L_);

        if (FAILED(hr = memstream->OpenForReadOnly(buf.GetData(), buf.GetCount())))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to create stream for config ressource %s\r\n",
                m_TableDefinition.first.Schema.c_str());
            return hr;
        }

        if (FAILED(hr = r.ReadConfig(memstream, schemaconfig)))
        {
            log::Error(_L_, hr, L"\r\nFailed to load schema \"%s\"\r\n", m_TableDefinition.first.Schema.c_str());
            return hr;
        }
    }
    else
    {
        if (FAILED(hr = r.ReadConfig(m_TableDefinition.first.Schema.c_str(), schemaconfig)))
        {
            log::Error(_L_, hr, L"\r\nFailed to load schema \"%s\"\r\n", m_TableDefinition.first.Schema.c_str());
            return hr;
        }
    }

    m_TableDefinition.second =
        TableOutput::GetColumnsFromConfig(_L_, m_TableDefinition.first.Key.c_str(), schemaconfig);

    if (!m_TableDefinition.second)
    {
        log::Error(
            _L_,
            E_FAIL,
            L"\r\nSchema %s (key=%s) failed to load: empty column list\r\n",
            m_TableDefinition.first.Schema.c_str(),
            m_TableDefinition.first.Key.c_str());
        return E_FAIL;
    }

    return S_OK;
}

HRESULT SqlImportAgent::InitializeTable(TableDescription& table)
{
    HRESULT hr = E_FAIL;

    auto pSqlConnection = TableOutput::GetSqlConnection(_L_, nullptr);

    if (FAILED(hr = pSqlConnection->Connect(m_databaseOutput.ConnectionString.c_str())))
    {
        log::Error(_L_, hr, L"Could not connect to SQL %s\r\n", m_databaseOutput.ConnectionString.c_str());
        return hr;
    }

    switch (table.Disposition)
    {
        case TableDisposition::AsIs:
            break;
        case TableDisposition::Truncate:
            if (FAILED(hr = pSqlConnection->TruncateTable(table.name)))
            {
                log::Error(_L_, hr, L"Failed to truncate table %s\r\n", table.name.c_str());
            }
            break;
        case TableDisposition::CreateNew:
        {
            const auto& Columns = GetTableColumns();
            if (Columns)
            {
                log::Error(_L_, hr, L"Failed to get table definition for %s\r\n", table.name.c_str());
                return hr;
            }
            if (pSqlConnection->IsTablePresent(table.name))
            {
                if (FAILED(hr = pSqlConnection->DropTable(table.name)))
                {
                    log::Error(_L_, hr, L"Failed to create table %s\r\n", table.name.c_str());
                }
            }
            if (FAILED(hr = pSqlConnection->CreateTable(table.name, Columns, table.bCompress, table.bTABLOCK)))
            {
                log::Error(_L_, hr, L"Failed to create table %s\r\n", table.name.c_str());
            }
        }
        break;
        default:
            break;
    }

    if (!m_TableDefinition.first.BeforeStatement.empty())
    {
        log::Info(
            _L_, L"\tExecuting \"before\" import statement for table %s\r\n", m_TableDefinition.first.name.c_str());
        if (FAILED(hr = pSqlConnection->ExecuteStatement(m_TableDefinition.first.BeforeStatement)))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to execute \"before\" statement for table %s\r\n",
                m_TableDefinition.first.name.c_str());
        }
    }

    m_pSqlConnection = pSqlConnection;

    return S_OK;
}

std::shared_ptr<TableOutput::IWriter> SqlImportAgent::GetOutputWriter(ImportItem& input)
{
    HRESULT hr = E_FAIL;

    const ImportDefinition::Item* pDefItem = input.DefinitionItemLookup(ImportDefinition::Import);

    if (pDefItem == nullptr)
    {
        log::Error(_L_, E_POINTER, L"No Import definition item found for item %s\r\n", input.name.c_str());
        return nullptr;
    }

    const auto& columns = GetTableColumns();
    if (!columns)
    {
        log::Error(_L_, E_FAIL, L"Failed to load columns for table %s, key %s\r\n", pDefItem->tableName.c_str());
        return nullptr;
    }

    auto retval = TableOutput::GetSqlWriter(_L_, nullptr);

    if (FAILED(hr = retval->SetConnection(m_pSqlConnection)))
    {
        log::Error(_L_, hr, L"Failed to connect to SQL server %s\r\n", m_databaseOutput.ConnectionString.c_str());
        return nullptr;
    }

    if (FAILED(hr = retval->SetSchema(columns)))
    {
        log::Error(_L_, hr, L"Failed to add schema definition to reader\r\n");
        return nullptr;
    }

    if (FAILED(hr = retval->BindColumns(pDefItem->tableName.c_str())))
    {
        log::Error(_L_, hr, L"Failed to bind columns to SQL reader\r\n");
        return nullptr;
    }
    return retval;
}

HRESULT SqlImportAgent::ImportCSVData(ImportItem& input)
{
    HRESULT hr = E_FAIL;

    GetSystemTime(&input.importStart);

    BOOST_SCOPE_EXIT(&input) { GetSystemTime(&input.importEnd); }
    BOOST_SCOPE_EXIT_END;

    const ImportDefinition::Item* pDefItem = input.DefinitionItemLookup(ImportDefinition::Import);

    if (pDefItem == nullptr)
    {
        log::Error(
            _L_, E_POINTER, L"No Import definition item found for item %s\r\n", m_TableDefinition.first.name.c_str());
        return E_POINTER;
    }

    const auto& columns = GetTableColumns();
    if (!columns)
    {
        log::Error(
            _L_, E_FAIL, L"Failed to load columns for table %s, key %s\r\n", m_TableDefinition.first.name.c_str());
        return E_FAIL;
    }

    auto pStream = input.GetInputStream(_L_);

    if (pStream)
        input.ullBytesExtracted = pStream->GetSize();
    else
    {
        log::Error(_L_, E_FAIL, L"No input stream for table %s, key %s\r\n", m_TableDefinition.first.name.c_str());
        return E_FAIL;
    }

    auto pCSV = std::make_unique<TableOutput::CSV::FileReader>(_L_);

    if (FAILED(hr = pCSV->OpenStream(pStream)))
    {
        log::Error(_L_, hr, L"Failed to open CSV file %s\r\n", input.name.c_str(), hr);
        return hr;
    }

    switch (pCSV->GetEncoding())
    {
        case OutputSpec::Encoding::UTF16:
            log::Verbose(_L_, L"\tImporting %s (UTF16)...\r\n", input.name.c_str());
            break;
        case OutputSpec::Encoding::UTF8:
            log::Verbose(_L_, L"\tImporting %s (UTF8)...\r\n", input.name.c_str());
            break;
        default:
            log::Error(_L_, E_INVALIDARG, L"Unsupported encoding in %s\r\n", input.name.c_str());
            return E_INVALIDARG;
    }

    if (!columns)
    {
        if (FAILED(hr = pCSV->PeekHeaders()))
        {
            log::Error(_L_, hr, L"\r\nFailed to peek headers for file %s\r\n", input.name.c_str());
            return hr;
        }

        if (FAILED(hr = pCSV->PeekTypes())
            && hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))  // it is not an issue if peek types hit EOF
        {
            log::Error(_L_, hr, L"\r\nFailed to peek types for file %s\r\n", input.name.c_str());
            return hr;
        }
    }
    else
    {
        if (FAILED(hr = pCSV->SetSchema(columns)))
        {
            log::Error(_L_, hr, L"\r\nFailed to set import schema for file %s\r\n", input.name.c_str());
            return hr;
        }
        if (FAILED(hr = pCSV->SkipHeaders()))
        {
            log::Error(_L_, hr, L"\r\nFailed to skip header line for file %s\r\n", input.name.c_str());
            return hr;
        }
    }

    auto pSQL = TableOutput::GetSqlWriter(_L_, nullptr);

    if (FAILED(hr = pSQL->SetConnection(m_pSqlConnection)))
    {
        log::Error(_L_, hr, L"Failed to connect to SQL server %s\r\n", m_databaseOutput.ConnectionString.c_str());
        return hr;
    }

    if (FAILED(hr = pSQL->SetSchema(columns)))
    {
        log::Error(_L_, hr, L"Failed to add schema definition to reader\r\n");
        return hr;
    }

    if (FAILED(hr = pSQL->BindColumns(pDefItem->tableName.c_str())))
    {
        log::Error(_L_, hr, L"Failed to bind columns to SQL reader\r\n");
        return hr;
    }

    CsvToSql convert(_L_);

    if (FAILED(hr = convert.Initialize(std::move(pCSV), std::move(pSQL))))
    {
        log::Error(_L_, hr, L"\r\nFailed to initialize CsvToSql converter\r\n");
    }

    TableOutput::CSV::FileReader::Record record;
    while (SUCCEEDED(hr = convert.MoveNextLine(record)) || hr != HRESULT_FROM_WIN32(ERROR_HANDLE_EOF))
    {
        if (FAILED(hr))
        {
            log::Verbose(
                _L_, L"\r\nINFO: Failed to import line %I64d (hr=0x%lx)\r\n", convert.CSV()->GetCurrentLine(), hr);
        }
    }

    input.ullLinesImported = convert.CSV()->GetCurrentLine();
    log::Verbose(_L_, L"%I64d lines imported for %s\r\n", convert.CSV()->GetCurrentLine(), input.name.c_str());

    log::Verbose(_L_, L"\tImported %s...\r\n", input.name.c_str());

    input.Stream->Close();
    input.Stream = nullptr;
    return S_OK;
}

static HRESULT stripNonValidXMLCharacters(LPWSTR in, size_t cchSize)
{

    for (size_t i = 0; i < cchSize; i++)
    {
        auto current = in[i];
        if (!((current == 0x9) || (current == 0xA) || (current == 0xD) || ((current >= 0x20) && (current <= 0xD7FF))
              || ((current >= 0xE000) && (current <= 0xFFFD))))
        {
            in[i] = L'@';
        }
    }
    return S_OK;
}

HRESULT SqlImportAgent::ImportSingleEvent(
    ITableOutput& output,
    const ImportItem& input,
    const EVT_HANDLE& hSystemContext,
    const EVT_HANDLE& hEvent)
{
    HRESULT hr = E_FAIL;

    const auto evtlib = ExtensionLibrary::GetLibrary<EvtLibrary>(_L_);
    if (evtlib == nullptr)
    {
        log::Error(_L_, E_FAIL, L"Unable to load the wevtapi.dll extension library\r\n");
        return E_FAIL;
    }

    // When you render the user data or system section of the event, you must specify
    // the EvtRenderEventValues flag. The function returns an array of variant values
    // for each element in the user data or system section of the event. For user data
    // or event data, the values are returned in the same order as the elements are
    // defined in the event. For system data, the values are returned in the order defined
    // in the EVT_SYSTEM_PROPERTY_ID enumeration.
    DWORD dwBufferUsed = 0;
    DWORD dwPropertyCount = 0;
    CBinaryBuffer buffer;
    if (!evtlib->EvtRender(hSystemContext, hEvent, EvtRenderEventValues, 0L, nullptr, &dwBufferUsed, &dwPropertyCount))
    {
        const auto lastError = GetLastError();

        if (ERROR_INSUFFICIENT_BUFFER != lastError)
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(lastError), L"Failed to render event (%s)\r\n", input.name.c_str());
            return hr;
        }

        if (!buffer.SetCount(dwBufferUsed))
        {
            log::Error(
                _L_, hr = E_OUTOFMEMORY, L"Failed to allocate event rendering buffer (%s)\r\n", input.name.c_str());
            return hr;
        }

        if (!evtlib->EvtRender(
                hSystemContext,
                hEvent,
                EvtRenderEventValues,
                (DWORD)buffer.GetCount(),
                buffer.GetData(),
                &dwBufferUsed,
                &dwPropertyCount))
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(lastError), L"Failed to render event (%s)\r\n", input.name.c_str());
            return hr;
        }
    }

    // ComputerName
    PEVT_VARIANT pRenderedValues = (PEVT_VARIANT)buffer.GetData();
    output.WriteString(pRenderedValues[EvtSystemComputer].StringVal);

    // TimeCreated
    ULONGLONG ullTimeStamp = pRenderedValues[EvtSystemTimeCreated].FileTimeVal;
    FILETIME ft = {0};
    ft.dwHighDateTime = (DWORD)((ullTimeStamp >> 32) & 0xFFFFFFFF);
    ft.dwLowDateTime = (DWORD)(ullTimeStamp & 0xFFFFFFFF);
    output.WriteFileTime(ft);

    // Provider Name
    output.WriteString(pRenderedValues[EvtSystemProviderName].StringVal);

    // Provider GUID
    if (NULL != pRenderedValues[EvtSystemProviderGuid].GuidVal)
        output.WriteGUID(*(pRenderedValues[EvtSystemProviderGuid].GuidVal));
    else
        output.WriteNothing();

    DWORD EventID = pRenderedValues[EvtSystemEventID].UInt16Val;
    if (EvtVarTypeNull != pRenderedValues[EvtSystemQualifiers].Type)
    {
        EventID = MAKELONG(pRenderedValues[EvtSystemEventID].UInt16Val, pRenderedValues[EvtSystemQualifiers].UInt16Val);
    }
    output.WriteInteger(EventID);

    output.WriteInteger((DWORD)(
        (EvtVarTypeNull == pRenderedValues[EvtSystemVersion].Type) ? 0 : pRenderedValues[EvtSystemVersion].ByteVal));

    output.WriteInteger((DWORD)(
        (EvtVarTypeNull == pRenderedValues[EvtSystemLevel].Type) ? 0 : pRenderedValues[EvtSystemLevel].ByteVal));

    output.WriteInteger((DWORD)(
        (EvtVarTypeNull == pRenderedValues[EvtSystemOpcode].Type) ? 0 : pRenderedValues[EvtSystemOpcode].ByteVal));

    // Event record ID
    output.WriteInteger(pRenderedValues[EvtSystemEventRecordId].UInt64Val);

    // Correlation ActivityID
    if (EvtVarTypeNull != pRenderedValues[EvtSystemActivityID].Type)
        output.WriteGUID(*(pRenderedValues[EvtSystemActivityID].GuidVal));
    else
        output.WriteNothing();

    if (EvtVarTypeNull != pRenderedValues[EvtSystemRelatedActivityID].Type)
        output.WriteGUID(*(pRenderedValues[EvtSystemRelatedActivityID].GuidVal));
    else
        output.WriteNothing();

    output.WriteInteger((DWORD)pRenderedValues[EvtSystemProcessID].UInt32Val);
    output.WriteInteger((DWORD)pRenderedValues[EvtSystemThreadID].UInt32Val);

    if (EvtVarTypeNull != pRenderedValues[EvtSystemUserID].Type)
    {
        LPTSTR StringSid = NULL;
        if (ConvertSidToStringSid(pRenderedValues[EvtSystemUserID].SidVal, &StringSid))
        {
            output.WriteString(StringSid);
            LocalFree(StringSid);
        }
        else
            output.WriteNothing();
    }
    else
        output.WriteNothing();

    // The EvtRenderEventXml flag tells EvtRender to render the event as an XML string.
    CBinaryBuffer xmlbuffer;
    {
        DWORD dwBufferUsed = 0L;
        DWORD dwPropertyCount = 0L;

        if (FAILED(
                hr = evtlib->EvtRender(
                    nullptr, hEvent, EvtRenderEventXml, 0L, nullptr, &dwBufferUsed, &dwPropertyCount)))
        {
            if (HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) == hr)
            {
                if (!xmlbuffer.SetCount(dwBufferUsed))
                {
                    log::Error(
                        _L_, E_OUTOFMEMORY, L"Not enough memory to render event in xml for %s\r\n", input.name.c_str());
                    output.WriteNothing();
                }
                else if (FAILED(
                             hr = evtlib->EvtRender(
                                 nullptr,
                                 hEvent,
                                 EvtRenderEventXml,
                                 (DWORD)xmlbuffer.GetCount(),
                                 xmlbuffer.GetData(),
                                 &dwBufferUsed,
                                 &dwPropertyCount)))
                {
                    log::Error(_L_, hr, L"EvtRender failed to render event in xml for %s\r\n", input.name.c_str());
                    output.WriteNothing();
                }
                else
                {
                    stripNonValidXMLCharacters(xmlbuffer.GetP<WCHAR>(0), (DWORD)xmlbuffer.GetCount() / sizeof(WCHAR));
                    output.WriteXML(xmlbuffer.GetP<WCHAR>(0), (DWORD)xmlbuffer.GetCount() / sizeof(WCHAR));
                }
            }
            else
                output.WriteNothing();
        }
        else
            output.WriteNothing();
    }
    if (FAILED(output.WriteEndOfLine()))
    {
        log::Error(_L_, hr, L"EvtRender failed to send event for %s\r\n", input.name.c_str());
    }

    return S_OK;
}

HRESULT SqlImportAgent::ImportEvtxData(ImportItem& input)
{
    HRESULT hr = E_FAIL;

    GetSystemTime(&input.importStart);

    BOOST_SCOPE_EXIT(&input) { GetSystemTime(&input.importEnd); }
    BOOST_SCOPE_EXIT_END;

    BOOST_SCOPE_EXIT(&input)
    {
        if (input.Stream)
        {
            input.Stream->Close();
            input.Stream = nullptr;
        }
    }
    BOOST_SCOPE_EXIT_END;

    auto pWriter = GetOutputWriter(input);
    if (!pWriter)
    {
        log::Error(_L_, E_FAIL, L"Failed to create the output writer to store import %s\r\n", input.name.c_str());
        return E_FAIL;
    }
    BOOST_SCOPE_EXIT(&pWriter)
    {
        if (pWriter)
        {
            pWriter->Close();
            pWriter = nullptr;
        }
    }
    BOOST_SCOPE_EXIT_END;

    auto pStream = input.GetInputStream(_L_);

    if (!pStream)
    {
        log::Error(_L_, E_FAIL, L"No valid input stream\r\n");
        return E_FAIL;
    }

    input.ullBytesExtracted = pStream->GetSize();

    wstring strTempFile;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    wstring strID = input.name;
    std::replace(begin(strID), end(strID), L'\r', L'_');
    std::replace(begin(strID), end(strID), L'\n', L'_');
    std::replace(begin(strID), end(strID), L'\t', L'_');
    std::replace(begin(strID), end(strID), L'\\', L'_');

    if (FAILED(hr = ::UtilGetUniquePath(_L_, m_tempOutput.Path.c_str(), strID.c_str(), strTempFile, hFile)))
    {
        log::Error(_L_, hr, L"Failed to create temporary file in %s\r\n", m_tempOutput.Path.c_str());
        return hr;
    }

    BOOST_SCOPE_EXIT(strTempFile, &hFile, &_L_)
    {
        if (hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
        ::UtilDeleteTemporaryFile(_L_, strTempFile.c_str());
    }
    BOOST_SCOPE_EXIT_END;

    auto pTempStream = std::dynamic_pointer_cast<TemporaryStream>(pStream);
    if (pTempStream)
    {
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
        if (FAILED(hr = pTempStream->MoveTo(strTempFile.c_str())))
        {
            log::Error(_L_, hr, L"Failed to move file event log to file %s\r\n");
            return hr;
        }
        pTempStream->Close();
        pStream->Close();
    }
    else
    {
        auto pFileStream = std::make_shared<FileStream>(_L_);

        if (!pFileStream)
        {
            log::Error(_L_, E_OUTOFMEMORY, L"Failed to create file stream\r\n");
            return E_OUTOFMEMORY;
        }

        if (FAILED(hr = pFileStream->OpenHandle(hFile)))
        {
            log::Error(_L_, E_OUTOFMEMORY, L"Failed to open handle to temporary file\r\n");
            return E_OUTOFMEMORY;
        }
        hFile = INVALID_HANDLE_VALUE;

        ULONGLONG ullCopiedBytes = 0LL;
        if (FAILED(hr = pStream->CopyTo(pFileStream, &ullCopiedBytes)))
        {
            log::Error(_L_, hr, L"Failed to open handle to temporary file\r\n");
            return hr;
        }
        pStream->Close();
    }

    // Event Log opening

    const auto evtlib = ExtensionLibrary::GetLibrary<EvtLibrary>(_L_);
    if (evtlib == nullptr)
    {
        log::Error(_L_, E_FAIL, L"Unable to load the wevtapi.dll extension library\r\n");
        return E_FAIL;
    }
    {
        auto hResults = evtlib->EvtQuery(nullptr, strTempFile.c_str(), nullptr, EvtQueryFilePath);

        if (hResults == NULL)
        {
            log::Error(
                _L_,
                hr = HRESULT_FROM_WIN32(GetLastError()),
                L"Failed to query EventLog file %s\r\n",
                strTempFile.c_str());
            return hr;
        }

        BOOST_SCOPE_EXIT(&hResults, &evtlib) { evtlib->EvtClose(hResults); }
        BOOST_SCOPE_EXIT_END;

        constexpr auto ARRAY_SIZE = 100;
        EVT_HANDLE hEvents[ARRAY_SIZE];
        DWORD dwReturned = 0;

        // Identify the components of the event that you want to render. In this case,
        // render the system section of the event.
        EVT_HANDLE hSystemContext = evtlib->EvtCreateRenderContext(0, nullptr, EvtRenderContextSystem);
        if (NULL == hSystemContext)
        {
            log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"EvtCreateRenderContext failed\r\n");
            return hr;
        }

        BOOST_SCOPE_EXIT(&evtlib, hSystemContext)
        {
            if (hSystemContext)
                evtlib->EvtClose(hSystemContext);
        }
        BOOST_SCOPE_EXIT_END;

        while (true)
        {
            // Get a block of events from the result set.
            if (FAILED(hr = evtlib->EvtNext(hResults, ARRAY_SIZE, hEvents, INFINITE, 0, &dwReturned)))
            {
                if (hr != HRESULT_FROM_WIN32(ERROR_NO_MORE_ITEMS))
                {
                    log::Error(_L_, hr, L"Failed to retrieve events\r\n");
                    return hr;
                }
                else
                    break;
            }
            BOOST_SCOPE_EXIT(&evtlib, &hEvents, &dwReturned)
            {
                for (DWORD i = 0; i < dwReturned; i++)
                {
                    if (hEvents[i])
                    {
                        evtlib->EvtClose(hEvents[i]);
                        hEvents[i] = NULL;
                    }
                }
            }
            BOOST_SCOPE_EXIT_END;

            auto& output = pWriter->GetTableOutput();

            for (DWORD i = 0; i < dwReturned; i++)
            {

                HRESULT hr1 = ImportSingleEvent(output, input, hSystemContext, hEvents[i]);

                if (FAILED(hr1))
                {
                    output.AbandonRow();
                    log::Error(_L_, hr1, L"Failed to import event from log %s\r\n", input.name.c_str());
                }

                evtlib->EvtClose(hEvents[i]);
                hEvents[i] = NULL;

                input.ullLinesImported++;
            }
        }
    }
    return S_OK;
}

HRESULT SqlImportAgent::ImportHiveData(ImportItem& input)
{
    HRESULT hr = E_FAIL;

    GetSystemTime(&input.importStart);

    BOOST_SCOPE_EXIT(&input) { GetSystemTime(&input.importEnd); }
    BOOST_SCOPE_EXIT_END;

    BOOST_SCOPE_EXIT(&input)
    {
        if (input.Stream)
        {
            input.Stream->Close();
            input.Stream = nullptr;
        }
    }
    BOOST_SCOPE_EXIT_END;

    auto pWriter = GetOutputWriter(input);
    if (!pWriter)
    {
        log::Error(_L_, E_FAIL, L"Failed to create the output writer to store import %s\r\n", input.name.c_str());
        return E_FAIL;
    }
    BOOST_SCOPE_EXIT(&pWriter)
    {
        if (pWriter)
        {
            pWriter->Close();
            pWriter = nullptr;
        }
    }
    BOOST_SCOPE_EXIT_END;

    auto& output = pWriter->GetTableOutput();

    auto pStream = input.GetInputStream(_L_);

    if (!pStream)
    {
        log::Error(_L_, E_FAIL, L"No input stream to import for %s\r\n", input.name.c_str());
        return E_FAIL;
    }

    input.ullBytesExtracted = pStream->GetSize();

    RegistryHive regwalker(_L_);

    if (FAILED(hr = regwalker.LoadHive(*pStream)))
    {
        log::Error(_L_, hr, L"Failed to load hive %s\r\n", input.name.c_str());
        return hr;
    }

    if (FAILED(
            hr = regwalker.Walk(

                [&output, &input](const RegistryKey* pKey) {
                    output.WriteFileTime(pKey->GetKeyLastModificationTime());
                    output.WriteString(pKey->GetShortKeyName().c_str());
                    output.WriteString(pKey->GetKeyName().c_str());

                    output.WriteNothing();
                    output.WriteNothing();
                    output.WriteNothing();

                    output.WriteEndOfLine();

                    input.ullLinesImported++;
                },
                [&output, &input](const RegistryValue* pValue) {
                    output.WriteFileTime(pValue->GetParentKey()->GetKeyLastModificationTime());
                    output.WriteString(pValue->GetParentKey()->GetShortKeyName().c_str());
                    output.WriteString(pValue->GetParentKey()->GetKeyName().c_str());

                    output.WriteString(pValue->GetValueName().c_str());

                    DWORD i = 0;
                    while (g_ValyeTypeDefinitions[i].Type != pValue->GetType() && i < _countof(g_ValyeTypeDefinitions))
                    {
                        i++;
                    }
                    if (i == _countof(g_ValyeTypeDefinitions))
                        output.WriteString("REG_NO_TYPE (!)");
                    else
                        output.WriteString(g_ValyeTypeDefinitions[i].szTypeName);

                    const BYTE* pDatas = NULL;
                    size_t dwLen = 0;

                    dwLen = pValue->GetDatas(&pDatas);

                    switch (pValue->GetType())
                    {
                        case ValueType::RegNone:
                            output.WriteNothing();
                            break;
                        case ValueType::RegSZ:
                        case ValueType::ExpandSZ:
                            if ((dwLen != 0) && (pDatas != nullptr))
                                output.WriteString((WCHAR*)(pDatas));
                            else
                                output.WriteNothing();
                            break;
                        case ValueType::RegDWORD:
                        case ValueType::RegDWORDBE:
                            if ((dwLen != 0) && (pDatas != nullptr))
                                output.WriteInteger(*(DWORD*)(pDatas));
                            else
                                output.WriteNothing();
                            break;
                        case ValueType::RegQWORD:
                            if ((dwLen != 0) && (pDatas != nullptr))
                                output.WriteInteger(((LARGE_INTEGER*)pDatas)->QuadPart);
                            else
                                output.WriteNothing();
                            break;
                        case ValueType::RegBin:
                            if ((dwLen != 0) && (pDatas != nullptr))
                                output.WriteBytes(pDatas, (DWORD)dwLen);
                            else
                                output.WriteNothing();
                            break;
                        case ValueType::RegLink:
                            output.WriteNothing();
                            break;
                        case ValueType::RegMaxType:
                            output.WriteNothing();
                            break;
                        case ValueType::RegResourceDescr:
                            output.WriteNothing();
                            break;
                        case ValueType::RegResourceList:
                            output.WriteNothing();
                            break;
                        case ValueType::RegResourceReqList:
                            output.WriteNothing();
                            break;
                        case ValueType::RegMultiSZ:
                        {
                            WCHAR* wstrTmp = (WCHAR*)pDatas;
                            size_t dwTmp = 0;
                            bool bFirst = true;
                            wstringstream stream;

                            while (dwTmp <= (dwLen / 2))
                            {

                                if (wcslen(wstrTmp) == 0)
                                {
                                    break;
                                }

                                if (bFirst)
                                {
                                    stream << L"|" << wstrTmp;
                                    bFirst = false;
                                }

                                dwTmp += wcslen(wstrTmp);
                                wstrTmp = wstrTmp + 1 + wcslen(wstrTmp);
                            }

                            output.WriteString(stream.str().c_str());
                        }
                        break;
                        default:
                            output.WriteNothing();
                            break;
                    }

                    output.WriteEndOfLine();

                    input.ullLinesImported++;
                })))
    {
        log::Error(_L_, hr, L"Failed to load hive %s\r\n", input.name.c_str());
        return hr;
    }

    return S_OK;
}

HRESULT SqlImportAgent::Finalize()
{
    HRESULT hr = E_FAIL;

    if (!m_TableDefinition.first.AfterStatement.empty())
    {
        log::Info(
            _L_, L"\tExecuting \"after\" import statement for table %s\r\n", m_TableDefinition.first.name.c_str());
        if (FAILED(hr = m_pSqlConnection->ExecuteStatement(m_TableDefinition.first.AfterStatement)))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to execute \"after\" statement for table %s\r\n",
                m_TableDefinition.first.name.c_str());
        }
    }
    return S_OK;
}

HRESULT SqlImportAgent::ImportTask(ImportMessage::Message request)
{
    HRESULT hr = E_FAIL;

    ImportMessage::RequestType type = request->m_Request;

    switch (request->Item().format)
    {
        case ImportItem::CSV:
            switch (type)
            {
                case ImportMessage::Import:
                    if (FAILED(hr = ImportCSVData(request->m_item)))
                    {
                        SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        return hr;
                    }
                    SendResult(ImportNotification::MakeImportNotification(request->m_item));
                    break;
            }
            break;
        case ImportItem::EventLog:
            switch (type)
            {
                case ImportMessage::Import:
                    if (FAILED(hr = ImportEvtxData(request->m_item)))
                    {
                        SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        return hr;
                    }
                    SendResult(ImportNotification::MakeImportNotification(request->m_item));
                    break;
            }
            break;
        case ImportItem::RegistryHive:
            switch (type)
            {
                case ImportMessage::Import:
                    if (FAILED(hr = ImportHiveData(request->m_item)))
                    {
                        SendResult(ImportNotification::MakeFailureNotification(hr, request->m_item));
                        return hr;
                    }
                    SendResult(ImportNotification::MakeImportNotification(request->m_item));
                    break;
            }
            break;
        default:
            break;
    }
    return S_OK;
}

void SqlImportAgent::run()
{
    HRESULT hr = E_FAIL;

    bool bStopping = false;

    ImportMessage::Message request = GetRequest();

    while (request)
    {
        if (request->m_Request == ImportMessage::Complete)
        {
            log::Verbose(_L_, L"ImportAgent received a complete message\r\n");
        }
        else
        {
            if (m_pSqlConnection == nullptr)
            {
                auto notify = ImportNotification::MakeFailureNotification(E_FAIL, request->m_item);
                SendResult(notify);
            }
            else
            {
                ImportTask(request);
            }
        }

        if (request->m_Request == ImportMessage::Complete)
        {
            bStopping = true;
        }

        request = nullptr;

        if (bStopping)
        {
            if (!concurrency::try_receive<ImportMessage::Message>(m_source, request))
            {
                request = nullptr;
                done();
            }
        }
        else
            request = GetRequest();
    }

    return;
}

SqlImportAgent::~SqlImportAgent()
{
    if (m_pSqlConnection)
    {
        HRESULT hr = E_FAIL;
        if (FAILED(hr = m_pSqlConnection->Disconnect()))
        {
            log::Error(_L_, hr, L"Could not disconnet from SQL %s\r\n", m_databaseOutput.ConnectionString.c_str());
        }
        m_pSqlConnection = nullptr;
    }
}
