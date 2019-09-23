//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <regex>

#include <boost/scope_exit.hpp>

#include "LogFileWriter.h"
#include "WideAnsi.h"
#include "Temporary.h"
#include "ParameterCheck.h"

#include "ConcurrentCabDecompress.h"

#include "StreamAgent.h"
#include "StreamTransform.h"

#include "FileAgent.h"
#include "XORTransform.h"
#include "HashTransform.h"
#include "ResourceAgent.h"
#include "ReadWriteTransform.h"
#include "EmbeddedResource.h"

using namespace Concurrency;
using namespace std;

typedef struct _DECOMP_FILE_HANDLE
{
    ConcurrentCabDecompress* pOwner;
    LogFileWriter* pWriter;

    Concurrency::unbounded_buffer<StreamMessage::Message> result_buffer;
    Concurrency::unbounded_buffer<StreamMessage::Message> request_buffer;

    std::shared_ptr<StreamTransform<XORTransform>> pXORTransform;
    std::shared_ptr<StreamTransform<HashTransform>> pHashTransform;
    std::shared_ptr<StreamAgent> pAgent;
} DECOMP_FILE_HANDLE, *PDECOMP_FILE_HANDLE;

//--------------------------- CConcurrentCabDecompress ---------------------------- //

/*++

ConcurrentCabDecompress::GetHRESULTFromERF

Routine Description:
Converts a FDI error code to it's corresponding HRESULT

Parameters:
erf -   The FDI error code structure

Return:
The HRESULT mapping for erf
--*/
HRESULT ConcurrentCabDecompress::GetHRESULTFromERF(__in ERF& erf)
{
    switch (erf.erfOper)
    {
        case FDIERROR_NONE:
            LOG((*m_pW), (L"FDI Error: No error code specified\r\n"));
            return S_OK;

        case FDIERROR_ALLOC_FAIL:
            LOG((*m_pW), (L"FDI Error: FDIERROR_ALLOC_FAIL\r\n"));
            return E_OUTOFMEMORY;

        case FDIERROR_CABINET_NOT_FOUND:
            LOG((*m_pW), (L"FDI Error: FDIERROR_CABINET_NOT_FOUND\r\n"));
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        case FDIERROR_NOT_A_CABINET:
            LOG((*m_pW), (L"FDI Error: FDIERROR_NOT_A_CABINET\r\n"));
            return E_FAIL;

        case FDIERROR_UNKNOWN_CABINET_VERSION:
            LOG((*m_pW), (L"FDI Error: FDIERROR_UNKNOWN_CABINET_VERSION\r\n"));
            return E_FAIL;

        case FDIERROR_CORRUPT_CABINET:
            LOG((*m_pW), (L"FDI Error: FDIERROR_CORRUPT_CABINET\r\n"));
            return E_FAIL;

        case FDIERROR_BAD_COMPR_TYPE:
            LOG((*m_pW), (L"FDI Error: FDIERROR_BAD_COMPR_TYPE\r\n"));
            return E_FAIL;

        case FDIERROR_MDI_FAIL:
            LOG((*m_pW), (L"FDI Error: FDIERROR_MDI_FAIL\r\n"));
            return E_FAIL;

        case FDIERROR_TARGET_FILE:
            LOG((*m_pW), (L"FDI Error: FDIERROR_TARGET_FILE\r\n"));
            return E_FAIL;

        case FDIERROR_RESERVE_MISMATCH:
            LOG((*m_pW), (L"FDI Error: FDIERROR_RESERVE_MISMATCH\r\n"));
            return E_FAIL;

        case FDIERROR_WRONG_CABINET:
            LOG((*m_pW), (L"FDI Error: FDIERROR_WRONG_CABINET\r\n"));
            return E_FAIL;

        case FDIERROR_USER_ABORT:
            LOG((*m_pW), (L"FDI Error: FDIERROR_USER_ABORT\r\n"));
            return E_ABORT;

        default:
            _ASSERT(FALSE);
            LOG((*m_pW), (L"FDI Error: Unknown error code (%x%lx)\r\n", erf.erfOper));
            return E_FAIL;
    }
}

ConcurrentCabDecompress::ConcurrentCabDecompress(LogFileWriter* pW)
    : m_pW(pW)
{
    m_dwXOR = 0L;
}

ConcurrentCabDecompress::~ConcurrentCabDecompress() {}

VOID* ConcurrentCabDecompress::static_FDIAlloc(__in DWORD cb)
{
    return new BYTE[cb];
}

VOID ConcurrentCabDecompress::static_FDIFree(__in VOID* pv)
{
    _ASSERT(pv);
    delete[] pv;
}

/*++

CCabDecompress::static_FDIOpen

Routine Description:
Wraps CreateFileA for FDI's fopen operations.

Opens the file for read access only and ignores any other flags passed
by the FDI function. This function is called by FDI to open up a CAB
file.

Parameters:
pszFile -   Path to the file that is opened for read access

All other parameters are ignored

Return:
On success: An initialized DECOMP_FILE_HANDLE structure with pOwner set
to NULL

-1 otherwise
--*/
INT_PTR ConcurrentCabDecompress::static_FDIOpen(__in CHAR FAR* pszFile, __in INT, __in INT)
{
    HRESULT hr = E_FAIL;
    PDECOMP_FILE_HANDLE phDecomp = NULL;

    if (pszFile == NULL)
    {
        hr = E_POINTER;
        return -1;
    }

    //
    // Initialize and allocate a new DECOMP_FILE_HANDLE structure
    //
    phDecomp = new DECOMP_FILE_HANDLE;

    if (phDecomp == NULL)
    {
        hr = E_OUTOFMEMORY;
        delete phDecomp;
        return -1;
    }

    phDecomp->pWriter = new LogFileWriter();
    phDecomp->pOwner = NULL;  // can't get the owner for this function (not easily atleast)

    WCHAR swzFileName[MAX_PATH];

    if (FAILED(hr = AnsiToWide(phDecomp->pWriter, pszFile, swzFileName, MAX_PATH)))
    {
        delete phDecomp;
        delete phDecomp;
        return -1;
    }

    shared_ptr<StreamAgent> pAgent;

    {
        wstring FileName(swzFileName);

        wstring ResName, MotherShip, NameInArchive, Format;

        if (SUCCEEDED(
                hr = EmbeddedResource::SplitResourceReference(
                    phDecomp->pWriter, FileName, MotherShip, ResName, NameInArchive, Format)))
        {
            auto pResAgent =
                make_shared<ResourceAgent>(phDecomp->pWriter, phDecomp->request_buffer, phDecomp->result_buffer);

            if (!pResAgent)
            {
                hr = E_OUTOFMEMORY;
                delete phDecomp;
                return -1;
            }

            HRSRC hRes = NULL;
            HMODULE hModule = NULL;
            if (FAILED(
                    hr = EmbeddedResource::LocateResource(
                        phDecomp->pWriter, MotherShip, ResName, L"BINARY", hModule, hRes)))
            {
                delete phDecomp;
                return -1;
            }

            if (FAILED(hr = pResAgent->OpenForReadOnly(hModule, hRes)))
            {
                delete phDecomp;
                return -1;
            }

            pResAgent->start();
            pAgent = pResAgent;
        }
        else if (hr == HRESULT_FROM_WIN32(ERROR_NO_MATCH))
        {
            shared_ptr<FileAgent> pFileAgent =
                make_shared<FileAgent>(phDecomp->pWriter, phDecomp->request_buffer, phDecomp->result_buffer);

            if (!pFileAgent)
            {
                hr = E_OUTOFMEMORY;
                delete phDecomp;
                return -1;
            }

            if (FAILED(
                    hr = pFileAgent->OpenFile(
                        swzFileName,
                        GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL)))
            {
                delete phDecomp;
                return -1;
            }

            pFileAgent->start();
            pAgent = pFileAgent;
        }
        else
        {
            LOG(*phDecomp->pWriter,
                (L"ERROR: Failure during parsing of reference %s (hr=0x%lx)\r\n", FileName.c_str(), hr));
            {
                delete phDecomp;
                return -1;
            }
        }
    }

    phDecomp->pAgent = pAgent;

    return (INT_PTR)phDecomp;
}

/*++

CCabDecompress::static_FDIRead

Routine Description:
Wraps ReadFile for FDI's read operation.

Parameters:
hf      -   Pointer to a preivously initialized DECOMP_FILE_HANDLE structure
allocated via static_FDIOpen or FDINotify functions.

See _read() for rest of the parameters

Return:
See _read() doc
--*/
UINT ConcurrentCabDecompress::static_FDIRead(__in INT_PTR hf, __in VOID* pv, __in UINT cb)
{
    HRESULT hr = E_FAIL;
    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;
    UINT retval = 0;

    if (phDecomp == NULL || pv == NULL)
        return (UINT)-1;

    if (phDecomp->pAgent == NULL)
        return (UINT)-1;

    shared_ptr<StreamMessage> request = StreamMessage::MakeReadRequest(cb);

    Concurrency::send(phDecomp->request_buffer, request);

    shared_ptr<StreamMessage> result = Concurrency::receive(phDecomp->result_buffer);
    if (result)
    {
        if (FAILED(result->HResult()))
        {
            LOG((*phDecomp->pOwner->m_pW), (L"FDI: ReadFile failed: 0x%lx\r\n", HRESULT_FROM_WIN32(GetLastError())));
            return (UINT)-1;
        }

        if (request->Buffer().GetCount() > MAXDWORD)
        {
            LOG((*phDecomp->pOwner->m_pW), (L"FDI: ReadFile: Too many bytes were read, FDI overflow\r\n"));
            return (UINT)-1;
        }

        CopyMemory(pv, request->Buffer().GetData(), request->Buffer().GetCount());

        retval = (UINT)request->Buffer().GetCount();
    }
    else
    {
        LOG((*phDecomp->pOwner->m_pW), (L"FDI: ReadFile: No the result expected\r\n"));
    }
    return retval;
}

/*++

CCabDecompress::static_FDIWrite

Routine Description:
Wraps WriteFile for FDI's write operation

Parameters:
hf  -   Pointer to a preivously initialized DECOMP_FILE_HANDLE structure
allocated via static_FDIOpen or FDINotify functions.

See _write() doc for the rest of the parameters

Return:
See _write() doc
--*/
UINT ConcurrentCabDecompress::static_FDIWrite(__in INT_PTR hf, __in VOID* pv, __in UINT cb)
{
    HRESULT hr = E_FAIL;

    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;

    if (phDecomp == NULL || pv == NULL)
        return (UINT)-1;

    ConcurrentCabDecompress* pThis = (ConcurrentCabDecompress*)pv;

    if (hf == (INT_PTR)-1 || hf == NULL)
    {
        LOG(*pThis->m_pW, (L"FDIWrite called with invalid file handle\r\n"));
        return (UINT)-1;
    }

    ULONGLONG ullBytesWritten = 0;

    CBinaryBuffer buffer((LPBYTE)pv, cb);

    shared_ptr<StreamMessage> request = StreamMessage::MakeWriteRequest(buffer);

    Concurrency::send(phDecomp->request_buffer, request);

    shared_ptr<StreamMessage> result = Concurrency::receive(phDecomp->result_buffer);

    if (result)
    {
        if (FAILED(result->HResult()))
        {
            LOG(*pThis->m_pW, (L"FDIWrite failed: 0x%lx\r\n", result->HResult()));
            return (UINT)-1;
        }
        if (result->BytesWritten() > MAXDWORD)
        {
            LOG((*phDecomp->pOwner->m_pW), (L"FDI: WriteFile: Too many bytes were written, FDI overflow\r\n"));
            return (UINT)-1;
        }
        else
        {
            ullBytesWritten = result->BytesWritten();
        }
    }
    return static_cast<UINT>(ullBytesWritten);
}

/*++

CCabDecompress::static_FDIClose

Routine Description:
Wrapper for FDI's fclose operation. Releases resources and destroys
a DECOMP_FILE_HANDLE structure.

Parameters:
hf  -   Pointer to a preivously initialized DECOMP_FILE_HANDLE structure
allocated via static_FDIOpen or FDINotify functions.

Return:
0 if close was successfull
1 otherwise
--*/
INT ConcurrentCabDecompress::static_FDIClose(__in INT_PTR hf)
{
    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;

    if (phDecomp == NULL)
        return -1;
    if (phDecomp->pAgent == NULL)
        return -1;

    shared_ptr<StreamMessage> request = StreamMessage::MakeCloseRequest();

    Concurrency::send(phDecomp->request_buffer, request);

    shared_ptr<StreamMessage> result = Concurrency::receive(phDecomp->result_buffer);

    if (result)
    {
        if (FAILED(result->HResult()))
        {
            LOG((*phDecomp->pOwner->m_pW), (L"FDI: Close: Failed during closure\r\n"));
        }
    }
    if (phDecomp->pWriter != NULL)
        delete phDecomp->pWriter;

    Concurrency::agent::wait(phDecomp->pAgent.get());
    delete phDecomp;
    return 0;
}

/*++

CCabDecompress::static_FDISeek

Routine Description:
Wraps SetFilePointer for FDI's seek operation

Parameters:
hf  -   Pointer to a preivously initialized DECOMP_FILE_HANDLE structure
allocated via static_FDIOpen or FDINotify functions.

See fseek documentation for the rest of parameters

Return:
See fseek documentation
--*/
LONG ConcurrentCabDecompress::static_FDISeek(__in INT_PTR hf, __in LONG dist, __in INT seektype)
{
    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;

    if (phDecomp == NULL)
        return -1;

    HRESULT hr = E_FAIL;
    ULONG64 ullNewPosition = 0;

    shared_ptr<StreamMessage> request = StreamMessage::MakeSetFilePointer(seektype, dist);

    Concurrency::send(phDecomp->request_buffer, request);

    shared_ptr<StreamMessage> result = Concurrency::receive(phDecomp->result_buffer);

    if (result)
    {
        if (FAILED(result->HResult()))
        {
            LOG(*phDecomp->pOwner->m_pW, (L"SetFilePointerEx failed 0x%x\r\n", hr));
            return -1;
        }
        else
        {
            ullNewPosition = result->GetNewPosition();
            if (ullNewPosition > MAXLONG)
            {
                hr = E_NOT_VALID_STATE;
                LOG(*phDecomp->pOwner->m_pW,
                    (L"SetFilePointerEx new position is greater than MAXLONG, overflowing return value\r\n", hr));
            }
        }
    }
    return static_cast<LONG>(ullNewPosition);
}

/*++

CCabDecompress::static_FDINotify

Routine Description:
Static stub for CCabDecompress::FDINotify

Parameters:
See CCabDecompress::FDINotify

Return:
See CCabDecompress::FDINotify
--*/
INT_PTR ConcurrentCabDecompress::static_FDINotify(__in FDINOTIFICATIONTYPE fdint, __in PFDINOTIFICATION pfdin)
{
    if (pfdin == NULL || pfdin->pv == NULL)
        return -1;

    return ((ConcurrentCabDecompress*)pfdin->pv)->FDINotify(fdint, pfdin);
}

/*++

CCabDecompress::FDINotify

Routine Description:
Handler for the FDI notifcations

Parameters:
See FDI documentation

Return:
See FDI documentation.
--*/
INT_PTR ConcurrentCabDecompress::FDINotify(__in FDINOTIFICATIONTYPE fdint, __in PFDINOTIFICATION pfdin)
{
    INT_PTR piRetVal = -1;  // default return value aborts the cabbing.

    if (pfdin == NULL || pfdin->pv == NULL)
        goto End;

    switch (fdint)
    {
        case fdintCOPY_FILE:
        {
            HRESULT hr = E_FAIL;
            wstring strFileName;

            //
            // Skip extracting the file if anything fails
            //
            piRetVal = 0;

            //
            // Create and initialize an new DECOMP_FILE_HANDLE structure for the
            // file.
            //
            PDECOMP_FILE_HANDLE phDecomp = new DECOMP_FILE_HANDLE;

            if (phDecomp == NULL)
            {
                LOG(*m_pW, (L"Out of memory, skipping file: %s", strFileName.c_str()));
                goto End;
            }
            phDecomp->pWriter = NULL;
            phDecomp->pOwner = this;

            //
            // Generate a unique filename for the file that will be extracted
            //
            string s = pfdin->psz1;
            strFileName = wstring(s.begin(), s.end());

            shared_ptr<StreamTransform<XORTransform>> pXORTransform;

            if (XORTransform::IsNameXORPrefixed(strFileName.c_str()) == S_OK)
            {
                WCHAR szUnprefix[MAX_PATH];
                DWORD dwPattern;

                pXORTransform = make_shared<StreamTransform<XORTransform>>(0x00000000);

                if (FAILED(pXORTransform->Transform.GetXORPatternFromName(strFileName.c_str(), dwPattern)))
                {
                    delete phDecomp;
                    goto End;
                }

                if (FAILED(pXORTransform->Transform.XORUnPrefixFileName(strFileName.c_str(), szUnprefix, MAX_PATH)))
                {
                    delete phDecomp;
                    goto End;
                }

                pXORTransform->Transform.SetXORPattern(dwPattern);
                strFileName = szUnprefix;
            }

            // Test if we wanna skip some files
            wstring MatchedSpec;
            if (m_ListToExtract.size() > 0)
            {
                bool bFound = false;
                for (auto i = m_ListToExtract.begin(); i != m_ListToExtract.end(); ++i)
                {
                    const wstring& spec = *i;

                    if (PathMatchSpec(strFileName.c_str(), spec.c_str()))
                    {
                        MatchedSpec = spec;
                        bFound = true;
                        break;
                    }
                }
                if (!bFound)
                {
                    delete phDecomp;
                    goto End;
                }
            }

            auto pAgent = m_MakeAgent(m_pW, strFileName, phDecomp->request_buffer, phDecomp->result_buffer);

            if (pAgent == nullptr)
            {
                LOG(*m_pW, (L"Out of memory, skipping file: %s\r\n", strFileName.c_str()));
                delete phDecomp;
                goto End;
            }

            phDecomp->pXORTransform = pXORTransform;
            phDecomp->pAgent = pAgent;

            VERBOSE(*m_pW, (L"Extracting %s...", strFileName.c_str()));
            m_ListExtracted.push_back(ExtractedItem(MatchedSpec, strFileName, pAgent));

            piRetVal = (INT_PTR)phDecomp;
        }
        break;

        case fdintCLOSE_FILE_INFO:

            piRetVal = static_FDIClose(pfdin->hf) == 0 ? TRUE : FALSE;
            VERBOSE(*m_pW, (L"Done\r\n"));
            break;

        case fdintPARTIAL_FILE:
        case fdintNEXT_CABINET:

            LOG(*m_pW, (L"Unexpected fdi callback, aborting decompression\r\n"));
            piRetVal = -1;
            break;

        default:
            piRetVal = 0;
    }

    //
    // piRetVal value falls through
    //
End:
    return piRetVal;
}

bool ConcurrentCabDecompress::IsRegularArchiveFile(const WCHAR* szCabFileName)
{
    WCHAR ext[_MAX_EXT];
    errno_t err;

    if (err = _wsplitpath_s(szCabFileName, NULL, 0L, NULL, 0L, NULL, 0L, ext, _MAX_EXT))
        return false;

    WCHAR* pCurrent = ext;

    while (*pCurrent)
    {
        *pCurrent = towupper(*pCurrent);
        pCurrent++;
    }

    if (!_wcsnicmp(ext, L".cab", wcslen(L".cab")))
        return true;
    return false;
}

/*++

CCabDecompress::ExtractCab

Routine Description:
Extracts all the files in a cabinet file to a directory.

Parameters:
pwszCabFilePath      -   The path to the cab file

pwszExtractRootDir   -   The path to the directory where the cab files will
be extracted to

hCancelEvent        -   Optional handle to an event that the caller can
signal to cancel the extraction process. While
extracting files this event is polled to see whether
it is set. If it is set, the method returns
immeidately

Return:
S_OK    -   Success
Error HR otherwise
--*/

HRESULT ConcurrentCabDecompress::ExtractFromCab(
    PCWSTR pwzCabFilePath,
    std::function<std::shared_ptr<StreamAgent>(
        LogFileWriter* pW,
        std::wstring aName,
        StreamMessage::ISource& source,
        StreamMessage::ITarget& target)> MakeAgent,
    const std::vector<std::wstring>& ListToExtract,
    std::vector<ExtractedItem>& ListExtracted,
    const DWORD dwXOR)
{
    HRESULT hr = E_FAIL;
    m_ListToExtract.clear();
    m_ListToExtract.reserve(ListToExtract.size());
    std::copy(ListToExtract.begin(), ListToExtract.end(), back_inserter(m_ListToExtract));

    return ExtractFromCab(pwzCabFilePath, MakeAgent, ListExtracted, dwXOR);
}

HRESULT ConcurrentCabDecompress::ExtractFromCab(
    __in PCWSTR pwzCabFilePath,
    std::function<std::shared_ptr<StreamAgent>(
        LogFileWriter* pW,
        std::wstring aName,
        StreamMessage::ISource& source,
        StreamMessage::ITarget& target)> MakeAgent,
    __inout std::vector<ExtractedItem>& ListExtracted,
    __in_opt const DWORD dwXOR)
{
    HRESULT hr = E_FAIL;
    HFDI hfdi = NULL;
    CHAR szCabFilePath[MAX_PATH];
    CHAR szCabFileName[MAX_PATH];
    PCHAR pszCabFileName = NULL;
    ERF fdiERF = {0};

    if (pwzCabFilePath == NULL)
    {
        hr = E_POINTER;
        goto End;
    }

    szCabFilePath[0] = 0;
    szCabFileName[0] = 0;

    //
    // Convert the cab file path to ansi and seperate the path and file name
    // components from the entire path.
    //
    if (FAILED(hr = WideToAnsi(m_pW, pwzCabFilePath, szCabFilePath, ARRAYSIZE(szCabFilePath))))
    {
        LOG(*m_pW, (L"WideToAnsi failed: 0x%lx\r\n", hr));
        goto End;
    }

    m_ListExtracted.clear();

    if (EmbeddedResource::IsResourceBasedArchiveFile(pwzCabFilePath))
    {
        pszCabFileName = szCabFilePath;
    }
    else if (IsRegularArchiveFile(pwzCabFilePath))
    {
        pszCabFileName = strrchr(szCabFilePath, '\\');

        if (pszCabFileName != NULL)
            pszCabFileName++;
        else
            pszCabFileName = szCabFilePath;

        hr = StringCchCopyA(szCabFileName, ARRAYSIZE(szCabFileName), pszCabFileName);

        if (FAILED(hr))
        {
            LOG(*m_pW, (L"StringCopyA failed (0x%lx)\r\n", hr));
            goto End;
        }

        *pszCabFileName = 0;
    }

    m_dwXOR = dwXOR;

    m_MakeAgent = MakeAgent;

    //
    // Create the FDI context and start the copy operation
    //
    hfdi = FDICreate(
        static_FDIAlloc,
        static_FDIFree,
        static_FDIOpen,
        static_FDIRead,
        static_FDIWrite,
        static_FDIClose,
        static_FDISeek,
        cpu80386,
        &fdiERF);

    if (hfdi == NULL)
    {
        hr = GetHRESULTFromERF(fdiERF);
        goto End;
    }

    if (!FDICopy(hfdi, szCabFileName, szCabFilePath, 0, static_FDINotify, NULL, this))
    {
        hr = GetHRESULTFromERF(fdiERF);
        goto End;
    }

    ListExtracted.clear();

    m_ListExtracted.swap(ListExtracted);

    hr = S_OK;

End:

    if (hfdi != NULL)
    {
        if (!FDIDestroy(hfdi))
        {
            LOG(*m_pW, (L"FDIDestroy failed (0x%lx)\r\n", GetHRESULTFromERF(fdiERF)))
        }
    }

    return hr;
}

HRESULT ConcurrentCabDecompress::ExtractFromCab(
    __in PCWSTR pwszCabFilePath,
    __in PCWSTR pwszExtractRootDir,
    __in const vector<wstring>& ListToExtract,
    __inout vector<ExtractedItem>& ListExtractEd,
    __in_opt const DWORD dwXOR)
{
    HRESULT hr = E_FAIL;
    m_ListToExtract.clear();
    m_ListToExtract.reserve(ListToExtract.size());
    std::copy(ListToExtract.begin(), ListToExtract.end(), back_inserter(m_ListToExtract));

    return ExtractFromCab(pwszCabFilePath, pwszExtractRootDir, ListExtractEd, dwXOR);
}

HRESULT ConcurrentCabDecompress::ExtractFromCab(
    __in PCWSTR pwszCabFilePath,
    __in PCWSTR pwszExtractRootDir,
    __inout vector<ExtractedItem>& ListExtracted,
    __in_opt const DWORD dwXOR)
{
    HRESULT hr = E_FAIL;
    HFDI hfdi = NULL;
    PCHAR pszCabFileName = NULL;
    ERF fdiERF = {0};

    if (pwszCabFilePath == NULL || pwszExtractRootDir == NULL)
    {
        hr = E_POINTER;
        return hr;
    }

    if (!UtilPathIsDirectory(pwszExtractRootDir))
    {
        hr = E_INVALIDARG;
        LOG(*m_pW, (L"%s is not a directory\r\n", pwszExtractRootDir));
        return hr;
    }

    auto MakeFileAgent = [pwszExtractRootDir](
                             LogFileWriter* pW,
                             std::wstring aName,
                             StreamMessage::ISource& source,
                             StreamMessage::ITarget& target) -> std::shared_ptr<StreamAgent> {
        HRESULT hr = E_FAIL;
        wstring::size_type indexLastBackSlash;
        static const wstring::size_type npos = -1;

        wstring strFilePath;
        wstring strExtractDirectory = pwszExtractRootDir;

        if (strExtractDirectory.back() != L'\\' && strExtractDirectory.size() > 3)
        {
            strExtractDirectory.append(L"\\");
        }

        if ((indexLastBackSlash = aName.rfind(L"\\")) != npos)
        {
            wstring::iterator it = aName.begin() + indexLastBackSlash;
            strExtractDirectory.append(aName.begin(), it);

            aName.erase(aName.begin(), it + 1);

            WCHAR szOutputDir[MAX_PATH];

            if (FAILED(hr = GetOutputDir(pwszExtractRootDir, szOutputDir, MAX_PATH, true)))
            {
                LOG(*pW, (L"GetOutputDir failed, skipping file '%s': 0x%x\r\n", (LPCWSTR)aName.c_str(), hr));
                return nullptr;
            }
        }

        if (FAILED(hr = UtilGetUniquePath(strExtractDirectory.c_str(), aName.c_str(), strFilePath)))
        {
            LOG(*pW, (L"UtilGetUniquePath failed, skipping file '%s': 0x%x\r\n", (LPCWSTR)aName.c_str(), hr));
            return nullptr;
        }

        shared_ptr<FileAgent> pFileAgent = make_shared<FileAgent>(pW, source, target);

        if (FAILED(
                hr = pFileAgent->OpenFile(
                    strFilePath.c_str(),
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                    NULL)))
        {
            LOG(*pW,
                (L"CreateFile failed (path = %s): 0x%x\r\n", strFilePath.c_str(), HRESULT_FROM_WIN32(GetLastError())));
            return nullptr;
        }

        pFileAgent->start();
        return pFileAgent;
    };

    return ExtractFromCab(pwszCabFilePath, MakeFileAgent, ListExtracted, dwXOR);
}
