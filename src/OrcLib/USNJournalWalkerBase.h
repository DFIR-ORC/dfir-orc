//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "LocationSet.h"
#include "HeapStorage.h"

#include "IUSNJournalWalker.h"

#include "Windows.h"

#include <memory>
#include <unordered_set>

#pragma managed(push, off)

namespace Orc {
class MountedVolumeReader;

// Number of items in the VirtualStore
constexpr auto USN_MAX_NUMBER = (0x80000);

class USNJournalWalkerBase
{
public:
    USNJournalWalkerBase();
    virtual ~USNJournalWalkerBase();

    struct USN_RECORD_V2
    {
        DWORD RecordLength;
        WORD MajorVersion;
        WORD MinorVersion;
        DWORDLONG FileReferenceNumber;
        DWORDLONG ParentFileReferenceNumber;
        USN Usn;
        LARGE_INTEGER TimeStamp;
        DWORD Reason;
        DWORD SourceInfo;
        DWORD SecurityId;
        DWORD FileAttributes;
        WORD FileNameLength;
        WORD FileNameOffset;
        WCHAR FileName[1];
    };
    using PUSN_RECORD_V2 = USN_RECORD_V2*;

    struct USN_RECORD_V3
    {
        DWORD RecordLength;
        WORD MajorVersion;
        WORD MinorVersion;
        BYTE FileReferenceNumber[16];
        BYTE ParentFileReferenceNumber[16];
        USN Usn;
        LARGE_INTEGER TimeStamp;
        DWORD Reason;
        DWORD SourceInfo;
        DWORD SecurityId;
        DWORD FileAttributes;
        WORD FileNameLength;
        WORD FileNameOffset;
        WCHAR FileName[1];
    };
    using PUSN_RECORD_V3 = USN_RECORD_V3*;

    using USN_MAP = std::map<DWORDLONG, USN_RECORD*>;
    const USN_MAP& GetUSNMap();

    HRESULT ExtendNameBuffer(WCHAR** pCurrent);
    WCHAR* GetFullNameAndIfInLocation(USN_RECORD* pElt, DWORD* pdwLen, bool* pbInSpecificLocation);

protected:
    HeapStorage m_RecordStore;
    USN_MAP m_USNMap;

    std::unordered_set<DWORDLONG> m_LocationsRefNum;

    DWORDLONG m_dwlRootUSN;

    DWORD m_cchMaxComponentLength;
    DWORD m_dwRecordMaxSize;

    std::shared_ptr<VolumeReader> m_VolReader;

    DWORD m_dwWalkedItems;

    WCHAR* m_pFullNameBuffer;
    DWORD m_cbFullNameBufferLen;

};  // USNJournalWalkerBase
}  // namespace Orc

#pragma managed(pop)
