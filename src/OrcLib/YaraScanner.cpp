//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "EmbeddedResource.h"
#include "MemoryStream.h"
#include "FileStream.h"
#include "FileMappingStream.h"

#include "WideAnsi.h"
#include "ParameterCheck.h"

#include "ConfigFile_Common.h"

#include "YaraScanner.h"
#include "YaraStaticExtension.h"
#include "yara.h"

#include <boost/algorithm/string.hpp>

using namespace Orc;

Orc::YaraConfig Orc::YaraConfig::Get(const logger& pLog, const ConfigItem& item)
{
    HRESULT hr = E_FAIL;
    YaraConfig retval;

    if (item[CONFIG_YARA_SOURCE])
    {
        boost::split(retval.Sources(), (std::wstring_view) item[CONFIG_YARA_SOURCE], boost::is_any_of(L",;"));
    }

    if (item[CONFIG_YARA_BLOCK])
    {
        LARGE_INTEGER blockSize = {0L};
        if (SUCCEEDED(GetFileSizeFromArg(item[CONFIG_YARA_BLOCK].c_str(), blockSize))
            && blockSize.QuadPart > 0LL)
        {
            if (blockSize.QuadPart > MAXDWORD)
            {
                log::Error(pLog, hr, L"Configured block size too big (%I64d)\r\n", blockSize.QuadPart);
                return retval;
            }
            if (FAILED(hr = retval.SetBlockSize(blockSize.LowPart)))
            {
                log::Error(
                    pLog, hr, L"Failed to configure block size with %s\r\n", item[CONFIG_YARA_BLOCK].c_str());
                return retval;
            }
        }
    }
    if (item[CONFIG_YARA_OVERLAP])
    {
        LARGE_INTEGER overlapSize = {0L};
        if (SUCCEEDED(GetFileSizeFromArg(item[CONFIG_YARA_OVERLAP].c_str(), overlapSize))
            && overlapSize.QuadPart > 0LL)
        {
            if (overlapSize.QuadPart > MAXDWORD)
            {
                log::Error(pLog, hr, L"Configured overlap size too big (%I64d)\r\n", overlapSize.QuadPart);
                return retval;
            }
            if (FAILED(hr = retval.SetOverlapSize(overlapSize.LowPart)))
            {
                log::Error(
                    pLog,
                    hr,
                    L"Failed to configure overlap size with %s\r\n",
                    item[CONFIG_YARA_OVERLAP].c_str());
                return retval;
            }
        }
    }
    if (item[CONFIG_YARA_TIMEOUT])
    {
        DWORD timeout = 0L;
        if (SUCCEEDED(GetIntegerFromArg(item[CONFIG_YARA_TIMEOUT].c_str(), timeout)))
        {
            retval.SetTimeOut(std::chrono::seconds(timeout));
        }
    }
    if (item[CONFIG_YARA_SCAN_METHOD])
    {
        if (FAILED(retval.SetScanMethod((std::wstring)item[CONFIG_YARA_SCAN_METHOD])))
        {
            log::Error(
                pLog,
                hr,
                L"Failed to configure scan method with %s\r\n",
                item[CONFIG_YARA_SCAN_METHOD].c_str());
            return retval;
        }
    }

    retval._isValid = true;
    return retval;
}

HRESULT Orc::YaraConfig::SetScanMethod(const std::wstring& strMethod)
{
    if (!_wcsicmp(strMethod.c_str(), L"blocks"))
        _scanMethod = YaraScanMethod::Blocks;
    else if (!_wcsicmp(strMethod.c_str(), L"filemapping"))
        _scanMethod = YaraScanMethod::FileMapping;
    else
        return E_INVALIDARG;
    return S_OK;
}

HRESULT Orc::YaraScanner::Initialize(bool bWithCompiler)
{
    if (!m_yara)
    {
        m_yara = std::make_shared<YaraStaticExtension>(_L_);
        if (!m_yara)
        {
            log::Error(_L_, E_FAIL, L"Failed to load yara library\r\n");
            return E_FAIL;
        }

        m_yara->yr_initialize();
    }

    if (bWithCompiler && !m_pCompiler)
    {
        m_yara->yr_compiler_create(&m_pCompiler);
        if (!m_pCompiler)
        {
            log::Error(_L_, E_FAIL, L"Failed to create yara rules compiler\r\n");
            return E_FAIL;
        }
        m_yara->yr_compiler_set_callback(m_pCompiler, compiler_callback, this);
    }

    return S_OK;
}

HRESULT Orc::YaraScanner::Configure(std::unique_ptr<YaraConfig>& config)
{
    if (config && config->isValid())
    {
        if (config->_blockSize.has_value())
            m_config._blockSize = config->_blockSize.value();
        if (config->_overlapSize.has_value())
            m_config._overlapSize = config->_overlapSize.value();
        if (config->_timeOut.has_value())
            m_config._timeOut = config->_timeOut.value();

        m_config = *config;
    }
    else
    {
        m_config = YaraConfig();
        log::Verbose(_L_, L"Invalid or missing yara configuration, using default\r\n");
    }

    return S_OK;
}

HRESULT Orc::YaraScanner::AddRules(const std::wstring& yara_content_spec)
{
    HRESULT hr = E_FAIL;

    CBinaryBuffer buffer;

    if (EmbeddedResource::IsResourceBased(yara_content_spec))
    {

        if (FAILED(hr = EmbeddedResource::ExtractToBuffer(_L_, yara_content_spec, buffer)))
        {
            log::Error(_L_, hr, L"Failed to find&extract ressource %s\r\n", yara_content_spec.c_str());
            return hr;
        }

        if (FAILED(hr = AddRules(buffer)))
        {
            log::Error(_L_, hr, L"Failed to add rules from ressource %s\r\n", yara_content_spec.c_str());
            return hr;
        }
    }
    else
    {
        auto fstream = std::make_shared<FileStream>(_L_);
        if (!fstream)
            return E_OUTOFMEMORY;

        if (FAILED(hr = fstream->ReadFrom(yara_content_spec.c_str())))
        {
            log::Error(_L_, hr, L"Failed to open rules file %s\r\n", yara_content_spec.c_str());
            return hr;
        }

        if (FAILED(hr = AddRules(fstream)))
        {
            log::Error(_L_, hr, L"Failed to add rules from ressource %s\r\n", yara_content_spec.c_str());
            return hr;
        }
    }
    return S_OK;
}

HRESULT Orc::YaraScanner::AddRules(const std::shared_ptr<ByteStream>& stream)
{
    HRESULT hr = E_FAIL;
    auto memstream = std::dynamic_pointer_cast<Orc::MemoryStream>(stream);
    if (!memstream)
    {
        memstream = std::make_shared<MemoryStream>(_L_);

        if (FAILED(hr = memstream->OpenForReadWrite((ULONG)stream->GetSize())))
        {
            log::Error(_L_, hr, L"Failed to open memstream for %I64d bytes read/write\r\n", stream->GetSize());
            return hr;
        }
        ULONGLONG bytesCopied = 0LL;
        if (FAILED(hr = stream->CopyTo(memstream, &bytesCopied)))
        {
            log::Error(_L_, hr, L"Failed to copy yara stream's %I64d bytes\r\n", stream->GetSize());
            return hr;
        }
    }

    CBinaryBuffer buffer;
    memstream->GrabBuffer(buffer);

    if (buffer.empty())
    {
        log::Error(_L_, hr, L"Invalid empty yara rules buffer\r\n");
        return hr;
    }

    return AddRules(buffer);
}

HRESULT Orc::YaraScanner::AddRules(CBinaryBuffer& buffer)
{
    m_ErrorCount = 0;
    m_WarningCount = 0;

    // we need to make sure that buffer is null terminated because libyara heavily relies on this
    if (buffer.Get<UCHAR>(buffer.GetCount<UCHAR>() - sizeof(UCHAR)) != '\0')
    {
        buffer.SetCount(buffer.GetCount<UCHAR>() + sizeof(UCHAR));
        buffer.Get<UCHAR>(buffer.GetCount<UCHAR>() - sizeof(UCHAR)) = '\0';
    }

    auto errnum = m_yara->yr_compiler_add_string(m_pCompiler, buffer.GetP<const char>(), nullptr);

    if (errnum > 0)
    {
        log::Error(_L_, E_INVALIDARG, L"Errors occured while compiling YARA rules\r\n");
        return E_INVALIDARG;
    }

    return S_OK;
}

std::vector<std::string> Orc::YaraScanner::GetRulesSpec(const logger& pLog, LPCSTR szRules)
{
    HRESULT hr = E_FAIL;
    std::vector<std::string> rules;

    boost::split(rules, szRules, boost::is_any_of(";,"));

    if (rules.empty())
    {
        log::Error(pLog, E_INVALIDARG, L"Empty rules list to enable\r\n");
    }
    return rules;
}

std::vector<std::string> Orc::YaraScanner::GetRulesSpec(const logger& pLog, LPCWSTR szRules)
{
    HRESULT hr = E_FAIL;
    std::vector<std::string> rules;

    CBinaryBuffer buffer;
    if (FAILED(WideToAnsi(pLog, szRules, buffer)))
        return rules;

    auto pRules = buffer.GetP<CHAR>();
    boost::split(rules, pRules, boost::is_any_of(";,"));

    if (rules.empty())
    {
        log::Error(pLog, E_INVALIDARG, L"Empty rules list to enable\r\n");
    }
    return rules;
}

YR_RULES* Orc::YaraScanner::GetRules()
{
    if (m_pRules)
        return m_pRules;

    m_yara->yr_compiler_get_rules(m_pCompiler, &m_pRules);

    return m_pRules;
}

HRESULT Orc::YaraScanner::EnableRule(LPCSTR strRule)
{
    auto rules = GetRulesSpec(_L_, strRule);
    if (rules.empty())
        return E_INVALIDARG;

    YR_RULES* yr_rules = GetRules();
    if (!yr_rules)
    {
        log::Error(_L_, E_INVALIDARG, L"No compiled rules to enable\r\n");
        return E_INVALIDARG;
    }

    YR_RULE* yr_rule = nullptr;
    yr_rules_foreach(yr_rules, yr_rule)
    {
        for (const auto& rule : rules)
        {
            if (PathMatchSpecA(yr_rule->identifier, rule.c_str()))
            {
                m_yara->yr_rule_enable(yr_rule);
            }
        }
    }

    return S_OK;
}

HRESULT Orc::YaraScanner::DisableRule(LPCSTR strRule)
{
    auto rules = GetRulesSpec(_L_, strRule);
    if (rules.empty())
        return E_INVALIDARG;

    YR_RULES* yr_rules = GetRules();
    if (!yr_rules)
    {
        log::Error(_L_, E_INVALIDARG, L"No compiled rules to enable\r\n");
        return E_INVALIDARG;
    }

    YR_RULE* yr_rule = nullptr;
    yr_rules_foreach(yr_rules, yr_rule)
    {
        for (const auto& rule : rules)
        {
            if (PathMatchSpecA(yr_rule->identifier, rule.c_str()))
            {
                m_yara->yr_rule_disable(yr_rule);
            }
        }
    }

    return S_OK;
}

std::pair<std::vector<std::string>, std::vector<std::string>>
Orc::YaraScanner::ScannedRules(std::vector<std::string> rules)
{
    auto retval = std::make_pair(std::vector<std::string>(), std::vector<std::string>());

    std::sort(begin(rules), end(rules));
    auto new_end = std::unique(begin(rules), end(rules));
    rules.erase(new_end, end(rules));

    if (rules.empty())
        return retval;

    YR_RULES* yr_rules = GetRules();
    if (!yr_rules)
    {
        log::Error(_L_, E_INVALIDARG, L"No compiled rules to enable\r\n");
        return retval;
    }

    for (const auto& rule : rules)
    {
        bool bFound = false;
        YR_RULE* yr_rule = nullptr;
        yr_rules_foreach(yr_rules, yr_rule)
        {
            if (PathMatchSpecA(yr_rule->identifier, rule.c_str()) && !RULE_IS_DISABLED(yr_rule))
            {
                retval.first.push_back(rule.c_str());
                bFound = true;
                break;
            }
        }
        if (!bFound)
            retval.second.push_back(rule.c_str());
    }

    if (rules.size() != retval.first.size() + retval.second.size())
    {
        log::Debug(_L_, L"Lost some rules while checking if scanned\r\n");
    }

    return retval;
}

HRESULT Orc::YaraScanner::Scan(const LPCWSTR& szFileName, MatchingRuleCollection& matchingRules)
{
    HRESULT hr = E_FAIL;
    auto fileStream = std::make_shared<FileStream>(_L_);

    if (FAILED(hr = fileStream->ReadFrom(szFileName)))
    {
        log::Error(_L_, hr, L"Failed to open %s for yara scan\r\n", szFileName);
        return hr;
    }

    if (FAILED(Scan(fileStream, matchingRules)))
    {
        log::Error(_L_, hr, L"Failed to scan %s for yara scan\r\n", szFileName);
        return hr;
    }

    fileStream->Close();

    return S_OK;
}

HRESULT Orc::YaraScanner::Scan(const CBinaryBuffer& buffer, ULONG bytesToScan, MatchingRuleCollection& matchingRules)
{
    if (bytesToScan == 0)
        return S_OK;

    YR_RULES* pRules = GetRules();

    auto scan_details = std::make_pair(this, &matchingRules);

    switch (m_yara->yr_rules_scan_mem(
        pRules,
        buffer.GetP<const uint8_t>(),
        bytesToScan,
        0,
        scan_callback,
        &scan_details,
        (int)std::chrono::seconds(m_config.timeOut()).count()))
    {
        case ERROR_SUCCESS:
            return S_OK;
        case ERROR_INSUFFICIENT_MEMORY:
            return E_OUTOFMEMORY;
        case ERROR_TOO_MANY_SCAN_THREADS:
            log::Error(_L_, E_FAIL, L"Too many scan threads\r\n");
            return E_FAIL;
        case ERROR_SCAN_TIMEOUT:
            log::Error(_L_, E_FAIL, L"Yara scan timeout\r\n");
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        case ERROR_CALLBACK_ERROR:
            log::Error(_L_, E_FAIL, L"Yara callback return an error\r\n");
            return E_FAIL;
        case ERROR_TOO_MANY_MATCHES:
            log::Error(_L_, E_FAIL, L"Too many matches in yara scan\r\n");
            return S_OK;
        default:
            log::Error(_L_, E_FAIL, L"Undefined error code\r\n");
            return E_FAIL;
    }
}

HRESULT Orc::YaraScanner::Scan(const std::shared_ptr<ByteStream>& stream, MatchingRuleCollection& matchingRules)
{
    HRESULT hr = E_FAIL;
    if (stream->GetSize() < m_config.blockSize())
    {
        // Simple case, we scan all the file in one bloc
        auto [hr, memstream] = GetMemoryStream(stream);
        if (FAILED(hr))
            return hr;
        if (!memstream)
            return E_FAIL;

        CBinaryBuffer buffer;
        memstream->GrabBuffer(buffer);

        return Scan(buffer, (ULONG)buffer.GetCount(), matchingRules);
    }
    else
    {
        switch (m_config.ScanMethod())
        {
            case YaraScanMethod::Blocks:
                return Scan(stream, m_config.blockSize(), m_config.overlapSize(), matchingRules);
            case YaraScanMethod::FileMapping:
            {
                ULONG ulBytesScanned = 0;
                return ScanFileMapping(stream, matchingRules, ulBytesScanned);
            }
            default:
                return E_UNEXPECTED;
        }
    }
}

HRESULT Orc::YaraScanner::ScanFrom(
    const std::shared_ptr<ByteStream>& stream,
    DWORD dwMoveMethod,
    ULONGLONG pos,
    CBinaryBuffer& buffer,
    MatchingRuleCollection& matchingRules,
    ULONG& bytesScanned)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = stream->SetFilePointer(pos, dwMoveMethod, NULL)))
    {
        log::Error(_L_, hr, L"Failed to move inside stream for yara scan\r\n");
        return hr;
    }

    ULONGLONG leftToRead = buffer.GetCount();
    ULONGLONG bytesRead = 0L;

    while (leftToRead > 0)
    {
        ULONGLONG ullBytesRead = 0LL;
        if (FAILED(
                hr = stream->Read(buffer.GetP<BYTE>(static_cast<size_t>(bytesRead)), (UINT)leftToRead, &ullBytesRead)))
        {
            log::Error(_L_, hr, L"Failed to read %d bytes from stream for yara scan\r\n", buffer.GetCount());
            break;
        }

        if (ullBytesRead == 0)
            break;

        bytesRead += ullBytesRead;
        leftToRead -= ullBytesRead;
    }

    if (FAILED(hr = Scan(buffer, static_cast<ULONG>(bytesRead), matchingRules)))
    {
        log::Error(_L_, hr, L"Yara scan failed\r\n");
        bytesScanned = 0L;
        return hr;
    }
    bytesScanned = static_cast<ULONG>(bytesRead);
    return S_OK;
}

HRESULT Orc::YaraScanner::ScanFileMapping(
    const std::shared_ptr<ByteStream>& stream,
    MatchingRuleCollection& matchingRules,
    ULONG& bytesScanned)
{
    auto fmstream = std::make_shared<FileMappingStream>(_L_);
    if (!fmstream)
        return E_POINTER;

    HRESULT hr = fmstream->Open(INVALID_HANDLE_VALUE, PAGE_READWRITE, stream->GetSize(), L"YaraScan");
    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"Failed to create pagefile backed filemapping (size:%I64d)\r\n", stream->GetSize());
        return hr;
    }

    ULONGLONG ullBytesCopied = 0LL;
    if (FAILED(hr = stream->CopyTo(fmstream, &ullBytesCopied)))
    {
        log::Error(
            _L_,
            hr,
            L"Failed to copy file into file mapping (size:%I64d, copied:%I64d)\r\n",
            stream->GetSize(),
            ullBytesCopied);
        return hr;
    }

    if (ullBytesCopied != stream->GetSize())
    {
        log::Error(
            _L_,
            hr,
            L"Failed to copy all file content into file mapping (size:%I64d, copied:%I64d)\r\n",
            stream->GetSize(),
            ullBytesCopied);
        return hr;
    }

    auto buffer = fmstream->GetMappedData();

    if (buffer.GetCount() == ullBytesCopied)  // all good
    {
        if (FAILED(hr = Scan(buffer, matchingRules)))
        {
            log::Error(_L_, hr, L"Failed to scan file content in file mapping\r\n", stream->GetSize(), ullBytesCopied);
            return hr;
        }
    }
    else
    {
        log::Error(
            _L_,
            hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            L"Failed to scan file content in file mapping\r\n",
            stream->GetSize(),
            ullBytesCopied);
        return hr;
    }
    return S_OK;
}

HRESULT Orc::YaraScanner::Scan(
    const std::shared_ptr<ByteStream>& stream,
    ULONG blockSize,
    ULONG overlapSize,
    MatchingRuleCollection& matchingRules)
{
    HRESULT hr = E_FAIL;
    ULONGLONG ullBytesToScan = stream->GetSize();

    if (ullBytesToScan < blockSize)
        return Scan(stream, matchingRules);

    ULONGLONG ullBytesScanned = 0LL;

    if (FAILED(hr = stream->SetFilePointer(0, FILE_BEGIN, NULL)))
        return hr;

    CBinaryBuffer buffer(true);
    buffer.SetCount(blockSize);

    CBinaryBuffer overlap(true);
    overlap.SetCount(overlapSize);

    bool scanOverlapFlag = false;
    UINT overlapCurrentSize = 0L;
    UINT sizeToCopy = 0L;
    UINT posInBuffer = 0L;

    while (ullBytesScanned < ullBytesToScan)
    {
        ULONG bytes = 0L;
        if (FAILED(hr = ScanFrom(stream, FILE_CURRENT, 0LL, buffer, matchingRules, bytes)))
        {
            log::Error(_L_, hr, L"Stream yara scan failed\r\n");
            return hr;
        }
        if (bytes == 0)  // we have reached a (portentially unexpected) end of file
            return S_OK;

        // Saving beginning of block in end of overlap buffer
        sizeToCopy = std::min(bytes, overlapSize / 2);
        if (auto err = memcpy_s(overlap.GetP<BYTE>(overlapCurrentSize), sizeToCopy, buffer.GetP<BYTE>(), sizeToCopy))
        {
            log::Error(_L_, E_INVALIDARG, L"Error executing memcpy_s.\r\n");
        }
        overlapCurrentSize += sizeToCopy;

        // Scan overlap (but not the first time)
        if (scanOverlapFlag == true)
        {
            if (FAILED(hr = Scan(overlap, overlapCurrentSize, matchingRules)))
            {
                log::Error(_L_, hr, L"Stream yara overlap scan failed\r\n");
                return hr;
            }
            overlapCurrentSize = 0L;
        }
        else
        {
            scanOverlapFlag = true;
        }

        // Saving end of block in beginning of overlap buffer
        // Useless if bytes <= blocksize
        if (bytes < overlapSize / 2)
        {
            sizeToCopy = bytes;
            posInBuffer = 0;
        }
        else
        {
            sizeToCopy = overlapSize / 2;
            posInBuffer = bytes - sizeToCopy;
        }
        if (auto err = memcpy_s(overlap.GetP<BYTE>(), sizeToCopy, buffer.GetP<BYTE>(posInBuffer), sizeToCopy))
        {
            log::Error(_L_, E_INVALIDARG, L"Error executing memcpy_s.\r\n");
        }
        overlapCurrentSize = sizeToCopy;

        ullBytesScanned += bytes;
    }

    return S_OK;
}

HRESULT Orc::YaraScanner::PrintConfiguration()
{
    YR_RULES* yr_rules = GetRules();
    if (!yr_rules)
    {
        log::Error(_L_, E_INVALIDARG, L"No compiled rules to enable\r\n");
        return E_INVALIDARG;
    }

    log::Info(_L_, L"Yara scanner is configured with the following rules:\r\n");

    YR_RULE* yr_rule = nullptr;
    yr_rules_foreach(yr_rules, yr_rule)
    {
        log::Info(
            _L_, L"\tRule: %S (%s)\r\n", yr_rule->identifier, RULE_IS_DISABLED(yr_rule) ? L"disabled" : L"enabled");
    }
    return S_OK;
}

Orc::YaraScanner::~YaraScanner()
{
    if (m_pCompiler)
    {
        m_yara->yr_compiler_destroy(m_pCompiler);
        m_pCompiler = nullptr;
    }
}

void Orc::YaraScanner::compiler_message(int error_level, const char* file_name, int line_number, const char* message)
{
    switch (error_level)
    {
        case YARA_ERROR_LEVEL_ERROR:
            log::Error(_L_, E_FAIL, L"Error in yara rule: \"%S\" (line:%d)\r\n", message, line_number);
            m_ErrorCount++;
            break;
        case YARA_ERROR_LEVEL_WARNING:
            log::Error(_L_, E_FAIL, L"Warning in yara rule: \"%S\" (line:%d)\r\n", message, line_number);
            m_WarningCount++;
            break;
        default:
            log::Info(_L_, L"level:%d, line:%d, \"%S\"\r\n", error_level, line_number, message);
            break;
    }
}

int Orc::YaraScanner::scan_message(int message, void* message_data, MatchingRuleCollection& matchingRules)
{
    switch (message)
    {
        case CALLBACK_MSG_RULE_MATCHING:
        {
            log::Verbose(_L_, L"Text matched rule: %S\r\n", ((YR_RULE*)message_data)->identifier);
            matchingRules.push_back(((YR_RULE*)message_data)->identifier);
            break;
        }
        case CALLBACK_MSG_RULE_NOT_MATCHING:
            log::Verbose(_L_, L"Text did not match rule: %S\r\n", ((YR_RULE*)message_data)->identifier);
            break;
        case CALLBACK_MSG_SCAN_FINISHED:
            log::Verbose(_L_, L"Scan finished\r\n");
            break;
        case CALLBACK_MSG_IMPORT_MODULE:
            log::Verbose(_L_, L"Importing module %S\r\n", ((YR_MODULE_IMPORT*)message_data)->module_name);
            break;
        case CALLBACK_MSG_MODULE_IMPORTED:
            log::Verbose(_L_, L"Module %S imported\r\n", ((YR_OBJECT_STRUCTURE*)message_data)->identifier);
            break;
        default:
            break;
    }
    return CALLBACK_CONTINUE;
}

int Orc::YaraScanner::scan_callback(YR_SCAN_CONTEXT* context, int message, void* message_data, void* user_data)
{
    auto scan_data = (ScanData*)user_data;
    if (scan_data)
        return scan_data->first->scan_message(message, message_data, *(scan_data->second));
    else
        return CALLBACK_ERROR;
}

size_t Orc::YaraScanner::read(void* ptr, size_t size, size_t count, void* user_data)
{
    auto pStream = (ByteStream*)user_data;

    if (!pStream || !pStream->IsOpen() || !pStream->CanRead() == S_OK)
        return 0;

    ULONGLONG cbBytesRead = 0LL;

    if (FAILED(pStream->Read(ptr, size * count, &cbBytesRead)))
    {
        return 0;
    }
    return (size_t)cbBytesRead;
}

size_t Orc::YaraScanner::write(const void* ptr, size_t size, size_t count, void* user_data)
{
    auto pStream = (ByteStream*)user_data;

    if (!pStream || !pStream->IsOpen() || !pStream->CanWrite() == S_OK)
        return 0;

    ULONGLONG cbBytesWritten = 0LL;

    if (FAILED(pStream->Write((const PVOID)ptr, size * count, &cbBytesWritten)))
    {
        return 0;
    }
    return (size_t)cbBytesWritten;
}

struct YaraStream : YR_STREAM
{
    std::shared_ptr<ByteStream> byteStream;
};

std::pair<HRESULT, std::shared_ptr<MemoryStream>>
Orc::YaraScanner::GetMemoryStream(const std::shared_ptr<ByteStream>& byteStream)
{
    HRESULT hr = E_FAIL;
    auto memstream = std::dynamic_pointer_cast<MemoryStream>(byteStream);

    if (!memstream)
    {
        memstream = std::make_shared<MemoryStream>(_L_);
        if (!memstream)
            return std::make_pair(E_OUTOFMEMORY, nullptr);

        if (FAILED(hr = memstream->OpenForReadWrite(static_cast<DWORD>(byteStream->GetSize()))))
        {
            log::Error(_L_, hr, L"Failed to allocate %I64d bytes in memory stream\r\n", byteStream->GetSize());
            return std::make_pair(hr, nullptr);
        }
        ULONG64 ullBytesCopied = 0LL;
        if (FAILED(hr = byteStream->CopyTo(memstream, &ullBytesCopied)))
        {
            log::Error(_L_, hr, L"Failed to load stream %I64d bytes into memory stream\r\n", byteStream->GetSize());
            return std::make_pair(hr, nullptr);
        }
        if (ullBytesCopied != byteStream->GetSize())
        {
            log::Error(_L_, hr, L"Failed to load stream %I64d bytes into memory stream\r\n", byteStream->GetSize());
            return std::make_pair(hr, nullptr);
        }
    }
    return std::make_pair(S_OK, memstream);
}

std::unique_ptr<YR_STREAM> Orc::YaraScanner::GetYaraStream(const std::shared_ptr<ByteStream>& byteStream)
{
    auto retval = std::unique_ptr<YaraStream>();

    retval->read = YaraScanner::read;
    retval->write = YaraScanner::write;
    retval->byteStream = byteStream;
    retval->user_data = retval->byteStream.operator->();

    return retval;
}
