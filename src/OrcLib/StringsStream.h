//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include "ChainingStream.h"
#include "BinaryBuffer.h"

#pragma managed(push, off)

namespace Orc {

class ORCLIB_API StringsStream : public ChainingStream
{
public:
    typedef struct _Options
    {
        int minCharacters;
    } Options;

    typedef enum _ExtractType
    {
        EXTRACT_RAW,
        EXTRACT_ASM
    } ExtractType;

    typedef enum _StringType
    {
        TYPE_UNDETERMINED,
        TYPE_ASCII,
        TYPE_UNICODE
    } UTF16Type;

private:
    CBinaryBuffer m_Strings;
    size_t m_cchExtracted;

    size_t m_minChars;
    size_t m_maxChars;

    size_t extractImmediate(const CBinaryBuffer& aBuffer, UTF16Type& stringType);
    size_t extractString(const CBinaryBuffer& aBuffer, size_t offset, ExtractType& extractType, UTF16Type& stringType);

    HRESULT processBuffer(const CBinaryBuffer& aBuffer, CBinaryBuffer& strings);

public:
    StringsStream()
        : ChainingStream() {};

    STDMETHOD(IsOpen)()
    {
        if (m_pChainedStream == NULL)
            return S_FALSE;
        return m_pChainedStream->IsOpen();
    };
    STDMETHOD(CanRead)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanRead();
    };
    STDMETHOD(CanWrite)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanWrite();
    };
    STDMETHOD(CanSeek)()
    {
        if (m_pChainedStream == NULL)
            return E_POINTER;
        return m_pChainedStream->CanSeek();
    };

    //
    // CByteStream implementation
    //
    STDMETHOD(OpenForStrings)
    (const std::shared_ptr<ByteStream>& pChainedStream, size_t minChars, size_t maxChars);

    STDMETHOD(Read)
    (__out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
     __in ULONGLONG cbBytes,
     __out_opt PULONGLONG pcbBytesRead);

    STDMETHOD(Write)
    (__in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
     __in ULONGLONG cbBytesToWrite,
     __out_opt PULONGLONG pcbBytesWritten);

    STDMETHOD(SetFilePointer)
    (__in LONGLONG DistanceToMove, __in DWORD dwMoveMethod, __out_opt PULONG64 pCurrPointer)
    {
        if (m_pChainedStream == nullptr)
            return E_POINTER;
        return m_pChainedStream->SetFilePointer(DistanceToMove, dwMoveMethod, pCurrPointer);
    }

    STDMETHOD_(ULONG64, GetSize)()
    {
        if (m_pChainedStream == nullptr)
            return 0;
        return m_pChainedStream->GetSize();
    }
    STDMETHOD(SetSize)(ULONG64 ullNewSize)
    {
        if (m_pChainedStream == nullptr)
            return S_OK;
        return m_pChainedStream->SetSize(ullNewSize);
    }

    STDMETHOD(Close)()
    {
        if (m_pChainedStream == nullptr)
            return S_OK;
        return m_pChainedStream->Close();
    }

    virtual ~StringsStream(void);
};

}  // namespace Orc

#pragma managed(pop)
