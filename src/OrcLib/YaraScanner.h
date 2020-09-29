//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "YaraStaticExtension.h"

#include <chrono>
#include <optional>

#pragma managed(push, off)

namespace Orc {

using TimeOut = std::chrono::duration<int>;

using MatchingRuleCollection = std::vector<std::string>;

class ConfigItem;

using namespace std::chrono_literals;

class YaraScanner;

enum class YaraScanMethod
{
    Blocks,
    FileMapping
};

class YaraConfig
{
    friend class YaraScanner;

public:
    YaraConfig() = default;
    YaraConfig(const YaraConfig& other) = default;
    YaraConfig(YaraConfig&&) noexcept = default;

    YaraConfig& operator=(const YaraConfig& other) = default;

    static YaraConfig Get(const ConfigItem& item);

    HRESULT SetBlockSize(ULONG Size)
    {
        _blockSize.emplace(Size % 4096 == 0 ? Size : ((Size / 4096) + 1) * 4096);
        return S_OK;
    }
    ULONG blockSize() const
    {
        return _blockSize.value_or(20 * 1024 * 1024);  // Default to 20MB;
    }

    HRESULT SetOverlapSize(ULONG blockSize)
    {
        _overlapSize.emplace(blockSize % 4096 == 0 ? blockSize : ((blockSize / 4096) + 1) * 4096);
        return S_OK;
    }
    ULONG overlapSize() const
    {
        return _overlapSize.value_or(1024 * 1024);  // Default overloap is 1MB
    }

    HRESULT SetTimeOut(const TimeOut& time)
    {
        _timeOut.emplace(time);
        return S_OK;
    }
    TimeOut timeOut() const { return _timeOut.value_or(60s); }

    const std::vector<std::wstring>& Sources() const { return _Sources; }
    std::vector<std::wstring>& Sources() { return _Sources; }

    HRESULT SetScanMethod(const std::wstring& stgrMethod);

    YaraScanMethod ScanMethod() const { return _scanMethod.value_or(YaraScanMethod::Blocks); }

    bool isValid() const
    {
        if (!_isValid)
            return Validate();
        return _isValid;
    }

private:
    mutable bool _isValid = false;

    bool Validate() const
    {

        if (_blockSize.has_value() && (_blockSize.value() == 0 || _blockSize.value() % 0x1000))
            return false;
        if (_overlapSize.has_value() && (_overlapSize.value() == 0 || _overlapSize.value() % 0x1000))
            return false;

        if (_timeOut.has_value() && _timeOut.value() > 60min)
            return false;

        return _isValid = true;
    }

    std::optional<TimeOut> _timeOut;
    std::optional<ULONG> _blockSize;
    std::optional<ULONG> _overlapSize;
    std::vector<std::wstring> _Sources;
    std::optional<YaraScanMethod> _scanMethod;
};

class YaraScanner
{
public:
    YaraScanner() {}

    HRESULT Initialize(bool bWithCompiler = true);
    HRESULT Configure(std::unique_ptr<YaraConfig>& config);

    HRESULT AddRules(const std::wstring& yara_content_spec);
    HRESULT AddRules(const std::shared_ptr<ByteStream>& stream);
    HRESULT AddRules(CBinaryBuffer& buffer);

    HRESULT EnableRule(LPCSTR strRule);
    HRESULT DisableRule(LPCSTR strRule);

    std::pair<std::vector<std::string>, std::vector<std::string>> ScannedRules(std::vector<std::string> rules);

    HRESULT SetTimeOut(const TimeOut& timeout);

    HRESULT Scan(const CBinaryBuffer& buffer, ULONG bytesToScan, MatchingRuleCollection& matchingRules);
    HRESULT Scan(const CBinaryBuffer& buffer, MatchingRuleCollection& matchingRules)
    {
        return Scan(buffer, (ULONG)buffer.GetCount(), matchingRules);
    }
    HRESULT Scan(const std::shared_ptr<ByteStream>& stream, MatchingRuleCollection& matchingRules);
    HRESULT Scan(
        const std::shared_ptr<ByteStream>& stream,
        ULONG blockSize,
        ULONG overlapSize,
        MatchingRuleCollection& matchingRules);
    HRESULT Scan(const LPCWSTR& szFileName, MatchingRuleCollection& matchingRules);

    std::pair<HRESULT, MatchingRuleCollection> Scan(const CBinaryBuffer& buffer, ULONG bytesToScan)
    {
        MatchingRuleCollection matchingRules;
        HRESULT hr = Scan(buffer, bytesToScan, matchingRules);
        return std::make_pair(hr, matchingRules);
    }
    std::pair<HRESULT, MatchingRuleCollection> Scan(const CBinaryBuffer& buffer)
    {
        MatchingRuleCollection matchingRules;
        HRESULT hr = Scan(buffer, (ULONG)buffer.GetCount(), matchingRules);
        return std::make_pair(hr, matchingRules);
    }
    std::pair<HRESULT, MatchingRuleCollection> Scan(const std::shared_ptr<ByteStream>& stream)
    {
        MatchingRuleCollection matchingRules;
        HRESULT hr = Scan(stream, matchingRules);
        return std::make_pair(hr, matchingRules);
    }
    std::pair<HRESULT, std::vector<std::string>>
    Scan(const std::shared_ptr<ByteStream>& stream, ULONG blockSize, ULONG overlapSize)
    {
        MatchingRuleCollection matchingRules;
        HRESULT hr = Scan(stream, blockSize, overlapSize, matchingRules);
        return std::make_pair(hr, matchingRules);
    }
    std::pair<HRESULT, std::vector<std::string>> Scan(const LPCWSTR& szFileName)
    {
        MatchingRuleCollection matchingRules;
        HRESULT hr = Scan(szFileName, matchingRules);
        return std::make_pair(hr, matchingRules);
    }

    HRESULT PrintConfiguration();

    // takes are of the splitting of rules
    static std::vector<std::string> GetRulesSpec(LPCSTR szRules);
    static std::vector<std::string> GetRulesSpec(LPCWSTR szRules);

    ~YaraScanner();

private:
    using ScanData = std::pair<YaraScanner*, std::vector<std::string>*>;

    void compiler_message(int error_level, const char* file_name, int line_number, const char* message);
    static inline void
    compiler_callback(int error_level, const char* file_name, int line_number, const char* message, void* user_data)
    {
        auto pThis = (YaraScanner*)user_data;
        pThis->compiler_message(error_level, file_name, line_number, message);
    }

    int scan_message(int message, void* message_data, std::vector<std::string>& matchingRules);
    static inline int scan_callback(int message, void* message_data, void* user_data);

    static inline size_t read(void* ptr, size_t size, size_t count, void* user_data);
    static inline size_t write(const void* ptr, size_t size, size_t count, void* user_data);

    HRESULT ScanFrom(
        const std::shared_ptr<ByteStream>& stream,
        DWORD dwMoveMethod,
        ULONGLONG pos,
        CBinaryBuffer& buffer,
        MatchingRuleCollection& matchingRules,
        ULONG& bytesScanned);
    HRESULT ScanFileMapping(
        const std::shared_ptr<ByteStream>& stream,
        MatchingRuleCollection& matchingRules,
        ULONG& bytesScanned);

    std::pair<HRESULT, std::shared_ptr<MemoryStream>> GetMemoryStream(const std::shared_ptr<ByteStream>& byteStream);
    std::unique_ptr<YR_STREAM> GetYaraStream(const std::shared_ptr<ByteStream>& byteStream);

    YR_RULES* GetRules();

    std::shared_ptr<YaraStaticExtension> m_yara;

    YaraConfig m_config;

    YR_COMPILER* m_pCompiler = nullptr;
    YR_RULES* m_pRules = nullptr;
    ULONG m_ErrorCount = 0;
    ULONG m_WarningCount = 0;
};

}  // namespace Orc

#pragma managed(pop)
