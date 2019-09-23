//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#include "stdafx.h"

#include <WinIoCtl.h>

#include <algorithm>
#include <regex>
#include <cassert>

#include "OrcLib.h"

#ifdef NTRACK
#    include "NTrack.h"
#endif

#include "TaskTracker.h"
#include "LogFileWriter.h"

#include "Authenticode.h"

#include "CaseInsensitive.h"

using namespace std;

using namespace Orc;

#ifdef NTRACK
HRESULT TaskTracker::ParserDriverResults(PTRACEDATA TkResults)
{
    // temp map to 'remap' NTrack hashes to paths
    map<ULONG, wstring> tempMap;

    NTrackResults& results = m_TkResults;

    // building unique paths map
    m_TkResults.paths.clear();
    std::for_each(
        TkResults->UniqBinaryPath,
        TkResults->UniqBinaryPath + TkResults->UniqBinaryPathIndex,
        [&results, &tempMap](const PATH_DATA& item) {
            tempMap.insert(pair<ULONG, wstring>(item.PathHash, item.BinaryPath));
            results.paths.insert(item.BinaryPath);
        });

    InitializeMappings();

    std::set<std::wstring, CaseInsensitive> newset;

    for (std::set<std::wstring, CaseInsensitive>::iterator it = begin(results.paths); it != end(results.paths); ++it)
    {
        wstring new_path;
        if (ReParsePath(*it, new_path) == S_OK)
            newset.insert(new_path);
        else
            newset.insert(*it);
    }

    results.paths = newset;

    // adding drivers
    results.drivers.clear();
    results.drivers.reserve(TkResults->UniqDriversIndex);

    std::for_each(
        TkResults->UniqDrivers,
        TkResults->UniqDrivers + TkResults->UniqDriversIndex,
        [&results, tempMap](const ITEMDATA& item) {
            auto it = tempMap.find(item.PathHash);

            wstring temp;

            if (it == tempMap.end())
                temp = L"(null)";
            else
                temp = it->second;

            DriverData data;
            data.Path = std::move(temp);
            data.LoaderPid = item.Pid;
            data.BaseAddress = item.BaseAddress;
            data.LoadTime = item.LoadTime;
            results.drivers.push_back(data);
        });

    std::sort(results.drivers.begin(), results.drivers.end(), [](const DriverData& item1, const DriverData& item2) {
        return item1.Path < item2.Path;
    });

    // adding user mode modules info
    results.usermodules.clear();
    results.usermodules.reserve(TkResults->UniqUserModeIndex);

    std::for_each(
        TkResults->UniqUserModeBinaries,
        TkResults->UniqUserModeBinaries + TkResults->UniqUserModeIndex,
        [&results, tempMap](const ITEMDATA& item) {
            auto it = tempMap.find(item.PathHash);

            wstring temp;

            if (it == tempMap.end())
                temp = L"(null)";
            else
                temp = it->second;

            UserModuleData data;
            data.Path = temp;
            data.BaseAddress = item.BaseAddress;
            data.LoaderPid = item.Pid;
            data.LoadTime = item.LoadTime;

            results.usermodules.push_back(data);
        });

    // adding process related information
    results.processes.clear();
    results.processes.reserve(TkResults->AllProcessesIndex);

    std::for_each(
        TkResults->AllProcesses,
        TkResults->AllProcesses + TkResults->AllProcessesIndex,
        [&results, tempMap](const PROCESS_DATA& item) {
            ProcessData data;
            data.LoadTime = item.LoadTime;
            data.UnloadTime = item.UnloadTime;
            data.ParentId = item.ParentId;
            data.Pid = item.Pid;

            results.processes.push_back(data);
        });

    return S_OK;
}
#endif

HRESULT TaskTracker::LoadRunningTasksAndModules()
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_rc.EnumRunningCode()))
        return hr;
    m_brc = true;
    if (FAILED(hr = m_rp.EnumProcesses()))
        return hr;
    m_brp = true;
    return S_OK;
}

#ifdef NTRACK
HRESULT TaskTracker::LoadNTrackResults()
{
    HRESULT hr = E_FAIL;
    //
    // open the device
    //

    HANDLE hDevice =
        CreateFile(DRIVE_SYMB_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hDevice == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        log::Verbose(_L_, L"NTrack is not installed or not started\r\nCreateFile failed! (hr=0x%lx)\r\n", hr);
        return hr;
    }
    //
    // Printing Input & Output buffer pointers and size
    //

    PTRACEDATA TkResults = (PTRACEDATA)VirtualAlloc(NULL, sizeof(TRACEDATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!TkResults)
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"VirtualAlloc Failed\r\n");
        CloseHandle(hDevice);
        return hr;
    }

    ZeroMemory(TkResults, sizeof(_TRACEDATA));
    ULONG bytesReturned = 0;

    if (!DeviceIoControl(hDevice, (DWORD)IOCTL_GET_DATA, NULL, 0, TkResults, sizeof(TRACEDATA), &bytesReturned, NULL))
    {
        log::Error(_L_, hr = HRESULT_FROM_WIN32(GetLastError()), L"Error in DeviceIoControl\r\n");
        VirtualFree(TkResults, 0L, MEM_RELEASE);
        CloseHandle(hDevice);
        return hr;
    }
    CloseHandle(hDevice);

    log::Verbose(_L_, L"inBuffer (%d)\r\n", sizeof(_TRACEDATA));
    log::Verbose(_L_, L"OutBuffer (%d)\r\n", bytesReturned);

    if (TkResults->bCollisionsDetected == TRUE)
    {
        log::Error(
            _L_, E_FAIL, L"NTrack detected a collision in image path storage.\r\nInformation may not be accurate.\r\n");
        return E_FAIL;
    }

    if (FAILED(hr = ParserDriverResults(TkResults)))
    {
        VirtualFree(TkResults, 0L, MEM_RELEASE);
        return hr;
    }

    VirtualFree(TkResults, 0L, MEM_RELEASE);
    m_bTkResults = true;

    return S_OK;
}
#else
HRESULT TaskTracker::LoadNTrackResults()
{
    return S_OK;
}
#endif

HRESULT TaskTracker::LoadAutoRuns(const WCHAR* szPath)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_ars.LoadAutoRunsXml(szPath)))
        return hr;
    m_bars = true;
    return S_OK;
}

HRESULT TaskTracker::LoadAutoRuns(const CBinaryBuffer& XmlData)
{
    HRESULT hr = E_FAIL;

    if (FAILED(hr = m_ars.LoadAutoRunsXml(XmlData)))
        return hr;
    m_bars = true;
    return S_OK;
}

HRESULT TaskTracker::SaveAutoRuns(const std::shared_ptr<ByteStream>& stream)
{
    if (m_bars)
    {
        auto xmlstream = m_ars.GetXmlDataStream();

        if (xmlstream->GetSize() > 0LL)
        {
            HRESULT hr = E_FAIL;
            ULONGLONG cbWritten = 0LL;

            if (FAILED(hr = xmlstream->CopyTo(stream, &cbWritten)))
            {
                log::Error(
                    _L_,
                    hr,
                    L"Did not write complete data for autoruns (written=%I64d, expected=%I64d)\r\n",
                    cbWritten,
                    xmlstream->GetSize());
                return hr;
            }

            if (cbWritten != xmlstream->GetSize())
            {
                log::Error(
                    _L_,
                    E_FAIL,
                    L"Did not write complete data for autoruns (written=%I64d, expected=%I64d)\r\n",
                    cbWritten,
                    xmlstream->GetSize());
                return E_FAIL;
            }
        }
    }
    else
    {
        log::Error(_L_, E_UNEXPECTED, L"No autoruns data to save\r\n");
        return E_UNEXPECTED;
    }
    return S_OK;
}

HRESULT TaskTracker::InitializeMappings()
{
    HRESULT hr = E_FAIL;

    DWORD dwDrives = GetLogicalDrives();

    m_PathMappings.clear();

    {
        // Replace "\SystemRoot" with the expansion of %SystemRoot%
        DWORD dwLen = ExpandEnvironmentStrings(L"%SystemRoot%", NULL, 0L);
        auto szExpandedSystemRoot = (WCHAR*)calloc((dwLen + 1), sizeof(WCHAR));
        if (szExpandedSystemRoot == nullptr)
            return E_OUTOFMEMORY;
        dwLen = ExpandEnvironmentStrings(L"%SystemRoot%", szExpandedSystemRoot, dwLen + 1);
        m_PathMappings.push_back(pair<wstring, wstring>(wstring(szExpandedSystemRoot), wstring(L"\\SystemRoot")));
        free(szExpandedSystemRoot);
    }

    {
        // Replace "\Windows" with the expansion of %SystemRoot%
        DWORD dwLen = ExpandEnvironmentStrings(L"%SystemRoot%", NULL, 0L);
        auto szExpandedSystemRoot = (WCHAR*)calloc((dwLen + 1), sizeof(WCHAR));
        if (szExpandedSystemRoot == nullptr)
            return E_OUTOFMEMORY;

        dwLen = ExpandEnvironmentStrings(L"%SystemRoot%", szExpandedSystemRoot, dwLen + 1);
        m_PathMappings.push_back(pair<wstring, wstring>(wstring(szExpandedSystemRoot), wstring(L"\\WINDOWS")));
        free(szExpandedSystemRoot);
    }

    {
        // Replace "\System32" with the expansion of %SystemRoot%\System32
        DWORD dwLen = ExpandEnvironmentStrings(L"%SystemRoot%\\System32", NULL, 0L);
        auto szExpandedSystemRoot = (WCHAR*)calloc((dwLen + 1), sizeof(WCHAR));
        if (szExpandedSystemRoot == nullptr)
            return E_OUTOFMEMORY;

        dwLen = ExpandEnvironmentStrings(L"%SystemRoot%\\System32", szExpandedSystemRoot, dwLen + 1);
        m_PathMappings.push_back(pair<wstring, wstring>(wstring(szExpandedSystemRoot), wstring(L"\\System32")));
        free(szExpandedSystemRoot);
    }

    {
        // Replace \Program Files with the expansion of %ProgramFiles%
        DWORD dwLen = ExpandEnvironmentStrings(L"%ProgramFiles%", NULL, 0L);
        auto szExpandedProgramFiles = (WCHAR*)malloc((dwLen + 1) * sizeof(WCHAR));
        if (szExpandedProgramFiles == nullptr)
            return E_OUTOFMEMORY;
        dwLen = ExpandEnvironmentStrings(L"%ProgramFiles%", szExpandedProgramFiles, dwLen + 1);
        m_PathMappings.push_back(pair<wstring, wstring>(wstring(szExpandedProgramFiles), wstring(L"\\Program Files")));
        free(szExpandedProgramFiles);
    }

    {
        // Replace \Documents and Settings\All Users with the expansion of %ALLUSERSPROFILE%
        DWORD dwLen = ExpandEnvironmentStrings(L"%ALLUSERSPROFILE%", NULL, 0L);
        auto szExpandedAllUsers = (WCHAR*)malloc((dwLen + 1) * sizeof(WCHAR));
        if (szExpandedAllUsers == nullptr)
            return E_OUTOFMEMORY;
        dwLen = ExpandEnvironmentStrings(L"%ALLUSERSPROFILE%", szExpandedAllUsers, dwLen + 1);
        m_PathMappings.push_back(
            pair<wstring, wstring>(wstring(szExpandedAllUsers), wstring(L"\\Documents and Settings\\All Users")));
        free(szExpandedAllUsers);
    }

    {
        // Remove "\??\"
        m_PathMappings.push_back(pair<wstring, wstring>(wstring(L""), wstring(L"\\??\\")));
    }

    {
        // Remove "File not found: " (from autoruns)
        m_PathMappings.push_back(pair<wstring, wstring>(wstring(L""), wstring(L"File not found: ")));
    }

    for (wchar_t i = 0; i < 26; i++)
    {
        if (dwDrives & 1 << i)
        {
            WCHAR szPath[MAX_PATH];

            wstring szDevice(L"A:");
            szDevice[0] = L'A' + i;

            QueryDosDevice(szDevice.c_str(), szPath, MAX_PATH);

            m_PathMappings.push_back(pair<wstring, wstring>(szDevice, wstring(szPath)));
        }
    }

    m_Locations.EnumerateLocations();
    m_Locations.Consolidate(false, FSVBR::FSType::NTFS);
    m_AltitudeLocations = m_Locations.GetAltitudeLocations();

    return hr;
}

HRESULT TaskTracker::ReParsePath(const std::wstring& path, std::wstring& new_path)
{
    bool matched = false;
    for_each(
        m_PathMappings.begin(),
        m_PathMappings.end(),
        [&path, &new_path, &matched](const pair<wstring, wstring>& mapping) {
            if (!path.compare(0, mapping.second.length(), mapping.second))
            {
                new_path = path;
                new_path.replace(
                    begin(new_path),
                    begin(new_path) + mapping.second.length(),
                    begin(mapping.first),
                    end(mapping.first));
                matched = true;
            }
        });
    return matched ? S_OK : S_FALSE;
}

shared_ptr<SampleItem> TaskTracker::InsertSampleInformation(SampleItem& sample)
{
    shared_ptr<SampleItem> result;

    if (sample.FullPath.empty())
        return result;

    wstring filename;
    if (sample.FileName.empty())
    {
        WCHAR fname[_MAX_FNAME];
        WCHAR ext[_MAX_EXT];
        errno_t err;

        err = _wsplitpath_s(sample.FullPath.c_str(), NULL, 0, NULL, 0, fname, _MAX_FNAME, ext, _MAX_EXT);
        if (err != 0)
        {
            // TODO: create a error message list.
        }
        filename = fname;
        filename.append(ext);
    }
    else
    {
        filename = sample.FileName;
    }

    auto iter = m_Fullmap.find(sample.FullPath);
    if (iter == m_Fullmap.end())
    {
        // item not found, easy! we add it
        sample.FileName = std::move(filename);

        auto retval = m_Fullmap.insert(std::make_pair(sample.FullPath, make_shared<SampleItem>(sample)));

        result = retval.first->second;
    }
    else
    {
        // item found. checking for incompatibilities
        if (sample.Type != iter->second->Type)
        {
            if (iter->second->Type == STUndertermined)
                iter->second->Type = sample.Type;
        }
        result = iter->second;
    }

    // now determine the location of the sample
    for (const auto& loc : m_AltitudeLocations)
    {

        if (equalCaseInsensitive(sample.FullPath, loc->GetLocation(), loc->GetLocation().size()))
        {
            result->Path = sample.FullPath.substr(
                loc->GetLocation().length() - 1, sample.FullPath.length() + 1 - loc->GetLocation().length());
            result->Location = loc;
        }
        else
        {
            for (const auto& aPath : loc->GetPaths())
            {
                if (equalCaseInsensitive(sample.FullPath, aPath, aPath.size()))
                {
                    result->Path =
                        sample.FullPath.substr(aPath.length() - 1, sample.FullPath.length() + 1 - aPath.length());
                    result->Location = loc;
                }
            }
        }
    }

    // Now the short map insertion
    auto shortiter = m_Shortmap.find(filename);
    if (shortiter == m_Shortmap.end())
    {
        // item not found, easy! we add it
        m_Shortmap.insert(ShortNameSampleMap::value_type(result->FileName, result));
    }
    else
    {
        // this shortname already is present, checking full paths.
        auto er_iter = m_Shortmap.equal_range(filename);
        bool matched = false;

        std::for_each(
            er_iter.first, er_iter.second, [this, filename, result, &matched](ShortNameSampleMap::value_type& item) {
                if (item.second->FullPath.size() == result->FullPath.size()
                    && std::equal(
                        begin(item.second->FullPath),
                        end(item.second->FullPath),  // source range
                        begin(result->FullPath),  // dest range
                        [this](const wchar_t& c1, const wchar_t& c2) -> bool { return tolower(c1) == tolower(c2); }))
                {
                    matched = true;
                    // check if results are the same.
                    if (item.second != result)
                    {
                        // TODO: Add Error List
                        log::Info(
                            _L_,
                            L"INFO: We could not find the %s item in the short map\r\n",
                            item.second->FullPath.c_str());
                    }
                    else
                    {
                        // All OK, we matched :-)
                    }
                }
            });

        if (!matched)
        {
            // name was not found, inserting.
            m_Shortmap.insert(ShortNameSampleMap::value_type(result->FileName, result));
        }
    }
    result->Status = sample.Status;
    return result;
}

HRESULT TaskTracker::CoalesceResults(vector<shared_ptr<SampleItem>>& samplesinfo)
{
    HRESULT hr = E_FAIL;

    //
    // First let's create sample information (sample about the code files we'll be working on
    //
    if (m_bTkResults)
    {
        // Creating sample information for drivers
        std::for_each(m_TkResults.drivers.begin(), m_TkResults.drivers.end(), [this](const DriverData& item) {
            SampleItem sample;
            sample.FullPath = item.Path;
            sample.Type = Driver;

            if (auto result = InsertSampleInformation(sample))
                result->Status.Loader = Loaded;
        });

        // Creating sample information for loaded modules
        std::for_each(
            m_TkResults.usermodules.begin(), m_TkResults.usermodules.end(), [this](const UserModuleData& item) {
                SampleItem sample;
                sample.FullPath = item.Path;
                sample.Type = Module;

                if (auto result = InsertSampleInformation(sample))
                    result->Status.Loader = Loaded;
            });
    }

    if (m_brc)
    {
        // Adding sample information about runnig code
        Modules modules;

        if (FAILED(hr = m_rc.GetUniqueModules(modules)))
            return hr;

        std::for_each(begin(modules), end(modules), [this](ModuleInfo& info) {
            HRESULT hr = E_FAIL;
            SampleItem item;

            wstring new_path;
            if (FAILED(hr = ReParsePath(info.strModule, new_path)))
            {
                log::Error(_L_, hr, L"Failed while reparsing %s path\r\n", info.strModule.c_str());
            }
            else if (hr == S_OK)
                info.strModule = new_path;

            item.FullPath = info.strModule;

            if (item.Type == MODULETYPE_SYS)
                item.Type = Driver;
            else
                item.Type = Module;

            if (auto result = InsertSampleInformation(item))
                result->Status.Running = Runs;
        });
    }

    if (m_bars)
    {
        // Adding autoruns to sample information
        AutoRunsVector autoruns;
        if (FAILED(hr = m_ars.GetAutoRuns(autoruns)))
            return hr;

        for (const auto& item : autoruns)
        {

            if (wcslen(item.ImagePath) > 0)
            {
                SampleItem sample;

                wstring new_path;
                if (FAILED(hr = ReParsePath(item.ImagePath, new_path)))
                {
                    log::Error(_L_, hr, L"Failed while reparsing %s path\r\n", item.ImagePath);
                }
                else
                {
                    if (hr == S_OK)
                        sample.FullPath = new_path;
                    else
                        sample.FullPath = item.ImagePath;

                    if (auto result = InsertSampleInformation(sample))
                    {
                        result->Status.Registry = Registered;
                        if (item.IsVerified)
                            result->Status.Authenticode = SignedVerified;
                    }
                }
            }
        }
    }

    //
    // Now creating ProcessLife related data
    //

    // First the process "0" life... mostly drivers
    std::for_each(m_TkResults.drivers.begin(), m_TkResults.drivers.end(), [this](const DriverData& item) {
        auto causality = make_shared<CausalityItem>();
        if (!causality)
            return;

        causality->ParentCausality = nullptr;
        causality->Pid = item.LoaderPid;
        causality->Time = item.LoadTime;
        causality->Type = EventType::DriverLoads;
        auto sample = m_Fullmap.find(item.Path);

        if (sample != m_Fullmap.end())
        {
            causality->Sample = sample->second;
        }

        m_ProcessLife.insert(causality);
    });

    // Then Adding process life information (process created, process ends data)

    auto parent = make_shared<CausalityItem>();

    std::for_each(m_TkResults.processes.begin(), m_TkResults.processes.end(), [this, parent](const ProcessData& data) {
        auto create_item = make_shared<CausalityItem>();
        create_item->Pid = data.Pid;
        create_item->ParentPid = data.ParentId;
        create_item->Time = data.LoadTime;
        create_item->Type = EventType::ProcessCreated;

        parent->Pid = data.ParentId;
        parent->Type = EventType::ProcessCreated;

        m_ProcessLife.insert(create_item);

        if (data.UnloadTime.QuadPart != 0)
        {
            auto end_item = make_shared<CausalityItem>();

            end_item->Pid = data.Pid;
            end_item->ParentPid = data.ParentId;
            end_item->Time = data.UnloadTime;
            end_item->Type = EventType::ProcessEnds;
            end_item->ParentCausality = create_item->ParentCausality;

            m_ProcessLife.insert(end_item);
        }
    });

    if (m_brp)
    {
        // In case we don't have TKTracker results, we need to add Running Processes information to process life
        ProcessVector runningprocesses;
        if (FAILED(hr = m_rp.GetProcesses(runningprocesses)))
        {
            log::Error(_L_, hr, L"Could not list running processes\r\n");
            return hr;
        }

        auto process_item = make_shared<CausalityItem>();

        std::for_each(
            runningprocesses.cbegin(), runningprocesses.cend(), [this, &process_item, parent](const ProcessInfo& item) {
                process_item->Pid = item.m_Pid;
                process_item->Type = EventType::ProcessCreated;

                auto iter = m_ProcessLife.insert(process_item);

                if (iter.second)
                {
                    process_item = make_shared<CausalityItem>();
                    // newly inserted item, we try to add more info
                    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, item.m_Pid);
                    if (hProcess != NULL)
                    {
                        FILETIME CreateTime, EndTime, UserTime, KernelTime;
                        if (GetProcessTimes(hProcess, &CreateTime, &EndTime, &KernelTime, &UserTime))
                        {
                            (*iter.first)->Time.LowPart = CreateTime.dwLowDateTime;
                            (*iter.first)->Time.HighPart = CreateTime.dwHighDateTime;
                        }
                        CloseHandle(hProcess);
                    }
                    (*iter.first)->ParentPid = item.m_ParentPid;
                }
            });
    }

    // We do iterate again, this time to create parenting causalities
    std::for_each(
        m_ProcessLife.cbegin(), m_ProcessLife.cend(), [this, parent](const std::shared_ptr<CausalityItem>& item) {
            parent->Pid = item->ParentPid;
            parent->Type = EventType::ProcessCreated;

            auto iter = m_ProcessLife.find(parent);
            if (iter != m_ProcessLife.end())
            {
                item->ParentCausality = *iter;
            }
        });

    if (m_bTkResults)
    {
        // Then, create process life information about the files these processes did load
        std::for_each(
            m_TkResults.usermodules.begin(), m_TkResults.usermodules.end(), [this, parent](const UserModuleData& data) {
                auto moduleload_item = make_shared<CausalityItem>();

                moduleload_item->Pid = data.LoaderPid;
                moduleload_item->Type = EventType::ModuleLoads;
                moduleload_item->Time = data.LoadTime;

                auto sample = m_Fullmap.find(data.Path);
                if (sample != m_Fullmap.end())
                {
                    moduleload_item->Sample = sample->second;
                }

                parent->Pid = data.LoaderPid;
                parent->Type = EventType::ProcessCreated;

                auto iter = m_ProcessLife.find(parent);
                if (iter != m_ProcessLife.end())
                {
                    moduleload_item->ParentCausality = *iter;
                }

                m_ProcessLife.insert(moduleload_item);
            });
    }

    if (m_brc)
    {
        // Then, in case TKTracker is not available, the currently running modules
        Modules modules;

        if (FAILED(hr = m_rc.GetUniqueModules(modules, MODULETYPE_ALL)))
        {
            log::Error(_L_, hr, L"Failed to enumerate loaded modules\r\n");
            return hr;
        }

        std::for_each(begin(modules), end(modules), [this](ModuleInfo& item) {
            HRESULT hr = E_FAIL;
            wstring new_path;
            if (FAILED(hr = ReParsePath(item.strModule, new_path)))
            {
                log::Error(_L_, hr, L"Failed while reparsing %s path\r\n", item.strModule.c_str());
            }
            else if (hr == S_OK)
                item.strModule = new_path;
        });

        std::for_each(modules.cbegin(), modules.cend(), [this, parent](const ModuleInfo& item) {
            std::for_each(item.Pids.cbegin(), item.Pids.cend(), [this, parent, item](const DWORD dwPid) {
                auto moduleload_item = make_shared<CausalityItem>();
                moduleload_item->Pid = dwPid;
                moduleload_item->Type = EventType::ModuleLoads;

                auto sample = m_Fullmap.find(item.strModule);
                if (sample != m_Fullmap.end())
                {
                    moduleload_item->Sample = sample->second;
                }
                else
                {
                    log::Error(_L_, E_FAIL, L"Could not find module %s in full map\r\n", item.strModule.c_str());
                }

                auto iter_item = m_ProcessLife.insert(moduleload_item);

                if ((*iter_item.first)->ParentCausality == nullptr)
                {
                    parent->Pid = dwPid;
                    parent->Type = EventType::ProcessCreated;

                    auto iter_parent = m_ProcessLife.find(parent);
                    if (iter_parent != m_ProcessLife.end())
                    {
                        (*iter_item.first)->ParentCausality = *iter_parent;
                    }
                }
                if ((*iter_item.first)->Time.QuadPart == 0 && (*iter_item.first)->ParentCausality)
                {
                    (*iter_item.first)->Time.QuadPart = (*iter_item.first)->ParentCausality->Time.QuadPart;
                }
            });
        });
        /// Creating Module unloaded causalities
        auto iter = m_ProcessLife.begin();
        do
        {
            if ((*iter)->Type == EventType::ProcessCreated)
            {
                DWORD dwPid = (*iter)->Pid;

                std::vector<shared_ptr<CausalityItem>> modulesloaded;
                shared_ptr<CausalityItem> processexe;

                ++iter;

                while (iter != m_ProcessLife.end() && (*iter)->Type == EventType::ModuleLoads && (*iter)->Pid == dwPid)
                {
                    modulesloaded.push_back(*iter);
                    ++iter;
                }

                if (iter != m_ProcessLife.end() && (*iter)->Pid == dwPid && (*iter)->Type == EventType::ProcessEnds)
                {

                    std::for_each(
                        modulesloaded.begin(),
                        modulesloaded.end(),
                        [iter, dwPid, this](const shared_ptr<CausalityItem>& item) {
                            auto unloaded = make_shared<CausalityItem>();

                            unloaded->ParentCausality = *iter;
                            unloaded->Pid = dwPid;
                            unloaded->Sample = item->Sample;
                            unloaded->Time = (*iter)->Time;
                            unloaded->Type = EventType::ModuleUnloads;

                            m_ProcessLife.insert(unloaded);
                        });
                }
                else
                {
                    // Process may not have ended yet...
                }

                modulesloaded.clear();
            }

            if (iter != m_ProcessLife.end())
                ++iter;

        } while (iter != m_ProcessLife.end());
    }

    m_TimeLine.reserve(m_ProcessLife.size());

    auto backinserter = back_inserter(m_TimeLine);

    std::copy(m_ProcessLife.begin(), m_ProcessLife.end(), backinserter);
    std::sort(
        m_TimeLine.begin(),
        m_TimeLine.end(),
        [](const shared_ptr<CausalityItem>& i1, const shared_ptr<CausalityItem>& i2) -> bool {
            return i1->Time.QuadPart < i2->Time.QuadPart;
        });

    samplesinfo.clear();
    samplesinfo.reserve(m_Fullmap.size());

    std::for_each(
        m_Fullmap.cbegin(), m_Fullmap.cend(), [this, &samplesinfo](const FullPathSampleMap::value_type& item) {
            if (item.second->Status.Loader == LSUndetermined && m_brc)
            {
                if (item.second->Status.Running == Runs)
                    item.second->Status.Loader = Loaded;
            }
            if (item.second->Status.Running == LSUndetermined && m_brc)
            {
                item.second->Status.Running = DoesNotRun;
            }

            samplesinfo.push_back(item.second);
        });

    return S_OK;
}

HRESULT TaskTracker::CheckSampleSignatureStatus(const std::shared_ptr<SampleItem>& sample, AuthenticodeStatus& status)
{
    HRESULT hr = E_FAIL;

    if (sample->Status.Authenticode != ASUndetermined)
    {
        status = sample->Status.Authenticode;
        return S_OK;
    };

    Authenticode::AuthenticodeData data;

    if (FAILED(hr = m_authenticode.Verify(sample->FullPath.c_str(), data)))
    {
        log::Error(_L_, hr, L"Verify signature failed for %s\r\n", sample->FullPath.c_str());
    }
    else
    {
        if (data.isSigned && data.bSignatureVerifies)
            sample->Status.Authenticode = SignedVerified;
        if (data.isSigned && !data.bSignatureVerifies)
            sample->Status.Authenticode = SignedNotVerified;
        if (!data.isSigned)
            sample->Status.Authenticode = NotSigned;
        status = sample->Status.Authenticode;
    }

    return S_OK;
}

HRESULT TaskTracker::CheckSamplesSignature()
{
    std::for_each(m_Fullmap.cbegin(), m_Fullmap.cend(), [this](const FullPathSampleMap::value_type& item) {
        AuthenticodeStatus status;
        CheckSampleSignatureStatus(item.second, status);
    });
    m_authenticode.CloseCatalogState();
    return S_OK;
}

bool TaskTracker::HasHiddenProcesses()
{
    return true;
}

bool TaskTracker::HasHiddenDrivers()
{
    return true;
}
