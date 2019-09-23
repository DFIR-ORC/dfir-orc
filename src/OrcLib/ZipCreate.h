//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//

#pragma once

#include "OrcLib.h"

#include "ArchiveCreate.h"

#include "7zip/IStream.h"
#include "7zip/Archive/IArchive.h"

#pragma managed(push, off)

namespace Orc {

class ZipLibrary;
class TemporaryStream;

class ORCLIB_API ZipCreate : public ArchiveCreate
{
    friend class ArchiveCreate;
    friend class std::_Ref_count_obj<ZipCreate>;

public:
    typedef enum _CL
    {
        None = 0,
        Fastest = 1,
        Fast = 3,
        Normal = 5,
        Maximum = 7,
        Ultra = 9
    } CompressionLevel;

public:
    CompressionLevel GetCompressionLevel(const std::wstring& strLevel);

    // ArchiveCompress methods
    STDMETHOD(InitArchive)(__in PCWSTR pwzArchivePath, Archive::ArchiveCallback pCallback = nullptr);
    STDMETHOD(InitArchive)
    (__in const std::shared_ptr<ByteStream>& pOutputStream, Archive::ArchiveCallback pCallback = nullptr);

    STDMETHOD(SetCompressionLevel)(__in const std::wstring& strLevel);

    STDMETHOD(FlushQueue)();
    STDMETHOD(Complete)();
    STDMETHOD(Abort)();
    // end of ArchiveCompress methods

    ~ZipCreate(void);

private:
    std::shared_ptr<TemporaryStream> m_TempStream;
    std::shared_ptr<ByteStream> m_ArchiveStream;
    std::wstring m_ArchiveName;
    GUID m_FormatGUID;
    CompressionLevel m_CompressionLevel;

    ZipCreate(logger pLo, bool bComputeHash = false, DWORD XORPattern = 0x00000000);

    STDMETHOD(SetCompressionLevel)(const CComPtr<IOutArchive>& pArchiver, CompressionLevel level);

    STDMETHOD(Internal_FlushQueue)(bool bFinal);
};

}  // namespace Orc

#pragma managed(pop)
