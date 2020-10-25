//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#include "stdafx.h"

#include "StringsStream.h"

#include "WideAnsi.h"
#include "BinaryBuffer.h"

using namespace std;
using namespace Orc;

static const auto MAX_STRING_SIZE = 0x02000;
static const auto BLOCK_SIZE = 0x50000;

// Quick way of checking if a character value is displayable ascii
static bool isAscii[0x100] =
    /*          0     1     2     3        4     5     6     7        8     9     A     B        C     D     E     F
     */
    /* 0x00 */ {
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        true,
        true,
        false,
        false,
        true,
        false,
        false,
        /* 0x10 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0x20 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x30 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x40 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x50 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x60 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        /* 0x70 */ true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
        /* 0x80 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0x90 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xA0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xB0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xC0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xD0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xE0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        /* 0xF0 */ false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false};

HRESULT StringsStream::OpenForStrings(const shared_ptr<ByteStream>& pChained, size_t minChars, size_t maxChars)
{
    if (pChained == NULL)
        return E_POINTER;

    m_minChars = minChars;
    m_maxChars = maxChars;

    if (pChained->IsOpen() != S_OK)
    {
        Log::Error(L"Chained stream must be opened");
        return E_FAIL;
    }
    m_pChainedStream = pChained;
    return S_OK;
}

// return value is number of bytes processed
size_t StringsStream::extractImmediate(const CBinaryBuffer& aBuffer, UTF16Type& stringType)
{
    // Extract unicode or ascii from the immediate constant.

    size_t i = 0;

    if (!m_Strings.CheckCount((m_cchExtracted + MAX_STRING_SIZE) * sizeof(UCHAR)))
        return 0;

    switch (stringType)
    {
        case TYPE_ASCII:
            // Parse the immediate as ascii
            while (i < aBuffer.GetCount() && isAscii[aBuffer.Get<UCHAR>(i)])
            {
                m_Strings.Get<UCHAR>((m_cchExtracted * sizeof(UCHAR)) + i) = aBuffer.Get<UCHAR>(i);
                i++;
                m_cchExtracted++;
            }
            return i;

        case TYPE_UNICODE:
            // Parse the immediate as unicode
            while (i + 1 < aBuffer.GetCount() && isAscii[aBuffer.Get<UCHAR>(i)] && aBuffer.Get<UCHAR>(i + 1) == 0)
            {
                m_Strings.Get<UCHAR>(m_cchExtracted + i) = aBuffer.Get<UCHAR>(i);
                i += 2;
                m_cchExtracted++;
            }
            return i;

        case TYPE_UNDETERMINED:
            // Determine if this is ascii or unicode
            if (!isAscii[aBuffer.Get<UCHAR>(0)])
            {
                // Not unicode or ascii, return.
                return 0;
            }
            else if (aBuffer.GetCount() > 1 && aBuffer.Get<UCHAR>(1) == '\0')
            {
                // Recurse as Unicode
                stringType = TYPE_UNICODE;
                return extractImmediate(aBuffer, stringType);
            }
            else
            {
                // Recurse as Ascii
                stringType = TYPE_ASCII;
                return extractImmediate(aBuffer, stringType);
            }
        default:
            break;
    }
    return 0;
}

size_t StringsStream::extractString(
    const CBinaryBuffer& aBuffer,
    size_t offset,
    ExtractType& extractType,
    UTF16Type& stringType)
{
    // Process the string as either:
    // 1. ascii
    // 2. unicode
    // 3. x86 ASM stack pushes
    // TODO: 4. x64 ASM stack pushes
    //
    // To improve performance:
    //	Assumes MAX_STRING_SIZE > 1
    //	Assumes MinStringSize > 1
    //  Assumes offset + 3 < bufferSize
    // These assumptions must be validated by the calling function.

    // Supported string push formats
    // C6 45     mov byte [ebp+imm8], imm8
    // C6 85     mov byte [ebp+imm32], imm8
    // 66 C7 45  mov word [ebp+imm8], imm16
    // 66 C7 85  mov word [ebp+imm32], imm16
    // C7 45     mov dword [ebp+imm8], imm32
    // C7 85     mov dword [ebp+imm32], imm32

    if (!m_Strings.CheckCount((m_cchExtracted + MAX_STRING_SIZE) * sizeof(UCHAR)))
        return 0;

    // Set unknown string type
    extractType = EXTRACT_RAW;

    unsigned _int16 value = *((unsigned _int16*)(aBuffer.GetData() + offset));
    // Switch on the first two bytes
    switch (value)
    {
        case 0x45C6: {
            //  0  1  0  [0]
            // C6 45     mov byte [ebp+imm8], imm8
            size_t instSize = 4;
            size_t immSize = 1;
            size_t immOffset = instSize - immSize;
            size_t maxStringSize = 1;
            size_t i = 0;
            while (offset + i + instSize < aBuffer.GetCount() && m_Strings.GetCount() + maxStringSize < m_cchExtracted
                   && aBuffer.Get<UCHAR>(offset + i) == 0xC6 && aBuffer.Get<UCHAR>(offset + i + 1) == 0x45)
            {

                // Process this immediate
                CBinaryBuffer immBuffer((LPBYTE)aBuffer.GetData() + offset + immOffset + i, immSize);
                size_t size = this->extractImmediate(immBuffer, stringType);

                i += instSize;

                if ((stringType == TYPE_UNICODE && size < ((immSize + 1) / 2))
                    || (stringType == TYPE_ASCII && size < immSize))
                    break;
            }
            extractType = EXTRACT_ASM;
            return i;
        }
        case 0x85C6: {
            //  0  1  0  1  2  3  4  [0]
            // C6 85     mov byte [ebp+imm32], imm8
            size_t instSize = 8;
            size_t immSize = 1;
            size_t immOffset = instSize - immSize;
            size_t maxStringSize = 1;
            size_t i = 0;
            while (offset + i + instSize < aBuffer.GetCount()
                   && m_cchExtracted + maxStringSize < m_Strings.GetCount() / sizeof(UCHAR)
                   && aBuffer.Get<UCHAR>(offset + i) == 0xC6 && aBuffer.Get<UCHAR>(offset + i + 1) == 0x85)
            {
                // Process this immediate
                size_t size = this->extractImmediate(
                    CBinaryBuffer((LPBYTE)(aBuffer.GetData() + offset + immOffset + i), immSize), stringType);

                i += instSize;

                if ((stringType == TYPE_UNICODE && size < ((immSize + 1) / 2))
                    || (stringType == TYPE_ASCII && size < immSize))
                    break;
            }
            extractType = EXTRACT_ASM;
            return i;
        }
        case 0x45C7: {
            // 0  1  0  [0  1  2  3]
            // C7 45     mov dword [ebp+imm8], imm32
            size_t instSize = 7;
            size_t immSize = 4;
            size_t immOffset = instSize - immSize;
            size_t maxStringSize = 4;
            size_t i = 0;
            while (offset + i + instSize < aBuffer.GetCount()
                   && m_cchExtracted + maxStringSize < m_Strings.GetCount() / sizeof(UCHAR)
                   && aBuffer.Get<UCHAR>(offset + i) == 0xC7 && aBuffer.Get<UCHAR>(offset + i + 1) == 0x45)
            {
                // Process this immediate
                size_t size = this->extractImmediate(
                    CBinaryBuffer((LPBYTE)(aBuffer.GetData() + offset + immOffset + i), immSize), stringType);

                i += instSize;

                if ((stringType == TYPE_UNICODE && size < ((immSize + 1) / 2))
                    || (stringType == TYPE_ASCII && size < immSize))
                    break;
            }
            extractType = EXTRACT_ASM;
            return i;
        }
        case 0x85C7: {
            // 0  1  0  1  2  3  [0  1  2  3]
            // C7 85     mov dword [ebp+imm32], imm32
            size_t instSize = 10;
            size_t immSize = 4;
            size_t immOffset = instSize - immSize;
            size_t maxStringSize = 4;
            size_t i = 0;
            while (offset + i + instSize < aBuffer.GetCount()
                   && m_cchExtracted + maxStringSize < m_Strings.GetCount() / sizeof(UCHAR)
                   && aBuffer.Get<UCHAR>(offset + i) == 0xC7 && aBuffer.Get<UCHAR>(offset + i + 1) == 0x85)
            {

                // Process this immediate
                size_t size = this->extractImmediate(
                    CBinaryBuffer((LPBYTE)(aBuffer.GetData() + offset + immOffset + i), immSize), stringType);

                i += instSize;

                if ((stringType == TYPE_UNICODE && size < ((immSize + 1) / 2))
                    || (stringType == TYPE_ASCII && size < immSize))
                    break;
            }
            extractType = EXTRACT_ASM;
            return i;
        }
        case 0xC766:
            if (aBuffer.Get<UCHAR>(offset + 2) == 0x45)
            {
                // 0  1  2  0  [0  1]
                // 66 C7 45  mov word [ebp+imm8], imm16
                size_t instSize = 6;
                size_t immSize = 2;
                size_t immOffset = instSize - immSize;
                size_t maxStringSize = 2;
                size_t i = 0;
                while (offset + i + instSize < aBuffer.GetCount()
                       && m_cchExtracted + maxStringSize < m_Strings.GetCount() / sizeof(UCHAR)
                       && aBuffer.Get<UCHAR>(offset + i) == 0x66 && aBuffer.Get<UCHAR>(offset + i + 1) == 0xC7
                       && aBuffer.Get<UCHAR>(offset + i + 2) == 0x45)
                {

                    // Process this immediate
                    size_t size = this->extractImmediate(
                        CBinaryBuffer((LPBYTE)(aBuffer.GetData() + offset + immOffset + i), immSize), stringType);

                    i += instSize;

                    if ((stringType == TYPE_UNICODE && size < ((immSize + 1) / 2))
                        || (stringType == TYPE_ASCII && size < immSize))
                        break;
                }
                extractType = EXTRACT_ASM;
                return i;
            }
            else if (aBuffer.Get<UCHAR>(offset + 2) == 0x85)
            {
                // 0  1  2  0  1  2  3  [0  1]
                // 66 C7 85  mov word [ebp+imm32], imm16
                size_t i = 0;
                size_t instSize = 9;
                size_t immSize = 2;
                size_t immOffset = instSize - immSize;
                size_t maxStringSize = 2;
                while (offset + i + instSize < aBuffer.GetCount()
                       && m_cchExtracted + maxStringSize < m_Strings.GetCount() / sizeof(UCHAR)
                       && aBuffer.Get<UCHAR>(offset + i) == 0x66 && aBuffer.Get<UCHAR>(offset + i + 1) == 0xC7
                       && aBuffer.Get<UCHAR>(offset + i + 2) == 0x85)
                {

                    // Process this immediate
                    size_t size = this->extractImmediate(
                        CBinaryBuffer((LPBYTE)(aBuffer.GetData() + offset + immOffset + i), immSize), stringType);

                    i += instSize;

                    if ((stringType == TYPE_UNICODE && size < ((immSize + 1) / 2))
                        || (stringType == TYPE_ASCII && size < immSize))
                        break;
                }
                extractType = EXTRACT_ASM;
                return i;
            }
            break;

        default:
            // Try to parse as ascii or unicode
            if (isAscii[aBuffer.Get<UCHAR>(offset)])
            {
                // Consider unicode case
                if (aBuffer.Get<UCHAR>(offset + 1) == 0)  // No null dereference by assumptions
                {
                    // Parse as unicode
                    size_t i = 0;
                    while (offset + i + 1 < aBuffer.GetCount() && i / 2 < m_Strings.GetCount()
                           && isAscii[aBuffer.Get<UCHAR>(offset + i)] && aBuffer.Get<UCHAR>(offset + i + 1) == 0
                           && i / 2 + 1 < m_Strings.GetCount())
                    {
                        // Copy this character
                        m_Strings.CheckCount(m_cchExtracted + 1);
                        m_Strings.Get<UCHAR>(m_cchExtracted) = aBuffer.Get<UCHAR>(offset + i);
                        i += 2;
                        m_cchExtracted++;
                    }
                    stringType = TYPE_UNICODE;
                    return i;
                }
                else
                {
                    // Parse as ascii
                    size_t i = offset;
                    while (i < aBuffer.GetCount() && isAscii[aBuffer.Get<UCHAR>(i)])
                        i++;

                    m_Strings.CheckCount((m_cchExtracted + (i - offset)) * sizeof(UCHAR));

                    // Copy this string to the output
                    memcpy(
                        (LPBYTE)m_Strings.GetData() + (m_cchExtracted * sizeof(UCHAR)),
                        aBuffer.GetData() + offset,
                        (i - offset) * sizeof(UCHAR));
                    m_cchExtracted += i - offset;
                    stringType = TYPE_ASCII;
                    return i - offset;
                }
            }
    }

    return 0;
}

HRESULT StringsStream::processBuffer(const CBinaryBuffer& aBuffer, CBinaryBuffer& strings)
{
    // Process the contents of the specified file, and build the list of strings
    strings.CheckCount(MAX_STRING_SIZE + 1);
    m_cchExtracted = 0;

    size_t offset = 0;
    ExtractType extractType;
    while (offset + m_minChars < aBuffer.GetCount())
    {
        // Process this offset
        UTF16Type stringType = TYPE_UNDETERMINED;
        size_t cchPreviouslyExtracted = m_cchExtracted;
        size_t cbProcessed = extractString(aBuffer, offset, extractType, stringType);

        if ((m_cchExtracted - cchPreviouslyExtracted) >= m_minChars)
        {
            // Do something
            m_Strings.CheckCount(m_cchExtracted + 2);
            m_Strings.Get<UCHAR>(m_cchExtracted) = '\r';
            m_Strings.Get<UCHAR>(m_cchExtracted + 1) = '\n';
            m_cchExtracted += 2;
            // Advance the offset
            offset += cbProcessed;
        }
        else
        {
            // Advance the offset by 1
            m_cchExtracted = cchPreviouslyExtracted;
            offset += 1;
        }
    }

    return true;
}

HRESULT StringsStream::Read(
    __out_bcount_part(cbBytes, *pcbBytesRead) PVOID pReadBuffer,
    __in ULONGLONG cbBytes,
    __out_opt PULONGLONG pcbBytesRead)
{
    HRESULT hr = E_FAIL;

    if (pReadBuffer == NULL)
        return E_INVALIDARG;
    if (cbBytes > MAXDWORD)
        return E_INVALIDARG;
    if (pcbBytesRead == NULL)
        return E_POINTER;
    if (m_pChainedStream == NULL)
        return E_POINTER;

    *pcbBytesRead = 0LL;

    if (m_pChainedStream->CanRead() != S_OK)
        return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);

    ULONGLONG cbBytesRead = 0LL;

    CBinaryBuffer buffer;
    buffer.SetCount(static_cast<size_t>(cbBytes) + m_maxChars);

    if (FAILED(hr = m_pChainedStream->Read(pReadBuffer, cbBytes, &cbBytesRead)))
        return hr;

    if (cbBytesRead == 0LL)
        return S_OK;

    // Extract Strings here...
    CBinaryBuffer strings;
    if (FAILED(hr = processBuffer(CBinaryBuffer((LPBYTE)pReadBuffer, static_cast<size_t>(cbBytesRead)), strings)))
    {
        Log::Error(L"Failed to extract strings from read buffer [{}]", SystemError(hr));
        return hr;
    }
    if (m_cchExtracted * sizeof(UCHAR) > cbBytes)
    {
        // Extraction generates more bytes than input buffer size
        Log::Warn("Failed to extract strings (input size: {}, output size: {})", cbBytes, m_cchExtracted);
        Log::Info("Truncating {} bytes: ", m_cchExtracted * sizeof(UCHAR) - cbBytes);

        // TODO: print hex buffer
        //_L_->WriteBytesInHex(
        //    ((BYTE*)m_Strings.GetData()) + cbBytes,
        //    static_cast<DWORD>(m_cchExtracted * sizeof(UCHAR) - cbBytes),
        //    false);
        // Log::Info(L"");
    }

    if (m_cchExtracted == 0LL)
    {
        ((LPBYTE)pReadBuffer)[0] = '\r';
        ((LPBYTE)pReadBuffer)[0] = '\n';
        *pcbBytesRead = 2;
    }
    else
    {
        memcpy_s(
            pReadBuffer,
            static_cast<rsize_t>(cbBytes),
            m_Strings.GetData(),
            m_cchExtracted > cbBytes ? static_cast<rsize_t>(cbBytes) : m_cchExtracted);
    }

    *pcbBytesRead = m_cchExtracted;
    m_cchExtracted = 0;
    return S_OK;
}

HRESULT StringsStream::Write(
    __in_bcount(cbBytesToWrite) const PVOID pWriteBuffer,
    __in ULONGLONG cbBytesToWrite,
    __out_opt PULONGLONG pcbBytesWritten)
{
    HRESULT hr = E_FAIL;

    if (pWriteBuffer == NULL)
        return E_POINTER;
    if (pcbBytesWritten == NULL)
        return E_POINTER;
    if (m_pChainedStream == NULL)
        return E_POINTER;

    if (m_pChainedStream->CanWrite() != S_OK)
        return HRESULT_FROM_WIN32(ERROR_INVALID_ACCESS);

    *pcbBytesWritten = 0;

    if (cbBytesToWrite > MAXDWORD)
    {
        Log::Error("Too many bytes");
        return E_INVALIDARG;
    }

    CBinaryBuffer buffer;

    if (!buffer.SetCount((DWORD)cbBytesToWrite))
        return E_OUTOFMEMORY;

    // Extract strings here...

    ULONGLONG cbBytesWritten = 0;

    if (FAILED(hr = m_pChainedStream->Write(buffer.GetData(), buffer.GetCount(), &cbBytesWritten)))
        return hr;

    *pcbBytesWritten = cbBytesWritten;

    return S_OK;
}

StringsStream::~StringsStream(void) {}
