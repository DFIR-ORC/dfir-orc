//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"
#include "CaseInsensitive.h"
#include "Location.h"

#include <map>
#include <unordered_map>
#include <set>

#pragma managed(push, off)

namespace Orc {

class LocationSet
{
public:
    using ShadowFilters = std::set<std::wstring, CaseInsensitive>;
    using PathExcludes = std::set<std::wstring, CaseInsensitive>;

    typedef std::map<std::wstring, std::shared_ptr<Location>, CaseInsensitive> Locations;

    typedef enum
    {
        NotSet = 0,
        Lowest,
        Exact,
        Highest
    } Altitude;
    static Altitude GetAltitudeFromString(std::wstring_view svAltitude);
    static std::wstring GetStringFromAltitude(Altitude alt);

    static HRESULT ConfigureDefaultAltitude(const Altitude alt);

    static Altitude GetDefaultAltitude();

    typedef struct _VolumeLocations
    {

        ULONGLONG SerialNumber = 0LL;
        std::vector<std::wstring> SubDirs;
        std::vector<std::wstring> Paths;
        std::vector<std::shared_ptr<Location>> Locations;
        bool Parse = false;

        _VolumeLocations() = default;
        _VolumeLocations(_VolumeLocations&& locs) = default;
        _VolumeLocations(const _VolumeLocations& locs) = default;

    } VolumeLocations;

private:
    Locations m_Locations;
    std::unordered_map<ULONGLONG, VolumeLocations> m_Volumes;
    std::vector<std::shared_ptr<Location>> m_UniqueLocations;
    std::vector<std::shared_ptr<Location>> m_AltitudeLocations;
    std::vector<VolumeShadowCopies::Shadow> m_Shadows;

    Altitude m_Altitude = GetDefaultAltitude();

    bool m_bMountedVolumesPopulated = false;
    bool m_bPhysicalDrivesPopulated = false;
    bool m_bShadowsPopulated = false;
    bool m_bSystemObjectsPopulated =
        true;  // by default we don't want system objects to be populated -> way too dangerous
    bool m_bInterfacesPopulated = false;

    Location::Type DeduceLocationType(const WCHAR* szLocation);

    HRESULT PopulateMountedVolumes();
    HRESULT PopulatePhysicalDrives();
    HRESULT PopulateShadows();
    HRESULT PopulateSystemObjects(bool bInterfacesOnly);

    HRESULT AddLocation(
        const std::wstring& location,
        const std::shared_ptr<Location>& loc,
        std::shared_ptr<Location>& addedLoc,
        bool bToParse = true);
    HRESULT AddLocation(
        const std::wstring& strLocation,
        Location::Type locType,
        const std::wstring& subdir,
        std::shared_ptr<Location>& addedLoc,
        bool bToParse = true);

    HRESULT ParseShadowsForVolume(const std::shared_ptr<Location>& loc);
    HRESULT ParseAllVolumes();

    std::wstring KnownFolder(WCHAR* szCSIDL);
    std::wstring KnownFolder(DWORD dwCSIDL);

    HRESULT EliminateDuplicateLocations();
    HRESULT UniqueLocations(FSVBR::FSType filterFSTypes);

    HRESULT AltitudeLocations(
        LocationSet::Altitude alt,
        bool bParseShadows,
        const ShadowFilters& shadows,
        const PathExcludes& excludes,
        FSVBR::FSType filterFSTypes);

    HRESULT Reset();

public:
    LocationSet() {}

    LocationSet(LocationSet&& anOther)
    {
        std::swap(m_Locations, anOther.m_Locations);
        m_bMountedVolumesPopulated = anOther.m_bMountedVolumesPopulated;
        m_bPhysicalDrivesPopulated = anOther.m_bPhysicalDrivesPopulated;
        m_bShadowsPopulated = anOther.m_bShadowsPopulated;
        m_bSystemObjectsPopulated = anOther.m_bSystemObjectsPopulated;
    };

    LocationSet(const LocationSet& anOther) = default;
    LocationSet& operator=(const LocationSet&) = default;

    // accessors
    const Locations& GetLocations() const { return m_Locations; }
    std::vector<std::shared_ptr<Orc::Location>> GetParsedLocations();

    const std::vector<std::shared_ptr<Location>>& GetUniqueLocations() const { return m_UniqueLocations; };
    const std::vector<std::shared_ptr<Location>>& GetAltitudeLocations() const { return m_AltitudeLocations; };

    const std::unordered_map<ULONGLONG, VolumeLocations>& GetVolumes() const { return m_Volumes; }

    Altitude& GetAltitude() { return m_Altitude; }

    // enumerate / add location	methods
    HRESULT CanonicalizeLocation(
        const WCHAR* szLocation,
        std::wstring& canonicalLocation,
        Location::Type& locType,
        std::wstring& subpath);
    HRESULT EnumerateLocations();

    // configure enumeration
    void SetPopulateMountedVolumes(bool bPopulate) { m_bMountedVolumesPopulated = !bPopulate; }
    void SetPopulatePhysicalDrives(bool bPopulate) { m_bPhysicalDrivesPopulated = !bPopulate; }
    void SetPopulateSystemObjects(bool bPopulate) { m_bSystemObjectsPopulated = !bPopulate; }
    void SetPopulateShadows(bool bPopulate) { m_bShadowsPopulated = !bPopulate; }

    HRESULT
    AddLocation(const std::shared_ptr<Location>& loc, std::shared_ptr<Location>& addedLoc, bool bToParse = true);

    HRESULT
    AddLocations(const WCHAR* szLocation, std::vector<std::shared_ptr<Location>>& addedLocs, bool bToParse = true);

    HRESULT AddLocationsFromConfigItem(const ConfigItem& config);
    HRESULT AddLocationsFromArgcArgv(int argc, LPCWSTR argv[]);
    HRESULT AddKnownLocations(const ConfigItem& item);
    HRESULT AddKnownLocations();

    HRESULT
    Consolidate(
        bool bParseShadows,
        const ShadowFilters& shadows,
        const PathExcludes& excludes,
        FSVBR::FSType filterFSTypes)
    {
        Reset();

        // remove duplicate and useless locations (for example if we are dealing with an image disk then keep only the
        // location of the image disk)
        EliminateDuplicateLocations();
        EliminateUselessLocations(m_Locations);

        // try to valid all locations and remove invalid locations
        ValidateLocations(m_Locations);
        EliminateInvalidLocations(m_Locations);

        AltitudeLocations(m_Altitude, bParseShadows, shadows, excludes, filterFSTypes);

        return S_OK;
    }

    HRESULT Consolidate(bool bParseShadows, FSVBR::FSType filterFSTypes)
    {
        LocationSet::ShadowFilters shadows;
        LocationSet::PathExcludes excludes;
        return Consolidate(bParseShadows, shadows, excludes, filterFSTypes);
    }

    static HRESULT EliminateUselessLocations(Locations& locations);
    static HRESULT EliminateInvalidLocations(Locations& locations);

    static HRESULT ValidateLocations(const Locations& locations);
    static HRESULT ValidateLocation(const std::shared_ptr<Location>& loc);

    HRESULT IsEmpty(bool bLocToParse = true);

    // print locations
    HRESULT PrintLocation(const std::shared_ptr<Location>& loc, bool logAsDebug, LPCWSTR szIndent = L"") const;
    HRESULT PrintLocations(bool bOnlyParsedOnes, LPCWSTR szIndent = L"") const;
    HRESULT PrintLocationsByVolume(bool bOnlyParsedOnes, LPCWSTR szIndent = L"") const;
};

}  // namespace Orc

#pragma managed(pop)
