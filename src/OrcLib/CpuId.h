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


#if defined(_M_IX86) || defined(_M_X64) || defined(_M_AMD64)
// Intel x86 or x86_64 specific code
class CpuId final
{
public:
    static constexpr std::string_view kIntelManufacturerId = std::string_view("GenuineIntel");
    static constexpr std::string_view kAmdManufacturerId = std::string_view("AuthenticAMD");
    static constexpr std::string_view kARMManufacturerId = std::string_view("GenuineARM");
    
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

#elif defined(_M_ARM)
#error "Unsupported ARM32 architecture"
#elif defined(_M_ARM64)
// ARM 64-bit specific code

class CpuId final
{
public:
    static constexpr std::string_view kIntelManufacturerId = std::string_view("GenuineIntel");
    static constexpr std::string_view kAmdManufacturerId = std::string_view("AuthenticAMD");
    static constexpr std::string_view kARMManufacturerId = std::string_view("GenuineARM");

    CpuId();

    bool HasSSE3() const {return false;};
    bool HasPCLMULQDQ() const {return false;};
    bool HasMONITOR() const {return false;};
    bool HasSSSE3() const {return false;};
    bool HasFMA() const {return false;};
    bool HasCMPXCHG16B() const {return false;};
    bool HasSSE41() const {return false;};
    bool HasSSE42() const {return false;};
    bool HasMOVBE() const {return false;};
    bool HasPOPCNT() const {return false;};
    bool HasAES() const {return false;};
    bool HasXSAVE() const {return false;};
    bool HasOSXSAVE() const {return false;};
    bool HasAVX() const {return false;};
    bool HasF16C() const {return false;};
    bool HasRDRAND() const {return false;};

    bool HasMSR() const {return false;};
    bool HasCX8() const {return false;};
    bool HasSEP() const {return false;};
    bool HasCMOV() const {return false;};
    bool HasCLFSH() const {return false;};
    bool HasMMX() const {return false;};
    bool HasFXSR() const {return false;};
    bool HasSSE() const {return false;};
    bool HasSSE2() const {return false;};
    bool HasHyperThreading() const {return false;};

    bool HasFSGSBASE() const {return false;};
    bool HasBMI1() const {return false;};
    bool HasHLE() const {return false;};
    bool HasAVX2() const {return false;};
    bool HasBMI2() const {return false;};
    bool HasERMS() const {return false;};
    bool HasINVPCID() const {return false;};
    bool HasRTM() const {return false;};
    bool HasAVX512F() const {return false;};
    bool HasRDSEED() const {return false;};
    bool HasADX() const {return false;};
    bool HasAVX512PF() const {return false;};
    bool HasAVX512ER() const {return false;};
    bool HasAVX512CD() const {return false;};
    bool HasSHA() const {return false;};

    bool HasPREFETCHWT1() const  {return false;};

    bool HasLAHF() const {return false;};
    bool HasLZCNT() const {return false;};
    bool HasABM() const {return false;};
    bool HasSSE4a() const {return false;};
    bool HasXOP() const {return false;};
    bool HasTBM() const {return false;};

    bool HasSYSCALL() const {return false;};
    bool HasMMXEXT() const {return false;};
    bool HasRDTSCP() const {return false;};
    bool Has3DNOWEXT() const {return false;};
    bool Has3DNOW() const {return false;};

    const std::string& ManufacturerId() const {return m_manufacturerId;};
    const std::string& Brand() const {return m_brand;};

private:
    std::string m_manufacturerId;
    std::string m_brand;
};

#else
#error "Unsupported architecture"
#endif


}  // namespace Orc
