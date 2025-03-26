//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcLib.h"

#include <memory>

#include <WinCrypt.h>

#include <string>

#pragma managed(push, off)

#ifndef _LPCBYTE_DEFINED
typedef BYTE const* LPCBYTE;
#endif  //_LPCBYTE_DEFINED

namespace Orc {

class CBinaryBuffer
{
    friend class MemoryStream;

private:
    BYTE* m_pData;
    size_t m_size;
    bool m_bOwnMemory;
    bool m_bJunk;
    bool m_bVirtualAlloc;

    static HCRYPTPROV g_hProv;

public:
    using value_type = uint8_t;

    CBinaryBuffer& operator=(const CBinaryBuffer& other)
    {
        CBinaryBuffer newThis(other);
        RemoveAll();
        m_pData = newThis.m_pData;
        m_size = newThis.m_size;
        m_bVirtualAlloc = newThis.m_bVirtualAlloc;
        m_bOwnMemory = newThis.m_bOwnMemory;
        m_bJunk = newThis.m_bJunk;

        if (newThis.m_bOwnMemory)
        {
            newThis.m_pData = nullptr;
            newThis.m_size = 0;
            newThis.m_bOwnMemory = true;
            newThis.m_bJunk = true;
        }
        return *this;
    }

    CBinaryBuffer(const CBinaryBuffer& other);

    CBinaryBuffer(bool bVirtualAlloc = false)
        : m_pData(nullptr)
        , m_size(0L)
        , m_bOwnMemory(true)
        , m_bJunk(true)
        , m_bVirtualAlloc(bVirtualAlloc) {};

    // Move constructor.
    CBinaryBuffer(CBinaryBuffer&& other) noexcept
        : m_pData(other.m_pData)
        , m_size(other.m_size)
        , m_bOwnMemory(other.m_bOwnMemory)
        , m_bJunk(other.m_bJunk)
        , m_bVirtualAlloc(other.m_bVirtualAlloc)
    {
        // Copy the data pointer and its length from the
        // source object.
        if (other.m_bOwnMemory)
        {
            // Release the data pointer from the source object so that
            // the destructor does not free the memory multiple times.
            other.m_pData = nullptr;
            other.m_size = 0;
            other.m_bOwnMemory = true;
            other.m_bJunk = true;
        }
    }

    CBinaryBuffer(LPBYTE pBuf, size_t dwSize)
        : m_pData(pBuf)
        , m_size(dwSize)
        , m_bOwnMemory(false)
        , m_bJunk(false)
        , m_bVirtualAlloc(false)
    {
    }

    // Move assignment operator.
    CBinaryBuffer& operator=(CBinaryBuffer&& other) noexcept
    {
        if (this != &other)
        {
            // Free the existing resource.
            RemoveAll();  // ! may throw because of GetBinaryBufferHeap()

            // Copy the data pointer and its length from the
            // source object.
            m_pData = other.m_pData;
            m_bVirtualAlloc = other.m_bVirtualAlloc;
            m_size = other.m_size;
            m_bOwnMemory = other.m_bOwnMemory;
            m_bJunk = other.m_bJunk;

            if (m_bOwnMemory)
            {
                // Release the data pointer from the source object so that
                // the destructor does not free the memory multiple times.
                other.m_pData = nullptr;
                other.m_size = 0;
                other.m_bOwnMemory = true;
                other.m_bJunk = true;
            }
        }
        return *this;
    }

    bool CheckCount(size_t NewSize)
    {
        if (m_bOwnMemory && NewSize > m_size)
        {
            return SetCount(NewSize);
        }
        else if (NewSize > m_size)
            return false;
        return true;
    }

    bool SetCount(size_t NewSize);
    BYTE* GetData() const { return m_pData; }

    inline operator std::string_view() const
    {
        return std::string_view(reinterpret_cast<char*>(GetData()), GetCount());
    }

    HRESULT SetData(LPCBYTE pBuffer, size_t cbSize);
    HRESULT CopyTo(LPBYTE pBuffer, size_t cbSize);

    template <class T>
    const T& Get(size_t index = 0) const
    {
        if ((sizeof(T) * (index + 1)) > GetCount())
        {
            throw "Invalid range";
            ;
        }
        return *(reinterpret_cast<T*>(m_pData) + index);
    };

    template <class T>
    T& Get(size_t index = 0)
    {
        if ((sizeof(T) * (index + 1)) > GetCount())
        {
            throw "Invalid range";
            ;
        }
        return *(reinterpret_cast<T*>(m_pData) + index);
    };

    template <class T>
    const T* GetP(size_t index = 0) const
    {
        if ((sizeof(T) * (index + 1)) > GetCount())
        {
            throw "Invalid range";
            ;
        }
        return reinterpret_cast<T*>(m_pData) + index;
    };

    template <class T>
    T* GetP(size_t index = 0)
    {
        if ((sizeof(T) * (index + 1)) > GetCount())
        {
            throw "Invalid range";
            ;
        }
        return reinterpret_cast<T*>(m_pData) + index;
    };

    const BYTE& operator[](size_t index) const
    {
        if ((sizeof(BYTE) * (index + 1)) > GetCount())
        {
            throw "Invalid range";
        }
        return *(reinterpret_cast<BYTE*>(m_pData) + index);
    }

    BYTE& operator[](size_t index)
    {
        if ((sizeof(BYTE) * (index + 1)) > GetCount())
        {
            throw "Invalid range";
        }
        return *(reinterpret_cast<BYTE*>(m_pData) + index);
    }

    template <class T>
    T& operator[](size_t index)
    {
        if ((sizeof(T) * (index + 1)) > GetCount())
        {
            throw "Invalid range";
        }
        return *(reinterpret_cast<T*>(m_pData) + index);
    };

    void RemoveAll();

    template <typename T = char>
    size_t GetCount() const
    {
        if (nullptr == m_pData)
            return 0;
        return m_size / sizeof(T);
    }

    HRESULT GetSHA1(CBinaryBuffer& SHA1);
    HRESULT GetMD5(CBinaryBuffer& MD5);

    bool OwnsBuffer() const { return m_bOwnMemory; };
    void DeclareDataJunk() { m_bJunk = true; }

    std::wstring ToHex(bool b0xPrefix = false) const;

    void ZeroMe();

    bool empty() const { return m_size == 0; };

    const BYTE* cbegin() const { return m_pData; };
    const BYTE* cend() const { return m_pData + m_size; };

    BYTE* begin() { return m_pData; };
    BYTE* end() { return m_pData + m_size; };

    const BYTE* begin() const { return m_pData; };
    const BYTE* end() const { return m_pData + m_size; };

    template <class T>
    const T* cbegin() const
    {
        return (T*)m_pData;
    };

    template <class T>
    const T* cend() const
    {
        return (T*)((BYTE*)m_pData + m_size);
    };

    template <class T>
    const T* begin() const
    {
        return (T*)m_pData;
    };

    template <class T>
    const T* end() const
    {
        return (T*)((BYTE*)m_pData + m_size);
    };

    template <class T>
    T* begin()
    {
        return (T*)m_pData;
    };

    template <class T>
    T* end()
    {
        return (T*)((BYTE*)m_pData + m_size);
    };

    bool operator==(const CBinaryBuffer& other) const
    {
        if (GetCount() != other.GetCount())
            return false;
        return !memcmp(m_pData, other.m_pData, m_size);
    };

    ~CBinaryBuffer(void) { RemoveAll(); }
};
}  // namespace Orc

#pragma managed(pop)
