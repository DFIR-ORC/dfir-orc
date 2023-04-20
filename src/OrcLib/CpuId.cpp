//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2023 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

//
// See:
//   https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=msvc-170
//   https://en.wikipedia.org/wiki/CPUID
//

#include "CpuId.h"

namespace Orc {

CpuId::CpuId()
    : m_lastFunctionId {0}
    , m_lastExtendedFunctionId {0}
    , m_function1_ecx {0}
    , m_function1_edx {0}
    , m_function7_ebx {0}
    , m_function7_ecx {0}
    , m_function81_ecx {0}
    , m_function81_edx {0}
{
    std::array<int, 4> cpui;
    __cpuid(cpui.data(), 0);
    m_lastFunctionId = cpui[0];

    std::vector<std::array<int, 4>> cpuidResults;
    for (int i = 0; i <= m_lastFunctionId; ++i)
    {
        __cpuidex(cpui.data(), i, 0);
        cpuidResults.push_back(cpui);
    }

    // Capture vendor string
    char manufacturerId[0x20] {0};
    *reinterpret_cast<int*>(manufacturerId) = cpuidResults[0][1];
    *reinterpret_cast<int*>(manufacturerId + 4) = cpuidResults[0][3];
    *reinterpret_cast<int*>(manufacturerId + 8) = cpuidResults[0][2];
    m_manufacturerId = manufacturerId;

    if (m_manufacturerId == kIntelManufacturerId)
    {
        m_isIntel = true;
    }
    else if (m_manufacturerId == kAmdManufacturerId)
    {
        m_isAMD = true;
    }

    // load bitset with flags for function 0x00000001
    if (m_lastFunctionId >= 1)
    {
        m_function1_ecx = cpuidResults[1][2];
        m_function1_edx = cpuidResults[1][3];
    }

    // load bitset with flags for function 0x00000007
    if (m_lastFunctionId >= 7)
    {
        m_function7_ebx = cpuidResults[7][1];
        m_function7_ecx = cpuidResults[7][2];
    }

    // Calling __cpuid with 0x80000000 as the function_id argument
    // gets the number of the highest valid extended ID.
    __cpuid(cpui.data(), 0x80000000);
    m_lastExtendedFunctionId = cpui[0];

    std::vector<std::array<int, 4>> CpuidExtentedResults;
    char brand[0x40] {0};
    for (int i = 0x80000000; i <= m_lastExtendedFunctionId; ++i)
    {
        __cpuidex(cpui.data(), i, 0);
        CpuidExtentedResults.push_back(cpui);
    }

    // load bitset with flags for function 0x80000001
    if (m_lastExtendedFunctionId >= 0x80000001)
    {
        m_function81_ecx = CpuidExtentedResults[1][2];
        m_function81_edx = CpuidExtentedResults[1][3];
    }

    // Interpret CPU brand string if reported
    if (m_lastExtendedFunctionId >= 0x80000004)
    {
        memcpy(brand, CpuidExtentedResults[2].data(), sizeof(cpui));
        memcpy(brand + 16, CpuidExtentedResults[3].data(), sizeof(cpui));
        memcpy(brand + 32, CpuidExtentedResults[4].data(), sizeof(cpui));

        std::copy(std::cbegin(brand), std::cend(brand), std::back_inserter(m_brand));
    }
}

const std::string& CpuId::ManufacturerId() const
{
    return m_manufacturerId;
}

const std::string& CpuId::Brand() const
{
    return m_brand;
}

bool CpuId::HasSSE3() const
{
    return m_function1_ecx[0];
}

bool CpuId::HasPCLMULQDQ() const
{
    return m_function1_ecx[1];
}

bool CpuId::HasMONITOR() const
{
    return m_function1_ecx[3];
}

bool CpuId::HasSSSE3() const
{
    return m_function1_ecx[9];
}

bool CpuId::HasFMA() const
{
    return m_function1_ecx[12];
}

bool CpuId::HasCMPXCHG16B() const
{
    return m_function1_ecx[13];
}

bool CpuId::HasSSE41() const
{
    return m_function1_ecx[19];
}

bool CpuId::HasSSE42() const
{
    return m_function1_ecx[20];
}

bool CpuId::HasMOVBE() const
{
    return m_function1_ecx[22];
}

bool CpuId::HasPOPCNT() const
{
    return m_function1_ecx[23];
}

bool CpuId::HasAES() const
{
    return m_function1_ecx[25];
}

bool CpuId::HasXSAVE() const
{
    return m_function1_ecx[26];
}

bool CpuId::HasOSXSAVE() const
{
    return m_function1_ecx[27];
}

bool CpuId::HasAVX() const
{
    return m_function1_ecx[28];
}

bool CpuId::HasF16C() const
{
    return m_function1_ecx[29];
}

bool CpuId::HasRDRAND() const
{
    return m_function1_ecx[30];
}

bool CpuId::HasMSR() const
{
    return m_function1_edx[5];
}

bool CpuId::HasCX8() const
{
    return m_function1_edx[8];
}

bool CpuId::HasSEP() const
{
    return m_function1_edx[11];
}

bool CpuId::HasCMOV() const
{
    return m_function1_edx[15];
}

bool CpuId::HasCLFSH() const
{
    return m_function1_edx[19];
}

bool CpuId::HasMMX() const
{
    return m_function1_edx[23];
}

bool CpuId::HasFXSR() const
{
    return m_function1_edx[24];
}

bool CpuId::HasSSE() const
{
    return m_function1_edx[25];
}

bool CpuId::HasSSE2() const
{
    return m_function1_edx[26];
}

bool CpuId::HasHyperThreading() const
{
    return m_function1_edx[28];
}

bool CpuId::HasFSGSBASE() const
{
    return m_function7_ebx[0];
}

bool CpuId::HasBMI1() const
{
    return m_function7_ebx[3];
}
bool CpuId::HasHLE() const
{
    return m_isIntel && m_function7_ebx[4];
}

bool CpuId::HasAVX2() const
{
    return m_function7_ebx[5];
}

bool CpuId::HasBMI2() const
{
    return m_function7_ebx[8];
}
bool CpuId::HasERMS() const
{
    return m_function7_ebx[9];
}

bool CpuId::HasINVPCID() const
{
    return m_function7_ebx[10];
}

bool CpuId::HasRTM() const
{
    return m_isIntel && m_function7_ebx[11];
}

bool CpuId::HasAVX512F() const
{
    return m_function7_ebx[16];
}

bool CpuId::HasRDSEED() const
{
    return m_function7_ebx[18];
}

bool CpuId::HasADX() const
{
    return m_function7_ebx[19];
}

bool CpuId::HasAVX512PF() const
{
    return m_function7_ebx[26];
}

bool CpuId::HasAVX512ER() const
{
    return m_function7_ebx[27];
}
bool CpuId::HasAVX512CD() const
{
    return m_function7_ebx[28];
}

bool CpuId::HasSHA() const
{
    return m_function7_ebx[29];
}

bool CpuId::HasPREFETCHWT1() const
{
    return m_function7_ecx[0];
}

bool CpuId::HasLAHF() const
{
    return m_function81_ecx[0];
}

bool CpuId::HasLZCNT() const
{
    return m_isIntel && m_function81_ecx[5];
}

bool CpuId::HasABM() const
{
    return m_isAMD && m_function81_ecx[5];
}

bool CpuId::HasSSE4a() const
{
    return m_isAMD && m_function81_ecx[6];
}

bool CpuId::HasXOP() const
{
    return m_isAMD && m_function81_ecx[11];
}

bool CpuId::HasTBM() const
{
    return m_isAMD && m_function81_ecx[21];
}

bool CpuId::HasSYSCALL() const
{
    return m_isIntel && m_function81_edx[11];
}

bool CpuId::HasMMXEXT() const
{
    return m_isAMD && m_function81_edx[22];
}

bool CpuId::HasRDTSCP() const
{
    return m_isIntel && m_function81_edx[27];
}

bool CpuId::Has3DNOWEXT() const
{
    return m_isAMD && m_function81_edx[30];
}

bool CpuId::Has3DNOW() const
{
    return m_isAMD && m_function81_edx[31];
}

}  // namespace Orc
