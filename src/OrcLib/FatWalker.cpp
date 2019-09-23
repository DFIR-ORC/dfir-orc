//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Yann SALAUN
//
#include "stdafx.h"

#include "FatWalker.h"
#include "VolumeReader.h"
#include "Location.h"

#include <boost/algorithm/string.hpp>
#include <sstream>
#include <algorithm>

using namespace Orc;

FatWalker::FatWalker(logger pLog)
    : _L_(std::move(pLog))
    , m_ullRootDirectoryOffset(0)
    , m_bResurrectRecords(false)
{
}

FatWalker::~FatWalker() {}

HRESULT FatWalker::Init(const std::shared_ptr<Location>& loc, bool bResurrectRecords)
{
    HRESULT hr = E_FAIL;

    m_Location = loc;
    m_bResurrectRecords = bResurrectRecords;

    std::shared_ptr<VolumeReader> reader(m_Location->GetReader());

    if (nullptr == reader)
    {
        log::Error(_L_, hr, L"Failed to get reader from location %s\r\n", m_Location->GetLocation().c_str());
        return hr;
    }

    const CBinaryBuffer& vbr(reader->GetBootSector());
    ULONG ulSectorSize = reader->GetBytesPerSector();
    ULONG ulClusterSize = reader->GetBytesPerCluster();
    FSVBR::FSType fsType = reader->GetFSType();

    int nbReservedSectors = 0;
    int nbFatTables = 0;
    int nbSectorPerFatTable = 0;
    FatTable::FatTableType fatTableType = FatTable::FAT32;
    DWORD rootDirectoryCluster = 0;
    DWORD rootDirectorySize = 0;

    if (FSVBR::FSType::FAT12 == fsType || FSVBR::FSType::FAT16 == fsType)
    {
        fatTableType = FSVBR::FSType::FAT12 == fsType ? FatTable::FAT12 : FatTable::FAT16;
        PPackedFat1216BootSector fat1216VBR = reinterpret_cast<PPackedFat1216BootSector>(vbr.GetData());
        nbReservedSectors = (int)fat1216VBR->PackedGenFatBootSector.PackedBpb.ReservedSectors;
        nbFatTables = (int)fat1216VBR->PackedGenFatBootSector.PackedBpb.Fats;
        nbSectorPerFatTable = (int)fat1216VBR->PackedGenFatBootSector.PackedBpb.SectorsPerFat;
        rootDirectoryCluster = 2;
        rootDirectorySize = fat1216VBR->PackedGenFatBootSector.PackedBpb.RootEntries * 32;
    }
    else if (FSVBR::FSType::FAT32 == fsType)
    {
        PPackedFat32BootSector fat32VBR = reinterpret_cast<PPackedFat32BootSector>(vbr.GetData());
        nbReservedSectors = (int)fat32VBR->PackedGenFatBootSector.PackedBpb.ReservedSectors;
        nbFatTables = (int)fat32VBR->PackedGenFatBootSector.PackedBpb.Fats;
        nbSectorPerFatTable = (int)fat32VBR->NumberOfSectorsPerFat;
        rootDirectoryCluster = fat32VBR->RootDirectoryClusterNumber;
    }
    else
    {
        return E_FAIL;
    }

    ULONGLONG fatTableSize = static_cast<ULONGLONG>(nbSectorPerFatTable) * ulSectorSize;
    ULONGLONG ullFirstFatTableOffset = static_cast<ULONGLONG>(nbReservedSectors) * ulSectorSize;
    ULONGLONG ullSecondFatTableOffset = ullFirstFatTableOffset + fatTableSize;
    ULONGLONG ullFatTableOffset = 0;

    if (FAILED(hr = reader->Seek(ullFirstFatTableOffset)))
    {
        log::Error(
            _L_, hr, L"Failed to seek to first Fat table from location %s\r\n", m_Location->GetLocation().c_str());

        // try to seek second fat table
        if (FAILED(hr = reader->Seek(ullSecondFatTableOffset)))
        {
            log::Error(
                _L_, hr, L"Failed to seek to second Fat table from location %s\r\n", m_Location->GetLocation().c_str());
            return hr;
        }

        ullFatTableOffset = ullSecondFatTableOffset;
    }
    else
    {
        ullFatTableOffset = ullFirstFatTableOffset;
    }

    FatTable fatTable(fatTableType);
    CBinaryBuffer buffer;

    ULONGLONG ullBytesToRead = fatTableSize;
    ULONGLONG ullTotalBytesRead = 0;
    ULONGLONG ullBytesRead;
    boolean shouldSeek = false;

    // read fat table
    while (ullTotalBytesRead < fatTableSize)
    {
        // force seek of reader if needed
        if (shouldSeek && FAILED(hr = reader->Seek(ullFatTableOffset)))
        {
            log::Error(
                _L_,
                hr,
                L"Failed to seek when reading the Fat table from location %s\r\n",
                m_Location->GetLocation().c_str());
            return hr;
        }

        if (FAILED(hr = reader->Read(buffer, ullBytesToRead, ullBytesRead)))
        {
            log::Error(
                _L_, hr, L"Failed to read the Fat table from location %s\r\n", m_Location->GetLocation().c_str());
            return hr;
        }

        shouldSeek = true;
        ullFatTableOffset += ullBytesRead;
        ullTotalBytesRead += ullBytesRead;
        ullBytesToRead -= ullBytesRead;

        std::shared_ptr<CBinaryBuffer> bufferPtr = std::make_shared<CBinaryBuffer>(buffer);
        fatTable.AddChunk(bufferPtr);
    }

    m_ullRootDirectoryOffset =
        ullFirstFatTableOffset + ((ULONGLONG)nbFatTables * (ULONGLONG)nbSectorPerFatTable * (ULONGLONG)ulSectorSize);

    if (FSVBR::FSType::FAT12 == fsType || FSVBR::FSType::FAT16 == fsType)
    {
        // root directory is not stored in fat table!
        if (S_OK != (hr = ReadRootDirectory(m_RootDirectoryBuffer, rootDirectorySize)))
        {
            log::Error(
                _L_, hr, L"Failed to read root directory from location %s\r\n", m_Location->GetLocation().c_str());
            return hr;
        }

        m_ullRootDirectoryOffset += rootDirectorySize;
    }
    else
    {
        // get root directory clusters and read them
        FatTable::ClusterChain clusterChain;
        fatTable.FillClusterChain(rootDirectoryCluster, clusterChain);

        // read root directory - it starts at cluster 2. there is actually no cluster 0 and no cluster 1
        if (S_OK != (hr = ReadClusterChain(clusterChain, m_RootDirectoryBuffer)))
        {
            log::Error(
                _L_, hr, L"Failed to read root directory from location %s\r\n", m_Location->GetLocation().c_str());
            return hr;
        }
    }

    // keep a set of clusters that have already been parsed (folder clusters)
    ParsedClusterSet parsedClusterSet;
    parsedClusterSet.insert(rootDirectoryCluster);

    // parse root directory
    FatFileEntryList subFolders;
    ParseFolder(fatTable, m_RootDirectoryBuffer, m_RootFolder, subFolders);

    while (!subFolders.empty())
    {
        // we put the deleted folder entries at the end of the structure
        // we need to parse active folders first
        std::partition(subFolders.begin(), subFolders.end(), [](const FatFileEntry* subfolder) {
            return subfolder != nullptr && !subfolder->IsDeleted();
        });

        // get current subfolder
        const FatFileEntry* subfolder(subFolders.front());
        subFolders.pop_front();

        if (nullptr == subfolder || !subfolder->IsFolder())
            continue;

        // get the subfolder cluster chain and check if we have already parsed at least one its clusters
        bool alreadyParsed = false;
        const FatTable::ClusterChain& clusterChain(subfolder->GetClusterChain());
        std::for_each(
            std::begin(clusterChain),
            std::end(clusterChain),
            [&parsedClusterSet, &alreadyParsed](const FatTableEntry& entry) {
                if (entry.IsUsed())
                {
                    if (parsedClusterSet.find(entry.GetValue()) != parsedClusterSet.end())
                        alreadyParsed = true;
                }
            });

        if (alreadyParsed)
            continue;

        // read subfolder entries
        CBinaryBuffer buffer;
        if (S_OK != (hr = ReadClusterChain(clusterChain, buffer)))
        {
            log::Error(_L_, hr, L"Failed to read subfolder %s\r\n", subfolder->m_Name.c_str());
            continue;
        }

        // update parsed cluster set
        std::for_each(
            std::begin(clusterChain), std::end(clusterChain), [&parsedClusterSet](const FatTableEntry& entry) {
                if (entry.IsUsed())
                {
                    parsedClusterSet.insert(entry.GetValue());
                }
            });

        // parse subfolder
        ParseFolder(fatTable, buffer, subfolder, subFolders);
    }

    return S_OK;
}

HRESULT FatWalker::Process(const Callbacks& callbacks)
{
    std::shared_ptr<VolumeReader> reader(m_Location->GetReader());

    if (nullptr == reader)
    {
        return E_FAIL;
    }

    FatFileEntryList fileEntryList;
    fileEntryList.push_back(m_RootFolder);

    while (!fileEntryList.empty())
    {
        const FatFileEntry* fileEntry(fileEntryList.front());
        fileEntryList.pop_front();

        FatFileSystemByName::iterator it0, it1;
        boost::tie(it0, it1) = m_FatFS.equal_range(boost::make_tuple(fileEntry));

        std::for_each(it0, it1, [callbacks, &fileEntryList, &reader](const std::shared_ptr<FatFileEntry>& fileEntry) {
            if (fileEntry->IsFolder())
            {
                fileEntryList.push_back(fileEntry.get());
            }

            callbacks.m_FileEntryCall(reader, fileEntry->GetFullName().c_str(), fileEntry);
        });
    }

    return S_OK;
}

UCHAR FatWalker::ComputeDosNameChecksum(const FatFile83& fatFile83)
{
    UCHAR sum = 0;

    // In FatFile83 'Extension' follows 'Filename'
    const auto dosName = fatFile83.Filename;
    const auto dosNameLen = sizeof(FatFile83::Filename) + sizeof(FatFile83::Extension);  // 11

    for (int i = 0; i < dosNameLen; i++)
    {
        sum = ((sum & 1) << 7) + (sum >> 1) + dosName[i];
    }

    return sum;
}

HRESULT FatWalker::ReadRootDirectory(CBinaryBuffer& buffer, DWORD size)
{
    HRESULT hr = E_FAIL;
    std::shared_ptr<VolumeReader> reader(m_Location->GetReader());

    if (nullptr == reader)
    {
        log::Error(_L_, hr, L"Failed to get reader from location %s\r\n", m_Location->GetLocation().c_str());
        return hr;
    }

    ULONG ulClusterSize = reader->GetBytesPerCluster();

    buffer.SetCount(size);
    buffer.ZeroMe();

    if (S_OK != (hr = reader->Seek(m_ullRootDirectoryOffset)))
    {
        log::Error(
            _L_, hr, L"Failed to seek to root directory from location %s\r\n", m_Location->GetLocation().c_str());
        return hr;
    }
    else
    {
        ULONGLONG ullBytesRead;

        if (S_OK != (hr = reader->Read(buffer, size, ullBytesRead) && size == ullBytesRead))
        {
            log::Error(
                _L_, hr, L"Failed to read root directory from location %s\r\n", m_Location->GetLocation().c_str());
            return hr;
        }
    }

    return S_OK;
}

HRESULT FatWalker::ReadClusterChain(const FatTable::ClusterChain& clusterChain, CBinaryBuffer& buffer)
{
    HRESULT hr = E_FAIL;
    std::shared_ptr<VolumeReader> reader(m_Location->GetReader());

    if (nullptr == reader)
    {
        log::Error(_L_, hr, L"Failed to get reader from location %s\r\n", m_Location->GetLocation().c_str());
        return hr;
    }

    ULONG ulClusterSize = reader->GetBytesPerCluster();
    ULONGLONG offset = 0;

    size_t buffer_size = 0;
    if (!msl::utilities::SafeMultiply(clusterChain.size(), ulClusterSize, buffer_size))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);

    buffer.SetCount(buffer_size);
    buffer.ZeroMe();

    hr = S_OK;

    std::for_each(
        std::begin(clusterChain),
        std::end(clusterChain),
        [this, &hr, ulClusterSize, &offset, &reader, &buffer](const FatTableEntry& entry) {
            ULONG clusterNumber = 0;

            if (entry.IsUsed() && (clusterNumber = entry.GetValue()) >= 2)
            {
                CBinaryBuffer localBuffer;
                localBuffer.SetCount(ulClusterSize);
                ULONGLONG ullBytesRead;
                ULONGLONG seekOffset =
                    m_ullRootDirectoryOffset + ((ULONGLONG)(clusterNumber - 2) * (ULONGLONG)ulClusterSize);

                if (S_OK != (hr = reader->Seek(seekOffset)))
                {
                    log::Error(
                        _L_,
                        hr,
                        L"Failed to seek to cluster number %d from location %s\r\n",
                        clusterNumber,
                        m_Location->GetLocation().c_str());
                }
                else
                {
                    if (S_OK
                        == (hr = reader->Read(localBuffer, ulClusterSize, ullBytesRead)
                                && ulClusterSize == ullBytesRead))
                    {
                        memcpy(buffer.GetData() + offset, localBuffer.GetData(), ulClusterSize);
                        offset += ulClusterSize;
                    }
                    else
                    {
                        log::Error(
                            _L_,
                            hr,
                            L"Failed to read cluster number %d from location %s\r\n",
                            clusterNumber,
                            m_Location->GetLocation().c_str());
                    }
                }
            }
        });

    return hr;
}

HRESULT FatWalker::ParseFolder(
    const FatTable& fatTable,
    CBinaryBuffer& folder,
    const FatFileEntry* parentFolder,
    FatFileEntryList& subFolders)
{
    HRESULT hr = E_FAIL;
    LongFilenamesByChecksums longFilenamesByChecksums;

    for (size_t i = 0; i < folder.GetCount() / 32; i++)  // entry size is 32bytes
    {
        GenFatFile* genFatFile = reinterpret_cast<GenFatFile*>(folder.GetData() + (i * 32));
        bool bIsDeletedEntry = false;

        if (genFatFile->FirstByte == 0x0)
        {
            break;
        }
        else if (genFatFile->FirstByte == 0xE5)
        {
            if (!m_bResurrectRecords)
                continue;

            bIsDeletedEntry = true;
        }
        else if (genFatFile->FirstByte == 0x2E)  // . or .. entries
        {
            continue;
        }

        FatFileEntry fileEntry;
        LongFilename longFilename;
        bool bIsLFN = false;
        bool bIsSpecial = false;

        if (FAILED(
                hr = ParseFileEntry(
                    fatTable,
                    genFatFile,
                    fileEntry,
                    longFilename,
                    longFilenamesByChecksums,
                    bIsDeletedEntry,
                    bIsLFN,
                    bIsSpecial)))
        {
            if (parentFolder != nullptr && !parentFolder->IsDeleted() && !bIsDeletedEntry)
                log::Error(_L_, hr, L"Failed to parse Fat file entry. Will continue parsing next entry.\r\n");

            continue;
        }

        if (parentFolder != nullptr && parentFolder->IsDeleted() && !bIsDeletedEntry)
            continue;

        if (bIsSpecial)
            continue;

        if (bIsLFN)
        {
            LongFilenameList& list(longFilenamesByChecksums[longFilename.m_Checksum]);
            list.push_back(longFilename);
        }
        else
        {
            LongFilenameList& list(longFilenamesByChecksums[fileEntry.m_Checksum]);

            if (!list.empty())
            {
                std::wstring fullname;
                bool firstDone = false;

                for (const LongFilename& longFilename : list)
                {
                    if (!firstDone && longFilename.m_IsFirst)
                    {
                        fullname = longFilename.m_Filename;
                        firstDone = true;
                    }
                    else if (firstDone)
                    {
                        fullname = longFilename.m_Filename + fullname;

                        if (longFilename.m_SeqNumber == 0x1)
                            break;
                    }
                }

                list.clear();
                fileEntry.m_LongName = fullname;
            }

            fileEntry.m_ParentFolder = parentFolder;

            PrintFileEntry(fileEntry);
            FatFileSystem::iterator it = m_FatFS.insert(std::make_shared<FatFileEntry>(fileEntry)).first;

            if (fileEntry.IsFolder())
            {
                subFolders.push_back(it->get());
            }
        }
    }

    return S_OK;
}

HRESULT FatWalker::ParseFileEntry(
    const FatTable& fatTable,
    GenFatFile* genFatFile,
    FatFileEntry& fileEntry,
    LongFilename& longFileName,
    const LongFilenamesByChecksums& longFilenameByChecksums,
    bool bIsDeleted,
    bool& bLFN,
    bool& bIsSpecial)
{
    bIsSpecial = false;

    if (genFatFile->Attributes == 0x0F)  // LFN
    {
        bLFN = true;

        if (bIsDeleted)
        {
            genFatFile->FirstByte = 0x40;  // remove 0xE5
        }

        FatFileLFN* fatFileLFN = reinterpret_cast<FatFileLFN*>(genFatFile);
        BYTE seq = fatFileLFN->SequenceNumber & 0x1F;

        std::wstring name1(std::begin(fatFileLFN->Name1), std::end(fatFileLFN->Name1));
        std::wstring name2(std::begin(fatFileLFN->Name2), std::end(fatFileLFN->Name2));
        std::wstring name3(std::begin(fatFileLFN->Name3), std::end(fatFileLFN->Name3));

        longFileName.m_Filename = name1 + name2 + name3;
        std::size_t pos = longFileName.m_Filename.find(L'\0');

        if (pos != std::string::npos)
            longFileName.m_Filename = longFileName.m_Filename.substr(0, pos);

        longFileName.m_SeqNumber = seq;
        longFileName.m_Checksum = fatFileLFN->Checksum;

        if (genFatFile->FirstByte & 0x40)  // first physical LFN entry / last logical LFN entry
        {
            longFileName.m_IsFirst = true;
        }
    }
    else
    {
        bLFN = false;
        FatFile83* fatFile83 = reinterpret_cast<FatFile83*>(genFatFile);

        if (fatFile83->Attributes & 0x08)
        {
            bIsSpecial = true;
            std::wstring volumeName(std::begin(fatFile83->Filename), std::end(fatFile83->Extension));
            boost::algorithm::trim_right(volumeName);

            log::Debug(_L_, L"VOLUME NAME : %s\r\n", volumeName.c_str());
        }
        else
        {
            if (bIsDeleted)
                fileEntry.m_bIsDeleted = true;

            DWORD attributes = 0;

            if (fatFile83->Attributes & 0x1)  // readonly
            {
                attributes |= FILE_ATTRIBUTE_READONLY;
            }

            if (fatFile83->Attributes & 0x2)  // hidden
            {
                attributes |= FILE_ATTRIBUTE_HIDDEN;
            }

            if (fatFile83->Attributes & 0x4)  // system
            {
                attributes |= FILE_ATTRIBUTE_SYSTEM;
            }

            if (fatFile83->Attributes & 0x10)
            {
                fileEntry.m_bIsFolder = true;
            }

            fileEntry.m_Attributes = attributes;

            if (bIsDeleted)
            {
                UCHAR checksum;

                if (S_OK == BruteForceDeletedEntryFirstChar(*fatFile83, longFilenameByChecksums, checksum))
                {
                    fileEntry.m_Checksum = checksum;
                }
                else
                {
                    fatFile83->Filename[0] = '?';
                }
            }
            else
            {
                fileEntry.m_Checksum = ComputeDosNameChecksum(*fatFile83);
            }

            // get full filename
            std::wstring name(std::begin(fatFile83->Filename), std::end(fatFile83->Filename));
            boost::algorithm::trim_right(name);

            std::wstring extension(std::begin(fatFile83->Extension), std::end(fatFile83->Extension));
            boost::algorithm::trim_right(extension);

            if (extension.empty())
            {
                fileEntry.m_Name = name;
            }
            else
            {
                fileEntry.m_Name = name + L"." + extension;
            }

            // fill cluster chain
            DWORD firstCluster = 0;

            if (fatTable.GetEntrySizeInBits() == 32)  // only in Fat32
            {
                firstCluster = fatFile83->FirstClusterHigh2Bytes;
                ;
                firstCluster <<= 16;
                firstCluster += fatFile83->FirstClusterLow2Bytes;
            }
            else
            {
                firstCluster = fatFile83->FirstClusterLow2Bytes;
            }

            if (S_OK != fatTable.FillClusterChain(firstCluster, fileEntry.m_ClusterChain))
            {
                return E_FAIL;
            }

            // size
            fileEntry.m_ulSize = fatFile83->FileSize;

            // now we can update the map of segments for this file entry
            // I assume here that the reader object is not null
            fileEntry.FillSegmentDetailsMap(m_ullRootDirectoryOffset, m_Location->GetReader()->GetBytesPerCluster());

            // times
            DosDateTimeToFileTime(fatFile83->CreationDate, fatFile83->CreationTime, &fileEntry.m_CreationTime);
            fileEntry.m_CreationTime.dwLowDateTime += fatFile83->CreationTimeMs * 10;

            DosDateTimeToFileTime(fatFile83->ModifiedDate, fatFile83->ModifiedTime, &fileEntry.m_ModificationTime);
            DosDateTimeToFileTime(fatFile83->LastAccessDate, 0, &fileEntry.m_AccessTime);
        }
    }

    return S_OK;
}

HRESULT FatWalker::BruteForceDeletedEntryFirstChar(
    const FatFile83& fatFile83,
    const LongFilenamesByChecksums& longFilenameByChecksums,
    UCHAR& checksum) const
{
    for (unsigned char i = 32; i < 0xFF; i++)
    {
        UCHAR sum = ComputeDosNameChecksum(fatFile83);

        if (longFilenameByChecksums.find(sum) != longFilenameByChecksums.end()
            && !longFilenameByChecksums.at(sum).empty())
        {
            checksum = sum;
            return S_OK;
        }
    }

    return E_FAIL;
}

HRESULT FatWalker::PrintFileEntry(const FatFileEntry& fileEntry) const
{
    std::wstringstream ss;
    ss << fileEntry;

    log::Debug(_L_, L"%s\r\n", ss.str().c_str());

    return S_OK;
}
