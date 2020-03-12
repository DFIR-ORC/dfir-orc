//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "stdafx.h"

#include "OrcLib.h"

#include "ArchiveCreate.h"

#include <string>
#include <memory>

#include "ByteStream.h"

#include "MFTRecord.h"
#include "VolumeReader.h"

#include <fci.h>

#pragma managed(push, off)

using CABCOMPRESSCALLBACK = HRESULT(PVOID pContext, ULONG64 CabbedSoFar, ULONG64 TotalSize);

constexpr auto CC_STREAM_FLAG_ADD_TO_CAB = 0x1;
constexpr auto CC_STREAM_FLAG_DONT_CLOSE = 0x2;

namespace Orc {

class CAB_ITEM
{

private:
    bool operator==(const CAB_ITEM&) const { throw E_NOTIMPL; }

public:
    CHAR szCabbedName[CB_MAX_CAB_PATH];  // name of the file after being cabbed
    CHAR szKey[17];  // key to identify the stream during FCIOpen calls
    DWORD dwFlags;  // flags about the streams
    Archive::ArchiveItem Item;

    CAB_ITEM()
    {
        memset(szCabbedName, 0, CB_MAX_CAB_PATH * sizeof(CHAR));
        memset(szKey, 0, 17 * sizeof(CHAR));
        dwFlags = 0L;
    }

    CAB_ITEM(const CAB_ITEM& other)
        : Item(other.Item)
    {
        strcpy_s(szCabbedName, other.szCabbedName);
        strcpy_s(szKey, other.szKey);
        dwFlags = other.dwFlags;
    };
    CAB_ITEM(CAB_ITEM&& other)
    {
        strcpy_s(szCabbedName, other.szCabbedName);
        memset(other.szCabbedName, 0, CB_MAX_CAB_PATH * sizeof(CHAR));
        strcpy_s(szKey, other.szKey);
        memset(other.szKey, 0, 17 * sizeof(CHAR));
        dwFlags = other.dwFlags;
        other.dwFlags = 0;
        std::swap(Item, other.Item);
    };
};

class ORCLIB_API CabCreate : public ArchiveCreate
{

protected:
    std::vector<CAB_ITEM> m_StreamList;
    HANDLE m_hFCI;
    ERF m_currentFCIError;
    CCAB m_currentCab;

    ULONG64 m_cbTotalData;  // # of bytes that needs to be compressed
    ULONG64 m_cbCabbedSoFar;  // # of bytes that have been compressed

    CABCOMPRESSCALLBACK m_pCallback;
    PVOID m_pCallbackContext;

    bool m_bTempInMemory;

    static FNFCIALLOC(FCIAlloc);
    static FNFCIFREE(FCIFree);
    static FNFCIOPEN(FCIOpen);
    static FNFCIREAD(FCIRead);
    static FNFCIWRITE(FCIWrite);
    static FNFCICLOSE(FCIClose);
    static FNFCISEEK(FCISeek);
    static FNFCIDELETE(FCIDelete);
    static FNFCIGETTEMPFILE(FCIGetTempFileName);
    static FNFCIFILEPLACED(FCIFilePlaced);
    static FNFCIGETNEXTCABINET(FCIGetNextCab);
    static FNFCIGETOPENINFO(FCIOpenInfo);
    static FNFCISTATUS(FCIStatus);

    HRESULT GetHRESULTFromERF();

    HRESULT AddStreamToStreamList(
        __in const std::shared_ptr<ByteStream> pStreamToAdd,
        __in DWORD dwFlags,
        __in_opt PCWSTR pwzCabbedName,
        __deref_opt_out CAB_ITEM** ppAddedStreamEntry);

    HRESULT AddStreamToStreamList(
        __in const Archive::ArchiveItem& anItem,
        __in DWORD dwFlags,
        __deref_opt_out CAB_ITEM** ppAddedStreamEntry);

    HRESULT FindStreamInStreamList(
        __in_opt PCSTR pszKey,
        __in_opt ByteStream* pStream,
        __deref_opt_out_opt CAB_ITEM** ppStreamEntry);

    HRESULT CreateFCIContext(
        __in PCSTR pszOutputStreamKey,
        __in_opt CABCOMPRESSCALLBACK pCallback,
        __in_opt PVOID pCallbackContext);

public:
    CabCreate(logger pLog, bool bComputeHash = false, DWORD XORPattern = 0x00000000, bool bTempInMemory = true);
    ~CabCreate();

    // ArchiveCompress methods
    STDMETHOD(InitArchive)(__in const std::filesystem::path& path, Archive::ArchiveCallback pCallback = nullptr);
    STDMETHOD(InitArchive)(__in PCWSTR pwzArchivePath, Archive::ArchiveCallback pCallback = nullptr);
    STDMETHOD(InitArchive)
    (__in const std::shared_ptr<ByteStream>& pOutputStream, Archive::ArchiveCallback pCallback = nullptr);

    STDMETHOD(SetCompressionLevel)(__in const std::wstring& strLevel);

    STDMETHOD(FlushQueue)();

    STDMETHOD(Complete)();
    STDMETHOD(Abort)();
};
}  // namespace Orc

#pragma managed(pop)
