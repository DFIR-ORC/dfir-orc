//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2021 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <memory>

#include <boost/smart_ptr/make_local_shared.hpp>

//
// AllocationPolicy is an abstraction layer providing the same generic pointer interface to stack or heap allocated
// resource.
//
// BEWARE: AllocationPolicy behavior has not been heavily tested.
//
// As an example, the instanciator of a template using this policy will be able to choose how an internal resource is
// allocated: stack, reference or any smart pointer. The templated code will then refer to the allocated resource using
// a generic pointer interface like '->Foo()' instead of '.Foo()' for a stack allocation.
//
//
// template <typename BufferT, typename BufferAllocationPolicy = AllocationPolicy::Stack<BufferT>>
// class BufferStreamWrapper
// {
//    BufferStreamWrapper(BufferT buffer)
//      : m_buffer(BufferAllocationPolicy::MakePtr(std::move(buffer)))
//    {
//    }
//
//    // The attribute 'm_buffer' can be allocated on the stack or be a reference
//    void PrintBufferAddress() {
//      std::cout << "Buffer address: " << m_buffer->data() << std::endl;
//    }
//
// private:
//   BufferAllocationPolicy::Ptr m_buffer;
// }
//

namespace Orc {
namespace AllocationPolicy {

template <typename T>
class Stack
{
public:
    class PtrAdapter final
    {
    public:
        template <typename... Args>
        PtrAdapter(Args&&... args)
            : value(std::forward<Args>(args)...)
        {
        }

        constexpr T* operator->() { return &value; }
        constexpr T* operator->() const { return &value; }

        constexpr T& operator*() { return value; }
        constexpr const T& operator*() const { return value; }

    private:
        T value;
    };

    using Ptr = PtrAdapter;

    template <typename... Args>
    static constexpr Ptr MakePtr(Args&&... args)
    {
        return PtrAdapter(std::forward<Args>(args)...);
    }
};

template <typename T>
class Reference
{
public:
    class PtrAdapter final
    {
    public:
        PtrAdapter(T& ref)
            : value(ref)
        {
        }

        constexpr T* operator->() { return &value; }
        constexpr const T* operator->() const { return &value; }

        constexpr T& operator*() { return value; }
        constexpr const T& operator*() const { return value; }

    private:
        T& value;
    };

    using Ptr = PtrAdapter;

    static PtrAdapter MakePtr(T& ref) { return PtrAdapter(ref); }
};

template <typename T>
class ReferenceWrapper
{
public:
    class PtrAdapter final
    {
    public:
        using type = typename T::type;

        PtrAdapter(T& reference_wrapper)
            : value(reference_wrapper.get())
        {
        }

        type* operator->() { return &value; }
        const type* operator->() const { return &value; }

        type& operator*() { return value; }
        const type& operator*() const { return value; }

    private:
        type& value;
    };

    using Ptr = PtrAdapter;

    template <typename U>
    static PtrAdapter MakePtr(U& ref)
    {
        return PtrAdapter(std::reference_wrapper<U>(ref));
    }
};

template <typename T>
class UniquePtr
{
public:
    using Ptr = std::unique_ptr<T>;

    template <typename... Args>
    static Ptr MakePtr(Args&&... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }
};

template <typename T>
class SharedPtr
{
public:
    using Ptr = std::shared_ptr<T>;

    template <typename... Args>
    static Ptr MakePtr(Args&&... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
};

template <typename T>
class LocalSharedPtr
{
public:
    using Ptr = boost::local_shared_ptr<T>;

    template <typename... Args>
    static Ptr MakePtr(Args&&... args)
    {
        return boost::make_local_shared<T>(std::forward<Args>(args)...);
    }
};

}  // namespace AllocationPolicy
}  // namespace Orc
