//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include "StructuredOutputWriter.h"

#include "XmlOutputWriter.h"

#include "OutputSpec.h"

#include "LogFileWriter.h"

#include "FileStream.h"

using namespace Orc;

std::shared_ptr<StructuredOutputWriter> StructuredOutputWriter::GetWriter(
    const logger& pLog,
    const std::shared_ptr<ByteStream>& stream,
    OutputSpec::Encoding encoding)
{
    HRESULT hr = E_FAIL;

    auto retval = std::make_shared<XmlOutputWriter>(pLog);

    if (FAILED(hr = retval->SetOutput(stream, encoding)))
    {
        log::Error(pLog, hr, L"Failed to set output to stream\r\n");
        return nullptr;
    }
    return retval;
}

std::shared_ptr<StructuredOutputWriter> StructuredOutputWriter::GetWriter(const logger& pLog, const OutputSpec& outFile)
{
    HRESULT hr = E_FAIL;

    switch (outFile.Type)
    {
        case OutputSpec::Kind::None:
            return nullptr;
        case OutputSpec::Kind::File:
        case OutputSpec::Kind::StructuredFile:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::XML:
        case OutputSpec::Kind::StructuredFile | OutputSpec::Kind::JSON:
        {
            auto retval = std::make_shared<XmlOutputWriter>(pLog);

            auto stream = std::make_shared<FileStream>(pLog);

            if (FAILED(hr = stream->WriteTo(outFile.Path.c_str())))
            {
                log::Error(pLog, hr, L"Failed to open file %s for writing\r\n", outFile.Path.c_str());
                return nullptr;
            }

            if (FAILED(hr = retval->SetOutput(stream, outFile.OutputEncoding)))
            {
                log::Error(pLog, hr, L"Failed to set output to %s\r\n", outFile.Path.c_str());
                return nullptr;
            }
            return retval;
        }
        break;
        case OutputSpec::Kind::Directory:
            break;
        default:
            log::Error(pLog, E_INVALIDARG, L"Invalid type of output to create StructuredOutputWriter\r\n");
            return nullptr;
    }
    return nullptr;
}

std::shared_ptr<StructuredOutputWriter> StructuredOutputWriter::GetWriter(
    const logger& pLog,
    const OutputSpec& outFile,
    const std::wstring& strPattern,
    const std::wstring& strName)
{
    HRESULT hr = E_FAIL;

    if (outFile.Type != OutputSpec::Kind::Directory)
    {
        log::Error(pLog, E_INVALIDARG, L"Invalid type of output to create StructuredOutputWriter\r\n");
        return nullptr;
    }

    std::wstring strFile;
    OutputWriter::GetOutputFileName(strPattern, strName, strFile);

    auto retval = std::make_shared<XmlOutputWriter>(pLog);

    WCHAR szOutputFile[MAX_PATH];
    StringCchPrintf(szOutputFile, MAX_PATH, L"%s\\%s", outFile.Path.c_str(), strFile.c_str());
    auto stream = std::make_shared<FileStream>(pLog);

    if (FAILED(hr = stream->WriteTo(szOutputFile)))
    {
        log::Error(pLog, hr, L"Failed to open file %s for writing\r\n", szOutputFile);
        return nullptr;
    }

    if (FAILED(hr = retval->SetOutput(stream)))
    {
        log::Error(pLog, hr, L"Failed to set output to %s\r\n", szOutputFile);
        return nullptr;
    }
    return retval;
}
