//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcCommand.h"

#include "UtilitiesMain.h"

#pragma managed(push, off)

#if (_WIN32_WINNT < 0x0600)

typedef struct _STARTUPINFOEXW
{
    STARTUPINFOW StartupInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList;
} STARTUPINFOEXW, *LPSTARTUPINFOEXW;

typedef STARTUPINFOEXW STARTUPINFOEX;
typedef LPSTARTUPINFOEXW LPSTARTUPINFOEX;

//
// Extended process and thread attribute support
//

constexpr auto PROC_THREAD_ATTRIBUTE_NUMBER = 0x0000FFFF;
constexpr auto PROC_THREAD_ATTRIBUTE_THREAD = 0x00010000;  // Attribute may be used with thread creation;
constexpr auto PROC_THREAD_ATTRIBUTE_INPUT = 0x00020000;  // Attribute is input only;
constexpr auto PROC_THREAD_ATTRIBUTE_ADDITIVE =
    0x00040000;  // Attribute may be "accumulated," e.g. bitmasks, counters, etc.;

enum PROC_THREAD_ATTRIBUTE_NUM
{
    ProcThreadAttributeParentProcess = 0,
    ProcThreadAttributeExtendedFlags,
    ProcThreadAttributeHandleList,
    ProcThreadAttributeGroupAffinity,
    ProcThreadAttributePreferredNode,
    ProcThreadAttributeIdealProcessor,
    ProcThreadAttributeUmsThread,
    ProcThreadAttributeMitigationPolicy,
    ProcThreadAttributeMax
};

constexpr auto ProcThreadAttributeValue(PROC_THREAD_ATTRIBUTE_NUM Number, BOOL Thread, BOOL Input, BOOL Additive)
{
    return (
        ((Number)&PROC_THREAD_ATTRIBUTE_NUMBER) | ((Thread != FALSE) ? PROC_THREAD_ATTRIBUTE_THREAD : 0)
        | ((Input != FALSE) ? PROC_THREAD_ATTRIBUTE_INPUT : 0)
        | ((Additive != FALSE) ? PROC_THREAD_ATTRIBUTE_ADDITIVE : 0));
}

constexpr auto PROC_THREAD_ATTRIBUTE_PARENT_PROCESS =
    ProcThreadAttributeValue(ProcThreadAttributeParentProcess, FALSE, TRUE, FALSE);
constexpr auto PROC_THREAD_ATTRIBUTE_EXTENDED_FLAGS =
    ProcThreadAttributeValue(ProcThreadAttributeExtendedFlags, FALSE, TRUE, TRUE);
constexpr auto PROC_THREAD_ATTRIBUTE_HANDLE_LIST =
    ProcThreadAttributeValue(ProcThreadAttributeHandleList, FALSE, TRUE, FALSE);
constexpr auto PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY =
    ProcThreadAttributeValue(ProcThreadAttributeGroupAffinity, TRUE, TRUE, FALSE);
constexpr auto PROC_THREAD_ATTRIBUTE_PREFERRED_NODE =
    ProcThreadAttributeValue(ProcThreadAttributePreferredNode, FALSE, TRUE, FALSE);
constexpr auto PROC_THREAD_ATTRIBUTE_IDEAL_PROCESSOR =
    ProcThreadAttributeValue(ProcThreadAttributeIdealProcessor, TRUE, TRUE, FALSE);
constexpr auto PROC_THREAD_ATTRIBUTE_UMS_THREAD =
    ProcThreadAttributeValue(ProcThreadAttributeUmsThread, TRUE, TRUE, FALSE);
constexpr auto PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY =
    ProcThreadAttributeValue(ProcThreadAttributeMitigationPolicy, FALSE, TRUE, FALSE);

#endif

namespace Orc {

class DownloadTask;

namespace Command::Mothership {
class ORCUTILS_API Main : public UtilitiesMain
{
public:
    class Configuration : public UtilitiesMain::Configuration
    {
    public:
        bool bNoWait = false;
        bool bPreserveJob = false;
        bool bUseWMI = false;
        bool bUseLocalCopy = false;
        DWORD dwCreationFlags = 0L;
        std::wstring strCmdLineArgs;
        std::wstring strParentName;
        OutputSpec Temporary;
        DWORD dwParentID = 0L;

        Configuration() {};
    };

private:
    Configuration config;

    STARTUPINFOEX m_si;
    PROCESS_INFORMATION m_pi;

    bool m_bAllowedBreakAwayOnJob;
    HANDLE m_hParentProcess;

    std::shared_ptr<DownloadTask> m_DownloadTask;

    HRESULT ChangeTemporaryEnvironment();

    HRESULT Launch(const std::wstring& strToExecute, const std::wstring& strArgs);

    HRESULT LaunchSelf();
    HRESULT LaunchWMI();
    HRESULT LaunchRun();
    HRESULT DownloadLocalCopy();

public:
    void Configure(int argc, const wchar_t* argv[]) override;

    static LPCWSTR ToolName() { return L"Mothership"; }
    static LPCWSTR ToolDescription() { return nullptr; }

    static ConfigItem::InitFunction GetXmlConfigBuilder();
    static LPCWSTR DefaultConfiguration() { return nullptr; }
    static LPCWSTR ConfigurationExtension() { return nullptr; }

    static ConfigItem::InitFunction GetXmlLocalConfigBuilder();
    static LPCWSTR LocalConfiguration() { return nullptr; }
    static LPCWSTR LocalConfigurationExtension() { return L"xml"; };

    static LPCWSTR DefaultSchema() { return nullptr; }

    Main()
        : UtilitiesMain()
    {
        ZeroMemory(&m_si, sizeof(STARTUPINFOEX));
        m_si.StartupInfo.cb = sizeof(STARTUPINFO);

        ZeroMemory(&m_pi, sizeof(PROCESS_INFORMATION));

        m_si.StartupInfo.wShowWindow = SW_HIDE;
        m_si.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;

        m_hParentProcess = INVALID_HANDLE_VALUE;
        m_bAllowedBreakAwayOnJob = false;
    };

    HRESULT GetConfigurationFromConfig(const ConfigItem& configitem) { return S_OK; }  // No Configuration support
    HRESULT GetLocalConfigurationFromConfig(const ConfigItem& configitem);  // Local Configuration support

    HRESULT GetConfigurationFromArgcArgv(int argc, const WCHAR* argv[]);

    HRESULT CheckConfiguration();

    HRESULT Run();

    void PrintUsage();
    void PrintParameters();
    void PrintFooter();
};
}  // namespace Command::Mothership
}  // namespace Orc
#pragma managed(pop)
