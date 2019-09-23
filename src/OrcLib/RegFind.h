//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Pierre-Sébastien BOST
//
#pragma once

#include "OrcLib.h"

#include "RegistryWalker.h"
#include "ByteStream.h"
#include "CaseInsensitive.h"
#include "FileFind.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <iterator>
#include <regex>

#include <boost\algorithm\searching\boyer_moore.hpp>

#pragma managed(push, off)

namespace Orc {

class ConfigItem;

struct ValueTypeDefinition
{
    LPCWSTR szTypeName;
    ValueType Type;
};

static const ValueTypeDefinition g_ValyeTypeDefinitions[] = {{L"_REG_NONE_", RegNone},
                                                             {L"REG_SZ", RegSZ},
                                                             {L"REG_EXPAND_SZ", ExpandSZ},
                                                             {L"REG_BINARY", RegBin},
                                                             {L"REG_DWORD", RegDWORD},
                                                             {L"REG_DWORD_LITTLE_ENDIAN", RegDWORD},
                                                             {L"REG_DWORD_BIG_ENDIAN", RegDWORDBE},
                                                             {L"REG_LINK", RegLink},
                                                             {L"REG_MULTI_SZ", RegMultiSZ},
                                                             {L"REG_RESOURCE_LIST", RegResourceList},
                                                             {L"REG_FULL_RESOURCE_DESCRIPTOR", RegResourceDescr},
                                                             {L"REG_RESOURCE_REQUIREMENTS_LIST", RegResourceReqList},
                                                             {L"REG_QWORD", RegQWORD},
                                                             {L"REG_QWORD_LITTLE_ENDIAN", RegQWORD},
                                                             {L"_REG_FIRST_INVALID_", RegMaxType}};

class ORCLIB_API RegFind
{
public:
    class ORCLIB_API SearchTerm
    {

    public:
        typedef enum _Criteria
        {
            NONE = 0,
            KEY_NAME = 1,
            KEY_NAME_REGEX = 1 << 1,
            KEY_PATH = 1 << 2,
            KEY_PATH_REGEX = 1 << 3,
            VALUE_NAME = 1 << 4,
            VALUE_NAME_REGEX = 1 << 5,
            VALUE_TYPE = 1 << 6,
            DATA_SIZE_EQUAL = 1 << 7,
            DATA_SIZE_LESS = 1 << 8,
            DATA_SIZE_MORE = 1 << 9,
            DATA_CONTENT = 1 << 10,
            DATA_CONTENT_REGEX = 1 << 11,
            DATA_CONTAINS = 1 << 12
        } Criteria;

        Criteria m_criteriaRequired;
        std::wstring m_TermClassName;  // Name of the template from wich this term is issued

        std::string m_strKeyName;  // Name of the key
        std::regex m_regexKeyName;  // regex Name of the key

        std::string m_strPathName;  // Name of the key path
        std::regex m_regexPathName;  // regex path of the key

        std::string m_strValueName;  // Value of the key
        std::regex m_regexValueName;  // regex Value of the key

        ValueType m_ValueType;

        ULONGLONG m_ulDataSize;
        ULONGLONG m_ulDataSizeLowLimit;
        ULONGLONG m_ulDataSizeHighLimit;

        CBinaryBuffer m_DataContent;
        CBinaryBuffer m_WDataContent;  // Unicode version
        CBinaryBuffer m_DataContentContains;
        CBinaryBuffer m_WDataContentContains;

        std::regex m_regexDataContentPattern;
        std::wregex m_wregexDataContentPattern;
        std::string m_strRegexDataContentPattern;

        SearchTerm()
        {
            m_criteriaRequired = Criteria::NONE;
            m_ulDataSize = 0L;
            m_ulDataSizeHighLimit = 0L;
            m_ulDataSizeLowLimit = 0L;
            m_ValueType = ValueType::RegMaxType;
            m_TermClassName = L"DEFAULT";
        };

        SearchTerm(SearchTerm&& other)
        {
            m_criteriaRequired = other.m_criteriaRequired;
            std::swap(m_strKeyName, other.m_strKeyName);
            std::swap(m_strValueName, other.m_strValueName);
            std::swap(m_regexKeyName, other.m_regexKeyName);
            std::swap(m_regexValueName, other.m_regexValueName);
            std::swap(m_regexDataContentPattern, other.m_regexDataContentPattern);
            std::swap(m_strRegexDataContentPattern, other.m_strRegexDataContentPattern);
            std::swap(m_wregexDataContentPattern, other.m_wregexDataContentPattern);

            m_ulDataSize = other.m_ulDataSize;
            m_ulDataSizeHighLimit = other.m_ulDataSizeHighLimit;
            m_ulDataSizeLowLimit = other.m_ulDataSizeLowLimit;
            std::swap(m_DataContent, other.m_DataContent);
            std::swap(m_WDataContent, other.m_WDataContent);
            std::swap(m_DataContentContains, other.m_DataContentContains);
            std::swap(m_WDataContentContains, other.m_WDataContentContains);

            m_ValueType = other.m_ValueType;
            std::swap(m_TermClassName, other.m_TermClassName);
        };

        std::string GetDescription() const;
        std::string GetCriteriaDescription() const;
        const std::wstring& GetTermName() const;

        void SetTermName(const std::wstring& wstrName);

        static Criteria KeyNameMask() { return static_cast<Criteria>(KEY_NAME | KEY_NAME_REGEX); };
        bool DependsOnKeyName() const { return m_criteriaRequired & KeyNameMask() ? true : false; };

        static Criteria KeyPathMask() { return static_cast<Criteria>(KEY_PATH | KEY_PATH_REGEX); };
        bool DependsOnKeyPath() const { return m_criteriaRequired & KeyPathMask() ? true : false; };

        static Criteria ValueNameMask() { return static_cast<Criteria>(VALUE_NAME | VALUE_NAME_REGEX); };
        bool DependsOnValueName() const { return m_criteriaRequired & ValueNameMask() ? true : false; };

        static Criteria ValueTypeMask() { return static_cast<Criteria>(VALUE_TYPE); };
        bool DependsOnValueType() const { return m_criteriaRequired & ValueTypeMask() ? true : false; };

        static Criteria DataMask()
        {
            return static_cast<Criteria>(
                DATA_CONTENT | DATA_CONTENT_REGEX | DATA_SIZE_EQUAL | DATA_SIZE_MORE | DATA_SIZE_LESS | DATA_CONTAINS);
        };
        bool DependsOnData() const { return m_criteriaRequired & DataMask() ? true : false; };

        static Criteria ValueOrDataMask()
        {
            return static_cast<Criteria>(ValueNameMask() | ValueTypeMask() | DataMask());
        }
        bool DependsOnValueOrData() { return m_criteriaRequired & ValueOrDataMask() ? true : false; }
    };

    class ORCLIB_API Match
    {
    public:
        class ORCLIB_API KeyNameMatch
        {
            friend Match;

        private:
            KeyNameMatch(const RegistryKey* const MatchingRegistryKey);

        public:
            std::string KeyName;
            std::string ShortKeyName;
            std::string ClassName;

            DWORD SubKeysCount;
            DWORD ValuesCount;

            FILETIME LastModificationTime;
            KeyType Type;

            KeyNameMatch(KeyNameMatch&& other)
            {
                std::swap(KeyName, other.KeyName);
                std::swap(ShortKeyName, other.ShortKeyName);
                std::swap(ClassName, other.ClassName);
                SubKeysCount = other.SubKeysCount;
                ValuesCount = other.ValuesCount;
                LastModificationTime = other.LastModificationTime;
                Type = other.Type;
            };

            KeyNameMatch& KeyNameMatch::operator=(const KeyNameMatch& other)
            {
                KeyName = other.KeyName;
                ShortKeyName = other.ShortKeyName;
                ClassName = other.ClassName;
                SubKeysCount = other.SubKeysCount;
                ValuesCount = other.ValuesCount;
                LastModificationTime = other.LastModificationTime;
                Type = other.Type;
                return *this;
            };
        };

        class ORCLIB_API ValueNameMatch
        {
            friend Match;

        private:
            ValueNameMatch(const RegistryValue* const MatchingRegsitryValue);

        public:
            std::string KeyName;
            std::string ShortKeyName;
            std::string ClassName;
            KeyType KeyType;

            FILETIME LastModificationTime;

            std::string ValueName;
            ValueType ValueType;

            std::unique_ptr<BYTE> Datas;
            size_t DatasLength;

            ValueNameMatch(const RegFind::Match::ValueNameMatch& other)
            {
                KeyType = other.KeyType;
                ValueType = other.ValueType;

                ShortKeyName = other.ShortKeyName;
                KeyName = other.KeyName;
                ClassName = other.ClassName;
                ValueName = other.ValueName;
                LastModificationTime = other.LastModificationTime;

                DatasLength = other.DatasLength;

                BYTE* Tmp = new BYTE[other.DatasLength];
                CopyMemory(Tmp, other.Datas.get(), other.DatasLength);
                Datas.reset(Tmp);
            }

            ValueNameMatch(ValueNameMatch&& other)
            {
                KeyType = other.KeyType;
                ValueType = other.ValueType;

                std::swap(ShortKeyName, other.ShortKeyName);
                std::swap(KeyName, other.KeyName);
                std::swap(ClassName, other.ClassName);
                std::swap(ValueName, other.ValueName);
                LastModificationTime = other.LastModificationTime;

                DatasLength = other.DatasLength;
                other.DatasLength = 0LL;

                std::swap(Datas, other.Datas);
                other.Datas = nullptr;
            }

            ValueNameMatch& ValueNameMatch::operator=(const ValueNameMatch& other)
            {
                KeyType = other.KeyType;
                ValueType = other.ValueType;

                ShortKeyName = other.ShortKeyName;
                KeyName = other.KeyName;
                ClassName = other.ClassName;
                ValueName = other.ValueName;
                LastModificationTime = other.LastModificationTime;

                DatasLength = other.DatasLength;
                BYTE* Tmp = new BYTE[other.DatasLength];
                CopyMemory(Tmp, other.Datas.get(), other.DatasLength);
                Datas.reset(Tmp);
                return *this;
            };
        };

        Match(Match&& other)
        {
            std::swap(Term, other.Term);
            std::swap(MatchingKeys, other.MatchingKeys);
            std::swap(MatchingValues, other.MatchingValues);
        };

        Match(const std::shared_ptr<SearchTerm>& aTerm)
            : Term(aTerm) {};

        HRESULT AddKeyNameMatch(const RegistryKey* const RegKey)
        {
            MatchingKeys.push_back(KeyNameMatch(RegKey));
            return S_OK;
        };

        HRESULT AddValueNameMatch(const RegistryValue* const RegValue)
        {
            MatchingValues.push_back(ValueNameMatch(RegValue));
            return S_OK;
        };

        HRESULT Reset()
        {
            Term.reset();
            MatchingKeys.clear();
            MatchingValues.clear();
            return S_OK;
        }

        std::shared_ptr<SearchTerm> Term;
        std::vector<KeyNameMatch> MatchingKeys;
        std::vector<ValueNameMatch> MatchingValues;

        HRESULT Write(const logger& pLog, ITableOutput& output, const FILETIME& CollectionDate);
        HRESULT Write(
            const logger& pLog,
            const std::shared_ptr<StructuredOutputWriter>& pWriter,
            const FILETIME& CollectionDate);
    };

    typedef std::function<void(const std::vector<std::shared_ptr<Match>>& aMatch)> FoundKeyMatchCallback;
    typedef std::function<void(const std::vector<std::shared_ptr<Match>>& aMatch)> FoundValueMatchCallback;

private:
    typedef std::unordered_multimap<
        std::string,
        std::shared_ptr<SearchTerm>,
        CaseInsensitiveUnorderedAnsi,
        CaseInsensitiveUnorderedAnsi>
        TermMap;

    static inline size_t hashSearchTermUnordered(const std::shared_ptr<SearchTerm> s)
    {
        ULONG hash = 0;
        int c;

        const CHAR* pStr = s->m_strKeyName.c_str();

        while ((c = tolower((unsigned char)*pStr++)) != 0)
            hash = c + (hash << 6) + (hash << 16) - hash;

        pStr = s->m_strPathName.c_str();
        while ((c = tolower((unsigned char)*pStr++)) != 0)
            hash = c + (hash << 6) + (hash << 16) - hash;

        pStr = s->m_strRegexDataContentPattern.c_str();
        while ((c = tolower((unsigned char)*pStr++)) != 0)
            hash = c + (hash << 6) + (hash << 16) - hash;

        pStr = s->m_strValueName.c_str();
        while ((c = tolower((unsigned char)*pStr++)) != 0)
            hash = c + (hash << 6) + (hash << 16) - hash;

        pStr = (CHAR*)s->m_TermClassName.c_str();
        while ((c = tolower((unsigned char)*pStr++)) != 0)
            hash = c + (hash << 6) + (hash << 16) - hash;

        const BYTE* pBuffer = s->m_DataContent.GetData();
        size_t size = s->m_DataContent.GetCount();
        DWORD i;
        for (i = 0; i < size; i++)
        {
            hash = pBuffer[i] + (hash << 6) + (hash << 16) - hash;
        }

        pBuffer = s->m_DataContentContains.GetData();
        size = s->m_DataContentContains.GetCount();
        for (i = 0; i < size; i++)
        {
            hash = pBuffer[i] + (hash << 6) + (hash << 16) - hash;
        }

        return hash;
    }

    struct SearchTermUnordered : std::function<bool(std::shared_ptr<SearchTerm>, std::shared_ptr<SearchTerm>)>
    {
        bool operator()(const std::shared_ptr<SearchTerm>& s1, const std::shared_ptr<SearchTerm>& s2) const
        {
            bool IsEqual = false;
            IsEqual = (s1->m_criteriaRequired == s2->m_criteriaRequired) && (s1->m_ulDataSize == s2->m_ulDataSize)
                && (s1->m_ulDataSizeHighLimit == s2->m_ulDataSizeHighLimit)
                && (s1->m_ulDataSizeLowLimit == s2->m_ulDataSizeLowLimit);

            return IsEqual;
        };
        bool operator()(const SearchTerm& s1, const SearchTerm& s2) const
        {
            bool IsEqual = false;
            IsEqual = (s1.m_criteriaRequired == s2.m_criteriaRequired) && (s1.m_ulDataSize == s2.m_ulDataSize)
                && (s1.m_ulDataSizeHighLimit == s2.m_ulDataSizeHighLimit)
                && (s1.m_ulDataSizeLowLimit == s2.m_ulDataSizeLowLimit);

            return IsEqual;
        };
        size_t operator()(const std::shared_ptr<SearchTerm>& s) const { return hashSearchTermUnordered(s); }
    };

    typedef std::unordered_multimap<
        std::shared_ptr<SearchTerm>,
        std::shared_ptr<Match>,
        SearchTermUnordered,
        SearchTermUnordered>
        MatchesMap;

    logger _L_;

    TermMap m_ExactKeyNameSpecs;
    TermMap m_ExactKeyPathSpecs;
    TermMap m_ExactValueNameSpecs;
    std::vector<std::shared_ptr<SearchTerm>> m_Specs;

    MatchesMap m_Matches;

    // Name specs: Only depend on KeyName (aka ShotKeyName)
    SearchTerm::Criteria ExactKeyName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const;
    SearchTerm::Criteria RegexKeyName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const;

    SearchTerm::Criteria MatchKeyName(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const RegistryKey* const Regkey) const;

    // Name specs: Only depend on KeyPath
    SearchTerm::Criteria ExactKeyPath(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const;
    SearchTerm::Criteria RegexKeyPath(const std::shared_ptr<SearchTerm>& aTerm, const RegistryKey* const Regkey) const;

    SearchTerm::Criteria MatchKeyPath(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const RegistryKey* const Regkey) const;

    // Full path specs: Only depend on ValueName
    SearchTerm::Criteria
    ExactValueName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const;
    SearchTerm::Criteria
    RegexValueName(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const;

    SearchTerm::Criteria MatchValueName(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const RegistryValue* const RegValue) const;

    // Data specs: only depend on Datas
    SearchTerm::Criteria
    ExactDatas(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const;
    SearchTerm::Criteria
    RegexDatas(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const;
    SearchTerm::Criteria
    DatasContains(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const;

    SearchTerm::Criteria
    DatasSizeMatch(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const;

    SearchTerm::Criteria MatchDataAndSize(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const RegistryValue* const RegValue) const;

    SearchTerm::Criteria
    ValueType_(const std::shared_ptr<SearchTerm>& aTerm, const RegistryValue* const RegValue) const;
    SearchTerm::Criteria MatchValueType(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const RegistryValue* const RegValue) const;

    SearchTerm::Criteria LookupSpec(
        const std::shared_ptr<SearchTerm>& aTerm,
        const SearchTerm::Criteria matched,
        std::shared_ptr<Match>& aMatch,
        const RegistryKey* const Regkey) const;
    SearchTerm::Criteria LookupSpec(
        const std::shared_ptr<SearchTerm>& aTerm,
        const SearchTerm::Criteria matched,
        std::shared_ptr<Match>& aMatch,
        const RegistryValue* const RegValue) const;

    const std::vector<std::shared_ptr<Match>> FindMatch(const RegistryKey* const RegKey);
    const std::vector<std::shared_ptr<Match>> FindMatch(const RegistryValue* const RegValue);

    static ValueType GetRegistryValueType(LPCWSTR szValueType);

public:
    RegFind(logger pLog)
        : _L_(std::move(pLog)) {};

    static std::shared_ptr<SearchTerm> GetSearchTermFromConfig(const ConfigItem& item, logger pLog = nullptr);

    HRESULT AddRegFindFromConfig(const std::vector<ConfigItem>& items);
    HRESULT AddRegFindFromTemplate(const std::vector<ConfigItem>& items);
    HRESULT AddSearchTerm(const std::shared_ptr<SearchTerm>& MatchSpec);

    HRESULT Find(
        const std::shared_ptr<ByteStream>& location,
        FoundKeyMatchCallback aKeyCallback,
        FoundValueMatchCallback aValueCallback);

    const MatchesMap& Matches() const { return m_Matches; }
    void ClearMatches() { m_Matches.clear(); }

    void PrintSpecs() const;

    ~RegFind(void) {};
};

}  // namespace Orc

#pragma managed(pop)
