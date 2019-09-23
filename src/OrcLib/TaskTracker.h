//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include <WinDef.h>

#include <vector>
#include <set>
#include <string>
#include <map>
#include <memory>

#include "RunningCode.h"
#include "RunningProcesses.h"
#include "AutoRuns.h"
#include "Authenticode.h"
#include "CaseInsensitive.h"
#include "LocationSet.h"

#pragma managed(push, off)

#ifdef NTRACK
struct _TRACEDATA;
#endif

namespace Orc {

struct ProcessData
{
    DWORD Pid;
    DWORD ParentId;
    LARGE_INTEGER LoadTime;
    LARGE_INTEGER UnloadTime;
};

struct DriverData
{
    DWORD LoaderPid;
    LARGE_INTEGER LoadTime;
    PVOID BaseAddress;
    std::wstring Path;
};

struct UserModuleData
{
    DWORD LoaderPid;
    LARGE_INTEGER LoadTime;
    PVOID BaseAddress;
    std::wstring Path;
};

struct NTrackResults
{
    std::vector<ProcessData> processes;
    std::set<std::wstring, CaseInsensitive> paths;
    std::vector<DriverData> drivers;
    std::vector<UserModuleData> usermodules;
};

using PNTrackResults = NTrackResults*;

// A driver:
//	is registered to load (or not)
/// is signed (or not)
//  signature verifies (or not)
//  has been loaded
//  has been unloaded
//  is seen running

// an .EXE can be:
//   is registered to load (or not)
//   is signed (or not)
//   signature verifies (or not)
//   has been loaded
//   has been unloaded
//   is seen running

// an .DLL can be:
//   registered to load (svchost, rundll, etc...)
//   is signed (or not)
//   signature verifies (or not)
//   has been loaded
//   has been unloaded
//   is seen loaded

enum RegisteredStatus
{
    RESUndetermined = 0,
    NotRegistered,
    Registered
};

enum AuthenticodeStatus
{
    ASUndetermined = 0,
    NotSigned,
    SignedVerified,
    SignedNotVerified,
    SignedTampered
};

enum LoadStatus
{
    LSUndetermined = 0,
    Loaded,
    LoadedAndUnloaded
};

enum RunningStatus
{
    RUSUndetermined = 0,
    DoesNotRun,
    Runs
};

struct SampleStatus
{
    RegisteredStatus Registry;
    AuthenticodeStatus Authenticode;
    LoadStatus Loader;
    RunningStatus Running;

    SampleStatus()
        : Registry(RESUndetermined)
        , Authenticode(ASUndetermined)
        , Loader(LSUndetermined)
        , Running(RUSUndetermined) {};

    SampleStatus& operator=(const struct SampleStatus& other)
    {
        if (other.Registry > Registry)
            Registry = other.Registry;
        if (other.Authenticode > Authenticode)
            Authenticode = other.Authenticode;
        if (other.Loader > Loader)
            Loader = other.Loader;
        if (other.Running > Running)
            Running = other.Running;
        return *this;
    };
};

static const WCHAR* szSampleType[] = {L"STUndetermined", L"Driver", L"Process", L"Module", NULL};

enum SampleType
{
    STUndertermined = 0,
    Driver,
    Process,
    Module
};

class SampleItem
{
public:
    SampleStatus Status;
    SampleType Type;
    std::shared_ptr<Location> Location;
    std::wstring FullPath;
    std::wstring Path;
    std::wstring FileName;
    SampleItem()
        : Status()
        , Type(STUndertermined) {};

    SampleItem(SampleItem&& other)
    {
        Status = other.Status;
        other.Status = SampleStatus();
        Type = other.Type;
        other.Type = STUndertermined;
        std::swap(Location, other.Location);
        std::swap(FullPath, other.FullPath);
        std::swap(Path, other.Path);
        std::swap(FileName, other.FileName);
    }
    SampleItem(const SampleItem&) = default;
};

using ShortNameSampleMap = std::multimap<std::wstring, std::shared_ptr<SampleItem>, CaseInsensitive>;
using LocationMap = std::multimap<std::wstring, std::shared_ptr<SampleItem>, CaseInsensitive>;
using FullPathSampleMap = std::map<std::wstring, std::shared_ptr<SampleItem>, CaseInsensitive>;
using SampleInformation = std::vector<std::shared_ptr<SampleItem>>;

static const WCHAR* szEventType[] =
    {L"Undefined", L"DriverLoads", L"ProcessCreated", L"ModuleLoads", L"ModuleUnloads", L"ProcessEnds", NULL};

enum class EventType
{
    Undefined = 0,
    DriverLoads,
    ProcessCreated,
    ModuleLoads,
    ModuleUnloads,
    ProcessEnds
};

struct CausalityItem
{

    DWORD Pid;
    DWORD ParentPid;

    std::shared_ptr<SampleItem> Sample;
    std::shared_ptr<CausalityItem> ParentCausality;
    LARGE_INTEGER Time;
    EventType Type;

    CausalityItem()
    {
        Pid = 0;
        ParentPid = 0;
        Time.QuadPart = 0;
        Type = EventType::Undefined;
    }

    bool operator<(const CausalityItem& item) { return Time.QuadPart < item.Time.QuadPart; }

    bool operator==(const CausalityItem& item)
    {
        if (Pid == item.Pid)
        {
            if (Type == item.Type)
            {
                switch (Type)
                {
                    case EventType::ProcessCreated:
                        return true;
                        break;
                    case EventType::ProcessEnds:
                        return true;
                        break;
                    case EventType::DriverLoads:
                    case EventType::ModuleLoads:
                    case EventType::ModuleUnloads:
                    {
                        CaseInsensitive compare;

                        if (Sample != nullptr && item.Sample != nullptr)
                            return compare(Sample->FullPath, item.Sample->FullPath);
                    }
                    break;
                }
            }
            else
                return false;
        }
        else
            return false;
    }
};

struct ProcessLife
{

    bool operator()(const std::shared_ptr<CausalityItem>& s1, const std::shared_ptr<CausalityItem>& s2) const
    {
        if (s1->Pid == s2->Pid)
        {

            if (s1->Type == s2->Type)
            {
                switch (s1->Type)
                {
                    case EventType::ProcessCreated:
                    case EventType::ProcessEnds:
                        return false;
                    case EventType::DriverLoads:
                    case EventType::ModuleLoads:
                    case EventType::ModuleUnloads:
                        if (s1->Time.QuadPart == s2->Time.QuadPart)
                        {
                            CaseInsensitive compare;

                            if (s1->Sample != nullptr && s2->Sample != nullptr)
                                return compare(s1->Sample->FullPath, s2->Sample->FullPath);
                            else
                                return false;
                        }
                        return s1->Time.QuadPart < s2->Time.QuadPart;
                }
                return false;
            }
            else
                return s1->Type < s2->Type;
        }
        else
            return s1->Pid < s2->Pid;
    };
};

class ORCLIB_API TaskTracker
{
public:
    using TimeLine = std::vector<std::shared_ptr<CausalityItem>>;
    using ProcessLifeLine = std::set<std::shared_ptr<CausalityItem>, ProcessLife>;

private:
    logger _L_;

    std::vector<std::pair<std::wstring, std::wstring>> m_PathMappings;

    bool m_bTkResults;
    NTrackResults m_TkResults;

    bool m_brc;
    RunningCode m_rc;

    bool m_brp;
    RunningProcesses m_rp;

    bool m_bars;
    AutoRuns m_ars;

    Authenticode m_authenticode;

    LocationSet m_Locations;
    std::vector<std::shared_ptr<Location>> m_AltitudeLocations;

    ShortNameSampleMap m_Shortmap;
    FullPathSampleMap m_Fullmap;
    TimeLine m_TimeLine;
    ProcessLifeLine m_ProcessLife;

#ifdef NTRACK
    HRESULT ParserDriverResults(_TRACEDATA* TkResults);
#endif

    HRESULT ReParsePath(const std::wstring& path, std::wstring& newPath);

    bool IsVisibleStillRunning(DWORD Pid, DWORD ParentId);
    bool IsRegisteredTerminated(DWORD Pid, DWORD ParentId);

    bool IsDriverUnloaded(std::wstring path);

    std::shared_ptr<SampleItem> InsertSampleInformation(SampleItem& sample);

public:
    TaskTracker(const logger& pLog)
        : m_rc(pLog)
        , m_bTkResults(false)
        , m_brc(false)
        , m_brp(false)
        , m_rp(pLog)
        , m_bars(false)
        , m_ars(pLog)
        , m_authenticode(pLog)
        , m_Locations(pLog)
        , _L_(pLog) {};

    HRESULT InitializeMappings();
    HRESULT LoadRunningTasksAndModules();
    HRESULT LoadNTrackResults();
    HRESULT LoadAutoRuns(const WCHAR* szAutoRunsPath);
    HRESULT LoadAutoRuns(const CBinaryBuffer& XmlData);
    HRESULT SaveAutoRuns(const std::shared_ptr<ByteStream>& stream);

    HRESULT CoalesceResults(std::vector<std::shared_ptr<SampleItem>>& sampleinfo);

    HRESULT CheckSampleSignatureStatus(const std::shared_ptr<SampleItem>& sample, AuthenticodeStatus& status);
    HRESULT CheckSamplesSignature();

    const std::vector<DriverData>& Drivers() const { return m_TkResults.drivers; }
    const std::vector<UserModuleData>& UserModules() const { return m_TkResults.usermodules; }
    const std::set<std::wstring, CaseInsensitive>& Paths() const { return m_TkResults.paths; }
    const std::vector<ProcessData>& Processes() const { return m_TkResults.processes; }
    const TimeLine& GetTimeLine() const { return m_TimeLine; };
    std::vector<std::shared_ptr<Location>> GetAltitudeLocations() const { return m_AltitudeLocations; };

    bool HasHiddenProcesses();
    bool HasHiddenDrivers();
};
}  // namespace Orc

#pragma managed(pop)
