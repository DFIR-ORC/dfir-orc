//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include <string>
#include <algorithm>
#include <regex>

#include <boost/scope_exit.hpp>

#include "CabExtract.h"

#include "LogFileWriter.h"
#include "ParameterCheck.h"

#include "Temporary.h"
#include "FileStream.h"
#include "MemoryStream.h"
#include "NTFSStream.h"
#include "XORStream.h"
#include "CryptoHashStream.h"
#include "WideAnsi.h"

#include "EmbeddedResource.h"

using namespace std;

using namespace Orc;

CabExtract::CabExtract(logger pLog, bool bComputeHash)
    : ArchiveExtract(std::move(pLog), bComputeHash)
{
}

CabExtract::~CabExtract(void) {}

/*
CabDecompress::GetHRESULTFromERF

Converts a FDI error code to it's corresponding HRESULT
*/
HRESULT CabExtract::GetHRESULTFromERF(__in ERF& erf)
{
    switch (erf.erfOper)
    {
        case FDIERROR_NONE:
            log::Error(_L_, S_OK, L"FDI: No error code specified\r\n");
            return S_OK;

        case FDIERROR_ALLOC_FAIL:
            log::Error(_L_, E_OUTOFMEMORY, L"FDI: FDIERROR_ALLOC_FAIL\r\n");
            return E_OUTOFMEMORY;

        case FDIERROR_CABINET_NOT_FOUND:
            log::Error(_L_, HRESULT_FROM_WIN32(ERROR_NOT_FOUND), L"FDI: FDIERROR_CABINET_NOT_FOUND\r\n");
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        case FDIERROR_NOT_A_CABINET:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_NOT_A_CABINET\r\n");
            return E_FAIL;

        case FDIERROR_UNKNOWN_CABINET_VERSION:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_UNKNOWN_CABINET_VERSION\r\n");
            return E_FAIL;

        case FDIERROR_CORRUPT_CABINET:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_CORRUPT_CABINET\r\n");
            return E_FAIL;

        case FDIERROR_BAD_COMPR_TYPE:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_BAD_COMPR_TYPE\r\n");
            return E_FAIL;

        case FDIERROR_MDI_FAIL:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_MDI_FAIL\r\n");
            return E_FAIL;

        case FDIERROR_TARGET_FILE:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_TARGET_FILE\r\n");
            return E_FAIL;

        case FDIERROR_RESERVE_MISMATCH:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_RESERVE_MISMATCH\r\n");
            return E_FAIL;

        case FDIERROR_WRONG_CABINET:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_WRONG_CABINET\r\n");
            return E_FAIL;

        case FDIERROR_USER_ABORT:
            log::Error(_L_, E_FAIL, L"FDI: FDIERROR_USER_ABORT\r\n");
            return E_ABORT;

        default:
            log::Error(_L_, E_FAIL, L"FDI: Unknown error code (0x%lx)\r\n", erf.erfOper);
            return E_FAIL;
    }
}

VOID* CabExtract::static_FDIAlloc(ULONG cb)
{
    return new BYTE[cb];
}

VOID CabExtract::static_FDIFree(_In_opt_ VOID* pv)
{
    if (pv)
    {
        delete[] static_cast<BYTE*>(pv);
    }
}

bool CabExtract::IsCancelled()
{
    return false;
}

/*
CCabDecompress::static_FDIOpen

Wraps CreateFileA for FDI's fopen operations.

Opens the file for read access only and ignores any other flags passed
by the FDI function. This function is called by FDI to open up a CAB file.

Parameters:
pszFile - Path to the file that is opened for read access

All other parameters are ignored

Return:
An initialized DECOMP_FILE_HANDLE structure with pOwner set to NULL
*/
INT_PTR CabExtract::static_FDIOpen(__in CHAR FAR* pszFile, __in INT, __in INT)
{
    HRESULT hr = E_FAIL;
    PDECOMP_FILE_HANDLE phDecomp = NULL;

    if (pszFile == NULL)
    {
        hr = E_POINTER;
        return -1;
    }

#ifdef _WIN64
    LARGE_INTEGER li;
    if (FAILED(hr = GetIntegerFromHexaString(pszFile, li)))
    {
        hr = E_INVALIDARG;
        return -1;
    }
    CabExtract* pThis = (CabExtract*)li.QuadPart;
#else
    DWORD dwPointer = 0L;
    if (FAILED(hr = GetIntegerFromHexaString(pszFile, dwPointer)))
    {
        hr = E_INVALIDARG;
        return -1;
    }
    CabExtract* pThis = (CabExtract*)dwPointer;
#endif

    // Initialize and allocate a new DECOMP_FILE_HANDLE structure
    phDecomp = new DECOMP_FILE_HANDLE;

    if (phDecomp == NULL)
    {
        hr = E_OUTOFMEMORY;
        return -1;
    }

    phDecomp->pOwner = pThis;

    if (FAILED(hr = pThis->m_MakeArchiveStream(phDecomp->Item.Stream)))
    {
        delete phDecomp;
        return -1;
    }

    return (INT_PTR)phDecomp;
}

/*
CCabDecompress::static_FDIRead

Wraps ReadFile for FDI's read operation.

Parameters:
hf - Pointer to a preivously initialized DECOMP_FILE_HANDLE structure
allocated via static_FDIOpen or FDINotify functions.
*/
UINT CabExtract::static_FDIRead(__in INT_PTR hf, __in VOID* pv, __in UINT cb)
{
    HRESULT hr = E_FAIL;
    ULONGLONG ullBytesRead = 0;
    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;

    if (phDecomp == NULL || pv == NULL)
        return (UINT)-1;

    if (phDecomp->Item.Stream == nullptr)
        return (UINT)-1;

    if (!phDecomp->pOwner || phDecomp->pOwner->IsCancelled())
        return (UINT)-1;

    if (FAILED(hr = phDecomp->Item.Stream->Read(pv, cb, &ullBytesRead)))
    {
        log::Error(phDecomp->pOwner->_L_, HRESULT_FROM_WIN32(GetLastError()), L"FDI: ReadFile failed\r\n");
        return (UINT)-1;
    }

    if (ullBytesRead > MAXDWORD)
    {
        log::Error(phDecomp->pOwner->_L_, E_INVALIDARG, L"FDI: ReadFile: Too many bytes were read, FDI overflow\r\n");
        return (UINT)-1;
    }

    return static_cast<DWORD>(ullBytesRead);
}

/*
CCabDecompress::static_FDIWrite

Wraps WriteFile for FDI's write operation

Parameters:
hf - Pointer to a preivously initialized DECOMP_FILE_HANDLE structure allocated
via static_FDIOpen or FDINotify functions.
*/
UINT CabExtract::static_FDIWrite(__in INT_PTR hf, __in VOID* pv, __in UINT cb)
{
    HRESULT hr = E_FAIL;

    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;

    if (phDecomp == NULL || pv == NULL)
        return (UINT)-1;

    CabExtract* pThis = (CabExtract*)pv;

    if (hf == (INT_PTR)-1 || hf == NULL)
    {
        log::Info(pThis->_L_, L"FDIWrite called with invalid file handle\r\n");
        return 0L;
    }

    if (phDecomp->pOwner && phDecomp->pOwner->IsCancelled())
        return (UINT)-1;

    ULONGLONG ullBytesWritten = 0L;

    if (FAILED(hr = phDecomp->Item.Stream->Write(pv, cb, &ullBytesWritten)))
    {
        log::Error(pThis->_L_, HRESULT_FROM_WIN32(GetLastError()), L"FDIWrite failed\r\n");
        return (UINT)-1;
    }
    if (ullBytesWritten > MAXDWORD)
    {
        log::Error(pThis->_L_, E_INVALIDARG, L"FDI: WriteFile: Too many bytes were read, FDI overflow\r\n");
        return (UINT)-1;
    }
    return static_cast<UINT>(ullBytesWritten);
}

/*
CCabDecompress::static_FDIClose

Wrapper for FDI's fclose operation. Releases resources and destroys
a DECOMP_FILE_HANDLE structure.

Parameters:
hf - Pointer to a preivously initialized DECOMP_FILE_HANDLE structure
allocated via static_FDIOpen or FDINotify functions.
*/
INT CabExtract::static_FDIClose(__in INT_PTR hf)
{
    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;

    if (phDecomp == NULL)
        return -1;
    if (phDecomp->Item.Stream == nullptr)
        return -1;

    phDecomp->Item.Stream->Close();

    delete phDecomp;

    return 0;
}

/*
CCabDecompress::static_FDISeek

Wraps SetFilePointer for FDI's seek operation

Parameters:
hf  -   Pointer to a preivously initialized DECOMP_FILE_HANDLE structure
allocated via static_FDIOpen or FDINotify functions.

See fseek documentation for the rest of parameters
*/
LONG CabExtract::static_FDISeek(__in INT_PTR hf, __in LONG dist, __in INT seektype)
{
    PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)hf;

    if (phDecomp == NULL)
        return -1;
    if (!phDecomp->pOwner || phDecomp->pOwner->IsCancelled())
        return -1;
    if (phDecomp->Item.Stream == nullptr)
        return (UINT)-1;

    HRESULT hr = E_FAIL;
    ULONG64 ullNewPosition;

    if (FAILED(hr = phDecomp->Item.Stream->SetFilePointer(dist, seektype, &ullNewPosition)))
    {
        log::Error(phDecomp->pOwner->_L_, hr, L"SetFilePointerEx failed\r\n");
        return -1;
    }
    return static_cast<LONG>(ullNewPosition);
}

/*
CCabDecompress::static_FDINotify

Static stub for CCabDecompress::FDINotify
*/
INT_PTR CabExtract::static_FDINotify(__in FDINOTIFICATIONTYPE fdint, __in PFDINOTIFICATION pfdin)
{
    if (pfdin == NULL || pfdin->pv == NULL)
        return -1;

    return ((CabExtract*)pfdin->pv)->FDINotify(fdint, pfdin);
}

INT_PTR CabExtract::FDINotify(__in FDINOTIFICATIONTYPE fdint, __in PFDINOTIFICATION pfdin)
{
    INT_PTR piRetVal = -1;  // default return value aborts the cabbing.

    if (pfdin == NULL || pfdin->pv == NULL)
        return piRetVal;

    if (IsCancelled())
        return piRetVal;

    switch (fdint)
    {
        case fdintCOPY_FILE:
        {
            bool keepDecomp = false;
            HRESULT hr = E_FAIL;
            wstring strFilePath;
            // Skip extracting the file if anything fails
            piRetVal = 0;

            // Create and initialize an new DECOMP_FILE_HANDLE structure for the
            // file.
            PDECOMP_FILE_HANDLE phDecomp = new DECOMP_FILE_HANDLE;

            if (phDecomp == NULL)
            {
                log::Error(_L_, E_OUTOFMEMORY, L"Out of memory, skipping file %S\r\n", pfdin->psz1);
                return piRetVal;
            }

            BOOST_SCOPE_EXIT(&phDecomp, &keepDecomp)
            {
                if (!keepDecomp && phDecomp)
                {
                    delete phDecomp;
                    phDecomp = nullptr;
                }
            }
            BOOST_SCOPE_EXIT_END;

            phDecomp->pOwner = this;

            // Generate a unique filename for the file that will be extracted
            std::shared_ptr<ByteStream> pStream;
            shared_ptr<XORStream> pXORStream;

            string s = pfdin->psz1;
            wstring strFileName = wstring(s.begin(), s.end());

            if (XORStream::IsNameXORPrefixed(strFileName.c_str()) == S_OK)
            {
                WCHAR szUnprefix[MAX_PATH];
                DWORD dwPattern;

                pXORStream = make_shared<XORStream>(_L_);

                if (FAILED(hr = pXORStream->GetXORPatternFromName(strFileName.c_str(), dwPattern)))
                    return piRetVal;

                if (FAILED(hr = pXORStream->XORUnPrefixFileName(strFileName.c_str(), szUnprefix, MAX_PATH)))
                    return piRetVal;

                pXORStream->SetXORPattern(dwPattern);
                strFileName = szUnprefix;
            }

            // Test if we wanna skip some files
            if (m_ShouldBeExtracted != nullptr)
            {
                if (!m_ShouldBeExtracted(strFileName))
                    return piRetVal;
            }

            // a this point, decision is taken to extract the file
            phDecomp->Item.NameInArchive = strFileName;

            auto filestream = m_MakeWriteAbleStream(phDecomp->Item);

            if (pXORStream)
            {

                if (m_bComputeHash)
                {
                    auto hashstream = make_shared<CryptoHashStream>(_L_);
                    if (FAILED(
                            hr = hashstream->OpenToWrite(
                                CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1,
                                filestream)))
                        return piRetVal;
                    if (FAILED(hr = pXORStream->OpenForXOR(hashstream)))
                        return piRetVal;
                }
                else
                {
                    if (FAILED(hr = pXORStream->OpenForXOR(filestream)))
                        return piRetVal;
                }
                pStream = pXORStream;
            }
            else
            {
                if (m_bComputeHash)
                {
                    auto hashstream = make_shared<CryptoHashStream>(_L_);
                    if (FAILED(hashstream->OpenToWrite(
                            CryptoHashStream::Algorithm::MD5 | CryptoHashStream::Algorithm::SHA1,
                            filestream)))
                        return piRetVal;
                    pStream = hashstream;
                }
                else
                {
                    pStream = filestream;
                }
            }

            keepDecomp = true;  // for scoper

            phDecomp->Item.Stream = pStream;

            log::Verbose(_L_, L"Extracting %s...", strFileName.c_str());
            piRetVal = (INT_PTR)phDecomp;
        }
        break;

        case fdintCLOSE_FILE_INFO:
        {
            PDECOMP_FILE_HANDLE phDecomp = (PDECOMP_FILE_HANDLE)pfdin->hf;

            ArchiveItem extracted;

            std::swap(extracted.NameInArchive, phDecomp->Item.NameInArchive);
            std::swap(extracted.Path, phDecomp->Item.Path);

            auto hashstream =
                std::dynamic_pointer_cast<CryptoHashStream>(ByteStream::GetHashStream(phDecomp->Item.Stream));
            if (hashstream)
            {
                hashstream->GetMD5(extracted.MD5);
                hashstream->GetSHA1(extracted.SHA1);
            }

            piRetVal = static_FDIClose(pfdin->hf) == 0 ? TRUE : FALSE;

            if (m_Callback != nullptr)
                m_Callback(extracted);

            m_Items.push_back(std::move(extracted));

            log::Verbose(_L_, L"Done\r\n");
        }
        break;
        case fdintPARTIAL_FILE:
        case fdintNEXT_CABINET:

            log::Error(_L_, E_ABORT, L"Unexpected fdi callback, aborting decompression\r\n");
            piRetVal = -1;
            break;
        case fdintENUMERATE:
            log::Verbose(_L_, L"Enumerate call\r\n");
            piRetVal = 0;
            break;
        default:
            piRetVal = 0;
    }

    return piRetVal;
}

HRESULT CabExtract::Extract(
    __in ArchiveExtract::MakeArchiveStream MakeArchiveStream,
    __in const ArchiveExtract::ItemShouldBeExtractedCallback pShouldExtractCB,
    __in ArchiveExtract::MakeOutputStream MakeWriteAbleStream)
{
    HRESULT hr = E_FAIL;
    HFDI hfdi = NULL;
    ERF fdiERF = {0};

    if (MakeArchiveStream == nullptr || MakeWriteAbleStream == nullptr)
    {
        hr = E_POINTER;
        return hr;
    }

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
        return GetHRESULTFromERF(fdiERF);
    }

    BOOST_SCOPE_EXIT((&hfdi))
    {
        if (hfdi != NULL)
        {
            if (!FDIDestroy(hfdi))
            {
            }
        }
    }
    BOOST_SCOPE_EXIT_END

    CHAR szThis[20];
    StringCchPrintfA(szThis, 20, "0x%p", this);

    m_MakeArchiveStream = MakeArchiveStream;
    m_MakeWriteAbleStream = MakeWriteAbleStream;
    m_ShouldBeExtracted = pShouldExtractCB;

    if (!FDICopy(hfdi, szThis, "", 0, static_FDINotify, NULL, this))
    {
        hr = IsCancelled() ? E_ABORT : GetHRESULTFromERF(fdiERF);
        return hr;
    }
    return S_OK;
}
