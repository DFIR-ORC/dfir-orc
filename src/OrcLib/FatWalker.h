//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#pragma once

#include "OrcLib.h"
#include "BinaryBuffer.h"
#include "FatTable.h"
#include "FatFileEntry.h"
#include "FatDataStructures.h"
#include "FileSystem.h"

#include <list>
#include <set>
#include <map>
#include <memory>

#pragma managed(push, off)

namespace Orc {

class Location;
class VolumeReader;

class ORCLIB_API FatWalker
{
public:
    FatWalker(logger pLog);
    ~FatWalker();

    typedef std::function<void(
        const std::shared_ptr<VolumeReader>& volreader,
        const WCHAR* szFullName,
        const std::shared_ptr<FatFileEntry>& fileEntry)>
        FatFileEntryCall;

    class Callbacks
    {
    public:
        FatFileEntryCall m_FileEntryCall;
    };

    HRESULT Init(const std::shared_ptr<Location>& loc, bool bResurrectRecords);
    HRESULT Process(const Callbacks& Callbacks);

    static UCHAR ComputeDosNameChecksum(const FatFile83& fatFile83);
    struct LongFilename
    {
        std::wstring m_Filename;
        int m_SeqNumber = 0;
        bool m_IsFirst = false;
        UCHAR m_Checksum = 0x0;
    };

    typedef std::pair<const FatFileEntry*, const FatFileEntry*> ParentFolderAndSubFolder;
    typedef std::list<ParentFolderAndSubFolder> SubFolderList;
    typedef std::list<const FatFileEntry*> FatFileEntryList;
    typedef std::set<DWORD> ParsedClusterSet;
    typedef std::vector<LongFilename> LongFilenameList;
    typedef std::map<UCHAR, LongFilenameList> LongFilenamesByChecksums;

private:
    HRESULT ReadRootDirectory(CBinaryBuffer& buffer, DWORD size);
    HRESULT ReadClusterChain(const FatTable::ClusterChain& clusterChain, CBinaryBuffer& buffer);

    HRESULT ParseFolder(
        const FatTable& fatTable,
        CBinaryBuffer& folder,
        const FatFileEntry* parentFolder,
        FatFileEntryList& subFolders);
    HRESULT ParseFileEntry(
        const FatTable& fatTable,
        GenFatFile* genFatFile,
        FatFileEntry& fileEntry,
        LongFilename& longFileName,
        const LongFilenamesByChecksums& longFileNameByChecksums,
        bool bIsDeleted,
        bool& bLFN,
        bool& bIsSpecial);

    HRESULT BruteForceDeletedEntryFirstChar(
        const FatFile83&,
        const LongFilenamesByChecksums& longFileNameByChecksums,
        UCHAR& checksum) const;
    HRESULT PrintFileEntry(const FatFileEntry& fileEntry) const;

private:
    logger _L_;
    std::shared_ptr<Location> m_Location;
    CBinaryBuffer m_RootDirectoryBuffer;
    ULONGLONG m_ullRootDirectoryOffset;
    bool m_bResurrectRecords;

    FatFileSystem m_FatFS;
    const FatFileEntry* m_RootFolder = nullptr;

    FatFileSystemByName& m_FatFSByName = m_FatFS;
    FatFileSystemBySize& m_FatFSBySize = boost::multi_index::get<1>(m_FatFS);
};  // FatWalker

}  // namespace Orc

#pragma managed(pop)
