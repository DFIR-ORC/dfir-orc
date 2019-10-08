//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ByteStream.h"

#include "ArchiveFormat.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

#include <concrt.h>

#pragma managed(push, off)

namespace Orc {

class ArchiveExtract;
class ArchiveCompress;

class ORCLIB_API Archive
{
public:
    class ArchiveItem
    {
    public:
        enum Status
        {
            Waiting,
            Selected,
            Processing,
            Done
        };

    public:
        DWORD Index = (DWORD)-1;
        std::wstring NameInArchive;
        std::wstring Path;
        std::shared_ptr<ByteStream> Stream;
        ULONGLONG Size = 0LL;
        CBinaryBuffer SHA1;
        CBinaryBuffer MD5;
        bool bDeleteWhenAdded = false;
        Status currentStatus = Waiting;
        FILETIME modifiedTime = {0L, 0L};
        DWORD attrib = 0L;
        bool isDir = false;

        ArchiveItem() = default;

        ArchiveItem(ArchiveItem&& other) noexcept
        {
            Index = other.Index;
            Size = other.Size;
            modifiedTime = other.modifiedTime;
            isDir = other.isDir;
            bDeleteWhenAdded = other.bDeleteWhenAdded;
            attrib = other.attrib;
            std::swap(NameInArchive, other.NameInArchive);
            std::swap(Path, other.Path);
            std::swap(Stream, other.Stream);
            std::swap(MD5, other.MD5);
            std::swap(SHA1, other.SHA1);
            currentStatus = other.currentStatus;
        }
        ArchiveItem(const ArchiveItem& other) = default;

        ArchiveItem& operator=(const ArchiveItem& other) = default;
    };

    using ArchiveItems = std::vector<ArchiveItem>;
    using ArchiveIndexes = std::unordered_map<size_t, size_t>;
    using ArchiveCallback = std::function<void(const ArchiveItem& item)>;

protected:
    logger _L_;

    ArchiveItems m_Items;
    ArchiveIndexes m_Indexes;
    concurrency::critical_section m_cs;
    bool m_bComputeHash;

    std::wstring m_Password;
    ArchiveCallback m_Callback;

public:
    Archive(logger pLog, bool bComputeHash)
        : _L_(std::move(pLog))
        , m_bComputeHash(bComputeHash)
        , m_Callback(nullptr) {};

    const ArchiveItems& Items() { return m_Items; };

    const ArchiveItem& operator[](const std::wstring& aCabbedName);

    static ArchiveFormat GetArchiveFormat(const std::wstring_view& filepath);
    static std::wstring_view GetArchiveFormatString(const ArchiveFormat format);

    HRESULT SetCallback(ArchiveCallback aCallback);

    HRESULT SetPassword(const std::wstring& pwd)
    {
        m_Password = pwd;
        return S_OK;
    }

    virtual ~Archive(void);
};

}  // namespace Orc

#pragma managed(pop)
