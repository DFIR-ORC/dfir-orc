//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include "NtfsFileInfo.h"

#include "FileStream.h"

#include "Authenticode.h"
#include "Temporary.h"

#include "NtfsDataStructures.h"

#include <Winternl.h>
#include <AccCtrl.h>

#include <numeric>
#include <strsafe.h>
#include <sstream>

using namespace Orc;

NtfsFileInfo::NtfsFileInfo(
    std::wstring strComputerName,
    const std::shared_ptr<VolumeReader>& pVolReader,
    Intentions DefaultIntentions,
    const std::vector<Filter>& Filters,
    LPCWSTR szFullName,
    DWORD dwLen,
    Authenticode& codeVerifyTrust)
    : FileInfo(std::move(strComputerName), pVolReader, DefaultIntentions, Filters, szFullName, dwLen, codeVerifyTrust)
{
}

NtfsFileInfo::~NtfsFileInfo() {}

HRESULT NtfsFileInfo::OpenHash()
{
    if (GetFileReferenceNumber() == $BADCLUS_FILE_REFERENCE_NUMBER)
        return HRESULT_FROM_WIN32(ERROR_DATA_NOT_ACCEPTED);

    return FileInfo::OpenHash();
}

HRESULT NtfsFileInfo::HandleIntentions(const Intentions& intention, ITableOutput& output)
{
    HRESULT hr = FileInfo::HandleIntentions(intention, output);

    if (E_FAIL == hr)
    {
        switch (intention)
        {
            case Intentions::FILEINFO_LASTATTRCHGDATE:
                hr = WriteLastAttrChangeDate(output);
                break;

            case Intentions::FILEINFO_FN_CREATIONDATE:
                hr = WriteFileNameCreationDate(output);
                break;

            case Intentions::FILEINFO_FN_LASTMODDATE:
                hr = WriteFileNameLastModificationDate(output);
                break;

            case Intentions::FILEINFO_FN_LASTACCDATE:
                hr = WriteFileNameLastAccessDate(output);
                break;

            case Intentions::FILEINFO_FN_LASTATTRMODDATE:
                hr = WriteFileNameLastAttrModificationDate(output);
                break;

            case Intentions::FILEINFO_USN:
                hr = WriteUSN(output);
                break;

            case Intentions::FILEINFO_FRN:
                hr = WriteFRN(output);
                break;

            case Intentions::FILEINFO_PARENTFRN:
                hr = WriteParentFRN(output);
                break;

            case Intentions::FILEINFO_EXTENDED_ATTRIBUTE:
                hr = WriteExtendedAttributes(output);
                break;

            case Intentions::FILEINFO_ADS:
                hr = WriteADS(output);
                break;

            case Intentions::FILEINFO_OWNERID:
                hr = WriteOwnerId(output);
                break;

            case Intentions::FILEINFO_OWNERSID:
                hr = WriteOwnerSid(output);
                break;

            case Intentions::FILEINFO_OWNER:
                hr = WriteOwner(output);
                break;

            case Intentions::FILEINFO_FILENAMEID:
                hr = WriteFilenameID(output);
                break;

            case Intentions::FILEINFO_DATAID:
                hr = WriteDataID(output);
                break;

            case Intentions::FILEINFO_FILENAMEFLAGS:
                hr = WriteFilenameFlags(output);
                break;

            case Intentions::FILEINFO_SEC_DESCR_ID:
                hr = WriteSecDescrID(output);
                break;

            case Intentions::FILEINFO_EA_SIZE:
                hr = WriteEASize(output);
                break;

            case Intentions::FILEINFO_FILENAME_IDX:
                hr = WriteFilenameIndex(output);
                break;

            case Intentions::FILEINFO_DATA_IDX:
                hr = WriteDataIndex(output);
                break;

            case Intentions::FILEINFO_SNAPSHOTID:
                hr = WriteSnapshotID(output);
                break;

            case Intentions::FILEINFO_SSDEEP:
                hr = WriteSSDeep(output);
                break;

            case Intentions::FILEINFO_TLSH:
                hr = output.WriteNothing();
                break;

            case Intentions::FILEINFO_SIGNED_HASH:
                hr = WriteSignedHash(output);
                break;

            default:
                return E_FAIL;
                break;
        }
    }

    return hr;
}

HRESULT NtfsFileInfo::WriteLastAttrChangeDate(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);

    return E_NOTIMPL;
}

HRESULT NtfsFileInfo::WriteFileNameCreationDate(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);
    return E_NOTIMPL;
}

HRESULT NtfsFileInfo::WriteFileNameLastModificationDate(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);
    return E_NOTIMPL;
}

HRESULT NtfsFileInfo::WriteFileNameLastAttrModificationDate(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);
    return E_NOTIMPL;
}

HRESULT NtfsFileInfo::WriteFileNameLastAccessDate(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);
    return E_NOTIMPL;
}

HRESULT NtfsFileInfo::WriteExtendedAttributes(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);
    return E_NOTIMPL;
}

HRESULT NtfsFileInfo::WriteEASize(ITableOutput& output)
{
    DBG_UNREFERENCED_PARAMETER(output);
    return E_NOTIMPL;
}

HRESULT NtfsFileInfo::WriteFilenameIndex(ITableOutput& output)
{
    return output.WriteNothing();
}

HRESULT NtfsFileInfo::WriteDataIndex(ITableOutput& output)
{
    return output.WriteNothing();
}

HRESULT NtfsFileInfo::WriteSnapshotID(ITableOutput& output)
{
    return output.WriteGUID(GUID_NULL);
}
