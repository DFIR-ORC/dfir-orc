//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2020 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include <type_traits>
#include <iterator>
#include <iostream>

namespace Orc {
namespace Traits {

template <class...>
constexpr std::false_type always_false {};

// clang-format off
template <
    typename T,
    typename U =
        std::decay_t<
        std::remove_cv_t<
        std::remove_pointer_t<
        std::remove_reference_t<
        std::remove_extent_t<
    T>>>>>>
struct remove_all : remove_all<U>
{
};
// clang-format on

template <typename T>
struct remove_all<T, T>
{
    using type = T;
};

template <typename T>
using remove_all_t = typename remove_all<T>::type;

//
// Is true if T has a 'type' member
//
template <class, class = std::void_t<>>
struct has_type_member : std::false_type
{
};

template <class T>
struct has_type_member<T, std::void_t<typename T::type>> : std::true_type
{
};

template <typename T>
static const bool has_type_member_v = has_type_member<T>::value;

//
// Is true if T has a 'value_type' member
//
template <typename T, typename = void>
struct has_value_type_member : std::false_type
{
};

template <typename T>
struct has_value_type_member<T, std::void_t<typename remove_all_t<T>::value_type>> : std::true_type
{
};

template <typename T>
constexpr auto has_value_type_member_v = has_value_type_member<T>::value;

//
// Is true if T has a 'value_type' member
//
template <typename T, typename = void>
struct has_char_type_member : std::false_type
{
};

template <typename T>
struct has_char_type_member<T, std::void_t<typename remove_all_t<T>::char_type>> : std::true_type
{
};

template <typename T>
constexpr auto has_char_type_member_v = has_char_type_member<T>::value;


//
// Provide underlying char type for the given type or 'void'
//

namespace detail {

template <typename... Ts>
struct underlying_char_type;

template <>
struct underlying_char_type<char>
{
    using type = char;
};

template <>
struct underlying_char_type<wchar_t>
{
    using type = wchar_t;
};

template <typename T>
struct underlying_char_type<T>
{
    using type = remove_all_t<typename T::value_type>;
};

template <template <typename, typename...> class X, typename T, typename... Args>
struct underlying_char_type<X<T, Args...>>
{
    using type = remove_all_t<typename underlying_char_type<T>::type>;
};

}  // namespace detail

template <typename T>
struct underlying_char_type
{
    using type = typename detail::underlying_char_type<remove_all_t<T>>::type;
};

template <typename T>
using underlying_char_type_t = typename underlying_char_type<T>::type;

template <typename T, typename Enable = void>
struct is_char_underlying_type
{
};

template <typename T>
struct is_char_underlying_type<T, std::enable_if_t<!has_value_type_member_v<T>>>
{
    using value_type = remove_all_t<T>;
    static constexpr bool value = std::is_same_v<value_type, char> || std::is_same_v<value_type, wchar_t>;
};

template <typename T>
struct is_char_underlying_type<T, std::enable_if_t<has_value_type_member_v<T>>>
{
    using value_type = typename remove_all_t<T>::value_type;
    static constexpr bool value = is_char_underlying_type<value_type>::value;
};

template <typename T>
constexpr bool is_char_underlying_type_v = is_char_underlying_type<T>::value;

//
// check if a character is printable
//
template <typename T>
bool isprintable(T c)
{
    using value_type = underlying_char_type_t<T>;

    if constexpr (std::is_same_v<char, value_type>)
    {
        return ::isprint(static_cast<unsigned char>(c));
    }
    else if constexpr (std::is_same_v<wchar_t, value_type>)
    {
        return ::iswprint(c);
    }
    else
    {
        static_assert(always_false<T>, "Unsupported type");
    }
}

//
// Set std::true_type if parameter is iterable
// https://stackoverflow.com/a/53967057
//
template <typename T, typename = void>
struct is_iterable : std::false_type
{
};

// this gets used only when we can call std::begin() and std::end() on that type
template <typename T>
struct is_iterable<T, std::void_t<decltype(std::cbegin(std::declval<T>())), decltype(std::cend(std::declval<T>()))>>
    : std::true_type
{
};

// Here is a helper:
template <typename T>
constexpr bool is_iterable_v = is_iterable<T>::value;

//
// Provide newline value for the given type
//
template <typename T>
struct newline;

template <>
struct newline<char>
{
    using value_type = char;
    static const value_type value = '\n';
};

template <>
struct newline<wchar_t>
{
    using value_type = wchar_t;
    static const value_type value = L'\n';
};

template <typename T>
using newline_t = typename newline<T>::value_type;

template <typename T>
constexpr auto newline_v = newline<T>::value;

//
// Provide space value for the given type
//
template <typename T>
struct space;

template <>
struct space<char>
{
    using value_type = char;
    static const value_type value = ' ';
};

template <>
struct space<wchar_t>
{
    using value_type = wchar_t;
    static const value_type value = L' ';
};

template <typename T>
using space_t = typename space<T>::value_type;

template <typename T>
constexpr auto space_v = space<T>::value;

//
// Provide space value for the given type
//
template <typename T>
struct dot;

template <>
struct dot<char>
{
    using value_type = char;
    static const value_type value = '.';
};

template <>
struct dot<wchar_t>
{
    using value_type = wchar_t;
    static const value_type value = L'.';
};

template <typename T>
using dot_t = typename dot<T>::value_type;

template <typename T>
constexpr auto dot_v = dot<T>::value;

//
// Provide semi-colon value for the given type
//
template <typename T>
struct semicolon;

template <>
struct semicolon<char>
{
    using value_type = char;
    static const value_type value = ':';
};

template <>
struct semicolon<wchar_t>
{
    using value_type = wchar_t;
    static const value_type value = L':';
};

template <typename T>
using semicolon_t = typename semicolon<T>::value_type;

template <typename T>
constexpr auto semicolon_v = semicolon<T>::value;

//
// Get std::cout or std::wcout depending on parameter
//
template <class T>
auto& get_std_out()
{
    if constexpr (std::is_same_v<T, char>)
    {
        return std::cout;
    }
    else
    {
        return std::wcout;
    }
}

//
// Strong type for integral type used as byte quantity
//
template <typename T>
struct ByteQuantity
{
    using value_type = T;

    explicit ByteQuantity(T quantity)
        : value(quantity)
    {
        if constexpr (!std::is_integral_v<T>)
        {
            static_assert(always_false<T>, "Only integral type are supported");
        }
    }

    operator T&() { return value; }
    operator T() const { return value; }

    T value;
};

//
// Strong type for integral type used as offset
//
template <class T>
struct Offset
{
    using value_type = T;

    explicit Offset(T offset)
    {
        if constexpr (std::is_integral_v<T>)
        {
            value = offset;
        }
        else
        {
            static_assert(always_false<T>, "Only integral type are supported");
        }
    }

    template <typename U>
    operator U() const
    {
        return static_cast<U>(value);
    }

    operator T&() { return value; }
    operator T() const { return value; }
    T value;
};

//
// Strong type for UTC time
//
template <class T>
struct TimeUtc
{
    using value_type = T;

    template <typename... Args>
    explicit TimeUtc(Args&&... args)
        : value(std::forward<Args>(args)...)
    {
    }

    operator T&() { return value; }
    // operator const T&() const { return value; }
    operator T() const { return value; }
    T value;
};

struct Boolean
{
    using value_type = bool;

    explicit Boolean(bool boolean)
        : value(boolean)
    {
    }

    operator bool() { return value; }
    operator bool() const { return value; }

    bool value;
};

struct BoolOnOff
{
    using value_type = bool;

    explicit BoolOnOff(bool on)
        : value(on)
    {
    }

    operator bool() { return value; }
    operator bool() const { return value; }

    bool value;
};

}  // namespace Traits
}  // namespace Orc
