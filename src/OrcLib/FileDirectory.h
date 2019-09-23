//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include <Winternl.h>
#include <functional>

#include "OrcLib.h"

#include "StructuredOutputWriter.h"
#include "TableOutputWriter.h"

#pragma managed(push, off)

namespace Orc {

struct FILE_DIRECTORY_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    WCHAR FileName[1];
};
using PFILE_DIRECTORY_INFORMATION = FILE_DIRECTORY_INFORMATION*;

class LogFileWriter;

class ORCLIB_API FileDirectory
{
public:
    typedef std::function<void(
        std::wstring&& strPileName,
        std::wstring&& strPath,
        const PFILE_DIRECTORY_INFORMATION pDirectoryInformation)>
        Callback;

    class ORCLIB_API FileInstance
    {
    public:
        std::wstring Name;
        std::wstring Path;
        LARGE_INTEGER CreationTime;
        LARGE_INTEGER LastAccessTime;
        LARGE_INTEGER LastWriteTime;
        LARGE_INTEGER ChangeTime;
        LARGE_INTEGER EndOfFile;
        LARGE_INTEGER AllocationSize;
        ULONG FileAttributes;

        FileInstance(
            std::wstring&& name,
            std::wstring&& path,
            const LARGE_INTEGER& creationTime,
            const LARGE_INTEGER& lastAccessTime,
            const LARGE_INTEGER& lastWriteTime,
            const LARGE_INTEGER& changeTime,
            const LARGE_INTEGER& endOfFile,
            const LARGE_INTEGER& allocationSize,
            ULONG fileAttributes) noexcept
        {
            std::swap(Name, name);
            std::swap(Path, path);
            CreationTime = creationTime;
            LastAccessTime = lastAccessTime;
            LastWriteTime = lastWriteTime;
            ChangeTime = changeTime;
            EndOfFile = endOfFile;
            AllocationSize = allocationSize;
            FileAttributes = fileAttributes;
        }

        HRESULT Write(const logger& pLog, ITableOutput& output, const FILETIME& CollectionDate) const;
        HRESULT Write(
            const logger& pLog,
            const std::shared_ptr<StructuredOutputWriter>& pWriter,
            const FILETIME& CollectionDate) const;
    };

private:
    logger _L_;

public:
    FileDirectory(logger pLog)
        : _L_(std::move(pLog)) {};

    HRESULT ParseFileDirectory(const std::wstring& aObjDir, Callback aCallback);
    HRESULT ParseFileDirectory(const std::wstring& aObjDir, std::vector<FileInstance>& objects);

    ~FileDirectory();
};

}  // namespace Orc

#pragma managed(pop)
