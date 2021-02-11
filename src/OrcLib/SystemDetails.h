//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "Utils/Result.h"
#include "OutputWriter.h"

#include <optional>
#include <string>
#include <array>
#include <set>
#include <filesystem>

#pragma managed(push, off)

using SOCKET_ADDRESS = struct _SOCKET_ADDRESS;

namespace Orc {

namespace TableOutput {
class IOutput;
}

class IWriter;
using ITableOutput = TableOutput::IOutput;

using SystemTags = std::set<std::wstring>;

class ORCLIB_API SystemDetails
{

private:
    static HRESULT LoadSystemDetails();

public:
    static HRESULT SetSystemType(std::wstring strSystemType);
    static HRESULT GetSystemType(std::wstring& strSystemType);
    static HRESULT GetSystemType(BYTE& systemType);

    static const SystemTags& GetSystemTags();
    static HRESULT SetSystemTags(SystemTags tags);

    static HRESULT GetDescriptionString(std::wstring& strDescription);
    static HRESULT WriteDescriptionString();
    static HRESULT WriteDescriptionString(ITableOutput& output);

    static HRESULT GetOSVersion(DWORD& dwMajor, DWORD& dwMinor);
    static std::pair<DWORD, DWORD> GetOSVersion();

    static HRESULT GetArchitecture(WORD& wArch);

    struct CPUInformation
    {
        DWORD Cores = 0LU;
        DWORD EnabledCores = 0LU;
        DWORD LogicalProcessors = 0LU;
        std::wstring Name;
        std::wstring Description;
    };

    static Result<std::vector<CPUInformation>> GetCPUInfo();

    static Result<MEMORYSTATUSEX> GetPhysicalMemory();

    static HRESULT GetPageSize(DWORD& dwPageSize);
    static HRESULT GetLargePageSize(DWORD& dwPageSize);

    static HRESULT GetComputerName_(std::wstring& strComputerName);
    static HRESULT WriteComputerName();
    static HRESULT WriteComputerName(ITableOutput& output);

    static HRESULT GetFullComputerName(std::wstring& strComputerName);
    static HRESULT WriteFullComputerName();
    static HRESULT WriteFullComputerName(ITableOutput& output);

    static HRESULT SetOrcComputerName(const std::wstring& strComputerName);
    static HRESULT GetOrcComputerName(std::wstring& strComputerName);
    static HRESULT WriteOrcComputerName();
    static HRESULT WriteOrcComputerName(ITableOutput& output);

    static HRESULT SetOrcFullComputerName(const std::wstring& strComputerName);
    static HRESULT GetOrcFullComputerName(std::wstring& strComputerName);
    static HRESULT WriteOrcFullComputerName();
    static HRESULT WriteOrcFullComputerName(ITableOutput& output);

    static HRESULT WriteProductType(ITableOutput& output);

    static HRESULT GetTimeStamp(std::wstring& strTimeStamp);
    static HRESULT GetTimeStampISO8601(std::wstring& strTimeStamp);
    static Result<Traits::TimeUtc<SYSTEMTIME>> GetTimeStamp();

    static HRESULT WhoAmI(std::wstring& strMe);
    static HRESULT AmIElevated(bool& bIsElevated);
    static HRESULT UserSID(std::wstring& strSID);

    static Result<DWORD> GetParentProcessId();

    static Result<std::wstring> GetCmdLine();
    static Result<std::wstring> GetCmdLine(DWORD dwPid);

    static HRESULT GetSystemLocale(std::wstring& strLocale);
    static HRESULT GetUserLocale(std::wstring& strLocale);
    static HRESULT GetSystemLanguage(std::wstring& strLocale);
    static HRESULT GetUserLanguage(std::wstring& strLocale);

    static HRESULT GetCurrentWorkingDirectory(std::filesystem::path& cwd);

    static HRESULT GetProcessBinary(std::wstring& strFullPath);

    enum DriveType
    {
        Drive_Unknown,
        Drive_No_Root_Dir,
        Drive_Removable,
        Drive_Fixed,
        Drive_Remote,
        Drive_CDRom,
        Drive_RamDisk
    };

    static DriveType GetPathLocation(const std::wstring& strAnyPath);

    struct PhysicalDrive
    {
        std::wstring Path;
        ULONG64 Size = 0LLU;
        ULONG32 SerialNumber = 0LU;
        std::wstring MediaType;

        std::optional<std::wstring> Status;
        std::optional<USHORT> Availability;
        std::optional<DWORD> ConfigManagerErrorCode;
    };

    static Result<std::vector<PhysicalDrive>> GetPhysicalDrives();

    struct MountedVolume
    {
        std::wstring FileSystem;
        std::wstring Label;
        std::wstring Path;
        std::wstring DeviceId;
        DriveType Type;
        ULONG64 Size = 0LLU;
        ULONG64 FreeSpace = 0LLU;
        DWORD SerialNumber = 0LU;
        bool bBoot = false;
        bool bSystem = false;

        std::optional<std::wstring> ErrorDesciption;
        std::optional<ULONG32> ErrorCode;
    };

    static Result<std::vector<MountedVolume>> GetMountedVolumes();

    struct QFE
    {
        std::wstring URL;
        std::wstring Description;
        std::wstring HotFixId;
        std::wstring InstallDate;
    };

    static Result<std::vector<QFE>> GetOsQFEs();

    struct EnvVariable
    {
        std::wstring Name;
        std::wstring Value;
    };

    static Result<std::vector<EnvVariable>> GetEnvironment();

    static bool IsWOW64();

    enum class AddressMode
    {
        UniCast,
        AnyCast,
        MultiCast,
        UnknownMode
    };
    enum class AddressType
    {
        IPV4,
        IPV6,
        IPUnknown
    };
    struct NetworkAddress
    {
        AddressType Type = AddressType::IPUnknown;
        AddressMode Mode = AddressMode::UnknownMode;
        std::wstring Address;
    };

    struct NetworkAdapter
    {
        std::wstring Name;
        std::wstring Description;
        std::wstring FriendlyName;
        std::wstring PhysicalAddress;
        std::vector<NetworkAddress> Addresses;
        std::vector<NetworkAddress> DNS;
        std::wstring DNSSuffix;
    };

    static Result<std::vector<NetworkAdapter>> GetNetworkAdapters();

private:
    static Result<NetworkAddress> GetNetworkAddress(SOCKET_ADDRESS& address);
};

static auto constexpr OrcComputerName = L"DFIR-OrcComputer";
static auto constexpr OrcFullComputerName = L"DFIR-OrcFullComputer";

}  // namespace Orc

#pragma managed(pop)
