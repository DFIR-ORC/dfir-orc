//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <string>
#include <set>

#pragma managed(push, off)

using SOCKET_ADDRESS = struct _SOCKET_ADDRESS;

namespace Orc {

namespace TableOutput {
class IOutput;
}

class IWriter;
using ITableOutput = TableOutput::IOutput;

class LogFileWriter;

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
    static HRESULT WriteDescriptionString(const logger& pLog);
    static HRESULT WriteDescriptionString(ITableOutput& output);

    static HRESULT GetOSVersion(DWORD& dwMajor, DWORD& dwMinor);
    static std::pair<DWORD, DWORD> GetOSVersion();

    static HRESULT GetArchitecture(WORD& wArch);

    static HRESULT GetPageSize(DWORD& dwPageSize);
    static HRESULT GetLargePageSize(DWORD& dwPageSize);

    static HRESULT GetComputerName_(std::wstring& strComputerName);
    static HRESULT WriteComputerName(const logger& pLog);
    static HRESULT WriteComputerName(ITableOutput& output);

    static HRESULT GetFullComputerName(std::wstring& strComputerName);
    static HRESULT WriteFullComputerName(const logger& pLog);
    static HRESULT WriteFullComputerName(ITableOutput& output);

    static HRESULT SetOrcComputerName(const std::wstring& strComputerName);
    static HRESULT GetOrcComputerName(std::wstring& strComputerName);
    static HRESULT WriteOrcComputerName(const logger& pLog);
    static HRESULT WriteOrcComputerName(ITableOutput& output);

    static HRESULT SetOrcFullComputerName(const std::wstring& strComputerName);
    static HRESULT GetOrcFullComputerName(std::wstring& strComputerName);
    static HRESULT WriteOrcFullComputerName(const logger& pLog);
    static HRESULT WriteOrcFullComputerName(ITableOutput& output);

    static HRESULT WriteProductype(ITableOutput& output);

    static HRESULT GetTimeStamp(std::wstring& strTimeStamp);

    static HRESULT WhoAmI(std::wstring& strMe);
    static HRESULT AmIElevated(bool& bIsElevated);
    static HRESULT UserSID(std::wstring& strSID);

    static HRESULT GetSystemLocale(std::wstring& strLocale);
    static HRESULT GetUserLocale(std::wstring& strLocale);
    static HRESULT GetSystemLanguage(std::wstring& strLocale);
    static HRESULT GetUserLanguage(std::wstring& strLocale);


    static HRESULT GetCurrentWorkingDirectory(std::wstring& strCMD);

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

    static bool IsWOW64();

    enum class AddressMode
    {
        UniCast, AnyCast, MultiCast, UnknownMode
    };
    enum class AddressType
    {
        IPV4, IPV6, IPUnknown
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

    static std::pair<HRESULT, const std::vector<NetworkAdapter>&> GetNetworkAdapters();

private:
    static std::pair<HRESULT, NetworkAddress> GetNetworkAddress(SOCKET_ADDRESS& address);

};

static auto constexpr OrcComputerName = L"DFIR-OrcComputer";
static auto constexpr OrcFullComputerName = L"DFIR-OrcFullComputer";

}  // namespace Orc

#pragma managed(pop)
