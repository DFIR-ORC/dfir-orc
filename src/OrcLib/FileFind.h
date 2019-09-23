//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "CaseInsensitive.h"

#include "VolumeReader.h"
#include "MFTWalker.h"
#include "MFTRecord.h"
#include "MftRecordAttribute.h"
#include "CryptoHashStream.h"
#include "LocationSet.h"
#include "TableOutput.h"
#include "YaraScanner.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <iterator>
#include <regex>

#pragma managed(push, off)

constexpr auto MAX_BYTES_IN_HEADER = 128;
constexpr auto MAX_BYTES_IN_BINSTRING = 1024;

namespace Orc {

class StructuredOutputWriter;

class ConfigItem;

class NTFSStream;

class YaraScanner;

using MatchingRuleCollection = std::vector<std::string>;

class ORCLIB_API FileFind
{
public:
    class ORCLIB_API SearchTerm
    {

    public:
        enum Criteria : LONG64
        {
            NONE = 0,
            NAME = 1 << 0,
            NAME_EXACT = 1 << 1,
            NAME_MATCH = 1 << 2,
            NAME_REGEX = 1 << 3,
            PATH_EXACT = 1 << 4,
            PATH_MATCH = 1 << 5,
            PATH_REGEX = 1 << 6,
            ADS = 1 << 7,
            ADS_EXACT = 1 << 8,
            ADS_MATCH = 1 << 9,
            ADS_REGEX = 1 << 10,
            EA = 1 << 11,
            EA_EXACT = 1 << 12,
            EA_MATCH = 1 << 13,
            EA_REGEX = 1 << 14,
            SIZE_EQ = 1 << 15,
            SIZE_GT = 1 << 16,
            SIZE_GE = 1 << 17,
            SIZE_LT = 1 << 18,
            SIZE_LE = 1 << 19,
            DATA_MD5 = 1 << 20,
            DATA_SHA1 = 1 << 21,
            DATA_SHA256 = 1 << 22,
            HEADER = 1 << 23,
            HEADER_REGEX = 1 << 24,
            HEADER_HEX = 1 << 25,
            ATTR_TYPE = 1 << 26,
            ATTR_NAME_EXACT = 1 << 27,
            ATTR_NAME_MATCH = 1 << 28,
            ATTR_NAME_REGEX = 1 << 29,
            CONTAINS = 1 << 30,
            YARA = 1 << 31
        };

        friend Criteria& operator^=(Criteria& left, const Criteria rigth)
        {
            left =
                static_cast<FileFind::SearchTerm::Criteria>(static_cast<ULONG64>(left) ^ static_cast<ULONG64>(rigth));
            return left;
        }
        friend Criteria& operator|=(Criteria& left, const Criteria rigth)
        {
            left =
                static_cast<FileFind::SearchTerm::Criteria>(static_cast<ULONG64>(left) | static_cast<ULONG64>(rigth));
            return left;
        }

        friend Criteria operator^(const Criteria left, const Criteria rigth)
        {
            return static_cast<FileFind::SearchTerm::Criteria>(
                static_cast<ULONG64>(left) ^ static_cast<ULONG64>(rigth));
        }
        friend Criteria operator|(const Criteria left, const Criteria rigth)
        {
            return static_cast<FileFind::SearchTerm::Criteria>(
                static_cast<ULONG64>(left) | static_cast<ULONG64>(rigth));
        }
        friend Criteria operator&(const Criteria left, const Criteria rigth)
        {
            return static_cast<FileFind::SearchTerm::Criteria>(
                static_cast<ULONG64>(left) & static_cast<ULONG64>(rigth));
        }

        Criteria Required = Criteria::NONE;

        std::wstring Name;  // Generic 'name information'

        std::wstring Path;  // Match against a full path (without DOS device name)
        std::wregex PathRegEx;  // Match against a full path regex (without DOS device name)

        std::wstring FileName;  // the actual file name
        std::wregex FileNameRegEx;  // Regular expression to match the file name against

        std::wstring ADSName;  // the name of the ADS
        std::wregex ADSNameRegEx;  // Regular expression to match the sub name against

        std::wstring EAName;  // the name of the EA
        std::wregex EANameRegEx;  // Regular expression to match the sub name against

        std::wstring AttrName;  // the Attribute Name
        std::wregex AttrNameRegEx;  // Regular expression to match the attribute name against

        DWORD dwAttrType = $UNUSED;

        ULONGLONG SizeEQ = 0LLU;
        ULONGLONG SizeL = 0LLU;
        ULONGLONG SizeG = 0LLU;

        CBinaryBuffer MD5;
        CBinaryBuffer SHA1;
        CBinaryBuffer SHA256;

        std::wstring strHeaderRegEx;
        std::regex HeaderRegEx;
        DWORD HeaderLen = 0L;

        CBinaryBuffer Header;

        CBinaryBuffer Contains;
        bool bContainsIsHex = false;

        std::wstring Yara;
        std::wstring YaraRulesSpec;
        std::vector<std::string> YaraRules;

        SearchTerm() {};

        SearchTerm(const std::wstring& strName)
        {
            Required = Criteria::NAME;
            Name = strName;
        };

        std::pair<bool, std::wstring> IsValidTerm();

        SearchTerm(SearchTerm&& other) noexcept = default;

        std::wstring GetDescription() const;

        static Criteria NameMask() { return NAME | NAME_EXACT | NAME_MATCH | NAME_REGEX; };
        bool DependsOnName() const { return Required & NameMask() ? true : false; };

        static Criteria PathMask() { return PATH_EXACT | PATH_MATCH | PATH_REGEX; };
        bool DependsOnPath() const { return Required & PathMask() ? true : false; };

        static Criteria DataNameOrSizeMask()
        {
            return ADS | ADS_EXACT | ADS_MATCH | ADS_REGEX | SIZE_EQ | SIZE_GT | SIZE_GE | SIZE_LT | SIZE_LE;
        };
        bool DependsOnDataNameOrSize() const { return Required & DataNameOrSizeMask() ? true : false; };

        static Criteria AttributeMask()
        {
            return ATTR_TYPE | ATTR_NAME_EXACT | ATTR_NAME_MATCH | ATTR_NAME_REGEX | EA | EA_EXACT | EA_MATCH
                | EA_REGEX;
        };
        bool DependsOnAttribute() const { return Required & AttributeMask() ? true : false; };

        static Criteria DataMask()
        {
            return HEADER | HEADER_HEX | HEADER_REGEX | DATA_MD5 | DATA_SHA1 | DATA_SHA256 | CONTAINS | YARA;
        };
        bool DependsOnData() const { return Required & DataMask() ? true : false; };

        bool DependsOnlyOnNameOrPath()
        {
            return !DependsOnDataNameOrSize() && !DependsOnAttribute() && !DependsOnData();
        }

        HRESULT AddTermToConfig(ConfigItem& item);
    };

    class ORCLIB_API Match
    {
    public:
        class NameMatch
        {
            friend Match;

        private:
            NameMatch(const PFILE_NAME pFileName, LPCWSTR szFullPath);
            NameMatch(const MFTWalker::FullNameBuilder pBuilder, PFILE_NAME pFileName);

        public:
            std::unique_ptr<BYTE[]> FileName;
            std::wstring FullPathName;

            NameMatch(NameMatch&& other) noexcept = default;

            NameMatch& NameMatch::operator=(const NameMatch& other)
            {
                size_t cbBytes = sizeof(FILE_NAME) + ((PFILE_NAME)other.FileName.get())->FileNameLength * sizeof(WCHAR);
                FileName = std::unique_ptr<BYTE[]>(new BYTE[cbBytes]);
                memcpy_s(FileName.get(), cbBytes, other.FileName.get(), cbBytes);

                FullPathName = other.FullPathName;
                return *this;
            };

            const PFILE_NAME FILENAME() const { return reinterpret_cast<const PFILE_NAME>(FileName.get()); };
        };

        class AttributeMatch
        {
            friend Match;
            friend FileFind;

        private:
            AttributeMatch(const std::shared_ptr<MftRecordAttribute>& pAttr);
            std::weak_ptr<DataAttribute> DataAttr;

        public:
            AttributeMatch(AttributeMatch&& other) noexcept = default;

            ATTRIBUTE_TYPE_CODE Type;
            USHORT InstanceID;
            std::wstring AttrName;
            ULONGLONG DataSize;

            std::shared_ptr<ByteStream> DataStream;
            std::shared_ptr<ByteStream> RawStream;
            CBinaryBuffer MD5, SHA1, SHA256;
            std::optional<MatchingRuleCollection> YaraRules;
        };

        Match(Match&& other) noexcept = default;

        Match(
            const std::shared_ptr<VolumeReader>& volReader,
            const std::shared_ptr<SearchTerm>& aTerm,
            const FILE_REFERENCE& aFRN,
            bool bDeleted)
            : VolumeReader(volReader)
            , Term(aTerm)
            , FRN(aFRN)
            , DeletedRecord(bDeleted) {};

        Match(const std::shared_ptr<VolumeReader>& volReader, const std::shared_ptr<SearchTerm>& aTerm)
            : VolumeReader(volReader)
            , Term(aTerm)
            , DeletedRecord(false)
        {
            FRN.SequenceNumber = 0;
            FRN.SegmentNumberHighPart = 0;
            FRN.SegmentNumberLowPart = 0L;
        };

        Match operator=(const Match& other) = delete;

        HRESULT AddFileNameMatch(const PFILE_NAME pFileName, LPCWSTR szFullPath)
        {
            MatchingNames.push_back(NameMatch(pFileName, szFullPath));
            return S_OK;
        };

        HRESULT AddFileNameMatch(const MFTWalker::FullNameBuilder pBuilder, PFILE_NAME pFileName)
        {
            MatchingNames.push_back(NameMatch(pBuilder, pFileName));
            return S_OK;
        };

        HRESULT AddAttributeMatch(
            const logger& pLog,
            const std::shared_ptr<VolumeReader> pVolReader,
            const std::shared_ptr<MftRecordAttribute>& pAttribute,
            std::optional<MatchingRuleCollection> matchedRules = std::nullopt);

        HRESULT AddAttributeMatch(
            const std::shared_ptr<MftRecordAttribute>& pAttribute,
            std::optional<MatchingRuleCollection> matchedRules = std::nullopt);

        HRESULT GetMatchFullName(const NameMatch& nameMatch, const AttributeMatch& attrMatch, std::wstring& strName);
        HRESULT GetMatchFullNames(std::vector<std::wstring>& strNames);

        HRESULT Reset()
        {
            FRN.SequenceNumber = 0;
            FRN.SegmentNumberHighPart = 0;
            FRN.SegmentNumberLowPart = 0L;
            Term.reset();
            MatchingNames.clear();
            MatchingAttributes.clear();
            StandardInformation.reset();
            return S_OK;
        }

        HRESULT Write(const logger& pLog, ITableOutput& output, const FILETIME& CollectionDate);
        HRESULT Write(
            const logger& pLog,
            const std::shared_ptr<StructuredOutputWriter>& pWriter,
            const FILETIME& CollectionDate);

        bool DeletedRecord;
        FILE_REFERENCE FRN;
        std::shared_ptr<SearchTerm> Term;
        std::vector<NameMatch> MatchingNames;
        std::vector<AttributeMatch> MatchingAttributes;
        std::unique_ptr<STANDARD_INFORMATION> StandardInformation;
        std::shared_ptr<VolumeReader> VolumeReader;
    };

    typedef std::function<void(const std::shared_ptr<Match>& aMatch, bool& bStop)> FoundMatchCallback;

public:
    FileFind(logger pLog, bool bProvideStream = true, SupportedAlgorithm matchHash = SupportedAlgorithm::Undefined)
        : _L_(std::move(pLog))
        , m_FullNameBuilder(nullptr)
        , m_bProvideStream(bProvideStream)
        , m_MatchHash(matchHash)
    {
        if (m_MatchHash != SupportedAlgorithm::Undefined)
            m_bProvideStream = true;
    };
    FileFind(const FileFind& other) = delete;
    FileFind(FileFind&& other) noexcept = default;

    HRESULT InitializeYara(std::unique_ptr<YaraConfig>& config);
    HRESULT InitializeYara()
    {
        std::unique_ptr<YaraConfig> config;
        return InitializeYara(config);
    }

    HRESULT CheckYara();

    static std::shared_ptr<SearchTerm> GetSearchTermFromConfig(const ConfigItem& item, logger pLog = nullptr);

    HRESULT AddTermsFromConfig(const ConfigItem& items);
    HRESULT AddTerm(const std::shared_ptr<SearchTerm>& FindSpec);

    HRESULT AddExcludeTermsFromConfig(const ConfigItem& items);
    HRESULT AddExcludeTerm(const std::shared_ptr<SearchTerm>& FindSpec);

    HRESULT Find(const LocationSet& locations, FoundMatchCallback aCallback, bool bParseI30Data);

    const std::vector<std::shared_ptr<Match>>& Matches() const { return m_Matches; }

    void PrintSpecs() const;

    ~FileFind(void);

private:
    using TermMapOfNames = std::unordered_multimap<
        std::wstring,
        std::shared_ptr<SearchTerm>,
        CaseInsensitiveUnordered,
        CaseInsensitiveUnordered>;
    using TermMapOfSizes = std::unordered_multimap<ULONGLONG, std::shared_ptr<SearchTerm>>;

    logger _L_;

    TermMapOfNames m_ExactNameTerms;
    TermMapOfNames m_ExactPathTerms;
    TermMapOfSizes m_SizeTerms;
    std::vector<std::shared_ptr<SearchTerm>> m_Terms;

    TermMapOfNames m_I30ExactNameTerms;
    TermMapOfNames m_I30ExactPathTerms;
    std::vector<std::shared_ptr<SearchTerm>> m_I30Terms;

    TermMapOfNames m_ExcludeNameTerms;
    TermMapOfNames m_ExcludePathTerms;
    TermMapOfSizes m_ExcludeSizeTerms;

    std::vector<std::shared_ptr<SearchTerm>> m_ExcludeTerms;

    std::vector<std::shared_ptr<SearchTerm>> m_AllTerms;

    static std::wregex& DOSPattern();
    static std::wregex& RegexPattern();
    static std::wregex& RegexOnlyPattern();

    static std::wregex& FileSpecPattern();

    MFTWalker::FullNameBuilder m_FullNameBuilder;
    MFTWalker::InLocationBuilder m_InLocationBuilder;
    std::shared_ptr<VolumeReader> m_pVolReader;

    std::unique_ptr<YaraScanner> m_YaraScan;

    std::vector<std::shared_ptr<Match>> m_Matches;

    bool m_bProvideStream = false;
    SupportedAlgorithm m_MatchHash = SupportedAlgorithm::Undefined;

    SupportedAlgorithm m_NeededHash = SupportedAlgorithm::Undefined;

    SearchTerm::Criteria DiscriminateName(const std::wstring& strName);
    SearchTerm::Criteria DiscriminateADS(const std::wstring& strADS);
    SearchTerm::Criteria DiscriminateEA(const std::wstring& strEA);

    // Name specs: Only depend on $FILE_NAME
    SearchTerm::Criteria ExactName(const std::shared_ptr<SearchTerm>& aTerm, const PFILE_NAME pFileName) const;
    SearchTerm::Criteria MatchName(const std::shared_ptr<SearchTerm>& aTerm, const PFILE_NAME pFileName) const;
    SearchTerm::Criteria RegexName(const std::shared_ptr<SearchTerm>& aTerm, const PFILE_NAME pFileName) const;

    SearchTerm::Criteria MatchName(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        std::shared_ptr<Match>& aFileMatch,
        const PFILE_NAME pFileName) const;
    SearchTerm::Criteria MatchName(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const Match::NameMatch& pNameMatch) const;

    SearchTerm::Criteria AddMatchingName(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        std::shared_ptr<Match>& aFileMatch,
        MFTRecord* pElt) const;
    SearchTerm::Criteria ExcludeMatchingName(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const std::shared_ptr<Match>& aFileMatch) const;

    // Full path specs: Only depend on full path
    SearchTerm::Criteria ExactPath(const std::shared_ptr<SearchTerm>& aTerm, const WCHAR* szFullName) const;
    SearchTerm::Criteria MatchPath(const std::shared_ptr<SearchTerm>& aTerm, const WCHAR* szFullName) const;
    SearchTerm::Criteria RegexPath(const std::shared_ptr<SearchTerm>& aTerm, const WCHAR* szFullName) const;

    SearchTerm::Criteria MatchPath(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        std::shared_ptr<Match>& aFileMatch,
        const PFILE_NAME pFileName) const;
    SearchTerm::Criteria MatchPath(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const Match::NameMatch& aFileMatch) const;

    SearchTerm::Criteria AddMatchingPath(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        std::shared_ptr<Match>& aFileMatch,
        MFTRecord* pElt) const;
    SearchTerm::Criteria ExcludeMatchingPath(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const std::shared_ptr<Match>& aFileMatch) const;

    // Data specs: only depend on $DATA attribute
    SearchTerm::Criteria
    ExactADS(const std::shared_ptr<SearchTerm>& aTerm, LPCWSTR szAttrName, size_t AttrNameLen) const;
    SearchTerm::Criteria
    MatchADS(const std::shared_ptr<SearchTerm>& aTerm, LPCWSTR szAttrName, size_t AttrNameLen) const;
    SearchTerm::Criteria
    RegexADS(const std::shared_ptr<SearchTerm>& aTerm, LPCWSTR szAttrName, size_t AttrNameLen) const;

    SearchTerm::Criteria SizeMatch(const std::shared_ptr<SearchTerm>& aTerm, ULONGLONG ullSize) const;

    SearchTerm::Criteria AddMatchingDataNameAndSize(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        std::shared_ptr<Match>& aFileMatch,
        MFTRecord* pElt) const;
    SearchTerm::Criteria ExcludeMatchingDataNameAndSize(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const std::shared_ptr<Match>& aFileMatch) const;

    SearchTerm::Criteria
    MatchHeader(const std::shared_ptr<SearchTerm>& aTerm, const std::shared_ptr<DataAttribute>& pDataAttr) const;
    SearchTerm::Criteria
    RegExHeader(const std::shared_ptr<SearchTerm>& aTerm, const std::shared_ptr<DataAttribute>& pDataAttr) const;
    SearchTerm::Criteria
    HexHeader(const std::shared_ptr<SearchTerm>& aTerm, const std::shared_ptr<DataAttribute>& pDataAttr) const;

    SearchTerm::Criteria
    MatchHash(const std::shared_ptr<SearchTerm>& aTerm, const std::shared_ptr<DataAttribute>& pDataAttr) const;
    SearchTerm::Criteria
    MatchContains(const std::shared_ptr<SearchTerm>& aTerm, const std::shared_ptr<DataAttribute>& pDataAttr) const;
    std::pair<SearchTerm::Criteria, std::optional<MatchingRuleCollection>>
    MatchYara(const std::shared_ptr<SearchTerm>& aTerm, const std::shared_ptr<DataAttribute>& pDataAttr) const;

    SearchTerm::Criteria AddMatchingData(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        std::shared_ptr<Match>& aFileMatch,
        MFTRecord* pElt) const;
    SearchTerm::Criteria ExcludeMatchingData(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const std::shared_ptr<Match>& aMatch) const;

    // Attribute specs: Only depend on one attribute value
    SearchTerm::Criteria ExactEA(
        const std::shared_ptr<SearchTerm>& aTerm,
        MFTRecord* pElt,
        const std::shared_ptr<MftRecordAttribute>& pAttr) const;
    SearchTerm::Criteria MatchEA(
        const std::shared_ptr<SearchTerm>& aTerm,
        MFTRecord* pElt,
        const std::shared_ptr<MftRecordAttribute>& pAttr) const;
    SearchTerm::Criteria RegexEA(
        const std::shared_ptr<SearchTerm>& aTerm,
        MFTRecord* pElt,
        const std::shared_ptr<MftRecordAttribute>& pAttr) const;

    SearchTerm::Criteria
    ExactAttr(const std::shared_ptr<SearchTerm>& aTerm, LPCWSTR szAttrName, size_t AttrNameLen) const;
    SearchTerm::Criteria
    MatchAttr(const std::shared_ptr<SearchTerm>& aTerm, LPCWSTR szAttrName, size_t AttrNameLen) const;
    SearchTerm::Criteria
    RegExAttr(const std::shared_ptr<SearchTerm>& aTerm, LPCWSTR szAttrName, size_t AttrNameLen) const;

    SearchTerm::Criteria AttrType(const std::shared_ptr<SearchTerm>& aTerm, const ATTRIBUTE_TYPE_CODE pAttr) const;

    SearchTerm::Criteria MatchAttributes(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        std::shared_ptr<Match>& aFileMatch,
        MFTRecord* pElt) const;
    SearchTerm::Criteria ExcludeMatchingAttributes(
        const std::shared_ptr<SearchTerm>& aTerm,
        SearchTerm::Criteria required,
        const std::shared_ptr<Match>& aFileMatch) const;

    SearchTerm::Criteria LookupTermInRecordAddMatching(
        const std::shared_ptr<SearchTerm>& aTerm,
        const SearchTerm::Criteria matched,
        std::shared_ptr<Match>& aMatch,
        MFTRecord* pElt) const;  // matches against a MFTRecord
    SearchTerm::Criteria LookupTermIn$I30AddMatching(
        const std::shared_ptr<SearchTerm>& aTerm,
        const SearchTerm::Criteria matched,
        std::shared_ptr<Match>& aMatch,
        const PFILE_NAME pFileName) const;  // matches against a $I30 $FILE_NALME
    SearchTerm::Criteria LookupTermInMatchExcludeMatching(
        const std::shared_ptr<SearchTerm>& aTerm,
        const SearchTerm::Criteria matched,
        const std::shared_ptr<Match>& aMatch) const;  // Matches a match for exclusion

    HRESULT ComputeMatchHashes(const std::shared_ptr<Match>& aMatch);
    HRESULT EvaluateMatchCallCallback(
        FileFind::FoundMatchCallback aCallback,
        bool& bStop,
        const std::shared_ptr<Match>& aMatch);

    HRESULT ExcludeMatch(const std::shared_ptr<Match>& aMatch);

    HRESULT FindMatch(MFTRecord* pElt, bool& bStop, FileFind::FoundMatchCallback aCallback);

    HRESULT FindI30Match(const PFILE_NAME pFileName, bool& bStop, FileFind::FoundMatchCallback aCallback);

    SupportedAlgorithm GetNeededHashAlgorithms();
};

}  // namespace Orc
#pragma managed(pop)
