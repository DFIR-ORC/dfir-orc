//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "CabCreate.h"

#include "LogFileWriter.h"

#include "Temporary.h"
#include "FileStream.h"
#include "MemoryStream.h"
#include "NTFSStream.h"
#include "XORStream.h"
#include "CryptoHashStream.h"
#include "WideAnsi.h"

#include <string>
#include <algorithm>
#include <regex>

using namespace std;
using namespace Orc;

constexpr auto CAB_MAX_SIZE = 0x80000000;

constexpr auto FCI_TEMPFILENAME = "*TEMP*";

constexpr auto COMPRESSION_TYPE = (tcompTYPE_LZX | tcompLZX_WINDOW_LO);

CabCreate::CabCreate(logger pLog, bool bComputeHash, DWORD XORPattern, bool bTempInMemory)
    : ArchiveCreate(std::move(pLog), bComputeHash, XORPattern)
    , m_cbCabbedSoFar(0)
    , m_cbTotalData(0)
    , m_currentCab {0}
    , m_currentFCIError {0}
    , m_pCallbackContext(nullptr)
{
    m_hFCI = NULL;
    m_bTempInMemory = bTempInMemory;
}

CabCreate::~CabCreate()
{
    Abort();
}

/*
CabCompress::AbortCab

Cancels processing of cab and closes all streams associated with the cab
*/
HRESULT CabCreate::Abort()
{
    if (m_hFCI != NULL)
    {
        if (!FCIDestroy(m_hFCI))
        {
            log::Error(_L_, GetHRESULTFromERF(), L"FCIDestroy failed\r\n");
        }
        m_hFCI = NULL;
    }

    // close all streams in the stream list except the ones marked as "don't close"
    for (auto entry : m_StreamList)
    {
        if (entry.Item.Stream != nullptr && !(entry.dwFlags & CC_STREAM_FLAG_DONT_CLOSE))
        {
            entry.Item.Stream->Close();
            entry.Item.Stream = nullptr;
        }
    }
    m_StreamList.clear();
    return S_OK;
}

/*
CabCompress::GetHRESULTFromERF

Returns a HRESULT representation of the FCI/FDI error

Parameters:
erf -   The FCI ERF structure which describes the error

Return:
The HRESULT code of the last fci operation.
*/
HRESULT CabCreate::GetHRESULTFromERF()
{
    switch (m_currentFCIError.erfOper)
    {
        case FCIERR_NONE:
            log::Error(_L_, S_OK, L"FCI: No error code specified\r\n");
            return S_OK;
        case FCIERR_ALLOC_FAIL:
            log::Error(_L_, E_OUTOFMEMORY, L"FCI: FCIERR_ALLOC_FAIL\r\n");
            return E_OUTOFMEMORY;
        case FCIERR_BAD_COMPR_TYPE:
            log::Error(_L_, E_INVALIDARG, L"FCI: FCIERR_BAD_COMPR_TYPE\r\n");
            return E_INVALIDARG;
        case FCIERR_USER_ABORT:
            log::Error(_L_, E_ABORT, L"FCI: FCIERR_USER_ABORT\r\n");
            return E_ABORT;
        case FCIERR_MCI_FAIL:
            log::Error(_L_, E_FAIL, L"FCI: FCIERR_MCI_FAIL\r\n");
            return E_FAIL;
        case FCIERR_OPEN_SRC:
            log::Error(_L_, m_currentFCIError.erfType, L"FCI: FCIERR_OPEN_SRC\r\n");
            return m_currentFCIError.erfType;
        case FCIERR_READ_SRC:
            log::Error(_L_, m_currentFCIError.erfType, L"FCI: FCIERR_READ_SRC\r\n");
            return m_currentFCIError.erfType;
        case FCIERR_TEMP_FILE:
            log::Error(_L_, m_currentFCIError.erfType, L"FCI: FCIERR_TEMP_FILE\r\n");
            return m_currentFCIError.erfType;
        case FCIERR_CAB_FILE:
            log::Error(_L_, m_currentFCIError.erfType, L"FCI: FCIERR_CAB_FILE\r\n");
            return m_currentFCIError.erfType;
        case FCIERR_CAB_FORMAT_LIMIT:
            log::Error(
                _L_,
                m_currentFCIError.erfType,
                L"FCI: FCIERR_CAB_FORMAT_LIMIT: Data-size or file-count exceeded CAB format limits, "
                L"i.e. Total-bytes (uncompressed) in a CAB-folder exceeded 0x7FFF8000 (~ 2GB), "
                L"or, CAB size (compressed) exceeded 0x7FFFFFFF "
                L"or, File-count in CAB exceeded 0xFFFF\r\n");
            return m_currentFCIError.erfType;
        default:
            log::Error(_L_, E_FAIL, L"Unknown FCI error code %i\r\n", m_currentFCIError.erfOper);
            return E_FAIL;
    }
}

/*
CabCompress::AddStreamToStreamList

Adds a stream to the database of streams associatied with the current
cab. Each stream added is given a unique key which the caller can pass
to FCIAddFile or FCICreate. During a FCIOpen call the keys are mapped
to their appropriate stream and the correct stream handled is returned.

Parameters:
pStreamToAdd        -   The stream to be added to the list
dwFlags             -   Properties of the stream (see CC_STREAM_FLAG* defines)
pwszCabbedName      -   If the stream is to be packaged into the cab, this
contains the name of the stream when placed in the cab

ppAddedStreamEntry  -   If present, this recieves a pointer to the newly
created CAB_STREAM structure which includes the stream's key
*/
HRESULT CabCreate::AddStreamToStreamList(
    __in const std::shared_ptr<ByteStream> pStreamToAdd,
    __in DWORD dwFlags,
    __in_opt PCWSTR pwszCabbedName,
    __deref_opt_out CAB_ITEM** ppAddedStreamEntry)
{
    HRESULT hr = E_FAIL;
    CAB_ITEM streamEntry;

    if (!pStreamToAdd)
        return E_INVALIDARG;

    if (pwszCabbedName)
    {
        // FCI apis only work with ANSI characters. Do the necessary conversion.
        if (FAILED(hr = WideToAnsi(_L_, pwszCabbedName, streamEntry.szCabbedName, ARRAYSIZE(streamEntry.szCabbedName))))
            return hr;
        streamEntry.Item.NameInArchive = pwszCabbedName;
    }

    // Generate unique key for this stream
    if (FAILED(hr = StringCchPrintfA(streamEntry.szKey, ARRAYSIZE(streamEntry.szKey), "%p", pStreamToAdd.get())))
        return hr;

    streamEntry.dwFlags = dwFlags;
    streamEntry.Item.Stream = pStreamToAdd;

    m_StreamList.push_back(streamEntry);

    if (ppAddedStreamEntry)
    {
        *ppAddedStreamEntry = &m_StreamList.back();
    }

    log::Verbose(_L_, L"Added stream: name = %s, stream = @%p\r\n", pwszCabbedName, pStreamToAdd.get());
    return S_OK;
}

HRESULT CabCreate::AddStreamToStreamList(
    __in const Archive::ArchiveItem& anItem,
    __in DWORD dwFlags,
    __deref_opt_out CAB_ITEM** ppAddedStreamEntry)
{
    HRESULT hr = E_FAIL;
    CAB_ITEM streamEntry;

    streamEntry.Item = anItem;

    if (!anItem.NameInArchive.empty())
    {
        // FCI apis only work with ANSI characters. Do the necessary conversion.
        if (FAILED(
                hr = WideToAnsi(
                    _L_, anItem.NameInArchive.c_str(), streamEntry.szCabbedName, ARRAYSIZE(streamEntry.szCabbedName))))
            return hr;
    }

    // Generate unique key for this stream
    if (FAILED(
            hr =
                StringCchPrintfA(streamEntry.szKey, ARRAYSIZE(streamEntry.szKey), "%p", streamEntry.Item.Stream.get())))
        return hr;

    streamEntry.dwFlags = dwFlags;

    m_StreamList.push_back(streamEntry);

    if (ppAddedStreamEntry)
    {
        *ppAddedStreamEntry = &m_StreamList.back();
    }

    log::Verbose(
        _L_, L"Added stream: name = %s, stream = @%p\r\n", anItem.NameInArchive.c_str(), streamEntry.Item.Stream.get());
    return S_OK;
}

/*
CabCompress::FindStreamInStreamList

Searches for a stream entry in the stream list

Parameters:
pwszKey          -  If present, the search is done by comparing the stream's
key (see FCIOpen)

pStream         -   If present, the search is done by looking address of the
stream (see FCIClose)

ppStreamEntry   -   If the search is successfull this will points to the CAB_STREAM
structure of the stream
*/
HRESULT CabCreate::FindStreamInStreamList(
    __in_opt PCSTR pszKey,
    __in_opt ByteStream* pStream,
    __deref_opt_out_opt CAB_ITEM** ppStreamEntry)
{
    if (ppStreamEntry)
        *ppStreamEntry = NULL;
    for (CAB_ITEM& StreamEntry : m_StreamList)
    {
        if ((pszKey && (strcmp(pszKey, StreamEntry.szKey) == 0))
            || (pStream && (StreamEntry.Item.Stream.get() == pStream)))
        {
            if (ppStreamEntry)
                *ppStreamEntry = &StreamEntry;
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

HRESULT CabCreate::InitArchive(__in PCWSTR pwzArchivePath, Archive::ArchiveCallback pCallback)
{
    HRESULT hr = E_FAIL;

    shared_ptr<FileStream> pOutStream = make_shared<FileStream>(_L_);

    if (FAILED(hr = pOutStream->WriteTo(pwzArchivePath)))
        return hr;

    if (FAILED(hr = InitArchive(pOutStream, pCallback)))
        return hr;

    return S_OK;
}

HRESULT CabCreate::SetCompressionLevel(__in const std::wstring& strLevel)
{
    DBG_UNREFERENCED_PARAMETER(strLevel);
    log::Verbose(_L_, L"Compression level for cabinet is ignored\r\n");
    return S_OK;
}

/*
CabCompress::InitCabStream

Initializes the object. This should be called before calling any of the other
function.

Parameters:
pOutputStream    -  The stream to which the cab will be written to
pCallback        -  The callback function that gets progress updates during cabbing
pCallbackContext -  The context passed to the callback function
*/
HRESULT CabCreate::InitArchive(__in const shared_ptr<ByteStream>& pOutputStream, Archive::ArchiveCallback pCallback)
{
    HRESULT hr = E_FAIL;
    CAB_ITEM* pStreamEntry = nullptr;

    if (m_Format != ArchiveFormat::Cabinet)
    {
        log::Error(_L_, E_NOT_VALID_STATE, L"Invalid format spec for a cabinet\r\n");
        return E_NOT_VALID_STATE;
    }

    if (m_hFCI != NULL)
        return E_PENDING;

    // Set initial values for vars
    ZeroMemory(&m_currentFCIError, sizeof(m_currentFCIError));
    ZeroMemory(&m_currentCab, sizeof(m_currentCab));

    m_cbTotalData = 0;
    m_cbCabbedSoFar = 0;

    m_currentCab.cb = CAB_MAX_SIZE;
    m_currentCab.cbFolderThresh = CAB_MAX_SIZE;

    // Adds the output stream to stream list, but make sure that it does not
    // get added to the cab (leave out the CC_STREAM_FLAG_ADD_TO_CAB flag).
    if (FAILED(hr = AddStreamToStreamList(pOutputStream, 0L, NULL, &pStreamEntry)))
    {
        log::Error(_L_, hr, L"AddStreamToStreamList failed\r\n");
        return hr;
    }

    if (FAILED(hr = StringCchCopyA(m_currentCab.szCab, ARRAYSIZE(m_currentCab.szCab), pStreamEntry->szKey)))
    {
        log::Error(_L_, hr, L"String copy failed: 0x%x\r\n", hr);
        return hr;
    }

    // Create FCI object. Use the key returned from AddStreamToStreamList as
    // the name of the cab so that FCIOpen file can return this stream to
    // compression engine.
    m_hFCI = FCICreate(
        &m_currentFCIError,
        FCIFilePlaced,
        FCIAlloc,
        FCIFree,
        FCIOpen,
        FCIRead,
        FCIWrite,
        FCIClose,
        FCISeek,
        FCIDelete,
        FCIGetTempFileName,
        &m_currentCab,
        this);

    if (m_hFCI == NULL)
    {
        hr = GetHRESULTFromERF();
        log::Error(_L_, hr, L"FCICreate failed: 0x%x\r\n", hr);
        return hr;
    }

    m_Callback = pCallback;
    return S_OK;
}

/*
CabCompress::FlushQueueToCab

Flushes all queued requests and finailizes the cab. Should be the last
call to the object. This call can potentially take a LONG time since
all the compression is done in here. Once this call returns, the
cab is completed and no other information can be added to the cab.
*/
HRESULT CabCreate::FlushQueue()
{

    if (m_hFCI == NULL)
    {
        log::Error(_L_, E_INVALIDARG, L"Uninitialized cab handle\r\n");
        Abort();
        return E_INVALIDARG;
    }

    // start adding files to the cab now
    HANDLE hFCI = m_hFCI;
    HRESULT hr = S_OK;

    m_Queue.erase(
        std::remove_if(
            m_Queue.begin(),
            m_Queue.end(),
            [hFCI, &hr, this](const Archive::ArchiveItem& Item) -> bool {
                CAB_ITEM* cabItem;
                if (FAILED(hr = AddStreamToStreamList(Item, CC_STREAM_FLAG_ADD_TO_CAB, &cabItem)))
                    return false;

                if (cabItem->dwFlags & CC_STREAM_FLAG_ADD_TO_CAB && SUCCEEDED(hr))
                {
                    log::Verbose(
                        _L_, L"FCIAddFile Adding: %S (stream key=%S)\r\n", cabItem->szCabbedName, cabItem->szKey);
                    if (!FCIAddFile(
                            hFCI,
                            cabItem->szKey,
                            cabItem->szCabbedName,
                            FALSE,
                            FCIGetNextCab,
                            FCIStatus,
                            FCIOpenInfo,
                            COMPRESSION_TYPE))
                    {
                        log::Error(_L_, hr = GetHRESULTFromERF(), L"FCIAddFile failed\r\n");
                        return false;
                    }
                    else
                    {
                        log::Verbose(
                            _L_, L"FCIAddFile added: %S (stream key=%S)\r\n", cabItem->szCabbedName, cabItem->szKey);

                        auto hashstream = std::dynamic_pointer_cast<CryptoHashStream>(cabItem->Item.Stream);
                        if (hashstream)
                        {
                            hashstream->GetMD5(cabItem->Item.MD5);
                            hashstream->GetSHA1(cabItem->Item.SHA1);
                        }

                        if (cabItem->Item.Stream != nullptr && !(cabItem->dwFlags & CC_STREAM_FLAG_DONT_CLOSE))
                        {
                            cabItem->Item.Stream->Close();
                            cabItem->Item.Stream = nullptr;
                        }

                        if (m_Callback)
                            m_Callback(cabItem->Item);

                        m_Items.push_back(std::move(cabItem->Item));
                        return true;
                    }
                }
                return false;
            }),
        m_Queue.end());

    if (FAILED(hr))
    {
        log::Error(_L_, hr, L"FlushQueueToCab failed while in FCIAddFile\r\n");
        Abort();
        return hr;
    }

    if (!FCIFlushFolder(m_hFCI, FCIGetNextCab, FCIStatus))
    {
        log::Error(_L_, hr = GetHRESULTFromERF(), L"FCIFlushFolder failed\r\n");
        Abort();
        return hr;
    }
    return S_OK;
}

/*
CabCreate::Complete

Flushes all queued requests and finailizes the cab. Should be the last
call to the object. This call can potentially take a LONG time since
all the compression is done in here. Once this call returns, the
cab is completed and no other information can be added to the cab.
*/
HRESULT CabCreate::Complete()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = FlushQueue()))
        return hr;

    if (!FCIFlushCabinet(m_hFCI, NULL, FCIGetNextCab, FCIStatus))
    {
        hr = GetHRESULTFromERF();
        log::Error(_L_, hr, L"FCIFlushCabinet failed with 0x%x\r\n");
        Abort();
        return hr;
    }

    if (m_hFCI != NULL)
    {
        if (!FCIDestroy(m_hFCI))
        {
            log::Error(_L_, E_FAIL, L"FCIDestroy failed\r\n");
        }
        m_hFCI = NULL;
    }

    // now close all streams in the stream list EVEN the ones marked as "don't close"
    for (auto StreamEntry : m_StreamList)
    {
        if (StreamEntry.Item.Stream != NULL)
        {
            StreamEntry.Item.Stream->Close();
            StreamEntry.Item.Stream = nullptr;
        }
    }
    m_StreamList.clear();
    return S_OK;
}

/*
CabCompress::FCIAlloc

Wraps malloc for FCI
*/
FNFCIALLOC(CabCreate::FCIAlloc)
{
    PBYTE pMem = NULL;

#pragma prefast(suppress : 419, "Suppressed this for now")

    return pMem = new BYTE[cb];
}

/*
CabCompress::FCIFree

Wraps free for FCI
*/
FNFCIFREE(CabCreate::FCIFree)
{
    delete[] static_cast<BYTE*>(memory);
    memory = NULL;
}

/*
CabCompress::FCIOpen
 
FCIOpen implementation. Returns a handle to the appropriate stream by
look for a stream which has the same key as 'pszFile'. When opening
a temporary file (pszFile == FCI_TEMPFILENAME) a new memory buffer is
instantiated and it's handle is returned.
*/
FNFCIOPEN(CabCreate::FCIOpen)
{
    HRESULT hr = E_FAIL;
    CabCreate* pThis = (CabCreate*)pv;
    constexpr auto pReturnERROR = (INT_PTR)-1;
    INT_PTR pHandleToReturn = pReturnERROR;

    UNREFERENCED_PARAMETER(pmode);
    UNREFERENCED_PARAMETER(oflag);

    if (strcmp(pszFile, FCI_TEMPFILENAME) == 0)
    {
        ByteStream* pTempStream = NULL;

        if (pThis->m_bTempInMemory)
        {
            // fci wants to create a temporary file. Create a new memory stream and
            // return it as the handle. No need to add this to the stream list.
            MemoryStream* pTempMemoryStream = new MemoryStream(pThis->_L_);
            if (!pTempMemoryStream)
            {
                *err = E_OUTOFMEMORY;
                return pReturnERROR;
            }

            if (FAILED(hr = pTempMemoryStream->OpenForReadWrite()))
            {
                *err = hr;
                delete pTempMemoryStream;
                return pReturnERROR;
            }
            pTempStream = pTempMemoryStream;
        }
        else
        {
            FileStream* pTempFileStream = new FileStream(pThis->_L_);

            if (!pTempFileStream)
            {
                *err = E_OUTOFMEMORY;
                return pReturnERROR;
            }

            if (FAILED(
                    hr = pTempFileStream->CreateNew(
                        L".cab.tmp", FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_FLAG_DELETE_ON_CLOSE, NULL)))
            {
                *err = hr;
                delete pTempFileStream;
                return pReturnERROR;
            }
            pTempStream = pTempFileStream;
        }

        pHandleToReturn = (INT_PTR)pTempStream;

        _ASSERT(oflag & O_CREAT);
    }
    else
    {
        CAB_ITEM* pCabStream = NULL;

        if (FAILED(hr = pThis->FindStreamInStreamList(pszFile, NULL, &pCabStream)))
        {
            *err = hr;
            return pReturnERROR;
        }

        pHandleToReturn = (INT_PTR)pCabStream->Item.Stream.get();
    }

    *err = S_OK;
    return pHandleToReturn;
}

/*
CabCompress::FCIRead

Wrapper for FCI to read data from a stream
*/
FNFCIREAD(CabCreate::FCIRead)
{
    HRESULT hr = E_FAIL;
    ULONGLONG BytesRead = 0;

    DBG_UNREFERENCED_PARAMETER(pv);

    if (hf == (INT_PTR)-1 || hf == NULL)
    {
        *err = hr;
        return static_cast<DWORD>(BytesRead);
    }

    ByteStream* pStream = (ByteStream*)hf;

    if (FAILED(hr = pStream->Read(memory, cb, &BytesRead)))
    {
        *err = hr;
        return static_cast<DWORD>(BytesRead);
    }

    return static_cast<DWORD>(BytesRead);
}

/*
CabCompress::FCIWrite

Wrapper for FCI to write data to a stream.
*/
FNFCIWRITE(CabCreate::FCIWrite)
{
    HRESULT hr = E_FAIL;
    ULONGLONG BytesWritten = (DWORD)-1;

    DBG_UNREFERENCED_PARAMETER(pv);

    if (hf == (INT_PTR)-1 || hf == NULL)
    {
        *err = E_FAIL;
        return (UINT)-1;
    }

    ByteStream* pStream = (ByteStream*)hf;
    if (FAILED(hr = pStream->Write(memory, cb, &BytesWritten)))
    {
        *err = hr;
        return (UINT)-1;
    }

    *err = S_OK;
    return static_cast<DWORD>(BytesWritten);
}

/*
CabCompress::FCIClose

Wrapper for closing a stream. Note: Only closes / destroys temporary
streams created by FCI. The other streams assoicatied wit the cab are
destroied by AbortCab(). Regular streams can not be closed and free'd
in this mehtod because FCI closes streams and then tries to re-open it.

Notes:
When cabbing is aborted during FCIOpen of a temp file, FCIOpen returns -1 as the File
Handle which is passed on to FCIClose without checking. This scenario could be hit in
the case of SQM when a SQM upload is aborted due to wait timeout. A check has been
added to catch these scenarios.
*/
FNFCICLOSE(CabCreate::FCIClose)
{
    HRESULT hr = E_FAIL;
    CabCreate* pThis = (CabCreate*)pv;

    if (hf == (INT_PTR)-1 || hf == NULL)
    {
        *err = E_FAIL;
        return (UINT)-1;
    }

    // Only delete the stream if it is not in the stream list (ie. it was a
    // temporary stream opened up by FCI)
    if (FAILED(hr = pThis->FindStreamInStreamList(NULL, (ByteStream*)hf, NULL)))
    {
        ((ByteStream*)hf)->Close();
        delete ((ByteStream*)hf);
        *err = S_OK;
        return (UINT)0;
    }

    return 0;
}

/*
CabCompress::FCISeek

Wrapper for seek a stream
*/
FNFCISEEK(CabCreate::FCISeek)
{
    HRESULT hr = E_FAIL;
    ULONG64 CurrPointer = 0;
    ByteStream* pStream = NULL;
    UNREFERENCED_PARAMETER(pv);

    if (hf == (INT_PTR)-1 || hf == NULL)
    {
        // LogWarning("FCISeek called with invalid file handle");
        goto End;
    }

    pStream = (ByteStream*)hf;

    hr = pStream->SetFilePointer(dist, seektype, &CurrPointer);

    if (FAILED(hr))
    {
        // LogError("SetFilePointer failed: 0x%x", hr);
        goto End;
    }

    hr = S_OK;

End:

    *err = hr;
    return (FAILED(hr)) ? (UINT)-1 : (LONG)CurrPointer;
}

/*
CaCompress::FCIDelete
             
Does not do anything
*/
FNFCIDELETE(CabCreate::FCIDelete)
{
    UNREFERENCED_PARAMETER(pszFile);
    UNREFERENCED_PARAMETER(err);
    UNREFERENCED_PARAMETER(pv);

    return 0;
}

/*
CabCompress::FCIOpenInfo

Retrieves the current data and time and then hands the rest of the call to
FCIOpen.
*/
FNFCIGETOPENINFO(CabCreate::FCIOpenInfo)
{
    FILETIME FileTime;

    GetSystemTimeAsFileTime(&FileTime);

    if (!FileTimeToDosDateTime(&FileTime, pdate, ptime))
    {
        *err = HRESULT_FROM_WIN32(GetLastError());

        return -1;
    }

    *pattribs = 0;  // _A_NORMAL

    return FCIOpen(pszName, 0, 0, err, pv);
}

/*
CabCompress::FCIStatus

Recieves statusFile progress callbacks from FCI and forwards it to
the client.
*/
FNFCISTATUS(CabCreate::FCIStatus)
{
    UNREFERENCED_PARAMETER(cb1);

    CabCreate* pThis = (CabCreate*)pv;

    if (typeStatus == statusFile && cb2 != 0)
    {
        pThis->m_cbCabbedSoFar += cb2;

        // TODO Implement a callback here...
    }

    return TRUE;
}

/*
CabCompress::FCIGetTempFileName

Wrapper for FCI to get temporary filenames. Returns a special filename
which FCIOpen recognises (see FCIOpen for more info).
*/
FNFCIGETTEMPFILE(CabCreate::FCIGetTempFileName)
{
    UNREFERENCED_PARAMETER(pv);

    return SUCCEEDED(StringCchCopyA(pszTempName, cbTempName, FCI_TEMPFILENAME));
}

/*
CabCompress::FCIFilePlaced

Does not do anything.
*/
FNFCIFILEPLACED(CabCreate::FCIFilePlaced)
{
    UNREFERENCED_PARAMETER(pccab);
    UNREFERENCED_PARAMETER(pszFile);
    UNREFERENCED_PARAMETER(cbFile);
    UNREFERENCED_PARAMETER(fContinuation);
    UNREFERENCED_PARAMETER(pv);

    return 0;
}

/*
CabCompress::FCIGetNextCab

Does not do anything.
*/
FNFCIGETNEXTCABINET(CabCreate::FCIGetNextCab)
{
    UNREFERENCED_PARAMETER(pccab);
    UNREFERENCED_PARAMETER(cbPrevCab);
    UNREFERENCED_PARAMETER(pv);

    return TRUE;
}
