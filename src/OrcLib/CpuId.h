//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2023 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <string>
#include <bitset>

namespace Orc {

class CpuId final
{
public:
    static constexpr std::string_view kIntelManufacturerId = std::string_view("GenuineIntel");
    static constexpr std::string_view kAmdManufacturerId = std::string_view("AuthenticAMD");

    CpuId();

    bool HasSSE3() const;
    bool HasPCLMULQDQ() const;
    bool HasMONITOR() const;
    bool HasSSSE3() const;
    bool HasFMA() const;
    bool HasCMPXCHG16B() const;
    bool HasSSE41() const;
    bool HasSSE42() const;
    bool HasMOVBE() const;
    bool HasPOPCNT() const;
    bool HasAES() const;
    bool HasXSAVE() const;
    bool HasOSXSAVE() const;
    bool HasAVX() const;
    bool HasF16C() const;
    bool HasRDRAND() const;

    bool HasMSR() const;
    bool HasCX8() const;
    bool HasSEP() const;
    bool HasCMOV() const;
    bool HasCLFSH() const;
    bool HasMMX() const;
    bool HasFXSR() const;
    bool HasSSE() const;
    bool HasSSE2() const;
    bool HasHyperThreading() const;

    bool HasFSGSBASE() const;
    bool HasBMI1() const;
    bool HasHLE() const;
    bool HasAVX2() const;
    bool HasBMI2() const;
    bool HasERMS() const;
    bool HasINVPCID() const;
    bool HasRTM() const;
    bool HasAVX512F() const;
    bool HasRDSEED() const;
    bool HasADX() const;
    bool HasAVX512PF() const;
    bool HasAVX512ER() const;
    bool HasAVX512CD() const;
    bool HasSHA() const;

    bool HasPREFETCHWT1() const;

    bool HasLAHF() const;
    bool HasLZCNT() const;
    bool HasABM() const;
    bool HasSSE4a() const;
    bool HasXOP() const;
    bool HasTBM() const;

    bool HasSYSCALL() const;
    bool HasMMXEXT() const;
    bool HasRDTSCP() const;
    bool Has3DNOWEXT() const;
    bool Has3DNOW() const;

    const std::string& ManufacturerId() const;
    const std::string& Brand() const;

private:
    int m_lastFunctionId;
    int m_lastExtendedFunctionId;
    std::string m_manufacturerId;
    std::string m_brand;
    bool m_isIntel;
    bool m_isAMD;
    std::bitset<32> m_function1_ecx;
    std::bitset<32> m_function1_edx;
    std::bitset<32> m_function7_ebx;
    std::bitset<32> m_function7_ecx;
    std::bitset<32> m_function81_ecx;
    std::bitset<32> m_function81_edx;
};

}  // namespace Orc
