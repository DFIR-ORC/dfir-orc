//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "CompressAPIExtension.h"

STDMETHODIMP_(HRESULT __stdcall) Orc::CompressAPIExtension::Initialize()
{
    ScopedLock sl(m_cs);

    if (m_bInitialized)
        return S_OK;

    if (IsLoaded())
    {
        Get(m_CreateCompressor, "CreateCompressor");
        Get(m_SetCompressorInformation, "SetCompressorInformation");
        Get(m_QueryCompressorInformation, "QueryCompressorInformation");
        Get(m_Compress, "Compress");
        Get(m_ResetCompressor, "ResetCompressor");
        Get(m_CloseCompressor, "CloseCompressor");
        Get(m_CreateDecompressor, "CreateDecompressor");
        Get(m_SetDecompressorInformation, "SetDecompressorInformation");
        Get(m_QueryDecompressorInformation, "QueryDecompressorInformation");
        Get(m_Decompress, "Decompress");
        Get(m_ResetDecompressor, "ResetDecompressor");
        Get(m_CloseDecompressor, "CloseDecompressor");

        m_bInitialized = true;
    }
    return S_OK;
}
