//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2011-2019 ANSSI. All Rights Reserved.
//
// Author(s): Jean Gautier (ANSSI)
//
#pragma once

#include "OrcException.h"

#include "Unicode.h"

#include <variant>

#pragma managed(push, off)

namespace Orc {

namespace Detail {

using namespace std::string_view_literals;

template <typename _T>
class BufferView
{

    template <typename _TT, size_t _DeclElts>
    friend class Buffer;

    BufferView(_T* ptr, ULONG Elts)
    {
        m_Ptr = ptr;
        m_Elts = Elts;
    }

public:
    ULONG size() const { return m_Elts; }
    _T* get() const { return m_Ptr; }
    const _T& operator[](ULONG idx) const
    {
        if (idx >= m_Elts)
            throw Exception(Severity::Fatal, E_INVALIDARG, L"Invalid index acces into BufferExView"sv);
        return m_Ptr[idx];
    }

private:
    _T* m_Ptr;
    ULONG m_Elts;
};

template <typename _T, size_t _DeclElts>
class Buffer
{
private:
    struct HeapStore
    {

        HeapStore(ULONG Elts)
            : m_Ptr(new _T[Elts])
            , m_EltsAlloc(Elts)
        {
            if (!m_Ptr)
                throw Orc::Exception(Severity::Fatal, E_OUTOFMEMORY);
        }
        HeapStore(_T* Ptr, ULONG Elts)
            : m_Ptr(Ptr)
            , m_EltsAlloc(Elts)
        {
            if (!m_Ptr)
                throw Orc::Exception(Severity::Fatal, E_POINTER);
        };
        HeapStore(HeapStore&& other) noexcept
        {
            std::swap(m_Ptr, other.m_Ptr);
            std::swap(m_EltsAlloc, other.m_EltsAlloc);
        }
        HeapStore(const HeapStore& other) = delete;

        HeapStore& operator=(HeapStore&& other) noexcept
        {
            std::swap(m_Ptr, other.m_Ptr);
            std::swap(m_EltsAlloc, other.m_EltsAlloc);
            return *this;
        };
        HeapStore& operator=(const HeapStore& other) = delete;

        ~HeapStore() { clear(); }

        void assign(const _T* Ptr, ULONG Elts)
        {
            if (Elts > m_EltsAlloc)
            {
                reserve(Elts);
            }
            std::copy(Ptr, Ptr + Elts, stdext::checked_array_iterator(m_Ptr, m_EltsAlloc));
            return;
        }

        void clear(void)
        {
            if (m_Ptr)
            {
                _T* Ptr = nullptr;
                std::swap(m_Ptr, Ptr);
                delete[] Ptr;
            }
            m_EltsAlloc = 0;
        }

        ULONG capacity() const { return m_EltsAlloc; }

        void reserve(ULONG Elts)
        {
            if (Elts < m_EltsAlloc)
                return;

            _T* OldPtr = nullptr;
            std::swap(OldPtr, m_Ptr);
            ULONG OldEltsAlloc = 0LU;
            std::swap(OldEltsAlloc, m_EltsAlloc);

            m_Ptr = new _T[Elts];
            m_EltsAlloc = Elts;
            std::copy(OldPtr, OldPtr + OldEltsAlloc, stdext::checked_array_iterator(m_Ptr, m_EltsAlloc));

            delete[] OldPtr;
        }

        _T* get(ULONG index = 0) const { return m_Ptr + index; }

        _T* relinquish()
        {
            auto ptr = m_Ptr;
            m_Ptr = nullptr;
            m_EltsAlloc = 0;
            return ptr;
        }

        _T* m_Ptr = nullptr;
        ULONG m_EltsAlloc = 0L;
    };

    struct ViewStore
    {

        ViewStore(_T* Ptr, ULONG Elts)
            : m_Ptr(Ptr)
            , m_EltsInView(Elts)
        {
            if (m_Ptr == nullptr)
                throw Orc::Exception(Severity::Fatal, E_POINTER);
        };

        ViewStore(ViewStore&& other) noexcept
        {
            std::swap(m_Ptr, other.m_Ptr);
            std::swap(m_EltsInView, other.m_EltsInView);
        }
        ViewStore& operator=(ViewStore&& other) noexcept
        {
            std::swap(m_Ptr, other.m_Ptr);
            std::swap(m_EltsInView, other.m_EltsInView);
            return *this;
        };

        // explicitely deleting copy operators
        ViewStore(const ViewStore& other) = delete;
        ViewStore& operator=(const ViewStore& other) = delete;

        ~ViewStore() { clear(); }

        void assign(const _T* Ptr, ULONG Elts)
        {
            if (Elts > m_EltsInView)
                throw Orc::Exception(Severity::Fatal, E_OUTOFMEMORY);

            std::copy(Ptr, Ptr + Elts, stdext::checked_array_iterator(m_Ptr, m_EltsInView));
            return;
        }

        void clear(void)
        {
            m_Ptr = nullptr;
            m_EltsInView = 0;
        }

        ULONG capacity() const { return m_EltsInView; }

        void reserve(ULONG Elts)
        {
            if (Elts > m_EltsInView)
                throw Orc::Exception(Severity::Fatal, E_OUTOFMEMORY);
        }

        _T* get(ULONG index = 0) const { return m_Ptr + index; }

        _T* relinquish()
        {
            auto ptr = new _T[m_EltsInView];
            std::copy(m_Ptr, m_Ptr + m_EltsInView, stdext::checked_array_iterator(ptr, m_EltsInView));
            m_Ptr = nullptr;
            m_EltsAlloc = 0;
            m_Owned = false;
            return ptr;
        }

        _T* m_Ptr = nullptr;
        ULONG m_EltsInView = 0L;
    };

    struct InnerStore
    {

        InnerStore() = default;
        InnerStore(InnerStore&& other) noexcept = default;
        InnerStore(const InnerStore& other) = delete;

        InnerStore& operator=(InnerStore&& other) noexcept = default;
        InnerStore& operator=(const InnerStore& other) = delete;

        template <typename Iterator>
        InnerStore(Iterator begin, Iterator end)
        {
            std::copy(begin, end, stdext::checked_array_iterator(m_Elts, _DeclElts));
        }

        ULONG capacity() const { return _DeclElts; }
        void reserve(ULONG Elts)
        {
            if (Elts > _DeclElts)
                throw Orc::Exception(
                    Severity::Fatal, L"Cannot reserve %d elements in a {} inner store"sv, Elts, _DeclElts);
        }
        void assign(const _T* Ptr, ULONG Elts)
        {
            if (Elts > _DeclElts)
            {
                throw Orc::Exception(
                    Severity::Fatal, L"Cannot assign %d elements to a {} inner store"sv, Elts, _DeclElts);
            }
            std::copy(Ptr, Ptr + Elts, stdext::checked_array_iterator(m_Elts, _DeclElts));
            return;
        }

        _T* get(ULONG index = 0) const { return const_cast<_T*>(m_Elts + index); }

        _T* relinquish()
        {
            auto ptr = new _T[_DeclElts];
            std::copy(m_Elts, m_Elts + _DeclElts, stdext::checked_array_iterator(pre, _DeclElts));
            return ptr;
        }

        bool owns() const { return true; }

        _T m_Elts[_DeclElts];
    };

    struct EmptyStore
    {
        EmptyStore() {};
        EmptyStore(EmptyStore&& other) noexcept = default;
        EmptyStore(const EmptyStore& other) = delete;

        EmptyStore& operator=(EmptyStore&& other) noexcept = default;
        EmptyStore& operator=(const EmptyStore& other) = delete;

        ULONG capacity() const { return 0; }
        void reserve(ULONG Elts) { throw Orc::Exception(Continue, L"Cannot reserve {} elements in empty store"sv, Elts); }
        void assign(const _T* Ptr, ULONG Elts)
        {
            throw Orc::Exception(Severity::Continue, L"Cannot assign {} elements to empty store"sv, Elts);
        }

        _T* get(ULONG index = 0) const { return nullptr; }

        _T* relinquish() { return nullptr; }
    };

public:
    using Store = std::variant<EmptyStore, InnerStore, HeapStore, ViewStore>;

    Buffer() {}
    Buffer(Buffer&& other) noexcept
    {
        std::swap(m_store, other.m_store);
        std::swap(m_EltsUsed, other.m_EltsUsed);
    }
    explicit Buffer(const Buffer& other) { assign(other); }

    Buffer(_In_reads_(Elts) _T* Ptr, _In_ ULONG Elts, _In_ bool Owned, _In_ ULONG Used) { set(Ptr, Elts, Owned, Used); }

    Buffer(const std::initializer_list<_T>& list)
    {
        resize((ULONG)list.size());
        std::copy(std::begin(list), std::end(list), stdext::checked_array_iterator(get(), capacity()));
        m_EltsUsed = (ULONG)list.size();
    }

    ~Buffer(void) {}

    bool is_heap() const { return std::holds_alternative<HeapStore>(m_store); }
    bool is_inner() const { return std::holds_alternative<InnerStore>(m_store); }
    bool is_view() const { return std::holds_alternative<ViewStore>(m_store); }

    bool full() const { return capacity() == size(); }

    bool empty() const { return std::holds_alternative<EmptyStore>(m_store); }
    void set(_In_reads_(Elts) _T* Ptr, _In_ ULONG Elts, _In_ ULONG Used)
    {
        if (Elts == 0)
            m_store = EmptyStore();
        else if (Elts <= _DeclElts)
        {
            m_store = InnerStore(Ptr, Ptr + Elts);
        }
        else
            m_store = HeapStore(Ptr, Elts);

        if (Used <= Elts)
            m_EltsUsed = Used;
    }

    void setUsed(_In_reads_(Elts) _T* Ptr, _In_ ULONG Elts) { set(Ptr, Elts, Elts); }
    void setUnused(_In_reads_(Elts) _T* Ptr, _In_ ULONG Elts) { set(Ptr, Elts, 0); }

    void resize(_In_ ULONG Elts, _In_ bool allow_growth = true)
    {
        if (Elts > capacity())
        {
            if (!allow_growth)
                throw Orc::Exception(
                    Severity::Continue,
                    E_INVALIDARG,
                    L"BufferEx::resize({}) illegal elt count (> {})"sv,
                    Elts,
                    capacity());
            reserve(Elts);
        }

        m_EltsUsed = Elts;
    }

    void assign(const _T* Ptr, ULONG Used = 1)
    {

        if (!Ptr)
            return;
        if (Used > capacity())
        {
            if (!empty())
                clear();
            reserve(Used);
        }
        std::visit([Ptr, Used](auto&& arg) { return arg.assign(Ptr, Used); }, m_store);
        m_EltsUsed = Used;
        return;
    }

    void assign(_In_ const Buffer& Other) { assign(Other.get_raw(), Other.size()); }
    void assign(_In_ const BufferView<_T>& Other) { assign(Other.m_Ptr, Other.m_Elts); }

    const Buffer& view_of(_T* Ptr, ULONG EltsInView, std::optional<ULONG> InUse = std::nullopt)
    {

        if (!empty())
            clear();

        if (!Ptr)
            return *this;

        m_store = ViewStore(Ptr, EltsInView);
        m_EltsUsed = InUse.value_or(0LU);
        return *this;
    }

    void view_of(_In_ const Buffer& Other, std::optional<ULONG> InUse = std::nullopt)
    {
        view_of(Other.get_raw(), Other.size(), InUse);
    }
    void view_of(_In_ const BufferView<_T>& Other, std::optional<ULONG> InUse = std::nullopt)
    {
        view_of(Other.m_Ptr, Other.m_Elts, InUse);
    }

    void unview()
    {
        if (is_view())
        {
            if (Elts <= _DeclElts)
            {
                m_store = InnerStore();
                std::copy(Src, Src + Elts, stdext::checked_array_iterator(get() + m_EltsUsed, capacity() - m_EltsUsed));
            }
        }
    }

    void append(_In_reads_(Elts) const _T* Src, _In_ ULONG Elts = 1)
    {
        if (!Src)
            return;

        auto capacity_ = capacity();
        auto size_ = size();
        auto new_size = size_ + Elts;

        if (new_size > capacity_)
        {
            if (Elts == 1)
            {
                auto new_capacity = std::max(size_ + Elts, capacity_ + capacity_ / 2);
                if (new_capacity < _DeclElts)
                    new_capacity = _DeclElts;
                reserve(new_capacity);
                capacity_ = new_capacity;
            }
            else
            {
                reserve(new_size);
                capacity_ = new_size;
            }
        }

        if (is_view() && (capacity_ < new_size))  // current store is a view, and is too small to contains the new
                                                  // elts -> we need to "mutate" to a owning one.
        {
            if (new_size <= _DeclElts)
            {
                auto new_store = InnerStore();

                std::visit([&new_store](const auto& arg) { new_store.assign(arg.get(), arg.capacity()); }, m_store);
                m_store = std::move(new_store);
                capacity_ = _DeclElts;
            }
            else
            {
                auto new_capacity = std::max(size_ + Elts, capacity_ + capacity_ / 2);
                auto new_store = HeapStore(new_capacity);
                std::visit([&new_store](const auto& arg) { new_store.assign(arg.get(), arg.capacity()); }, m_store);
                m_store = std::move(new_store);
                capacity_ = new_capacity;
            }
        }

        std::copy(Src, Src + Elts, stdext::checked_array_iterator(get() + size_, capacity_ - size_));
        m_EltsUsed = new_size;
    }
    void append(_In_ const Buffer& Other) { append(Other.get_raw(), Other.size()); }
    void append(_In_ BufferView<_T>&& Other) { append(Other.m_Ptr, Other.m_Elts); }

    void push_back(const _T& elt)
    {
        if (m_EltsUsed < capacity())
        {
            *get_raw(m_EltsUsed) = elt;
            m_EltsUsed++;
        }
        else
        {
            append(&elt, 1);
        }
    }

    void push_back(_T&& elt)
    {
        if (m_EltsUsed < capacity())
        {
            *get_raw(m_EltsUsed) = elt;
            m_EltsUsed++;
        }
        else
        {
            append(&elt, 1);
        }
    }

    void reserve(_In_ ULONG Elts)
    {
        if (Elts > (ULONG_PTR)-1 / sizeof(_T))
        {
            throw Orc::Exception(
                Severity::Fatal, HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW), L"BufferEx::Resize count overflow"sv);
        }
        if (Elts == 0L)
        {
            m_store = EmptyStore();
            m_EltsUsed = 0;
            return;
        }
        if (Elts <= _DeclElts)
        {
            if (!std::holds_alternative<InnerStore>(m_store))
                m_store = InnerStore();
        }
        else
        {
            if (Elts > capacity())
            {
                auto new_store = HeapStore(Elts);
                if (const auto current = get_raw())
                {
                    std::copy(current, current + size(), stdext::checked_array_iterator(new_store.get(), Elts));
                }

                m_store = std::move(new_store);
            }
        }

        if (Elts < m_EltsUsed)
            m_EltsUsed = Elts;
    }

    void clear()
    {
        m_store = EmptyStore();
        m_EltsUsed = 0L;
    }

    ULONG size(void) const { return m_EltsUsed; }
    constexpr static size_t elt_size(void) { return sizeof(_T); }
    constexpr static size_t inner_elts(void) { return _DeclElts; }

    void use(ULONG elts_used)
    {
        auto max_elts = capacity();
        if (elts_used < max_elts)
            m_EltsUsed = elts_used;
        else
            m_EltsUsed = max_elts;
    }

    ULONG capacity(void) const
    {
        return std::visit([](auto&& arg) -> ULONG { return arg.capacity(); }, m_store);
    }

    _T* relinquish(void)
    {
        return std::visit([](auto&& arg) -> _T* { return arg.relinquish(); }, m_store);
    }

    _T* get(ULONG index = 0) const
    {
        if (index && index >= m_EltsUsed)
            throw Exception(Severity::Fatal, E_INVALIDARG, L"Invalid index get({}) (size={})"sv, index, m_EltsUsed);

        auto ptr = get_raw(index);

        if (!ptr)
        {
            throw Exception(Severity::Fatal, E_INVALIDARG, L"Buffer NULL reference"sv);
        }
        return ptr;
    }

    template <typename _T_as>
    const _T_as* const get_as() const
    {

        auto ptr = get_raw();

        if (!ptr)
        {
            throw Exception(Severity::Fatal, E_INVALIDARG, L"Buffer NULL reference"sv);
        }
        return reinterpret_cast<const _T_as* const>(ptr);
    }

    _T* get_raw(ULONG index = 0) const
    {
        return std::visit(
            [index](auto&& arg) -> auto { return arg.get(index); }, m_store);
    }

    explicit operator _T*(void) const { return get(); }

    bool owns(void) const
    {
        return std::visit(
            [](auto&& arg) -> auto { return arg.owns(); }, m_store);
    }

    template <typename _TT>
    operator BufferView<_TT>() const
    {
        return BufferView<_TT>(reinterpret_cast<_TT*>(get_raw()), size() * (sizeof(_T) / sizeof(_TT)));
    }

    Buffer<_T, _DeclElts>& operator=(_In_ BufferView<_T>&& View)
    {
        assign(View.m_Ptr, View.m_Elts);
        return *this;
    }
    Buffer<_T, _DeclElts>& operator+=(_In_ BufferView<_T>&& View)
    {
        append(View.m_Ptr, View.m_Elts);
        return *this;
    }

    Buffer<_T, _DeclElts>& operator=(_In_ const Buffer<_T, _DeclElts>& Ptr)
    {
        assign(Ptr);
        return *this;
    }
    Buffer<_T, _DeclElts>& operator=(_In_opt_ const _T* Ptr) = delete;

    Buffer<_T, _DeclElts>& operator+=(_In_ const Buffer<_T, _DeclElts>& Ptr)
    {
        append(Ptr);
        return *this;
    }
    Buffer<_T, _DeclElts>& operator+=(_In_opt_ const _T& Elt)
    {
        append(&Elt, 1);
        return *this;
    }

    Buffer<_T, _DeclElts>& operator+=(_In_opt_ const _T* Ptr) = delete;

    const _T& operator[](ULONG idx) const
    {
        if (idx >= m_EltsUsed)
            throw Orc::Exception(Severity::Fatal, L"Index {} out of buffer range of {} elements"sv, idx, m_EltsUsed);
        return get()[idx];
    }

    _T& operator[](ULONG idx)
    {
        if (idx >= m_EltsUsed)
            throw Orc::Exception(Severity::Fatal, L"Index {} out of buffer range of {} elements"sv, idx, m_EltsUsed);
        return get()[idx];
    }

    template <typename _To, typename = std::enable_if_t<std::is_trivial<_To>::value>>
    explicit operator _To() const
    {
        if (sizeof(_To) != (sizeof(_T) * m_EltsUsed))
            throw Orc::Exception(
                Severity::Fatal, L"Illegal conversion from size {} to {}"sv, (sizeof(_T) * m_EltsUsed), sizeof(_To));
        return *(reinterpret_cast<const _To*>(get()));
    }

    static ULONG StrNLen(const CHAR* str, size_t numberOfElements)
    {
        ULONG len = 0;
        const CHAR* pCur = str;

        while (len < numberOfElements && *pCur != 0)
        {
            pCur++;
            len++;
        }
        return len;
    }
    static ULONG StrNLen(const WCHAR* str, size_t numberOfElements)
    {
        ULONG len = 0;
        const WCHAR* pCur = str;

        while (len < numberOfElements && *pCur != 0)
        {
            pCur++;
            len++;
        }
        return len;
    }

    std::wstring AsHexString(bool b0xPrefix = false, ULONG dwMaxBytes = std::numeric_limits<ULONG>::max())
    {
        using namespace std::string_literals;

        if (m_EltsUsed == 0)
            return L""s;

        std::wstring retval;

        constexpr wchar_t szNibbleToHex[] = {L"0123456789ABCDEF"};

        BufferView<UCHAR> view(*this);

        ULONG dumpLength = std::min<ULONG>(view.size(), dwMaxBytes);

        ULONG nLength = dumpLength * 2;
        ULONG nStart = 0;

        if (b0xPrefix)
        {
            nLength += 2;
            retval.resize(nLength);
            retval[0] = L'0';
            retval[1] = L'x';
            nStart = 2;
        }
        else
            retval.resize(nLength);

        for (ULONG i = 0; i < dumpLength; i++)
        {
            UCHAR byte = view[i];

            UCHAR nNibble = byte >> 4;
            retval[2 * i + nStart] = szNibbleToHex[nNibble];

            nNibble = byte & 0x0F;
            retval[2 * i + 1 + nStart] = szNibbleToHex[nNibble];
        }
        return retval;
    }

    std::string AsAnsiString(bool bCanEmbedNull = false) const
    {
        const auto _size = size();
        const auto _ptr = reinterpret_cast<const CHAR*>(get());
        if (bCanEmbedNull)
        {
            return std::string(_ptr, _size);  // No questions asked. We return the complete buffer as a string
        }
        else
        {
            if (_size == 0)
                return std::string();

            auto stringLength = StrNLen(_ptr, _size);

            if (stringLength == 0)
                return std::string();

            if (stringLength < _size)
                return std::string(_ptr, stringLength);  // string is null terminated, we remove the trailing \0
            return std::string(_ptr, _size);  // string is _not_ null terminated, we simply return the complete string
        }
    }

    std::wstring AsUnicodeString(bool bCanEmbedNull = false) const
    {
        const auto _size = size();
        auto _ptr = reinterpret_cast<const WCHAR*>(get());

        if (bCanEmbedNull)
        {
            std::wstring retval;
            Orc::SanitizeString(Orc::xml_string_table, _ptr, _size, retval);
            return retval;
        }
        else
        {
            if (_size == 0)
                return std::wstring();

            auto stringLength = StrNLen(_ptr, _size);

            if (stringLength == 0)
                return std::wstring();

            std::wstring retval;
            if (stringLength < _size)
                Orc::SanitizeString(
                    Orc::xml_string_table,
                    _ptr,
                    stringLength,
                    retval);  // string is null terminated, we remove the trailing \0
            else
                Orc::SanitizeString(
                    Orc::xml_string_table,
                    _ptr,
                    _size,
                    retval);  // string is _not_ null terminated, we simply return the complete string
            return retval;
        }
    }

    using allocator_type = std::allocator<_T>;
    using value_type = typename allocator_type::value_type;
    using const_reference = typename allocator_type::const_reference;
    using difference_type = typename allocator_type::difference_type;
    using size_type = typename allocator_type::size_type;
    using const_pointer = const typename allocator_type::pointer;

    class const_iterator
    {
    public:
        using value_type = typename allocator_type::value_type;
        using const_reference = typename allocator_type::const_reference;
        using const_pointer = const typename allocator_type::pointer;
        using difference_type = typename allocator_type::difference_type;
        using size_type = typename allocator_type::size_type;
        using iterator_category = typename std::bidirectional_iterator_tag;

        const_iterator(const _T* ptr) { m_current = const_cast<_T*>(ptr); }
        const_iterator(const_iterator&& other) noexcept { std::swap(m_current, other.m_current); };
        const_iterator(const const_iterator& other) noexcept { m_current = other.m_current; };

        ~const_iterator() {};

        bool operator==(const const_iterator& other) const { return m_current == other.m_current; }
        bool operator!=(const const_iterator& other) const { return m_current != other.m_current; }

        const_iterator& operator++()
        {
            m_current++;
            return *this;
        }
        const_reference operator*() const { return *m_current; }
        const_pointer operator->() const { return m_current; }

    private:
        mutable _T* m_current;
    };

    class iterator
    {
    public:
        using value_type = typename allocator_type::value_type;
        using reference = typename allocator_type::reference;
        using pointer = typename allocator_type::pointer;
        using iterator_category = typename std::bidirectional_iterator_tag;

        iterator(const _T* ptr) { m_current = const_cast<_T*>(ptr); }
        iterator(iterator&& other) noexcept { std::swap(m_current, other.m_current); };
        iterator(const iterator& other) noexcept { m_current = other.m_current; };

        ~iterator() {};

        bool operator==(const iterator& other) const { return m_current == other.m_current; }
        bool operator!=(const iterator& other) const { return m_current != other.m_current; }

        iterator& operator++()
        {
            m_current++;
            return *this;
        }
        reference operator*() const { return *m_current; }
        pointer operator->() const { return m_current; }

    private:
        mutable _T* m_current;
    };

public:
    iterator begin() const { return iterator(get_raw()); }
    const_iterator cbegin() const { return const_iterator(get_raw()); }
    iterator end() const { return iterator(get_raw() + size()); }
    const_iterator cend() const { return const_iterator(get_raw() + size()); }

private:
    Store m_store;
    ULONG m_EltsUsed = 0;
};

}  // namespace Detail

using ByteBuffer = Detail::Buffer<BYTE, 16>;

template <typename _T, size_t _DeclElts>
using Buffer = Detail::Buffer<_T, _DeclElts>;

}  // namespace Orc

#ifndef __cplusplus_cli

#    include <fmt/format.h>
#    include <fmt/printf.h>

namespace fmt {

template <typename _T, size_t _DeclElts, typename Char>
struct formatter<Orc::Buffer<_T, _DeclElts>, Char>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        auto it = ctx.begin();
        if (*it == ':')
            ++it;
        auto end = it;
        while (*end && *end != '}')
            ++end;

        elt_format.reserve(end - it + 3);
        elt_format.push_back('{');
        elt_format.push_back(':');
        elt_format.append(it, end);
        elt_format.push_back('}');
        elt_format.push_back('\0');

        return end;
    }

    template <typename FormatContext>
    auto format(const Orc::Buffer<_T, _DeclElts>& buffer, FormatContext& ctx)
    {
        for (const auto& item : buffer)
        {
            format_to(ctx.out(), &elt_format[0], item);
        }
        return ctx.out();
    }
    basic_memory_buffer<Char> elt_format;
};
template <typename _T, typename Char>
struct formatter<Orc::Detail::BufferView<_T>, Char>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        auto it = ctx.begin();
        if (*it == ':')
            ++it;
        auto end = it;
        while (*end && *end != '}')
            ++end;

        elt_format.reserve(end - it + 3);
        elt_format.push_back('{');
        elt_format.push_back(':');
        elt_format.append(it, end);
        elt_format.push_back('}');
        elt_format.push_back('\0');

        return end;
    }

    template <typename FormatContext>
    auto format(const Orc::Detail::BufferView<_T>& buffer, FormatContext& ctx)
    {
        for (const auto& item : buffer)
        {
            format_to(ctx.out(), &elt_format[0], item);
        }
        return ctx.out();
    }
    basic_memory_buffer<Char> elt_format;
};
}  // namespace fmt

#endif

#pragma managed(pop)
