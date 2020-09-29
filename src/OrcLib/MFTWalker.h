//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "NtfsDataStructures.h"

#include "VolumeReader.h"

#include "HeapStorage.h"

#include "Location.h"

#include "MFTRecord.h"
#include "MFTUtils.h"
#include "IMFT.h"

#include "CaseInsensitive.h"

#include <unordered_set>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <boost/logic/tribool.hpp>

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API MFTWalker
{
    friend class MFTRecord;

public:
    // MFT Parser callbacks
    using ElementCall = std::function<void(const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt)>;
    using FileNameCall = std::function<
        void(const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt, const PFILE_NAME pFileName)>;
    using AttributeCall = std::function<
        void(const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt, const AttributeListEntry& Attr)>;
    using DataCall = std::function<void(
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const std::shared_ptr<DataAttribute>& pAttr)>;
    using FileNameAndDataCall = std::function<void(
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PFILE_NAME pFileName,
        const std::shared_ptr<DataAttribute>& pAttr)>;
    using DirectoryCall = std::function<void(
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PFILE_NAME pFileName,
        const std::shared_ptr<IndexAllocationAttribute>& pAttr)>;
    using I30EntryCall = std::function<void(
        const std::shared_ptr<VolumeReader>& volreader,
        MFTRecord* pElt,
        const PINDEX_ENTRY pEntry,
        const PFILE_NAME pFileName,
        bool bCarvedEntry)>;
    using KeepRecordAliveCall = std::function<bool(const std::shared_ptr<VolumeReader>& volreader, MFTRecord* pElt)>;
    using SecurityDescriptorCall =
        std::function<void(const std::shared_ptr<VolumeReader>& volreader, const PSECURITY_DESCRIPTOR_ENTRY& pEntry)>;
    using ProgressCall = std::function<HRESULT(const ULONG dwProgress)>;

    class Callbacks
    {
    public:
        ElementCall ElementCallback;
        FileNameCall FileNameCallback;
        AttributeCall AttributeCallback;
        DataCall DataCallback;
        FileNameAndDataCall FileNameAndDataCallback;
        DirectoryCall DirectoryCallback;
        I30EntryCall I30Callback;
        SecurityDescriptorCall SecDescCallback;
        ProgressCall ProgressCallback;
        KeepRecordAliveCall KeepAliveCallback;

        Callbacks()
            : ElementCallback(nullptr)
            , FileNameCallback(nullptr)
            , AttributeCallback(nullptr)
            , DataCallback(nullptr)
            , FileNameAndDataCallback(nullptr)
            , DirectoryCallback(nullptr)
            , I30Callback(nullptr)
            , ProgressCallback(nullptr)
            , KeepAliveCallback(nullptr) {};
    };

    using FullNameBuilder =
        std::function<const WCHAR*(const PFILE_NAME pFileName, const std::shared_ptr<DataAttribute>& pDataAttr)>;
    using InLocationBuilder = std::function<bool(const PFILE_NAME pFileName)>;

public:
    MFTWalker()
        : m_SegmentStore(L"MFTSegmentStore")
    {
    }

    HRESULT Initialize(const std::shared_ptr<Location>& loc, bool bIncludeNoInUse = true);

    FullNameBuilder GetFullNameBuilder()
    {
        return [this](PFILE_NAME pFileName, const std::shared_ptr<DataAttribute>& pDataAttr) -> const WCHAR* {
            return GetFullNameAndIfInLocation(pFileName, pDataAttr, NULL, NULL);
        };
    }

    InLocationBuilder GetInLocationBuilder()
    {
        return [this](PFILE_NAME pFileName) -> bool { return IsInLocation(pFileName); };
    }

    HRESULT Walk(const Callbacks& pCallbacks);

    ULONG GetMFTRecordCount() const;
    HRESULT Statistics(const WCHAR* szMsg);

    ~MFTWalker();

private:
    HeapStorage m_SegmentStore;
    size_t m_CellStoreLastWalk = 0L;
    size_t m_CellStoreThreshold = 50 * 1024;

    std::unordered_map<MFTUtils::SafeMFTSegmentNumber, MFTRecord*> m_MFTMap;

    class ORCLIB_API MFTFileNameWrapper
    {
    public:
        PFILE_NAME m_pFileName;
        boost::logic::tribool m_InLocation;

        MFTFileNameWrapper(const MFTFileNameWrapper& pFileName)
            : m_InLocation(boost::indeterminate)
        {
            if (pFileName.m_pFileName != nullptr)
                throw std::exception("Invalid MFTFileNameWrapper construction");
            m_pFileName = nullptr;
        };
        MFTFileNameWrapper(const PFILE_NAME pFileName);
        MFTFileNameWrapper(MFTFileNameWrapper&& Other) noexcept
        {
            m_pFileName = Other.m_pFileName;
            Other.m_pFileName = nullptr;
            m_InLocation = Other.m_InLocation;
        }
        ~MFTFileNameWrapper() { free(m_pFileName); };

        PFILE_NAME FileName() const { return m_pFileName; };
    };

    std::unordered_map<MFTUtils::SafeMFTSegmentNumber, MFTFileNameWrapper> m_DirectoryNames;
    std::unordered_set<std::wstring, CaseInsensitiveUnordered> m_Locations;

    bool m_bIncludeNotInUse = false;

    ULONG m_ulMFTRecordCount = 0LU;

    std::shared_ptr<VolumeReader> m_pVolReader;
    std::shared_ptr<VolumeReader> m_pVolRandomReader;

    std::unique_ptr<IMFT> m_pMFT;

    // Internal call callbacks
    typedef std::function<HRESULT(MFTWalker* pThis, MFTRecord* pRecord, bool& bFreeRecord)> CallCallbackCall;
    CallCallbackCall m_pCallbackCall;
    Callbacks m_Callbacks;

    HRESULT SetCallbacks(const Callbacks& pCallbacks);
    HRESULT FullCallCallbackForRecord(MFTRecord* pRecord, bool& bFreeRecord);
    HRESULT SimpleCallCallbackForRecord(MFTRecord* pRecord, bool& bFreeRecord);

    HRESULT WalkRecords(bool bIsFinalWalk);

    DWORD m_dwWalkedItems = 0L;

    WCHAR* m_pFullNameBuffer = nullptr;
    DWORD m_dwFullNameBufferLen = 0LU;

    HRESULT ExtendNameBuffer(WCHAR** pCurrent);

    HRESULT UpdateAttributeList(MFTRecord* pRecord);

    bool IsRecordComplete(
        MFTRecord* pRecord,
        std::vector<MFT_SEGMENT_REFERENCE>& missingRecords,
        bool bAndAttributesComplete = true,
        bool bAndAllParents = true) const;
    bool AreAttributesComplete(const MFTRecord* pBaseRecord, std::vector<MFT_SEGMENT_REFERENCE>& missingRecords) const;

    HRESULT DeleteRecord(MFTRecord* pRecord);

    HRESULT AddDirectoryName(MFTRecord* pRecord);

    HRESULT AddRecord(MFTUtils::SafeMFTSegmentNumber& ullRecordIndex, CBinaryBuffer& Data, MFTRecord*& pRecord);
    HRESULT AddRecordCallback(MFTUtils::SafeMFTSegmentNumber& ullRecordIndex, CBinaryBuffer& Data);

    HRESULT ParseI30AndCallback(MFTRecord* pRecord);

    HRESULT Parse$SecureAndCallback(MFTRecord* pRecord);

    bool IsInLocation(PFILE_NAME pFileName);

    const WCHAR* GetFullNameAndIfInLocation(
        PFILE_NAME pFileName,
        const std::shared_ptr<DataAttribute>& pDataAttr,
        DWORD* pdwLen,
        bool* pbInSpecificLocation);
};

}  // namespace Orc

#pragma managed(pop)
