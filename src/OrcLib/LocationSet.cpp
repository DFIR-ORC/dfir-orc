//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "StdAfx.h"

#include <regex>
#include <algorithm>
#include <vector>
#include <sstream>
#include <locale>
#include <codecvt>

#include <boost/algorithm/string.hpp>
#include <boost/scope_exit.hpp>

#include "LocationSet.h"

#include "PartitionTable.h"
#include "DiskStructures.h"
#include "RefsDataStructures.h"

#include "PhysicalDiskReader.h"
#include "InterfaceReader.h"
#include "SystemStorageReader.h"
#include "SnapshotVolumeReader.h"
#include "ImageReader.h"
#include "MountedVolumeReader.h"
#include "OfflineMFTReader.h"

#include "ObjectDirectory.h"
#include "Kernel32Extension.h"

#include "Configuration/ConfigFile.h"

#include "SystemDetails.h"
#include "WMIUtil.h"

#include "NtfsDataStructures.h"
#include "ProfileList.h"

using namespace std;

using namespace Orc;

namespace {

Orc::Result<std::vector<std::wstring>> GetUserProfiles()
{
    const auto profiles = ProfileList::GetProfiles();
    if (profiles.has_error())
    {
        return profiles.error();
    }

    std::vector<wstring> profileLocations;
    for (auto& profile : profiles.value())
    {
        profileLocations.emplace_back(profile.ProfilePath.c_str());
    }

    return profileLocations;
}

std::vector<std::wstring> ExpandOrcStringsLocation(const std::wstring& rawLocation)
{
    using HandlerResult = Orc::Result<std::vector<std::wstring>>;
    using Handler = std::function<HandlerResult()>;

    if (rawLocation.empty())
    {
        return {};
    }

    // TODO: eventually cache the results, a better choice may be to Expand once
    const std::unordered_map<std::wstring, Handler> convertors = {{L"{UserProfiles}", GetUserProfiles}};

    std::vector<std::wstring> out;
    for (const auto& [key, convertor] : convertors)
    {
        if (boost::icontains(rawLocation, key))
        {
            auto values = convertor();
            if (!values)
            {
                Log::Error(L"Failed to expand orc variable: '{}'", key);
                continue;
            }

            for (const auto& value : values.value())
            {
                auto location = boost::ireplace_all_copy(rawLocation, key, value);
                out.emplace_back(std::move(location));
            }
        }
    }

    return out;
}

Result<GUID> ShadowFilterToGuid(const std::wstring& filter)
{
    std::error_code ec;
    GUID guid;
    ToGuid(std::wstring_view(filter), guid, ec);
    if (ec)
    {
        return ec;
    }

    return guid;
}

struct ShadowCopyLocations
{
    struct CompareGUID
    {
        bool operator()(const GUID& lhs, const GUID& rhs) const { return memcmp(&lhs, &rhs, sizeof(GUID)) < 0; }
    };

    Location::Ptr oldest;
    Location::Ptr mid;
    Location::Ptr newest;
    std::map<GUID, Location::Ptr, CompareGUID> guids;
};

void GetShadowCopyLocations(const std::vector<Location::Ptr>& locations, ShadowCopyLocations& shadows)
{
    std::vector<Location::Ptr> locationsWithShadow;
    std::copy_if(std::cbegin(locations), std::cend(locations), std::back_inserter(locationsWithShadow), [](auto& loc) {
        return loc->GetShadow() != nullptr;
    });

    if (locationsWithShadow.empty())
    {
        return;
    }

    std::sort(std::begin(locationsWithShadow), std::end(locationsWithShadow), [](const auto& lhs, const auto& rhs) {
        return lhs->GetShadow()->CreationTime < rhs->GetShadow()->CreationTime;
    });

    shadows.newest = locationsWithShadow[locationsWithShadow.size() - 1];
    shadows.mid = locationsWithShadow[(locationsWithShadow.size() - 1) / 2];
    shadows.oldest = locationsWithShadow[0];

    for (auto location : locationsWithShadow)
    {
        shadows.guids.emplace(location->GetShadow()->guid, location);
    }
}

void FilterLocations(
    const std::vector<Location::Ptr>& locations,
    const LocationSet::ShadowFilters& filters,
    std::set<Location::Ptr>& output)
{
    ShadowCopyLocations shadows;
    GetShadowCopyLocations(locations, shadows);
    if (shadows.guids.empty())
    {
        return;
    }

    if (shadows.newest && filters.find(L"newest") != std::cend(filters))
    {
        output.insert(shadows.newest);
    }

    if (shadows.mid && filters.find(L"mid") != std::cend(filters))
    {
        output.insert(shadows.mid);
    }

    if (shadows.oldest && filters.find(L"oldest") != std::cend(filters))
    {
        output.insert(shadows.oldest);
    }

    for (const auto& filter : filters)
    {
        auto guid = ShadowFilterToGuid(filter);
        if (guid)
        {
            auto it = shadows.guids.find(*guid);
            if (it != std::cend(shadows.guids))
            {
                output.insert(it->second);
            }
        }
    }
}

void GetExcludedVolumeLocations(
    LocationSet::VolumeLocations& volume,
    const LocationSet::PathExcludes& excludedPaths,
    std::vector<Location::Ptr>& excludedLocations)
{
    for (const auto& path : volume.Paths)
    {
        if (excludedPaths.find(path) != std::cend(excludedPaths))
        {
            std::copy(
                std::cbegin(volume.Locations), std::cend(volume.Locations), std::back_inserter(excludedLocations));
            Log::Info(L"Exclude: '{}'", path);
            break;
        }
    }

    for (const auto& location : volume.Locations)
    {
        if (excludedPaths.find(location->GetLocation()) != std::cend(excludedPaths))
        {
            std::copy(
                std::cbegin(volume.Locations), std::cend(volume.Locations), std::back_inserter(excludedLocations));
            Log::Info(L"Exclude: '{}'", location->GetLocation());
            break;
        }
    }
}

bool NotContain(const Location::Ptr& location, const std::vector<Location::Ptr>& locations)
{
    return std::find(std::cbegin(locations), std::cend(locations), location) == std::cend(locations);
}

}  // namespace

static const auto CSIDL_NONE = ((DWORD)-1);

struct SpecialFolder
{
    DWORD dwCSIDL;
    WCHAR* szCSIDL;
    WCHAR* swDescription;
};

SpecialFolder g_specialFolders[] = {
    {CSIDL_DESKTOP, L"CSIDL_DESKTOP", L"<desktop>"},
    {CSIDL_INTERNET, L"CSIDL_INTERNET", L"Internet Explorer (icon on desktop)"},
    {CSIDL_PROGRAMS, L"CSIDL_PROGRAMS", L"Start Menu\\Programs"},
    {CSIDL_CONTROLS, L"CSIDL_CONTROLS", L"My Computer\\Control Panel"},
    {CSIDL_PRINTERS, L"CSIDL_PRINTERS", L"My Computer\\Printers"},
    {CSIDL_PERSONAL, L"CSIDL_PERSONAL", L"My Documents"},
    {CSIDL_FAVORITES, L"CSIDL_FAVORITES", L"<user name>\\Favorites"},
    {CSIDL_STARTUP, L"CSIDL_STARTUP", L"Start Menu\\Programs\\Startup"},
    {CSIDL_RECENT, L"CSIDL_RECENT", L"<user name>\\Recent"},
    {CSIDL_SENDTO, L"CSIDL_SENDTO", L"<user name>\\SendTo"},
    {CSIDL_BITBUCKET, L"CSIDL_BITBUCKET", L"<desktop>\\Recycle Bin"},
    {CSIDL_STARTMENU, L"CSIDL_STARTMENU", L"<user name>\\Start Menu"},
    {CSIDL_MYDOCUMENTS, L"CSIDL_MYDOCUMENTS", L"My Documents"},
    {CSIDL_MYMUSIC, L"CSIDL_MYMUSIC", L"\"My Music\" folder"},
    {CSIDL_MYVIDEO, L"CSIDL_MYVIDEO", L"\"My Videos\" folder"},
    {CSIDL_DESKTOPDIRECTORY, L"CSIDL_DESKTOPDIRECTORY", L"<user name>\\Desktop"},
    {CSIDL_DRIVES, L"CSIDL_DRIVES", L"My Computer"},
    {CSIDL_NETWORK, L"CSIDL_NETWORK", L"Network Neighborhood (My Network Places)"},
    {CSIDL_NETHOOD, L"CSIDL_NETHOOD", L"<user name>\\nethood"},
    {CSIDL_FONTS, L"CSIDL_FONTS", L"windows\\fonts"},
    {CSIDL_TEMPLATES, L"CSIDL_TEMPLATES", L""},
    {CSIDL_COMMON_STARTMENU, L"CSIDL_COMMON_STARTMENU", L"All Users\\Start Menu"},
    {CSIDL_COMMON_PROGRAMS, L"CSIDL_COMMON_PROGRAMS", L"All Users\\Start Menu\\Programs"},
    {CSIDL_COMMON_STARTUP, L"CSIDL_COMMON_STARTUP", L"All Users\\Startup"},
    {CSIDL_COMMON_DESKTOPDIRECTORY, L"CSIDL_COMMON_DESKTOPDIRECTORY", L"All Users\\Desktop"},
    {CSIDL_APPDATA, L"CSIDL_APPDATA", L"<user name>\\Application Data"},
    {CSIDL_PRINTHOOD, L"CSIDL_PRINTHOOD", L"<user name>\\PrintHood"},
    {CSIDL_LOCAL_APPDATA, L"CSIDL_LOCAL_APPDATA", L"<user name>\\Local Settings\\Applicaiton Data (non roaming)"},
    {CSIDL_ALTSTARTUP, L"CSIDL_ALTSTARTUP", L"non localized startup"},
    {CSIDL_COMMON_ALTSTARTUP, L"CSIDL_COMMON_ALTSTARTUP", L"non localized common startup"},
    {CSIDL_COMMON_FAVORITES, L"CSIDL_COMMON_FAVORITES", L""},
    {CSIDL_INTERNET_CACHE, L"CSIDL_INTERNET_CACHE", L""},
    {CSIDL_COOKIES, L"CSIDL_COOKIES", L""},
    {CSIDL_HISTORY, L"CSIDL_HISTORY", L""},
    {CSIDL_COMMON_APPDATA, L"CSIDL_COMMON_APPDATA", L"All Users\\Application Data"},
    {CSIDL_WINDOWS, L"CSIDL_WINDOWS", L"GetWindowsDirectory()"},
    {CSIDL_SYSTEM, L"CSIDL_SYSTEM", L"GetSystemDirectory()"},
    {CSIDL_PROGRAM_FILES, L"CSIDL_PROGRAM_FILES", L"C:\\Program Files"},
    {CSIDL_MYPICTURES, L"CSIDL_MYPICTURES", L"C:\\Program Files\\My Pictures"},
    {CSIDL_PROFILE, L"CSIDL_PROFILE", L"USERPROFILE"},
    {CSIDL_SYSTEMX86, L"CSIDL_SYSTEMX86", L"x86 system directory on RISC"},
    {CSIDL_PROGRAM_FILESX86, L"CSIDL_PROGRAM_FILESX86", L"x86 C:\\Program Files on RISC"},
    {CSIDL_PROGRAM_FILES_COMMON, L"CSIDL_PROGRAM_FILES_COMMON", L"C:\\Program Files\\Common"},
    {CSIDL_PROGRAM_FILES_COMMONX86, L"CSIDL_PROGRAM_FILES_COMMONX86", L"x86 Program Files\\Common on RISC"},
    {CSIDL_COMMON_TEMPLATES, L"CSIDL_COMMON_TEMPLATES", L"All Users\\Templates"},
    {CSIDL_COMMON_DOCUMENTS, L"CSIDL_COMMON_DOCUMENTS", L"All Users\\Documents"},
    {CSIDL_COMMON_ADMINTOOLS, L"CSIDL_COMMON_ADMINTOOLS", L"All Users\\Start Menu\\Programs\\Administrative Tools"},
    {CSIDL_ADMINTOOLS, L"CSIDL_ADMINTOOLS", L"<user name>\\Start Menu\\Programs\\Administrative Tools"},
    {CSIDL_CONNECTIONS, L"CSIDL_CONNECTIONS", L"Network and Dial-up Connections"},
    {CSIDL_COMMON_MUSIC, L"CSIDL_COMMON_MUSIC", L"All Users\\My Music"},
    {CSIDL_COMMON_PICTURES, L"CSIDL_COMMON_PICTURES", L"All Users\\My Pictures"},
    {CSIDL_COMMON_VIDEO, L"CSIDL_COMMON_VIDEO", L"All Users\\My Video"},
    {CSIDL_RESOURCES, L"CSIDL_RESOURCES", L"Resource Direcotry"},
    {CSIDL_RESOURCES_LOCALIZED, L"CSIDL_RESOURCES_LOCALIZED", L"Localized Resource Direcotry"},
    {CSIDL_COMMON_OEM_LINKS, L"CSIDL_COMMON_OEM_LINKS", L"Links to All Users OEM specific apps"},
    {CSIDL_CDBURN_AREA, L"CSIDL_CDBURN_AREA", L"USERPROFILE\\Local Settings\\Application Data\\Microsoft\\CD Burning"},
    {CSIDL_COMPUTERSNEARME, L"CSIDL_COMPUTERSNEARME", L"Computers Near Me (computered from Workgroup membership)"},
    {CSIDL_NONE, NULL, NULL}};

DWORD g_dwKnownLocationsCSIDL[] = {CSIDL_DESKTOPDIRECTORY,  CSIDL_PROGRAMS,         CSIDL_FAVORITES,
                                   CSIDL_STARTUP,           CSIDL_BITBUCKET,        CSIDL_STARTMENU,
                                   CSIDL_COMMON_STARTMENU,  CSIDL_COMMON_STARTUP,   CSIDL_COMMON_DESKTOPDIRECTORY,
                                   CSIDL_APPDATA,           CSIDL_LOCAL_APPDATA,    CSIDL_ALTSTARTUP,
                                   CSIDL_COMMON_ALTSTARTUP, CSIDL_COMMON_FAVORITES, CSIDL_INTERNET_CACHE,
                                   CSIDL_COOKIES,           CSIDL_HISTORY,          CSIDL_COMMON_APPDATA,
                                   CSIDL_WINDOWS,           CSIDL_PROGRAM_FILES,    CSIDL_PROFILE,
                                   CSIDL_PROGRAM_FILESX86,  CSIDL_ADMINTOOLS,       CSIDL_NONE};

WCHAR* g_szKnownEnvPaths[] = {L"%PATH%", L"%ALLUSERSPROFILE%", L"%temp%", L"%tmp%", L"%APPDATA%", NULL};

LocationSet::Altitude LocationSet::GetAltitudeFromString(std::wstring_view svAltitude)
{
    using namespace std::string_view_literals;
    constexpr auto LOWEST = L"lowest"sv;
    constexpr auto HIGHEST = L"highest"sv;
    constexpr auto EXACT = L"exact"sv;

    if (equalCaseInsensitive(svAltitude, LOWEST, LOWEST.size()))
        return LocationSet::Altitude::Lowest;

    if (equalCaseInsensitive(svAltitude, HIGHEST, HIGHEST.size()))
        return LocationSet::Altitude::Highest;

    if (equalCaseInsensitive(svAltitude, EXACT, EXACT.size()))
        return LocationSet::Altitude::Exact;

    return LocationSet::Altitude::Lowest;
}

std::wstring LocationSet::GetStringFromAltitude(Altitude alt)
{
    switch (alt)
    {
        case LocationSet::Altitude::Lowest:
            return std::wstring(L"lowest");
        case LocationSet::Altitude::Exact:
            return std::wstring(L"exact");
        case LocationSet::Altitude::Highest:
            return std::wstring(L"highest");
    }
    return std::wstring();
}

constexpr auto OrcDefaultAltitudeEnv = L"DFIR-ORC_DEFAULT_ALTITUDE";

HRESULT Orc::LocationSet::ConfigureDefaultAltitude(const Altitude alt)
{
    HRESULT hr = E_FAIL;

    if (alt == LocationSet::Altitude::NotSet)
        return S_OK;

    std::wstring strAltitude = LocationSet::GetStringFromAltitude(alt);

    if (!SetEnvironmentVariableW(OrcDefaultAltitudeEnv, strAltitude.c_str()))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error(L"Failed to set %%{}%% to '{}' [{}]", OrcDefaultAltitudeEnv, strAltitude, SystemError(hr));
        return E_INVALIDARG;
    }

    Log::Info(L"Default altitude is now set to '{}'", strAltitude);
    return S_OK;
}

Orc::LocationSet::Altitude Orc::LocationSet::GetDefaultAltitude()
{

    using namespace msl::utilities;

    Altitude retval = Lowest;
    DWORD nbChars = GetEnvironmentVariableW(OrcDefaultAltitudeEnv, NULL, 0L);

    if (nbChars > 0)
    {
        CBinaryBuffer buffer;
        buffer.SetCount(SafeInt<USHORT>(nbChars) * sizeof(WCHAR));

        GetEnvironmentVariableW(OrcDefaultAltitudeEnv, buffer.GetP<WCHAR>(), nbChars);

        auto default_alt = GetAltitudeFromString(buffer.GetP<WCHAR>());

        if (default_alt > Altitude::NotSet)
        {
            return default_alt;
        }
    }
    return Altitude::Lowest;
}

Location::Type LocationSet::DeduceLocationType(const WCHAR* szLocation)
{
    HRESULT hr = E_FAIL;
    Location::Type retval = Location::Type::Undetermined;

    std::wregex physical_regex(REGEX_PHYSICALDRIVE, std::regex_constants::icase);
    std::wregex disk_regex(REGEX_DISK, std::regex_constants::icase);
    std::wregex partition_regex(REGEX_PARTITIONVOLUME, std::regex_constants::icase);
    std::wregex interface_regex(REGEX_INTERFACE, std::regex_constants::icase);
    std::wregex systemstorage_regex(REGEX_SYSTEMSTORAGE, std::regex_constants::icase);
    std::wregex snapshot_regex(REGEX_SNAPSHOT, std::regex_constants::icase);
    std::wregex volume_regex(REGEX_MOUNTED_VOLUME, std::regex_constants::icase);
    std::wregex harddiskvolume_regex(REGEX_MOUNTED_HARDDISKVOLUME, std::regex_constants::icase);
    std::wregex drive_regex(REGEX_MOUNTED_DRIVE);
    std::wregex image_regex(REGEX_IMAGE, std::regex_constants::icase);

    std::wstring Location(szLocation);
    std::wsmatch m;

    if (std::regex_match(Location, m, physical_regex))
    {
        if (m[REGEX_PHYSICALDRIVE_PARTITION_NUM].matched || m[REGEX_PHYSICALDRIVE_OFFSET].matched)
            retval = Location::Type::PhysicalDriveVolume;
        else
            retval = Location::Type::PhysicalDrive;
    }
    else if (std::regex_match(Location, m, disk_regex))
    {
        if (m[REGEX_DISK_PARTITION_NUM].matched || m[REGEX_DISK_OFFSET].matched)
            retval = Location::Type::PhysicalDriveVolume;
        else
            retval = Location::Type::PhysicalDrive;
    }
    else if (std::regex_match(Location, m, partition_regex))
    {
        retval = Location::Type::PartitionVolume;
    }
    else if (std::regex_match(Location, m, interface_regex))
    {
        if (m[REGEX_INTERFACE_PARTITION_NUM].matched || m[REGEX_INTERFACE_OFFSET].matched)
            retval = Location::Type::DiskInterfaceVolume;
        else
            retval = Location::Type::DiskInterface;
    }
    else if (std::regex_match(Location, m, snapshot_regex))
    {
        retval = Location::Type::Snapshot;
    }
    else if (std::regex_match(Location, m, volume_regex))
    {
        retval = Location::Type::MountedVolume;
    }
    else if (std::regex_match(Location, m, harddiskvolume_regex))
    {
        retval = Location::Type::MountedVolume;
    }
    else if (std::regex_match(Location, m, systemstorage_regex))
    {
        if (m[REGEX_SYSTEMSTORAGE_PARTITION_NUM].matched || m[REGEX_SYSTEMSTORAGE_OFFSET].matched)
            retval = Location::Type::SystemStorageVolume;
        else
            retval = Location::Type::SystemStorage;
    }
    else if (std::regex_match(Location, m, image_regex))
    {
        // File or Directory???
        if (m[REGEX_IMAGE_SPEC].matched)
        {
            wstring ImageLocation = m[REGEX_IMAGE_SPEC].str();
            DWORD dwAttrs = GetFileAttributes(ImageLocation.c_str());

            if (dwAttrs == INVALID_FILE_ATTRIBUTES)
            {
                // invalid path and/or non existing file/dir
                Log::Error(L"Could not determine reader for '{}'", ImageLocation);
                return Location::Type::Undetermined;
            }
            else if (dwAttrs & FILE_ATTRIBUTE_DIRECTORY)
            {
                // Directory: mounted volume
                return Location::Type::MountedVolume;
            }
            else
            {
                // File: dd.exe image or offline MFT?
                CDiskExtent extent;

                if (m[REGEX_IMAGE_PARTITION_NUM].matched)
                {
                    extent = CDiskExtent(ImageLocation.c_str());
                }
                else if (m[REGEX_IMAGE_OFFSET].matched)
                {
                    LARGE_INTEGER offset = {0}, size = {0}, sector = {0};

                    if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_OFFSET].str().c_str(), offset)))
                    {
                        Log::Error(
                            L"Invalid offset specified: '{}' [{}]", m[REGEX_IMAGE_OFFSET].str(), SystemError(hr));
                        return Location::Type::Undetermined;
                    }

                    if (m[REGEX_IMAGE_SIZE].matched)
                    {
                        if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_SIZE].str().c_str(), size)))
                        {
                            Log::Error(
                                L"Invalid size specified: '{}' [{}]", m[REGEX_IMAGE_SIZE].str(), SystemError(hr));
                            return Location::Type::Undetermined;
                        }
                    }
                    if (m[REGEX_IMAGE_SECTOR].matched)
                    {
                        if (FAILED(hr = GetFileSizeFromArg(m[REGEX_IMAGE_SECTOR].str().c_str(), sector)))
                        {
                            Log::Error(
                                L"Invalid sector size specified: '{}' [{}]",
                                m[REGEX_IMAGE_SECTOR].str().c_str(),
                                SystemError(hr));
                            return Location::Type::Undetermined;
                        }
                    }
                    extent = CDiskExtent(ImageLocation.c_str(), offset.QuadPart, size.QuadPart, sector.LowPart);
                }
                else
                {
                    extent = CDiskExtent(ImageLocation.c_str());
                }

                if (FAILED(hr = extent.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL)))
                {
                    Log::Error(L"Could not open location: {} [{}]", Location, SystemError(hr));
                    return Location::Type::Undetermined;
                }

                CBinaryBuffer buffer;
                buffer.SetCount(sizeof(PackedGenBootSector));
                DWORD dwBytesRead = 0;

                if (FAILED(hr = extent.Read(buffer.GetData(), (DWORD)buffer.GetCount(), &dwBytesRead)))
                {
                    Log::Error(L"Failed to read from location: {} [{}]", Location, SystemError(hr));
                    return Location::Type::Undetermined;
                }

                if (FSVBR::FSType::UNKNOWN != FSVBR::GuessFSType(buffer))
                {
                    // this file is a volume image
                    retval = Location::Type::ImageFileVolume;
                }
                else if (m[REGEX_IMAGE_PARTITION_NUM].matched && m[REGEX_IMAGE_PARTITION_NUM].compare(L"*"))
                {
                    PartitionTable pt;

                    auto imageFileName = m[REGEX_IMAGE_SPEC].str();

                    if (FAILED(hr = pt.LoadPartitionTable(imageFileName.c_str())))
                    {
                        Log::Error(L"Failed to load partition table for '{}' [{}]", imageFileName, SystemError(hr));
                        return Location::Type::Undetermined;
                    }

                    DWORD partNum = 0;
                    if (FAILED(GetIntegerFromArg(m[REGEX_IMAGE_PARTITION_NUM].str().c_str(), partNum)))
                    {
                        Log::Error(
                            L"Invalid partition number {} for '{}'",
                            m[REGEX_IMAGE_PARTITION_NUM].str().c_str(),
                            imageFileName.c_str());
                        return Location::Type::Undetermined;
                    }

                    if (pt.Table().size() >= partNum)
                        return Location::Type::ImageFileVolume;
                }
                else
                {
                    PMasterBootRecord pMBR = (PMasterBootRecord)buffer.GetData();
                    if (pMBR->MBRSignature[0] == 0x55 && pMBR->MBRSignature[1] == 0xaa)
                    {
                        // this file is a disk image file
                        retval = Location::Type::ImageFileDisk;
                    }
                    else
                    {
                        // offline MFT?
                        PMULTI_SECTOR_HEADER header = (PMULTI_SECTOR_HEADER)buffer.GetData();

                        if (!strncmp((const char*)header->Signature, "FILE", strlen("FILE")))
                        {
                            // this file appears to be an offline MFT
                            retval = Location::Type::OfflineMFT;
                        }
                    }
                }
            }
        }
    }
    return retval;
}

HRESULT LocationSet::CanonicalizeLocation(
    const WCHAR* szLocation,
    std::wstring& canonicalLocation,
    Location::Type& locType,
    std::wstring& subpath)
{
    std::wstring retval;
    std::wstring location;

    canonicalLocation.clear();
    subpath.clear();

    WCHAR szTemp[ORC_MAX_PATH] = {0};
    DWORD dwLen = ExpandEnvironmentStrings(szLocation, NULL, 0L);
    if (dwLen > ORC_MAX_PATH)
        return E_INVALIDARG;
    dwLen = ExpandEnvironmentStrings(szLocation, (LPWSTR)szTemp, ORC_MAX_PATH);

    location.assign(szTemp);

    locType = DeduceLocationType(location.c_str());
    if (locType == Location::Type::Undetermined)
        return E_INVALIDARG;

    switch (locType)
    {
        case Location::Type::MountedVolume: {
            std::wregex r1(REGEX_MOUNTED_DRIVE);
            std::wregex r2(REGEX_MOUNTED_VOLUME, std::regex_constants::icase);
            std::wregex r3(REGEX_MOUNTED_HARDDISKVOLUME, std::regex_constants::icase);
            std::wsmatch m;

            if (std::regex_match(location, m, r1))
            {
                if (m[REGEX_MOUNTED_DRIVE_LETTER].matched)
                {
                    wstring temp = m[REGEX_MOUNTED_DRIVE_LETTER];
                    temp += L":\\";
                    retval = std::move(temp);
                }
                if (m[REGEX_MOUNTED_DRIVE_SUBDIR].matched)
                {
                    if (m[REGEX_MOUNTED_DRIVE_SUBDIR].length() > 0)
                    {
                        wstring subdir = m[REGEX_MOUNTED_DRIVE_SUBDIR];
                        subpath = std::move(subdir);
                    }
                }
            }
            else if (std::regex_match(location, m, r2))
            {
                if (m[REGEX_MOUNTED_VOLUME_ID].matched)
                {
                    wstring temp = L"\\\\?\\Volume";
                    temp += m[REGEX_MOUNTED_VOLUME_ID];
                    retval = std::move(temp);
                }
            }
            else if (std::regex_match(location, m, r3))
            {
                if (m[REGEX_MOUNTED_HARDDISKVOLUME_ID].matched)
                {
                    wstring temp = L"\\\\.\\HarddiskVolume";
                    temp += m[REGEX_MOUNTED_HARDDISKVOLUME_ID];
                    retval = std::move(temp);
                }
            }
        }
        break;
        case Location::Type::PartitionVolume:
        case Location::Type::ImageFileVolume:
        case Location::Type::ImageFileDisk:
        case Location::Type::Snapshot:
        case Location::Type::OfflineMFT:
        case Location::Type::PhysicalDrive:
        case Location::Type::PhysicalDriveVolume:
        case Location::Type::DiskInterface:
        case Location::Type::DiskInterfaceVolume:
        case Location::Type::SystemStorage:
        case Location::Type::SystemStorageVolume:
            // these types do not require canonicalisation of Location
            retval = location;
            break;
        case Location::Type::Undetermined:
        default:
            retval.clear();
            break;
    }

    canonicalLocation = std::move(retval);
    return S_OK;
}

HRESULT LocationSet::EnumerateLocations()
{
    HRESULT hr = E_FAIL;

    Log::Trace("Starting to enumerate locations");

    if (!m_bMountedVolumesPopulated && FAILED(hr = PopulateMountedVolumes()))
    {
        Log::Warn("Failed to parse mounted volumes [{}]", SystemError(hr));
    }

    if (!m_bPhysicalDrivesPopulated && FAILED(hr = PopulatePhysicalDrives()))
    {
        Log::Warn("Failed to parse physical drives [{}]", SystemError(hr));
    }

    if (!m_bInterfacesPopulated && FAILED(hr = PopulateSystemObjects(true)))
    {
        Log::Warn("Failed to parse interfaces [{}]", SystemError(hr));
    }

    if (!m_bSystemObjectsPopulated && FAILED(hr = PopulateSystemObjects(false)))
    {
        Log::Warn("Failed to parse system objects [{}]", SystemError(hr));
    }

    if (!m_bShadowsPopulated && FAILED(hr = PopulateShadows()))
    {
        Log::Warn("Failed to add VSS shadow Copies [{}]", SystemError(hr));
    }

    if (FAILED(hr = EliminateDuplicateLocations()))
    {
        Log::Error("Failed to eliminate duplicate locations [{}]", SystemError(hr));
        return hr;
    }

    Log::Trace("End of location enumeration");

    return S_OK;
}

HRESULT
LocationSet::AddLocations(const WCHAR* szLocation, std::vector<std::shared_ptr<Location>>& addedLocs, bool bToParse)
{
    HRESULT hr = E_FAIL;

    std::vector<std::wstring> subLocations = ExpandOrcStringsLocation(szLocation);
    if (subLocations.size() > 1)
    {
        for (const auto& subLocation : subLocations)
        {
            hr = AddLocations(subLocation.c_str(), addedLocs, bToParse);
            if (FAILED(hr))
            {
                Log::Error(L"Failed to process location '{}' [{}]", subLocation, SystemError(hr));
                continue;
            }
        }

        return S_OK;
    }

    Location::Type locType = Location::Type::Undetermined;
    wstring canonical;
    wstring subdir;

    if (FAILED(hr = CanonicalizeLocation(szLocation, canonical, locType, subdir)))
        return hr;

    // ImageFile and Physical drive can turn to be more than one location.
    // We will add locations as needed

    std::shared_ptr<Location> addedLoc;
    wsmatch m;

    switch (locType)
    {
        case Location::Type::ImageFileDisk: {
            wregex image_regex(REGEX_IMAGE, std::regex_constants::icase);
            if (regex_match(canonical, m, image_regex))
            {
                PartitionTable pt;

                wstring imageFile;

                if (m[REGEX_IMAGE_SPEC].matched)
                {
                    imageFile = m[REGEX_IMAGE_SPEC].str();
                    if (FAILED(hr = pt.LoadPartitionTable(imageFile.c_str())))
                    {
                        Log::Error(L"Failed to load partition table for '{}' [{}]", canonical, SystemError(hr));
                        return hr;
                    }
                }
                else
                {
                    imageFile = canonical;
                }

                if (m[REGEX_IMAGE_PARTITION_SPEC].matched)
                {
                    Partition partition;
                    if (m[REGEX_IMAGE_PARTITION_NUM].matched)
                    {
                        UINT uiPartNum = std::stoi(m[REGEX_IMAGE_PARTITION_NUM].str());
                        std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                            if (part.PartitionNumber == uiPartNum)
                                partition = part;
                        });
                    }
                    else
                    {
                        std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                            if (part.IsBootable())
                                partition = part;
                        });
                    }
                    if (partition.PartitionNumber != 0)
                    {
                        wstring location = imageFile + L",offset=" + to_wstring((ULONGLONG)partition.Start) + L",size="
                            + to_wstring((ULONGLONG)partition.Size) + L",sector="
                            + to_wstring((ULONGLONG)partition.SectorSize);

                        if (FAILED(
                                hr =
                                    AddLocation(location, Location::Type::ImageFileVolume, subdir, addedLoc, bToParse)))
                        {
                            Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                        }
                        else
                        {
                            addedLocs.push_back(addedLoc);
                        }
                    }
                }
                else
                {
                    std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                        wstring location = imageFile + L",offset=" + to_wstring((ULONGLONG)part.Start) + L",size="
                            + to_wstring((ULONGLONG)part.Size) + L",sector=" + to_wstring((ULONGLONG)part.SectorSize);

                        if (FAILED(
                                hr =
                                    AddLocation(location, Location::Type::ImageFileVolume, subdir, addedLoc, bToParse)))
                        {
                            Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                        }
                        else
                        {
                            addedLocs.push_back(addedLoc);
                        }
                    });
                }
            }
        }
        break;
        case Location::Type::PhysicalDrive: {
            PartitionTable pt;

            if (FAILED(hr = pt.LoadPartitionTable(canonical.c_str())))
            {
                Log::Error(L"Failed to load partition table for '{}' [{}]", canonical, SystemError(hr));
                return hr;
            }

            std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                wstring location = canonical + L",offset=" + to_wstring((ULONGLONG)part.Start) + L",size="
                    + to_wstring((ULONGLONG)part.Size) + L",sector=" + to_wstring((ULONGLONG)part.SectorSize);

                if (FAILED(hr = AddLocation(location, Location::Type::PhysicalDriveVolume, subdir, addedLoc, bToParse)))
                {
                    Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                }
                else
                {
                    addedLocs.push_back(addedLoc);
                }
            });
        }
        break;
        case Location::Type::PhysicalDriveVolume: {
            wregex drive_regex(REGEX_PHYSICALDRIVE, std::regex_constants::icase);
            wregex disk_regex(REGEX_DISK, std::regex_constants::icase);

            if (regex_match(canonical, m, disk_regex))
            {
                if (m[REGEX_DISK_PARTITION_SPEC].matched || m[REGEX_DISK_OFFSET].matched)
                {
                    if (FAILED(hr = AddLocation(canonical, locType, subdir, addedLoc, bToParse)))
                    {
                        Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                    }
                    else
                    {
                        addedLocs.push_back(addedLoc);
                    }
                }
            }
            else if (regex_match(canonical, m, drive_regex))
            {
                if (m[REGEX_PHYSICALDRIVE_PARTITION_SPEC].matched || m[REGEX_PHYSICALDRIVE_OFFSET].matched)
                {
                    if (FAILED(hr = AddLocation(canonical, locType, subdir, addedLoc, bToParse)))
                    {
                        Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                    }
                    else
                    {
                        addedLocs.push_back(addedLoc);
                    }
                }
            }
        }
        break;
        case Location::Type::DiskInterface: {
            PartitionTable pt;

            if (FAILED(hr = pt.LoadPartitionTable(canonical.c_str())))
            {
                Log::Error(L"Failed to load partition table for '{}' [{}]", canonical, SystemError(hr));
                return hr;
            }

            std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                wstring location = canonical + L",offset=" + to_wstring((ULONGLONG)part.Start) + L",size="
                    + to_wstring((ULONGLONG)part.Size) + L",sector=" + to_wstring((ULONGLONG)part.SectorSize);

                if (FAILED(hr = AddLocation(location, locType, subdir, addedLoc, bToParse)))
                {
                    Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                }
                else
                {
                    addedLocs.push_back(addedLoc);
                }
            });
        }
        break;
        case Location::Type::DiskInterfaceVolume: {
            wregex interface_regex(REGEX_INTERFACE, std::regex_constants::icase);
            if (regex_match(canonical, m, interface_regex))
            {
                if (m[REGEX_INTERFACE_PARTITION_SPEC].matched || m[REGEX_INTERFACE_OFFSET].matched)
                {
                    if (FAILED(hr = AddLocation(canonical, locType, subdir, addedLoc, bToParse)))
                    {
                        Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                    }
                    else
                    {
                        addedLocs.push_back(addedLoc);
                    }
                }
            }
        }
        break;
        case Location::Type::SystemStorage: {
            auto volreader = std::make_shared<SystemStorageReader>(canonical.c_str());

            if (SUCCEEDED(hr = volreader->LoadDiskProperties()))
            {
                if (FAILED(
                        hr = AddLocation(canonical, Location::Type::SystemStorageVolume, subdir, addedLoc, bToParse)))
                {
                    Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                }
                else
                {
                    addedLocs.push_back(addedLoc);
                }
            }
            else
            {
                PartitionTable pt;

                if (FAILED(hr = pt.LoadPartitionTable(canonical.c_str())))
                {
                    Log::Error(L"Failed to load partition table for '{}' [{}]", canonical, SystemError(hr));
                    return hr;
                }

                std::for_each(begin(pt.Table()), end(pt.Table()), [&](const Partition& part) {
                    wstring location = canonical + L",offset=" + to_wstring((ULONGLONG)part.Start) + L",size="
                        + to_wstring((ULONGLONG)part.Size) + L",sector=" + to_wstring((ULONGLONG)part.SectorSize);

                    if (FAILED(hr = AddLocation(location, locType, subdir, addedLoc, bToParse)))
                    {
                        Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                    }
                    else
                    {
                        addedLocs.push_back(addedLoc);
                    }
                });
            }
        }
        break;
        case Location::Type::PartitionVolume: {
            wregex partition_regex(REGEX_PARTITIONVOLUME, std::regex_constants::icase);
            if (regex_match(canonical, m, partition_regex))
            {
                if (FAILED(hr = AddLocation(canonical, locType, subdir, addedLoc, bToParse)))
                {
                    Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                }
                else
                {
                    addedLocs.push_back(addedLoc);
                }
            }
        }
        break;
        case Location::Type::SystemStorageVolume: {
            wregex system_regex(REGEX_SYSTEMSTORAGE, std::regex_constants::icase);
            if (regex_match(canonical, m, system_regex))
            {
                if (m[REGEX_SYSTEMSTORAGE_PARTITION_SPEC].matched || m[REGEX_SYSTEMSTORAGE_OFFSET].matched)
                {
                    if (FAILED(hr = AddLocation(canonical, locType, subdir, addedLoc, bToParse)))
                    {
                        Log::Error(L"Failed to add location '{}' [{}]", canonical, SystemError(hr));
                    }
                    else
                    {
                        addedLocs.push_back(addedLoc);
                    }
                }
            }
        }
        break;
        case Location::Type::OfflineMFT:
        case Location::Type::MountedVolume:
        case Location::Type::Snapshot:
        case Location::Type::ImageFileVolume:
            if (FAILED(hr = AddLocation(canonical, locType, subdir, addedLoc, bToParse)))
            {
                Log::Error(L"Failed to add location '{}' [{}]", szLocation, SystemError(hr));
                return hr;
            }
            addedLocs.push_back(addedLoc);
            break;

        default:
            break;
    }

    return S_OK;
}

HRESULT LocationSet::AddLocationsFromConfigItem(const ConfigItem& config)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = EnumerateLocations()))
    {
        Log::Error("Failed to enumerate volume locations [{}]", SystemError(hr));
        return hr;  // fatal
    }

    if (!config)
        return S_OK;
    if (config.Type != ConfigItem::NODELIST)
        return E_INVALIDARG;
    if (config.strName != L"location")
        return E_INVALIDARG;

    hr = S_OK;
    for (auto& item : config.NodeList)
    {
        if (item.SubItems[CONFIG_VOLUME_ALTITUDE])
        {
            m_Altitude = GetAltitudeFromString((const std::wstring&)item.SubItems[CONFIG_VOLUME_ALTITUDE]);
        }

        if (((std::wstring_view)item)[0] == L'*')
        {
            hr = ParseAllVolumes();
            break;
        }
        else
        {
            std::vector<std::shared_ptr<Location>> addedLocs;
            hr = AddLocations(item.c_str(), addedLocs);
        }
    }

    return hr;
}

HRESULT LocationSet::AddLocationsFromArgcArgv(int argc, LPCWSTR argv[])
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = EnumerateLocations()))
        return hr;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != L'/' && argv[i][0] != L'+' && argv[i][0] != L'-')
        {
            if (argv[i][0] == L'*')
            {
                if (FAILED(hr = ParseAllVolumes()))
                    return hr;
                return S_OK;
            }
            else
            {
                std::vector<std::shared_ptr<Location>> addedLocs;
                if (FAILED(hr = AddLocations(argv[i], addedLocs)))
                    return hr;
            }
        }
    }

    return S_OK;
}

HRESULT LocationSet::AddKnownLocations(const ConfigItem& item)
{
    HRESULT hr = E_FAIL;
    if (item.Type != ConfigItem::NODE)
        return E_INVALIDARG;
    if (item.strName != L"knownlocations")
        return E_INVALIDARG;

    if (item)
        return AddKnownLocations();

    return S_OK;
}

HRESULT LocationSet::AddKnownLocations()
{
    Log::Trace(L"Adding known locations");
    HRESULT hr = E_FAIL;
    DWORD* pCurKnownLoc = g_dwKnownLocationsCSIDL;

    if (FAILED(hr = PopulateMountedVolumes()))
        return hr;

    if (pCurKnownLoc)
        while (*pCurKnownLoc != CSIDL_NONE)
        {
            auto curLoc = KnownFolder(*pCurKnownLoc);
            if (!curLoc.empty())
            {
                if (iswalpha(curLoc[0]) && curLoc[1] == L':' && curLoc[2] == L'\\')
                {
                    std::vector<std::shared_ptr<Location>> addedLocs;
                    if (FAILED(hr = AddLocations(curLoc.c_str(), addedLocs)))
                        return hr;
                }
                else
                {
                    Log::Error(L"Only absolute path locations are allowed ( C:\\... ), Entry '{}' ignored", curLoc);
                }
            }
            pCurKnownLoc++;
        }

    const WCHAR** szCurLoc = (const WCHAR**)g_szKnownEnvPaths;

    while (*szCurLoc)
    {
        std::vector<std::shared_ptr<Location>> addedLocs;

        DWORD dwSize = 0L;
        dwSize = ExpandEnvironmentStrings(*szCurLoc, NULL, 0L);

        if (dwSize > 0)
        {
            CBinaryBuffer buffer;
            if (!buffer.SetCount(dwSize * sizeof(WCHAR)))
                return E_OUTOFMEMORY;

            ExpandEnvironmentStrings(*szCurLoc, buffer.GetP<WCHAR>(0), dwSize);

            std::wstring PATH((LPWSTR)buffer.GetData());

            std::vector<std::wstring> paths;
            boost::split(paths, PATH, boost::is_any_of(L";,"));

            for (auto& path : paths)
            {
                if (FAILED(hr = AddLocations(path.c_str(), addedLocs)))
                {
                    Log::Warn(L"Invalid location '{}' ignored [{}]", path, SystemError(hr));
                }
            }
        }
        szCurLoc++;
    }

    return S_OK;
}

HRESULT LocationSet::PopulateMountedVolumes()
{
    wregex r(REGEX_MOUNTED_HARDDISKVOLUME, regex_constants::icase);

    if (m_bMountedVolumesPopulated)
        return S_OK;

    Log::Trace("Populating mounted volumes");
    HRESULT hr = E_FAIL;

    //
    //  Enumerate all volumes in the system.
    WCHAR szVolumeName[ORC_MAX_PATH];
    HANDLE hFindHandle = FindFirstVolume(szVolumeName, ARRAYSIZE(szVolumeName));

    if (hFindHandle == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Log::Error("Failed FindFirstVolumeW [{}]", SystemError(hr));
        return hr;
    }

    for (;;)
    {
        //
        //  Skip the \\?\ prefix and remove the trailing backslash.
        size_t Index = wcslen(szVolumeName) - 1;

        if (szVolumeName[0] != L'\\' || szVolumeName[1] != L'\\' || szVolumeName[2] != L'?' || szVolumeName[3] != L'\\')
        {
            hr = HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME);
            Log::Error(L"FindFirstVolumeW/FindNextVolumeW returned a bad path: '{}'", szVolumeName);
            break;
        }

        do
        {
            //
            //  QueryDosDeviceW does not allow a trailing backslash,
            //  so temporarily remove it.
            WCHAR szDeviceName[ORC_MAX_PATH];
            DWORD CharCount = 0L;
            if (szVolumeName[Index] == L'\\')
            {
                szVolumeName[Index] = L'\0';
                CharCount = QueryDosDevice(&szVolumeName[4], szDeviceName, ARRAYSIZE(szDeviceName));
                szVolumeName[Index] = L'\\';
            }
            else
            {
                CharCount = QueryDosDevice(&szVolumeName[4], szDeviceName, ARRAYSIZE(szDeviceName));
                szVolumeName[Index + 1] = L'\\';  // GetVolumePathNamesForVolumeName wants the trailing "\\"
            }

            if (CharCount == 0)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                Log::Warn(L"Failed QueryDosDeviceW [{}]", SystemError(hr));
                continue;
            }

            Log::Debug(L"Found a device: {}", szDeviceName);
            Log::Debug(L"Volume name: {}", szVolumeName);
            Log::Debug(L"Paths:");

            DWORD dwPathLen = 0;
            if (!GetVolumePathNamesForVolumeName(szVolumeName, NULL, 0L, &dwPathLen))
            {
                hr = HRESULT_FROM_WIN32(GetLastError());

                if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
                {
                    Log::Warn(
                        L"Failed GetVolumePathNamesForVolumeName for volume '{}' [{}]", szVolumeName, SystemError(hr));
                    continue;
                }
            }

            vector<wstring> paths;
            {
                WCHAR* szPathNames = (WCHAR*)HeapAlloc(GetProcessHeap(), 0L, dwPathLen * sizeof(TCHAR));
                if (szPathNames == nullptr)
                    return E_OUTOFMEMORY;

                BOOST_SCOPE_EXIT(&szPathNames)
                {
                    HeapFree(GetProcessHeap(), 0L, szPathNames);
                    szPathNames = NULL;
                }
                BOOST_SCOPE_EXIT_END

                if (!GetVolumePathNamesForVolumeName(szVolumeName, szPathNames, dwPathLen, &dwPathLen))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());

                    if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
                    {
                        Log::Warn(
                            L"Failed GetVolumePathNamesForVolumeName for volume '{}' [{}]",
                            szVolumeName,
                            SystemError(hr));
                        continue;
                    }
                }

                WCHAR* szPath = szPathNames;

                while (*szPath != L'\0')
                {
                    Log::Debug(L"'{}'", szPath);
                    wstring path(szPath);
                    szPath += path.size() + 1;
                    paths.push_back(std::move(path));
                }
            }

            // Determine the associated Physical Drives...
            //
            VOLUME_DISK_EXTENTS Extents;
            PVOLUME_DISK_EXTENTS pExtents = &Extents;

            ZeroMemory(pExtents, sizeof(VOLUME_DISK_EXTENTS));

            if (szVolumeName[Index] == L'\\')
                szVolumeName[Index] = L'\0';

            CDiskExtent volume(szVolumeName);
            if (FAILED(hr = volume.Open(FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL)))
            {
                if (hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
                {
                    Log::Debug(L"Failed to open volume '{}': access denied, volume rejected", szVolumeName);
                }
                else if (hr == HRESULT_FROM_WIN32(ERROR_BAD_DEVICE))
                {
                    Log::Debug(L"Failed to open volume '{}': bad device, volume rejected", szVolumeName);
                }
                else
                {
                    Log::Warn(L"Failed to open volume '{}' [{}]", szVolumeName, SystemError(hr));
                }
                continue;
            }

            if (szVolumeName[Index] == L'\0')
                szVolumeName[Index] = L'\\';

            if (SUCCEEDED(hr))
            {
                DWORD dwExtentsSize = 0L;
                if (!DeviceIoControl(
                        volume.m_hFile,
                        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                        NULL,
                        0L,
                        pExtents,
                        sizeof(VOLUME_DISK_EXTENTS),
                        &dwExtentsSize,
                        NULL))
                {
                    hr = HRESULT_FROM_WIN32(GetLastError());

                    if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
                    {
                        if (hr == HRESULT_FROM_WIN32(ERROR_INVALID_FUNCTION))
                        {
                            Log::Debug(
                                L"Expected failed IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS for volume '{}' [{}]",
                                szVolumeName,
                                SystemError(hr));
                        }
                        else
                        {
                            Log::Warn(
                                L"Failed IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS for volume '{}' [{}]",
                                szVolumeName,
                                SystemError(hr));
                            continue;
                        }
                    }
                    else
                    {
                        DWORD dwRequiredSize = sizeof(VOLUME_DISK_EXTENTS)
                            + ((sizeof(DISK_EXTENT) * (pExtents->NumberOfDiskExtents - ANYSIZE_ARRAY)));
                        pExtents = (PVOLUME_DISK_EXTENTS)HeapAlloc(GetProcessHeap(), 0L, dwRequiredSize);
                        if (pExtents == nullptr)
                        {
                            hr = E_OUTOFMEMORY;
                            break;
                        }
                        ZeroMemory(pExtents, dwRequiredSize);
                        if (!DeviceIoControl(
                                volume.m_hFile,
                                IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                NULL,
                                0L,
                                pExtents,
                                dwRequiredSize,
                                &dwExtentsSize,
                                NULL))
                        {
                            hr = HRESULT_FROM_WIN32(GetLastError());
                            Log::Warn(
                                L"Failed IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS for volume '{}' [{}]",
                                szVolumeName,
                                SystemError(hr));
                            continue;
                        }
                    }
                }

                volume.Close();

                // if no disk extent were found, do no continue with this volume
                if (pExtents->NumberOfDiskExtents)
                {
                    vector<CDiskExtent> diskextents;

                    for (UINT i = 0; i < pExtents->NumberOfDiskExtents; i++)
                    {
                        WCHAR szPhysDrive[ORC_MAX_PATH];
                        swprintf_s(szPhysDrive, L"\\\\.\\PhysicalDrive%d", pExtents->Extents[i].DiskNumber);

                        CDiskExtent ext(szPhysDrive);

                        ext.m_Length = pExtents->Extents[i].ExtentLength.QuadPart;
                        ext.m_Start = pExtents->Extents[i].StartingOffset.QuadPart;

                        diskextents.push_back(std::move(ext));
                    }

                    if (pExtents != &Extents)
                    {
                        HeapFree(GetProcessHeap(), 0L, pExtents);
                    }

                    szVolumeName[Index] = L'\0';
                    shared_ptr<Location> loc = make_shared<Location>(szVolumeName, Location::Type::MountedVolume);
                    szVolumeName[Index] = L'\\';

                    loc->m_Paths = std::move(paths);
                    loc->m_Type = Location::Type::MountedVolume;
                    loc->m_Extents = std::move(diskextents);
                    loc->SetParse(false);

                    std::shared_ptr<Location> addedLoc;
                    if (FAILED(hr = AddLocation(loc, addedLoc, false)))
                    {
                        Log::Warn(L"Failed to add Location '{}' [{}]", loc->GetLocation(), SystemError(hr));
                        continue;
                    }

                    for (auto& item : loc->m_Paths)
                    {
                        HRESULT hr2 = E_FAIL;
                        std::shared_ptr<Location> addedPath;
                        if (FAILED(hr2 = AddLocation(item, loc, addedPath, false)))
                        {
                            Log::Warn(L"Failed to add Location '{}' [{}]", loc->GetLocation(), SystemError(hr2));
                            continue;
                        }
                    }

                    wstring device_location = L"\\\\.\\HarddiskVolume";

                    std::wcmatch m;
                    if (std::regex_match(szDeviceName, m, r))
                    {
                        if (m[REGEX_MOUNTED_HARDDISKVOLUME_ID].matched)
                        {
                            device_location += m[REGEX_MOUNTED_HARDDISKVOLUME_ID];
                        }
                    }

                    shared_ptr<Location> dev_loc =
                        make_shared<Location>(device_location, Location::Type::MountedVolume);
                    dev_loc->m_Paths = loc->m_Paths;
                    dev_loc->m_Type = Location::Type::MountedVolume;
                    dev_loc->m_Extents = loc->m_Extents;

                    if (FAILED(hr = AddLocation(dev_loc, addedLoc, false)))
                    {
                        Log::Warn(L"Failed to add Location '{}' [{}]", dev_loc->GetLocation(), SystemError(hr));
                        continue;
                    }
                }
            }
        } while (false);

        //
        //  Move on to the next volume.
        if (!FindNextVolumeW(hFindHandle, szVolumeName, ARRAYSIZE(szVolumeName)))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());

            if (hr != HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES))
            {
                Log::Error(L"FindNextVolumeW failed [{}]", SystemError(hr));
                break;
            }
            hr = S_OK;
            break;
        }
    }

    FindVolumeClose(hFindHandle);
    hFindHandle = INVALID_HANDLE_VALUE;

    if (S_OK == hr)
        m_bMountedVolumesPopulated = true;

    return hr;
}

HRESULT LocationSet::PopulatePhysicalDrives()
{
    if (m_bPhysicalDrivesPopulated)
        return S_OK;

    Log::Trace(L"Populating physical drives");
    HRESULT hr = E_FAIL;
    WMI wmi;

    if (FAILED(hr = wmi.Initialize()))
    {
        Log::Error(L"Failed to initialize WMI [{}]", SystemError(hr));
        return hr;
    }

    vector<wstring> output;
    if (FAILED(hr = wmi.WMIEnumPhysicalMedia(output)))
    {
        Log::Error(L"Failed to enum physical media via WMI [{}]", SystemError(hr));
        return hr;
    }

    for (auto& drive : output)
    {
        std::vector<std::shared_ptr<Location>> addedLocs;

        if (FAILED(hr = AddLocations(drive.c_str(), addedLocs, false)))
        {
            Log::Warn(L"Failed to add physical drive '{}' [{}]", drive, SystemError(hr));
        }
    }

    m_bPhysicalDrivesPopulated = true;
    return S_OK;
}

HRESULT LocationSet::PopulateShadows()
{
    if (m_bShadowsPopulated)
        return S_OK;

    Log::Trace(L"Populating shadow volumes");
    HRESULT hr = E_FAIL;
    VolumeShadowCopies vss;

    if (FAILED(hr = vss.EnumerateShadows(m_Shadows)))
    {
        Log::Warn("VSS functionatility is not available [{}]", SystemError(hr));
    }
    else
    {
        for (auto& shadow : m_Shadows)
        {
            std::vector<std::shared_ptr<Location>> addedLocs;

            if (FAILED(hr = AddLocations(shadow.DeviceInstance.c_str(), addedLocs, false)))
            {
                Log::Error(L"Failed to add volume shadow copy '{}' [{}]", shadow.DeviceInstance, SystemError(hr));
            }
            else
            {
                for (auto& added : addedLocs)
                {
                    if (added->GetType() != Location::Type::Snapshot)
                    {
                        Log::Warn(L"Added location '{}' is not a snapshot", added->GetLocation());
                    }
                    else
                    {
                        added->SetShadow(shadow);
                        Log::Debug(L"Added VSS snapshot '{}'", added->GetLocation());
                    }
                }
            }
        }
    }
    m_bShadowsPopulated = true;
    return S_OK;
}

HRESULT LocationSet::PopulateSystemObjects(bool bInterfacesOnly)
{
    if (m_bSystemObjectsPopulated && m_bInterfacesPopulated)
        return S_OK;

    if (m_bSystemObjectsPopulated && !bInterfacesOnly)
        return S_OK;

    if (m_bInterfacesPopulated && bInterfacesOnly)
        return S_OK;

    if (bInterfacesOnly)
    {
        Log::Trace(L"Populating system objects (interfaces only)");
    }
    else
    {
        Log::Trace(L"Populating system objects");
    }

    HRESULT hr = E_FAIL;
    ObjectDirectory objDir;
    std::vector<ObjectDirectory::ObjectInstance> objects;

    const auto pk32 = ExtensionLibrary::GetLibrary<Kernel32Extension>();
    if (pk32 == nullptr)
    {
        Log::Error(L"Failed to obtain the Kernel32.dll as an extension library");
    }

    if (FAILED(hr = objDir.ParseObjectDirectory(L"\\GLOBAL??", objects)))
    {
        Log::Error(L"Failed to list object directory \\GLOBAL??");
        return hr;
    }

    static std::wregex global(L"\\\\GLOBAL\\?\\?\\\\", std::regex_constants::icase);
    static std::wregex NPF(
        L"\\\\\\\\.\\\\NPF_\\{[a-zA-Z0-9]{8}-[a-zA-Z0-9]{4}-[a-zA-Z0-9]{4}-[a-zA-Z0-9]{4}-[a-zA-Z0-9]{12}\\}",
        std::regex_constants::icase);
    static std::wregex Interfaces(REGEX_INTERFACE, std::regex_constants::icase);

    for (const auto& obj : objects)
    {
        wstring re = std::regex_replace(obj.Path, global, L"\\\\.\\");

        if (bInterfacesOnly && !std::regex_match(re, Interfaces))
        {
            Log::Debug(L"Skipping !interface object '{}'", re);
            continue;
        }
        if (re.compare(L"\\\\.\\WNVDevice") == 0)
        {
            Log::Warn(L"Skipping known bad device '{}'", re);
            continue;
        }
        if (std::regex_match(re, NPF))
        {
            Log::Warn(L"Skipping known NPF device '{}'", re);
            continue;
        }

        Log::Debug(L"PopulateSystemObjects::CreateFile '{}'", re);

        HANDLE hObj = CreateFile(
            re.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL);

        BOOST_SCOPE_EXIT(&hObj)
        {
            if (hObj != INVALID_HANDLE_VALUE)
                CloseHandle(hObj);
        }
        BOOST_SCOPE_EXIT_END;

        if (hObj == INVALID_HANDLE_VALUE)
            continue;

        STORAGE_PROPERTY_QUERY query;
        ZeroMemory(&query, sizeof(query));
        query.PropertyId = StorageDeviceProperty;

        STORAGE_DEVICE_DESCRIPTOR result;
        result.Version = sizeof(result);
        ZeroMemory(&result, sizeof(result));

        OVERLAPPED overlap;
        ZeroMemory(&overlap, sizeof(overlap));

        DWORD bytesReturned = 0L;
        BOOL bIoComplete = FALSE;

        Log::Debug(L"PopulateSystemObjects::IOCTL_STORAGE_QUERY_PROPERTY '{}'", re);
        if (!DeviceIoControl(
                hObj,
                IOCTL_STORAGE_QUERY_PROPERTY,
                &query,
                sizeof(query),
                &result,
                sizeof(result),
                &bytesReturned,
                &overlap))
        {
            if (GetLastError() == ERROR_IO_PENDING)
            {
                const DWORD PollInterval = 100;  // milliseconds
                const DWORD dwTimeout = 1000;

                for (DWORD waitTimer = 0; waitTimer < dwTimeout; waitTimer += PollInterval)
                {
                    DWORD bytes = 0L;

                    Log::Debug(L"PopulateSystemObjects::GetOverlappedResult '{}'", re);
                    if (GetOverlappedResult(hObj, &overlap, &bytes, FALSE) == FALSE)
                    {
                        if (GetLastError() != ERROR_IO_PENDING)
                        {
                            // The function call failed. ToDo: Error logging and recovery.
                            continue;
                        }
                        Sleep(PollInterval);
                    }
                    else
                    {
                        bIoComplete = TRUE;
                    }
                }

                if (bIoComplete == FALSE)
                {
                    if (pk32)
                    {
                        Log::Debug(L"PopulateSystemObjects::CancelIoEx '{}'", re);
                        BOOL result2 = pk32->CancelIoEx(hObj, &overlap);

                        if (result2 == TRUE || GetLastError() != ERROR_NOT_FOUND)
                        {
                            DWORD bytesReturned2 = 0L;
                            Log::Debug(L"PopulateSystemObjects::GetOverlappedResult(CancelIoEx) '{}'", re);
                            result2 = GetOverlappedResult(hObj, &overlap, &bytesReturned2, TRUE);

                            // ToDo: check result and log errors.
                        }
                    }
                }
            }
        }
        else
        {
            // Succeeded
            bIoComplete = TRUE;
        }

        if (result.BusType <= BusTypeUnknown || result.BusType >= BusTypeMax)
            continue;

        if (bIoComplete)
        {
            // First, look for a valid Volume
            auto reader = make_shared<MountedVolumeReader>(re.c_str());

            Log::Debug(L"PopulateSystemObjects::LoadDiskProperties '{}'", re);
            if (SUCCEEDED(hr = reader->LoadDiskProperties()))
            {
                // We have a volume
                std::vector<std::shared_ptr<Location>> addedLocs;

                if (FAILED(hr = AddLocations(re.c_str(), addedLocs, false)))
                {
                    Log::Error(L"Failed to add system object '{}' [{}]", re, SystemError(hr));
                }
            }
            else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_SYSTEM_LIMITATION))
            {
                // Device is a not supported file system, no need to look for a partition table
            }
            else
            {
                // Look for a partition table to enumerate
                PartitionTable pt;

                Log::Debug(L"PopulateSystemObjects::LoadPartitionTable '{}'", re);
                if (SUCCEEDED(pt.LoadPartitionTable(re.c_str())))
                {
                    for (const auto& part : pt.Table())
                    {
                        if (part.IsValid())
                        {
                            std::wstringstream buffer;

                            buffer << re;

                            if (part.Start > 0LL)
                            {
                                buffer << L",offset=" << part.Start;
                            }
                            if (part.Size > 0LL)
                            {
                                buffer << L",size=" << part.Size;
                            }
                            if (part.SectorSize > 0)
                            {
                                buffer << L",sector=" << part.SectorSize;
                            }

                            auto location = buffer.str();
                            auto reader = make_shared<SystemStorageReader>(location.c_str());

                            Log::Debug(L"PopulateSystemObjects::LoadDiskProperties '{}'", re);
                            if (FAILED(hr = reader->LoadDiskProperties()))
                                continue;

                            std::vector<std::shared_ptr<Location>> addedLocs;

                            if (FAILED(hr = AddLocations(location.c_str(), addedLocs, false)))
                            {
                                Log::Error(L"Failed to add system object '{}' [{}]", re, SystemError(hr));
                            }
                        }
                    }
                }
            }
        }
    }

    if (bInterfacesOnly)
        m_bInterfacesPopulated = true;
    else
        m_bSystemObjectsPopulated = true;

    return S_OK;
}

HRESULT LocationSet::AddLocation(
    const std::wstring& strLocation,
    Location::Type locType,
    const std::wstring& subdir,
    std::shared_ptr<Location>& addedLoc,
    bool bToParse)
{
    auto it = m_Locations.find(strLocation);

    if (it == end(m_Locations))
    {
        // easy, add the Location
        auto pair = std::make_pair<wstring, shared_ptr<Location>>(
            wstring(strLocation), make_shared<Location>(strLocation, locType));
        pair.second->SetParse(bToParse);
        addedLoc = pair.second;

        if (!subdir.empty())
            pair.second->m_SubDirs.push_back(subdir);

        m_Locations.insert(std::move(pair));
        Reset();
    }
    else
    {
        if (locType != it->second->GetType())
        {
            Log::Error(L"Same canonical form, different types for '{}' and '{}'", strLocation, it->first);
            return E_INVALIDARG;
        }
        else
        {
            // What should we do? Probably check for sub dirs.
            it->second->SetParse(bToParse || it->second->GetParse());
            if (!subdir.empty())
                it->second->m_SubDirs.push_back(subdir);
            addedLoc = it->second;
        }
    }
    return S_OK;
}

HRESULT
LocationSet::AddLocation(const std::shared_ptr<Location>& loc, std::shared_ptr<Location>& addedLoc, bool bToParse)
{
    const wstring& canonical = loc->GetLocation();

    auto it = m_Locations.find(canonical);

    if (it == end(m_Locations))
    {
        // easy, add the Location
        auto pair = std::make_pair<wstring, shared_ptr<Location>>(wstring(canonical), std::shared_ptr<Location>(loc));
        pair.second->SetParse(bToParse);
        addedLoc = pair.second;
        m_Locations.insert(std::move(pair));
        Reset();
    }
    else
    {
        if (loc->GetType() != it->second->GetType())
        {
            Log::Error(L"Same canonical form, different types for '{}' and '{}'", canonical, loc->GetLocation());
            addedLoc = nullptr;
            return E_INVALIDARG;
        }
        else
        {
            // merge subdirs
            it->second->SetParse(bToParse || it->second->GetParse());
            it->second->m_SubDirs.insert(end(it->second->m_SubDirs), begin(loc->m_SubDirs), end(loc->m_SubDirs));
            addedLoc = it->second;
        }
    }
    return S_OK;
}

HRESULT LocationSet::AddLocation(
    const std::wstring& location,
    const std::shared_ptr<Location>& loc,
    std::shared_ptr<Location>& addedLoc,
    bool bToParse)
{
    auto it = m_Locations.find(location);

    if (it == end(m_Locations))
    {
        // easy, add the Location
        auto pair = std::make_pair<wstring, shared_ptr<Location>>(wstring(location), std::shared_ptr<Location>(loc));
        pair.second->SetParse(bToParse);
        addedLoc = pair.second;
        m_Locations.insert(std::move(pair));
        Reset();
    }
    else
    {
        if (loc->GetType() != it->second->GetType())
        {
            Log::Error(L"Same canonical form, different types for '{}' and '{}'", location, loc->GetLocation());
            addedLoc = nullptr;
            return E_INVALIDARG;
        }
        else
        {
            // What should we do? Probably check for sub dirs.
            it->second->SetParse(bToParse || it->second->GetParse());
            it->second->m_SubDirs.insert(end(it->second->m_SubDirs), begin(loc->m_SubDirs), end(loc->m_SubDirs));
            addedLoc = it->second;
        }
    }
    return S_OK;
}

HRESULT LocationSet::ParseShadowsForVolume(const std::shared_ptr<Location>& loc)
{
    HRESULT hr = E_FAIL;

    if (loc->GetType() != Location::Type::MountedVolume)
        return S_OK;  // if location is not a mounted volume, no shadow to add

    for (const auto& loc : m_Locations)
    {
        const std::shared_ptr<Location>& location(loc.second);
        if (location->GetType() == Location::Type::Snapshot)
        {
            auto& shadow = location->GetShadow();

            if (shadow != nullptr
                && !location->GetLocation().compare(
                    0, location->GetLocation().length(), shadow->VolumeName, 0, location->GetLocation().length()))
                location->SetParse(true);
        }
    }

    return S_OK;
}

HRESULT LocationSet::ParseAllVolumes()
{
    for (const auto& loc : m_Locations)
    {
        loc.second->SetParse(true);
    }

    return S_OK;
}

std::wstring LocationSet::KnownFolder(DWORD dwCSIDL)
{
    using namespace std::string_literals;
    SpecialFolder* pCur = g_specialFolders;

    if (pCur)
        while (pCur->dwCSIDL != CSIDL_NONE)
        {
            if (pCur->dwCSIDL == dwCSIDL)
            {
                WCHAR Str[ORC_MAX_PATH];

                if (SUCCEEDED(SHGetFolderPath(NULL, pCur->dwCSIDL, NULL, SHGFP_TYPE_CURRENT, Str)))
                    return wstring(Str);
                break;
            }
            pCur++;
        }
    return L""s;
}

std::wstring LocationSet::KnownFolder(WCHAR* szCSIDL)
{
    using namespace std::string_literals;
    SpecialFolder* pCur = g_specialFolders;

    if (pCur)
        while (pCur->dwCSIDL != CSIDL_NONE)
        {
            if (!_wcsicmp(pCur->szCSIDL, szCSIDL))
            {
                WCHAR Str[ORC_MAX_PATH];

                if (SUCCEEDED(SHGetFolderPath(NULL, pCur->dwCSIDL, NULL, SHGFP_TYPE_CURRENT, Str)))
                    return std::wstring(Str);
                break;
            }
            pCur++;
        }
    return L""s;
}

inline bool equiv_Location(const std::wstring& s1, const std::wstring& s2)
{
    if (s1.size() > s2.size())
        return false;
    auto p1 = s1.begin();
    auto p2 = s2.begin();

    while (p1 != s1.end() && p2 != s2.end())
    {
        if (tolower(*p1) != tolower(*p2))
            return false;
        ++p1;
        ++p2;
    }
    return true;
}

HRESULT LocationSet::ValidateLocations(const Locations& locations)
{
    for (auto& locPair : locations)
    {
        ValidateLocation(locPair.second);
    }

    return S_OK;
}

HRESULT LocationSet::ValidateLocation(const std::shared_ptr<Location>& loc)
{
    HRESULT hr = E_FAIL;
    auto reader = loc->GetReader();

    if (reader != nullptr)
    {
        if (SUCCEEDED(hr = reader->LoadDiskProperties()))
        {
            loc->SetIsValid(true);
            return S_OK;
        }
        else if (reader->GetFSType() == FSVBR::FSType::BITLOCKER)
        {
            loc->SetIsValid(true);
            return S_OK;
        }
    }

    return hr;
}

HRESULT LocationSet::EliminateDuplicateLocations()
{
    for_each(begin(m_Locations), end(m_Locations), [](const std::pair<wstring, shared_ptr<Location>>& aPair) {
        vector<wstring>& subdirs = aPair.second->m_SubDirs;

        std::sort(begin(subdirs), end(subdirs), lessCaseInsensitive);

        auto newend = std::unique(begin(subdirs), end(subdirs), equiv_Location);

        subdirs.erase(newend, end(subdirs));
    });
    return S_OK;
}

HRESULT LocationSet::UniqueLocations(FSVBR::FSType filterFSTypes)
{
    if (!m_UniqueLocations.empty())
        return S_OK;

    std::vector<std::shared_ptr<Location>> retval;

    retval.reserve(m_Locations.size());

    std::for_each(
        begin(m_Locations),
        end(m_Locations),
        [&retval, filterFSTypes](const pair<std::wstring, std::shared_ptr<Location>>& aPair) {
            if (aPair.second
                && ((static_cast<std::int32_t>(aPair.second->GetFSType()) & static_cast<std::int32_t>(filterFSTypes))
                    > 0))
                retval.push_back(aPair.second);
        });

    std::sort(begin(retval), end(retval));
    auto new_end = std::unique(begin(retval), end(retval));
    retval.erase(new_end, end(retval));

    std::sort(
        begin(retval),
        end(retval),
        [](const std::shared_ptr<Location>& left, const std::shared_ptr<Location>& right) -> bool {
            if (left->GetType() != right->GetType())
            {
                return left->GetType() < right->GetType();
            }

            if (!left->GetPaths().empty() && !right->GetPaths().empty())
            {
                // both drives are mounted
                return lessCaseInsensitive(left->GetPaths()[0], right->GetPaths()[0]);
            }
            else if (left->GetPaths().empty() && !right->GetPaths().empty())
            {
                // only left is mounted
                return true;
            }
            else if (!left->GetPaths().empty() && right->GetPaths().empty())
            {
                // only right
                return false;
            }
            else
            {
                // Both locations are _not_ mounted as folders or drives
                return lessCaseInsensitive(left->GetIdentifier(), right->GetIdentifier());
            }
        });

    std::swap(m_UniqueLocations, retval);

    return S_OK;
}

HRESULT LocationSet::AltitudeLocations(
    LocationSet::Altitude alt,
    bool bParseShadows,
    const ShadowFilters& shadowFilters,
    const PathExcludes& excludedPaths,
    FSVBR::FSType filterFSTypes)
{
    HRESULT hr = E_FAIL;

    if (!m_AltitudeLocations.empty() && !m_Volumes.empty())
        return S_OK;

    if (FAILED(hr = UniqueLocations(filterFSTypes)))
        return hr;

    std::vector<std::shared_ptr<Location>> retval;

    m_Volumes.reserve(m_UniqueLocations.size());

    for (const auto& loc : m_UniqueLocations)
    {
        if (loc->IsValid())
        {
            if (loc->SerialNumber() != 0LL)
            {
                const auto it = m_Volumes.find(loc->SerialNumber());
                if (it != end(m_Volumes))
                {
                    it->second.Locations.push_back(loc);
                    if (!loc->GetPaths().empty())
                        it->second.Paths.insert(it->second.Paths.end(), begin(loc->GetPaths()), end(loc->GetPaths()));
                    if (!loc->GetSubDirs().empty())
                        it->second.SubDirs.insert(
                            it->second.SubDirs.end(), begin(loc->GetSubDirs()), end(loc->GetSubDirs()));
                    if (loc->GetParse())
                        it->second.Parse = true;
                }
                else
                {
                    std::pair<ULONGLONG, VolumeLocations> pair;

                    pair.first = loc->SerialNumber();
                    pair.second.SerialNumber = loc->SerialNumber();
                    pair.second.Locations.push_back(loc);
                    if (!loc->GetPaths().empty())
                        pair.second.Paths.assign(begin(loc->GetPaths()), end(loc->GetPaths()));
                    if (!loc->GetSubDirs().empty())
                        pair.second.SubDirs.assign(begin(loc->GetSubDirs()), end(loc->GetSubDirs()));
                    if (loc->GetParse())
                        pair.second.Parse = true;

                    m_Volumes.emplace(std::move(pair));
                }
            }
            else if (loc->GetType() == Location::Type::OfflineMFT)
            {
                retval.push_back(loc);
            }
            else if (loc->GetFSType() == FSVBR::FSType::BITLOCKER)
            {
                retval.push_back(loc);
            }
        }
    }

    if (m_Altitude == Altitude::Exact)
    {
        m_AltitudeLocations.assign(begin(m_UniqueLocations), end(m_UniqueLocations));
        return S_OK;
    }

    for (auto& aPair : m_Volumes)
    {
        std::vector<Location::Ptr> excludedLocations;
        ::GetExcludedVolumeLocations(aPair.second, excludedPaths, excludedLocations);

        std::sort(
            begin(aPair.second.Locations),
            end(aPair.second.Locations),
            [alt](std::shared_ptr<Location>& left, std::shared_ptr<Location>& right) -> bool {
                switch (alt)
                {
                    case Altitude::Lowest:
                        return ((DWORD)left->GetType()) < ((DWORD)right->GetType());
                    case Altitude::Highest:
                        return ((DWORD)left->GetType()) > ((DWORD)right->GetType());
                    case Altitude::Exact:
                        return ((DWORD)left->GetType()) < ((DWORD)right->GetType());
                }
                return ((DWORD)left->GetType()) < ((DWORD)right->GetType());
            });

        {
            auto& vec = aPair.second.Paths;
            sort(begin(vec), end(vec));
            vec.erase(unique(vec.begin(), vec.end()), vec.end());
        }
        {
            auto& vec = aPair.second.SubDirs;
            sort(begin(vec), end(vec));
            vec.erase(unique(vec.begin(), vec.end()), vec.end());
        }

        for (const auto& loc : aPair.second.Locations)
        {
            loc->m_Paths.clear();
            loc->m_SubDirs.clear();
        }

        // case Altitude::Lowest|Altitude::Highest
        std::set<Location::Ptr> shadowsSelection;
        if (bParseShadows && !shadowFilters.empty())
        {
            ::FilterLocations(aPair.second.Locations, shadowFilters, shadowsSelection);
        }

        switch (alt)
        {
            case Altitude::Lowest:
            case Altitude::Highest: {
                bool bActualData = false;
                for (const auto& loc : aPair.second.Locations)
                {
                    if (loc->GetType() == Location::Type::Snapshot)
                    {
                        if (bParseShadows)
                        {
                            if (shadowFilters.empty() || shadowsSelection.find(loc) != std::cend(shadowsSelection))
                            {
                                loc->m_Paths.assign(begin(aPair.second.Paths), end(aPair.second.Paths));
                                loc->m_SubDirs.assign(begin(aPair.second.SubDirs), end(aPair.second.SubDirs));
                                loc->m_bParse = aPair.second.Parse;
                                retval.push_back(loc);
                            }
                        }
                    }
                    else if (bActualData == false)
                    {
                        loc->m_Paths.assign(begin(aPair.second.Paths), end(aPair.second.Paths));
                        loc->m_SubDirs.assign(begin(aPair.second.SubDirs), end(aPair.second.SubDirs));
                        loc->m_bParse = aPair.second.Parse && ::NotContain(loc, excludedLocations);
                        retval.push_back(loc);
                        bActualData = true;
                    }
                }
            }
            break;
            case Altitude::Exact:
                for (const auto& loc : aPair.second.Locations)
                {
                    loc->m_Paths.assign(begin(aPair.second.Paths), end(aPair.second.Paths));
                    loc->m_SubDirs.assign(begin(aPair.second.SubDirs), end(aPair.second.SubDirs));
                    loc->m_bParse = aPair.second.Parse && ::NotContain(loc, excludedLocations);
                    retval.push_back(loc);
                }
                break;
        }
    }
    std::swap(m_AltitudeLocations, retval);
    return S_OK;
}

HRESULT LocationSet::Reset()
{
    m_Volumes.clear();
    m_AltitudeLocations.clear();
    m_UniqueLocations.clear();

    return S_OK;
}

HRESULT LocationSet::EliminateUselessLocations(Locations& locations)
{
    bool hasOfflineLocations = false;

    // check first if we are dealing with an image disk location
    std::for_each(
        std::begin(locations), std::end(locations), [&hasOfflineLocations](const Locations::value_type& aPair) {
            if (aPair.second->GetType() == Location::Type::ImageFileDisk
                || aPair.second->GetType() == Location::Type::ImageFileVolume)
            {

                hasOfflineLocations = true;
            }
        });

    // if there is an image disk location then remove locations with a different location type
    if (hasOfflineLocations)
    {
        for (typename Locations::iterator iter = locations.begin();
             (iter = std::find_if(
                  iter,
                  locations.end(),
                  [](const Locations::value_type& aPair) {
                      if (aPair.second->GetType() != Location::Type::ImageFileDisk
                          && aPair.second->GetType() != Location::Type::ImageFileVolume)
                      {
                          return true;
                      }

                      return false;
                  }))
             != locations.end();
             locations.erase(iter++))
            ;
    }

    return S_OK;
}

HRESULT LocationSet::EliminateInvalidLocations(Locations& locations)
{
    for (typename Locations::iterator iter = locations.begin(); (iter = std::find_if(
                                                                     iter,
                                                                     locations.end(),
                                                                     [](const Locations::value_type& aPair) {
                                                                         if (!aPair.second->IsValid())
                                                                         {
                                                                             return true;
                                                                         }

                                                                         return false;
                                                                     }))
         != locations.end();
         locations.erase(iter++))
        ;

    return S_OK;
}

HRESULT LocationSet::IsEmpty(bool bLocToParse)
{
    const auto& uniqueLocations = GetUniqueLocations();

    if (!bLocToParse && !uniqueLocations.empty())
        return S_OK;

    for (const auto& item : uniqueLocations)
    {
        if (item->GetParse())
            return S_OK;
    }
    return S_FALSE;
}

std::vector<std::shared_ptr<Orc::Location>> LocationSet::GetParsedLocations()
{
    std::vector<std::shared_ptr<Orc::Location>> locations;

    for (const auto& location : GetAltitudeLocations())
    {
        if (location->GetParse())
        {
            locations.push_back(location);
        }
    }

    return locations;
}

HRESULT LocationSet::PrintLocation(const std::shared_ptr<Location>& loc, bool logAsDebug, LPCWSTR szIndent) const
{
    std::wstringstream ss;
    ss << *loc;
    Log::Trace(L"{}", ss.str());
    return S_OK;
}

HRESULT LocationSet::PrintLocations(bool bOnlyParsedOnes, LPCWSTR szIndent) const
{
    const auto& locations = GetAltitudeLocations();

    for (const auto& item : locations)
    {
        if ((bOnlyParsedOnes && item->GetParse()) || !bOnlyParsedOnes)
        {
            PrintLocation(item, false);

            if (!item->GetPaths().empty())
            {
                Log::Info(L" ");
                for (const auto& location : item->GetPaths())
                {
                    Log::Info(L"'{}' ", location);
                }
            }

            if (!item->GetSubDirs().empty())
            {
                auto& subdirs = item->GetSubDirs();
                for (const auto& location : subdirs)
                {
                    Log::Info(L"location: {}", location);
                }
            }
        }
    }

    return S_OK;
}

HRESULT LocationSet::PrintLocationsByVolume(bool bOnlyParsedOnes, LPCWSTR szIndent) const
{
    for (const auto& vol : m_Volumes)
    {
        const auto& item = vol.second;

        Log::Info(L"{}Serial Number {:#x}", szIndent, item.SerialNumber);

        if (!item.Paths.empty())
        {
            Log::Info(L" ");
            auto& paths = item.Paths;
            for (const auto& location : paths)
            {
                Log::Info(L"'{}' ", location);
            }
        }

        if (!item.SubDirs.empty())
        {
            Log::Info(L"{}", szIndent);
            LPCWSTR szI = szIndent;
            auto& subdirs = item.SubDirs;
            size_t dwCharCount = 0;
            std::for_each(begin(subdirs), end(subdirs), [this, &dwCharCount, szI](const std::wstring& location) {
                dwCharCount += location.size();
                if (dwCharCount > 60)
                {
                    Log::Info(L"'{}': {}", location, szI);
                    dwCharCount = 0;
                }
                else
                {
                    Log::Info(L"'{}' ", location);
                }
            });
        }
        Log::Info(L"");

        for (const auto& loc : vol.second.Locations)
        {

            if ((bOnlyParsedOnes && loc->GetParse()) || !bOnlyParsedOnes)
            {
                PrintLocation(loc, false, szIndent);
            }
        }
    }

    return S_OK;
}
