//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <boost/scope_exit.hpp>

#include "FileDirectory.h"

#include "NtDllExtension.h"

#include "BinaryBuffer.h"

#include "SystemDetails.h"

#include "ObjectDirectory.h"

using namespace Orc;

//
// MessageId: STATUS_NO_MORE_FILES
//
// MessageText:
//
// {No More Files}
// No more files were found which match the file specification.
//
static const auto STATUS_NO_MORE_FILES = ((NTSTATUS)0x80000006L);

HRESULT FileDirectory::FileInstance::Write(ITableOutput& output, const std::wstring& strDescription) const
{
    SystemDetails::WriteComputerName(output);
    SystemDetails::WriteDescriptionString(output);

    output.WriteExactFlags(ObjectDirectory::File, ObjectDirectory::g_ObjectTypeDefinition);
    output.WriteString(Name.c_str());
    output.WriteString(Path.c_str());

    output.WriteNothing();
    output.WriteNothing();

    output.WriteString(strDescription);

    output.WriteEndOfLine();

    return S_OK;
}
HRESULT FileDirectory::FileInstance::Write(IStructuredOutput& pWriterOutput, LPCWSTR szElement) const
{
    pWriterOutput.BeginElement(szElement);

    pWriterOutput.WriteNamed(L"name", Name.c_str());
    pWriterOutput.WriteNamed(L"path", Path.c_str());

    if (CreationTime.QuadPart != 0LL)
        pWriterOutput.WriteNamed(L"creation_time", CreationTime.QuadPart);
    if (LastWriteTime.QuadPart != 0LL)
        pWriterOutput.WriteNamed(L"lastwrite_time", LastWriteTime.QuadPart);
    if (ChangeTime.QuadPart != 0LL)
        pWriterOutput.WriteNamed(L"lastchange_time", ChangeTime.QuadPart);
    if (LastAccessTime.QuadPart != 0LL)
        pWriterOutput.WriteNamed(L"lastaccess_time", LastAccessTime.QuadPart);
    if (AllocationSize.QuadPart != 0LL)
        pWriterOutput.WriteNamed(L"allocation", (size_t)AllocationSize.QuadPart);
    if (EndOfFile.QuadPart != 0LL)
        pWriterOutput.WriteNamed(L"end_of_file", (size_t)EndOfFile.QuadPart);
    if (FileAttributes != 0LL)
        pWriterOutput.WriteNamedAttributes(L"attributes", FileAttributes);

    pWriterOutput.EndElement(szElement);
    return S_OK;
}

HRESULT FileDirectory::ParseFileDirectory(const std::wstring& aObjDir, FileDirectory::Callback aCallback)
{
    HRESULT hr = E_FAIL;

    if (aCallback == nullptr)
        return E_POINTER;

    const auto pNtDll = ExtensionLibrary::GetLibrary<NtDllExtension>();
    if (pNtDll == nullptr)
        return E_FAIL;

    HANDLE hPipeRoot = CreateFile(
        aObjDir.c_str(),
        GENERIC_READ,
        FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);

    if (hPipeRoot == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    BOOST_SCOPE_EXIT(&hPipeRoot)
    {
        if (hPipeRoot != INVALID_HANDLE_VALUE)
            CloseHandle(hPipeRoot);
        hPipeRoot = INVALID_HANDLE_VALUE;
    }
    BOOST_SCOPE_EXIT_END;

    DWORD dwPageSize = 0L;
    if (FAILED(hr = SystemDetails::GetPageSize(dwPageSize)))
        return hr;

    CBinaryBuffer FileInformation(true);
    FileInformation.SetCount(dwPageSize * 4);
    IO_STATUS_BLOCK IoStatusBlock;
    ZeroMemory(&IoStatusBlock, sizeof(IoStatusBlock));

    while ((hr = pNtDll->NtQueryDirectoryFile(
                hPipeRoot,
                nullptr,
                nullptr,
                nullptr,
                &IoStatusBlock,
                FileInformation.GetData(),
                (ULONG)FileInformation.GetCount(),
                FileDirectoryInformation,
                FALSE,
                nullptr,
                FALSE))
           != HRESULT_FROM_NT(STATUS_NO_MORE_FILES))
    {

        if (FAILED(hr))
        {
            Log::Error("Failed NtQueryDirectoryFile (code: {:#x})", hr);
            return hr;
        }

        DWORD dwWalkedBytes = 0L;

        PFILE_DIRECTORY_INFORMATION pDirInfo = (PFILE_DIRECTORY_INFORMATION)FileInformation.GetData();

        while (dwWalkedBytes < IoStatusBlock.Information)
        {
            std::wstring strFileName;
            strFileName.assign(pDirInfo->FileName, pDirInfo->FileNameLength / sizeof(WCHAR));

            std::wstring path;
            path.reserve(aObjDir.length() + 1 + strFileName.length());

            path.assign(aObjDir);
            path.append(strFileName);

            aCallback(std::move(strFileName), std::move(path), pDirInfo);

            if (pDirInfo->NextEntryOffset == 0)
                break;

            dwWalkedBytes += pDirInfo->NextEntryOffset;
            pDirInfo = (PFILE_DIRECTORY_INFORMATION)(((BYTE*)pDirInfo) + pDirInfo->NextEntryOffset);
        }
    }

    return S_OK;
}

HRESULT FileDirectory::ParseFileDirectory(const std::wstring& aObjDir, std::vector<FileInstance>& objects)
{
    return ParseFileDirectory(
        aObjDir,
        [&objects, aObjDir](std::wstring&& strFileName, std::wstring&& strPath, const PFILE_DIRECTORY_INFORMATION pDI) {
            objects.emplace_back(
                std::move(strFileName),
                std::move(strPath),
                pDI->CreationTime,
                pDI->LastAccessTime,
                pDI->LastWriteTime,
                pDI->ChangeTime,
                pDI->EndOfFile,
                pDI->AllocationSize,
                pDI->FileAttributes);
        });
}

FileDirectory::~FileDirectory() {}
