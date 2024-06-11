//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <regex>
#include <sstream>

#include "Location.h"

#include "Configuration/ConfigFileReader.h"

#include "Stream/VolumeStreamReader.h"

#include "PhysicalDiskReader.h"
#include "InterfaceReader.h"
#include "SystemStorageReader.h"
#include "ShadowCopyVolumeReader.h"
#include "ImageReader.h"
#include "MountedVolumeReader.h"
#include "OfflineMFTReader.h"
#include "Text/Fmt/GUID.h"
#include "Text/Fmt/FSVBR.h"
#include "Utils/Guard.h"

using namespace std;

namespace {

void GetShadowCopyInformation(
    const std::wstring& shadowCopyVolumePath,
    GUID& shadowCopyId,
    VSS_TIMESTAMP& creationTime,
    VSS_VOLUME_SNAPSHOT_ATTRIBUTES& attributes,
    std::error_code& ec)
{
    using namespace Orc;

    const auto IOCTL_VOLSNAP_QUERY_APPLICATION_INFO = 0x53419C;
    const auto IOCTL_VOLSNAP_QUERY_CONFIG_INFO = 0x534194;

#pragma pack(push, 1)
    struct ConfigInfo
    {
        uint8_t unknown1[8];
        LARGE_INTEGER CreationTime;
    };

    struct ApplicationInformation
    {
        uint8_t length[4];
        GUID guid;
        GUID shadowCopyGuid;
        GUID shadowCopySetGuid;
        uint8_t snapshotContext[4];
        uint8_t unknown1[4];
        VSS_VOLUME_SNAPSHOT_ATTRIBUTES attributes;
        uint8_t unknown2[4];
    };
#pragma pack(pop)

    DWORD processed = 0;
    std::vector<std::byte> buffer;
    buffer.resize(1048576);

    Guard::FileHandle hShadowCopyVolume = CreateFileW(
        shadowCopyVolumePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);
    if (hShadowCopyVolume == INVALID_HANDLE_VALUE)
    {
        ec = LastWin32Error();
        Log::Error(L"Failed CreateFileW on '{}' [{}]", shadowCopyVolumePath, ec);
        return;
    }

    // Get shadow copy guids...
    BOOL rv = DeviceIoControl(
        hShadowCopyVolume.value(),
        IOCTL_VOLSNAP_QUERY_APPLICATION_INFO,
        NULL,
        0,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        &processed,
        NULL);
    if (!rv)
    {
        ec = LastWin32Error();
        Log::Debug("Failed IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS [{}]", ec);
        return;
    }

    if (processed < sizeof(ApplicationInformation))
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug("Failed IOCTL_VOLSNAP_QUERY_APPLICATION_FLAGS: unexpected output size");
        return;
    }

    auto appinfo = reinterpret_cast<ApplicationInformation*>(buffer.data());
    shadowCopyId = appinfo->shadowCopyGuid;
    attributes = appinfo->attributes;

    // Get shadow copy timestamps
    rv = DeviceIoControl(
        hShadowCopyVolume.value(),
        IOCTL_VOLSNAP_QUERY_CONFIG_INFO,
        NULL,
        0,
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        &processed,
        NULL);
    if (!rv)
    {
        ec = LastWin32Error();
        Log::Debug("Failed IOCTL_VOLSNAP_QUERY_CONFIG_INFO [{}]", ec);
        return;
    }

    if (processed < sizeof(ConfigInfo))
    {
        ec = std::make_error_code(std::errc::message_size);
        Log::Debug("Failed IOCTL_VOLSNAP_QUERY_CONFIG_INFO: unexpected output size");
        return;
    }

    auto configInfo = reinterpret_cast<ConfigInfo*>(buffer.data());
    creationTime = *reinterpret_cast<const VSS_TIMESTAMP*>(&configInfo->CreationTime);
}

void ParseVolumeSerial32(
    const std::wstring& path,
    const std::optional<uint64_t>& volumeOffset,
    uint32_t& volumeSerial32,
    std::error_code& ec)
{
    using namespace Orc;

    Guard::FileHandle hVolume = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_NO_BUFFERING,
        NULL);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed CreateFileW '{}' [{}]", path, ec);
        return;
    }

    const size_t kBlockSize = 4096;
    if (volumeOffset)
    {
        LARGE_INTEGER liVolumeOffset, newOffset;
        liVolumeOffset.QuadPart = volumeOffset.value();

        if (!SetFilePointerEx(hVolume.value(), liVolumeOffset, &newOffset, FILE_BEGIN))
        {
            ec = LastWin32Error();
            Log::Debug(L"Failed SetFilePointerEx '{}' [{}]", path, ec);
            return;
        }
    }

    std::vector<uint8_t> biosParameterBlock;
    biosParameterBlock.resize(kBlockSize * 2);

    DWORD processed = 0;
    if (!ReadFile(hVolume.value(), (LPVOID)biosParameterBlock.data(), biosParameterBlock.size(), &processed, NULL))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed ReadFile '{}' [{}]", path, ec);
        return;
    }

    std::string_view signature(reinterpret_cast<const char*>(biosParameterBlock.data() + 3), 8);
    std::optional<uint64_t> serialOffset;
    if (signature.substr(0, NTFS_VBR_SIGNATURE.size()) == NTFS_VBR_SIGNATURE)
    {
        serialOffset = 0x48;
    }
    else if (
        signature.substr(0, NTFS_VBR_SIGNATURE.size()) == FAT12_VBR_SIGNATURE
        || signature.substr(0, NTFS_VBR_SIGNATURE.size()) == FAT16_VBR_SIGNATURE
        || signature.substr(0, NTFS_VBR_SIGNATURE.size()) == FAT32_VBR_SIGNATURE)
    {
        serialOffset = 0x43;
    }

    if (!serialOffset)
    {
        ec = std::make_error_code(std::errc::function_not_supported);
        Log::Debug("Unknown volume signature", ec);
        return;
    }

    volumeSerial32 = *reinterpret_cast<uint32_t*>(biosParameterBlock.data() + *serialOffset);
}

void GetVolumeSerialUsingFileHandle(const std::wstring& path, uint32_t& volumeSerial32, std::error_code& ec)
{
    using namespace Orc;

    Guard::FileHandle hVolume = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (hVolume == INVALID_HANDLE_VALUE)
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed CreateFileW '{}' [{}]", path, ec);
        return;
    }

    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(hVolume.value(), &info))
    {
        ec = LastWin32Error();
        Log::Debug(L"Failed GetFileInformationByHandle '{}' [{}]", path, ec);
        return;
    }

    volumeSerial32 = info.dwVolumeSerialNumber;
}

void GetVolumeSerial32(std::wstring path, uint32_t& volumeSerial32, std::error_code& ec)
{
    using namespace Orc;

    std::optional<uint64_t> startOffset;
    std::wregex device(L"(.*),offset=([0-9]+),size=([0-9]+),sector=([0-9]+)");

    // parse 'path' to extract api compatible expression and offset
    std::wsmatch matches;
    auto match = std::regex_search(path, matches, device);
    if (match)
    {
        path = matches[1].str().c_str();
        startOffset = wcstoull(matches[2].str().c_str(), nullptr, 0);
        ParseVolumeSerial32(path, startOffset, volumeSerial32, ec);
        return;
    }

    const std::wregex globalRoot(L"\\\\\\\\\\?\\\\GLOBALROOT\\\\Device\\\\HarddiskVolume.*");
    const std::wregex harddiskVolume(L"\\\\\\\\.\\\\HarddiskVolume[0-9]+");
    const std::wregex volumeRoot(L"\\\\\\\\\\?\\\\Volume\\{.*\\}");

    if ((std::regex_match(path, globalRoot) || std::regex_match(path, harddiskVolume)
         || std::regex_match(path, volumeRoot))
        && path[path.size() - 1] != L'\\')
    {
        path.push_back(L'\\');
    }

    GetVolumeSerialUsingFileHandle(path, volumeSerial32, ec);
    if (ec)
    {
        ec.clear();
        ParseVolumeSerial32(path, 0, volumeSerial32, ec);
    }
}

void EnumerateShadows(
    const std::wstring& volume,
    const std::vector<std::wstring>& harddiskShadowCopyVolumeHints,
    std::vector<Orc::VolumeShadowCopies::Shadow>& shadows,
    std::error_code& ec)
{
    using namespace Orc;

    std::vector<uint8_t> buffer;
    buffer.resize(1048576);

    // Get volume serial to compare with each shadow copy volume serial
    uint32_t volumeSerial;
    GetVolumeSerial32(volume, volumeSerial, ec);
    if (ec)
    {
        Log::Error(L"Failed to read volume serial: {} [{}]", volume, ec);
        return;
    }

    for (auto hint : harddiskShadowCopyVolumeHints)
    {
        uint32_t shadowSerial;
        GetVolumeSerial32(hint, shadowSerial, ec);
        if (ec)
        {
            Log::Debug(L"Failed to read volume serial: {} [{}]", hint, ec);
            ec.clear();
            continue;
        }

        if (shadowSerial != volumeSerial)
        {
            Log::Debug(
                L"Shadow copy serials do not match (volume: {}, serial: {:#018x}, vss: {}, vss serial: {:#010x})",
                volume,
                volumeSerial,
                hint,
                shadowSerial);
            continue;
        }

        GUID shadowCopyId {};
        VSS_TIMESTAMP creationTime;
        VSS_VOLUME_SNAPSHOT_ATTRIBUTES attributes {};
        GetShadowCopyInformation(hint, shadowCopyId, creationTime, attributes, ec);
        if (ec)
        {
            Log::Error(L"Failed GetShadowCopyInformation (volume: {}, vss: {}) [{}]", volume, hint, ec);
            continue;
        }

        shadows.emplace_back(volume.c_str(), hint.c_str(), attributes, creationTime, shadowCopyId);
    }
}

}  // namespace

namespace Orc {

Location::Location(const std::wstring& Location, Location::Type type)
    : m_Location(Location)
    , m_Type(type)
{
}

const std::wstring Location::GetLocation() const
{
    if (m_Shadow)
    {
        if (m_shadowCopyParserType == Ntfs::ShadowCopy::ParserType::kMicrosoft)
        {
            return fmt::format(L"Shadow Copy {} ({})", m_Shadow->guid, m_Shadow->DeviceInstance);
        }
        else
        {
            return fmt::format(L"Shadow Copy {}", m_Shadow->guid);
        }
    }

    return m_Location;
}

void Location::EnumerateShadowCopies(
    std::vector<VolumeShadowCopies::Shadow>& shadows,
    std::optional<std::vector<std::wstring>> harddiskShadowCopyVolumeHints,
    std::error_code& ec)
{
    Log::Debug("Set volume shadow copy parser to '{}'", ToString(m_shadowCopyParserType));

    switch (m_shadowCopyParserType)
    {
        case Ntfs::ShadowCopy::ParserType::kMicrosoft: {
            EnumerateShadowCopiesWithMicrosoftParser(shadows, ec);
            if (ec && harddiskShadowCopyVolumeHints)
            {
                ec.clear();
                EnumerateShadowCopiesWithVolsnapDriver(*harddiskShadowCopyVolumeHints, shadows, ec);
            }
            break;
        }
        case Ntfs::ShadowCopy::ParserType::kInternal: {
            EnumerateShadowCopiesWithInternalParser(shadows, ec);
            break;
        }
        default:
            break;
    }

    if (ec)
    {
        ec = std::make_error_code(std::errc::not_supported);
        return;
    }

    for (auto& shadow : shadows)
    {
        shadow.parentIdentifier = GetIdentifier();
        if (shadow.parentVolume->ShortVolumeName())
        {
            shadow.VolumeName = shadow.parentVolume->ShortVolumeName();
        }
    }

    // Most recent snapshot first
    std::sort(std::begin(shadows), std::end(shadows), [](const auto& lhs, const auto& rhs) {
        return lhs.CreationTime > rhs.CreationTime;
    });
}

void Location::EnumerateShadowCopiesWithVolsnapDriver(
    const std::vector<std::wstring>& harddiskShadowCopyVolumeHints,
    std::vector<VolumeShadowCopies::Shadow>& shadows,
    std::error_code& ec)
{
    std::vector<VolumeShadowCopies::Shadow> shadowsEnumeration;
    ::EnumerateShadows(GetLocation(), harddiskShadowCopyVolumeHints, shadowsEnumeration, ec);
    if (ec)
    {
        Log::Error(L"Failed to enumerate shadow copy volumes from '{}' [{}]", GetLocation(), ec);
        return;
    }

    for (auto& shadow : shadowsEnumeration)
    {
        auto loc = std::make_shared<Location>(L"", Location::Type::Snapshot);
        loc->SetShadowCopyParser(m_shadowCopyParserType);
        loc->SetShadow(shadow);
        auto reader = loc->GetReader();
        if (!reader)
        {
            Log::Error("Failed to get reader for shadow copy {}", shadow.guid);
            continue;
        }

        HRESULT hr = reader->LoadDiskProperties();
        if (FAILED(hr))
        {
            Log::Error(L"Failed to load disk properties (path: {}) [{}]", GetLocation(), SystemError(hr));
            continue;
        }

        if (SerialNumber() == 0 || SerialNumber() != reader->VolumeSerialNumber())
        {
            Log::Debug(
                L"Shadow copy serials do not match (volume: {}, serial: {:#010x}, vss: {}, vss serial: {:#010x})",
                GetLocation(),
                reader->VolumeSerialNumber(),
                shadow.guid,
                SerialNumber());
            continue;
        }

        shadow.parentVolume = reader;
        shadows.push_back(std::move(shadow));
    }

    std::sort(
        std::begin(shadows),
        std::end(shadows),
        [](const VolumeShadowCopies::Shadow& lhs, const VolumeShadowCopies::Shadow& rhs) {
            return lhs.CreationTime < rhs.CreationTime;
        });
}

void Location::EnumerateShadowCopiesWithMicrosoftParser(
    std::vector<VolumeShadowCopies::Shadow>& shadows,
    std::error_code& ec)
{
    std::vector<VolumeShadowCopies::Shadow> everyShadows;

    VolumeShadowCopies vss;
    HRESULT hr = vss.EnumerateShadows(everyShadows);
    if (FAILED(hr))
    {
        ec = SystemError(hr);
        Log::Debug("Failed to enumerate shadow copies [{}]", ec);
        return;
    }

    for (auto& shadow : everyShadows)
    {
        auto loc = std::make_shared<Location>(L"", Location::Type::Snapshot);
        loc->SetShadowCopyParser(m_shadowCopyParserType);
        loc->SetShadow(shadow);
        auto reader = loc->GetReader();
        if (!reader)
        {
            Log::Debug("Failed to get reader for shadow copy {}", shadow.guid);
            continue;
        }

        hr = reader->LoadDiskProperties();
        if (FAILED(hr))
        {
            continue;
        }

        if (SerialNumber() == 0 || SerialNumber() != reader->VolumeSerialNumber())
        {
            continue;
        }

        shadow.parentVolume = reader;
        shadows.push_back(std::move(shadow));
    }

    std::sort(
        std::begin(shadows),
        std::end(shadows),
        [](const VolumeShadowCopies::Shadow& lhs, const VolumeShadowCopies::Shadow& rhs) {
            return lhs.CreationTime < rhs.CreationTime;
        });
}

void Location::EnumerateShadowCopiesWithInternalParser(
    std::vector<VolumeShadowCopies::Shadow>& shadows,
    std::error_code& ec)
{
    using namespace Ntfs::ShadowCopy;

    auto reader = GetReader();
    if (!reader)
    {
        Log::Debug("Failed to get reader");
        ec = std::make_error_code(std::errc::not_enough_memory);
        return;
    }

    if (reader->GetFSType() != FSVBR_FSType::NTFS)
    {
        Log::Debug("Ignore shadow copy for stream type: {}", reader->GetFSType());
        return;
    }

    auto dataStream = VolumeStreamReader(reader);

    Log::Debug(L"Enumerate shadow copies (location: {})", GetLocation());

    if (!HasSnapshotsIndex(dataStream, ec))
    {
        if (ec)
        {
            Log::Debug(L"Failed when looking for shadow copies {} [{}]", GetLocation(), ec);
        }

        return;
    }

    std::vector<Orc::Ntfs::ShadowCopy::ShadowCopyInformation> shadowCopies;
    GetShadowCopiesInformation(dataStream, shadowCopies, ec);
    if (ec)
    {
        Log::Error(L"Failed to parse shadow copies for {} [{}]", GetLocation(), ec);
        ec.clear();
        return;
    }

    for (const auto& shadowCopy : shadowCopies)
    {
        auto shadow = VolumeShadowCopies::Shadow(
            L"",
            GetLocation().c_str(),
            static_cast<VSS_VOLUME_SNAPSHOT_ATTRIBUTES>(shadowCopy.VolumeSnapshotAttributes()),
            *(reinterpret_cast<const VSS_TIMESTAMP*>(&shadowCopy.CreationTime())),
            shadowCopy.ShadowCopyId());

        shadow.parentVolume = GetReader();

        shadows.push_back(std::move(shadow));
    }
}

std::shared_ptr<VolumeReader> Location::GetReader()
{
    if (m_Reader != nullptr)
        return m_Reader;

    if (m_Type == Type::Undetermined)
    {
        // Cannot instantiate a reader for a Location we fail to determine
        return nullptr;
    }

    switch (m_Type)
    {
        case Type::MountedVolume:
        case Type::PartitionVolume:
            m_Reader = make_shared<MountedVolumeReader>(m_Location.c_str());
            break;
        case Type::Snapshot:
            if (m_Shadow != nullptr)
            {
                switch (m_shadowCopyParserType)
                {
                    case Ntfs::ShadowCopy::ParserType::kMicrosoft:
                        m_Reader = std::make_shared<SnapshotVolumeReader>(*m_Shadow);
                        break;
                    case Ntfs::ShadowCopy::ParserType::kInternal:
                        m_Reader = std::make_shared<ShadowCopyVolumeReader>(*m_Shadow);
                        break;
                    default:
                        Log::Debug("Unsupported shadow copy parser");
                }
            }
            break;
        case Type::PhysicalDrive:
        case Type::PhysicalDriveVolume:
            m_Reader = make_shared<PhysicalDiskReader>(m_Location.c_str());
            break;
        case Type::DiskInterface:
        case Type::DiskInterfaceVolume:
            m_Reader = make_shared<InterfaceReader>(m_Location.c_str());
            break;
        case Type::SystemStorage:
        case Type::SystemStorageVolume:
            m_Reader = make_shared<SystemStorageReader>(m_Location.c_str());
            break;
        case Type::ImageFileVolume:
        case Type::ImageFileDisk:
            m_Reader = make_shared<ImageReader>(m_Location.c_str());
            break;
        case Type::OfflineMFT:
            m_Reader = make_shared<OfflineMFTReader>(m_Location.c_str());
            break;
        default:
            break;
    }

    return m_Reader;
}

void Location::MakeIdentifier()
{
    if (m_Type == Type::Undetermined)
    {
        // Cannot instantiate a reader for a Location we fail to determine
        m_Identifier = L"Undetermined";
        return;
    }

    auto replace_reserved_chars = [](wstring& in) -> wstring {
        wstring retval = in;
        std::replace_if(
            begin(retval),
            end(retval),
            [](const WCHAR inchar) -> bool {
                return (
                    inchar == L'<' ||  //< (less than)
                    inchar == L'>' ||  //> (greater than)
                    inchar == L':' ||  //: (colon)
                    inchar == L'\"' ||  //" (double quote)
                    inchar == L'/' ||  /// (forward slash)
                    inchar == L'\\' ||  //\ (backslash)
                    inchar == L'|' ||  //| (vertical bar or pipe)
                    inchar == L'?' ||  //? (question mark)
                    inchar == L'*');  //* (asterisk)
            },
            L'_');

        return move(retval);
    };

    m_Identifier.clear();

    switch (m_Type)
    {
        case Type::MountedVolume: {
            static wregex r1(REGEX_MOUNTED_DRIVE);
            wsmatch s;

            if (regex_match(m_Location, s, r1))
            {
                if (s[REGEX_MOUNTED_DRIVE_SUBDIR].matched && s[REGEX_MOUNTED_DRIVE_SUBDIR].compare(L"\\") == 0)
                {
                    // no subdir specified, not a mount point
                    m_Identifier = L"Volume_" + s[REGEX_MOUNTED_DRIVE_LETTER].str();
                    break;
                }
            }
            if (!m_Paths.empty() && m_Identifier.empty())
            {
                for_each(begin(m_Paths), end(m_Paths), [&](const wstring& item) {
                    if (regex_match(item, s, r1))
                    {
                        if (s[REGEX_MOUNTED_DRIVE_LETTER].matched)
                        {
                            // no subdir specified, not a mount point
                            m_Identifier = L"Volume_" + s[REGEX_MOUNTED_DRIVE_LETTER].str();
                            if (s[REGEX_MOUNTED_DRIVE_SUBDIR].matched && s[REGEX_MOUNTED_DRIVE_SUBDIR].compare(L"\\"))
                            {
                                wstring str = s[REGEX_MOUNTED_DRIVE_SUBDIR].str();
                                m_Identifier += replace_reserved_chars(str);
                            }
                        }
                    }
                });
                if (!m_Identifier.empty())
                    break;
            }

            wregex r2(REGEX_MOUNTED_VOLUME);
            if (regex_match(m_Location, s, r2))
            {
                if (s[REGEX_MOUNTED_VOLUME_ID].matched)
                {
                    m_Identifier = L"Volume" + s[REGEX_MOUNTED_VOLUME_ID].str();
                    break;
                }
            }

            wregex r3(REGEX_MOUNTED_HARDDISKVOLUME, regex_constants::icase);
            if (regex_match(m_Location, s, r3))
            {
                if (s[REGEX_MOUNTED_HARDDISKVOLUME_ID].matched)
                {
                    m_Identifier = L"HarddiskVolume" + s[REGEX_MOUNTED_HARDDISKVOLUME_ID].str();
                    break;
                }
            }

            m_Identifier = replace_reserved_chars(m_Location);
        }
        break;
        case Type::Snapshot: {
            if (m_Shadow)
            {
                m_Identifier = fmt::format(L"{}_{}", m_Shadow->parentIdentifier, m_Shadow->guid);
            }
            else
            {
                Log::Error(L"Location::MakeIdentifier: unexpected code path");
                wregex r(REGEX_SNAPSHOT, regex_constants::icase);
                wsmatch s;

                if (regex_match(m_Location, s, r))
                {

                    m_Identifier = L"Snapshot_" + s[REGEX_SNAPSHOT_NUM].str();
                }
            }
        }
        break;
        case Type::PhysicalDrive:
        case Type::PhysicalDriveVolume: {
            wregex r_physical(REGEX_PHYSICALDRIVE, std::regex_constants::icase);
            wregex r_disk(REGEX_DISK, std::regex_constants::icase);
            wsmatch s;

            if (regex_match(m_Location, s, r_physical))
            {
                if (s[REGEX_PHYSICALDRIVE_PARTITION_SPEC].matched
                    && *s[REGEX_PHYSICALDRIVE_PARTITION_SPEC].first == L'*')
                {
                    m_Identifier = L"PhysicalDrive_" + s[REGEX_PHYSICALDRIVE_NUM].str() + L"_ActivePartition";
                    break;
                }
                else if (s[REGEX_PHYSICALDRIVE_PARTITION_NUM].matched)
                {
                    m_Identifier = L"PhysicalDrive_" + s[REGEX_PHYSICALDRIVE_NUM].str() + L"_Partition_"
                        + s[REGEX_PHYSICALDRIVE_PARTITION_NUM].str();
                    break;
                }
                else if (s[REGEX_PHYSICALDRIVE_OFFSET].matched)
                {
                    m_Identifier = L"PhysicalDrive_" + s[REGEX_PHYSICALDRIVE_NUM].str() + L"_Offset_"
                        + s[REGEX_PHYSICALDRIVE_OFFSET].str();
                    break;
                }
            }
            else if (regex_match(m_Location, s, r_disk))
            {
                if (s[REGEX_DISK_PARTITION_SPEC].matched && *s[REGEX_DISK_PARTITION_SPEC].first == L'*')
                {
                    m_Identifier = L"Disk_" + s[REGEX_DISK_GUID].str() + L"_ActivePartition";
                    break;
                }
                else if (s[REGEX_DISK_PARTITION_NUM].matched)
                {
                    m_Identifier =
                        L"Disk_" + s[REGEX_DISK_GUID].str() + L"_Partition_" + s[REGEX_DISK_PARTITION_NUM].str();
                    break;
                }
                else if (s[REGEX_DISK_OFFSET].matched)
                {
                    m_Identifier = L"Disk_" + s[REGEX_DISK_GUID].str() + L"_Offset_" + s[REGEX_DISK_OFFSET].str();
                    break;
                }
            }
            m_Identifier = wstring(L"Disk_") + replace_reserved_chars(m_Location);
        }
        break;
        case Type::PartitionVolume: {
            m_Identifier = wstring(L"PartitionVolume_") + replace_reserved_chars(m_Location);
        }
        break;
        case Type::DiskInterface:
        case Type::DiskInterfaceVolume: {
            std::wstringstream ss;
            ss << L"0x" << std::hex << SerialNumber();

            m_Identifier = wstring(L"DiskInterface_") + ss.str();
        }
        break;
        case Type::SystemStorage:
        case Type::SystemStorageVolume: {
            std::wstringstream ss;
            ss << L"0x" << std::hex << SerialNumber();

            m_Identifier = wstring(L"SystemStorage_") + ss.str();
        }
        break;
        case Type::ImageFileVolume: {
            wregex r(REGEX_IMAGE, regex_constants::icase);
            wsmatch s;

            if (regex_match(m_Location, s, r))
            {

                WCHAR szImageName[ORC_MAX_PATH];
                if (FAILED(GetFileNameForFile(s[REGEX_IMAGE_SPEC].str().c_str(), szImageName, ORC_MAX_PATH)))
                    return;

                m_Identifier = wstring(L"VolumeImage_") + szImageName;

                if (s[REGEX_IMAGE_PARTITION_NUM].matched)
                {
                    m_Identifier += L"_partition_";
                    m_Identifier += s[REGEX_IMAGE_PARTITION_NUM].str();
                }
                else
                {
                    if (s[REGEX_IMAGE_OFFSET].matched)
                    {
                        m_Identifier += L"_offset_";
                        m_Identifier += s[REGEX_IMAGE_OFFSET].str();
                    }
                    if (s[REGEX_IMAGE_SIZE].matched)
                    {
                        m_Identifier += L"_size_";
                        m_Identifier += s[REGEX_IMAGE_SIZE].str();
                    }
                    if (s[REGEX_IMAGE_SECTOR].matched)
                    {
                        m_Identifier += L"_sector_";
                        m_Identifier += s[REGEX_IMAGE_SECTOR].str();
                    }
                }
            }
        }
        break;
        case Type::ImageFileDisk: {
            wregex r(REGEX_IMAGE, regex_constants::icase);
            wsmatch s;

            if (regex_match(m_Location, s, r))
            {

                WCHAR szImageName[ORC_MAX_PATH];
                if (FAILED(GetFileNameForFile(s[REGEX_IMAGE_SPEC].str().c_str(), szImageName, ORC_MAX_PATH)))
                {
                    return;
                }

                m_Identifier = L"DiskImage_" + wstring(szImageName);

                if (s[REGEX_IMAGE_PARTITION_SPEC].matched && *s[REGEX_IMAGE_PARTITION_SPEC].first == L'*')
                {
                    m_Identifier += L"_ActivePartition";
                    break;
                }
                else if (s[REGEX_IMAGE_PARTITION_NUM].matched)
                {
                    m_Identifier += L"_Partition_" + s[REGEX_IMAGE_PARTITION_NUM].str();
                    break;
                }
            }
        }
        break;
        case Type::OfflineMFT: {
            WCHAR szImageName[ORC_MAX_PATH];
            if (SUCCEEDED(GetFileNameForFile(m_Location.c_str(), szImageName, ORC_MAX_PATH)))
            {
                m_Identifier = wstring(L"OfflineMFT_") + szImageName;
            }
            else
            {
                m_Identifier = wstring(L"OfflineMFT_") + replace_reserved_chars(m_Location);
            }
        }
        break;
        default:
            break;
    }
}

ULONGLONG Location::SerialNumber() const
{
    if (m_Reader != nullptr)
    {
        return m_Reader->VolumeSerialNumber();
    }

    if (m_Type == LocationType::Snapshot && m_Shadow && m_Shadow->parentVolume)
    {
        return m_Shadow->parentVolume->VolumeSerialNumber();
    }

    return 0LL;
}

FSVBR::FSType Location::GetFSType() const
{
    if (m_Reader != nullptr)
    {
        return m_Reader->GetFSType();
    }

    if (m_Type == LocationType::Snapshot && m_Shadow && m_Shadow->parentVolume)
    {
        return m_Shadow->parentVolume->GetFSType();
    }

    return FSVBR::FSType::UNKNOWN;
}

std::wostream& Orc::operator<<(std::wostream& o, const Location& l)
{
    std::wstringstream ss;
    bool isAMountedVolume = false;

    ss << ToString(l.GetType()) << L": " << l.GetLocation();

    if (l.GetType() == Location::Type::MountedVolume)
    {
        if (!l.GetPaths().empty())
        {
            ss << L" -";
            for (auto& path : l.GetPaths())
                ss << L" " << path;
        }
    }

    FSVBR::FSType fsType = l.GetFSType();
    ss << " - " << FSVBR::GetFSName(fsType);

    if (l.IsValid())
    {
        ss << L" - Valid (serial : 0x" << std::hex << l.SerialNumber() << L")";
    }
    else
    {
        ss << L" - Invalid";
    }

    if (l.GetParse())
    {
        ss << L" *";
    }

    o << ss.str();

    return o;
}

std::vector<std::wstring> GetMountPointList(const Orc::Location& location)
{
    std::vector<std::wstring> mountPoints;

    for (const auto& path : location.GetPaths())
    {
        if (location.GetSubDirs().empty())
        {
            mountPoints.push_back(fmt::format(L"\"{}\"", path));
            continue;
        }

        for (const auto& subdir : location.GetSubDirs())
        {
            const auto fullPath = std::filesystem::path(path) / subdir;
            mountPoints.push_back(fullPath);
        }
    }

    return mountPoints;
}

}  // namespace Orc
