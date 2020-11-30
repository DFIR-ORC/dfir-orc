#pragma once

//
// Define operators to enable combining enum class values as flags.
//
// To enable EnumFlags features use:
//
// namespace Foo {
//   ...
//   ENABLE_BITMASK_OPERATORS(FilesystemAttributes);
// }
//
// From http://blog.bitwigglers.org/using-enum-classes-as-type-safe-bitmasks/
//

#include <type_traits>

namespace Orc {

template <typename Enum>
struct EnableBitMaskOperators
{
    static const bool enable = false;
};

template <typename Enum>
typename std::enable_if<EnableBitMaskOperators<Enum>::enable, Enum>::type constexpr operator&(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

template <typename Enum>
typename std::enable_if<EnableBitMaskOperators<Enum>::enable, Enum>::type constexpr operator&=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
    return lhs;
}

template <typename Enum>
typename std::enable_if<EnableBitMaskOperators<Enum>::enable, Enum>::type constexpr operator|(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

template <typename Enum>
typename std::enable_if<EnableBitMaskOperators<Enum>::enable, Enum>::type constexpr operator|=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
    return lhs;
}

template <typename Enum>
typename std::enable_if<EnableBitMaskOperators<Enum>::enable, Enum>::type constexpr operator^(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
}

template <typename Enum>
typename std::enable_if<EnableBitMaskOperators<Enum>::enable, Enum>::type constexpr operator^=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
    return lhs;
}

template <typename Enum>
typename std::enable_if<EnableBitMaskOperators<Enum>::enable, Enum>::type constexpr operator~(Enum value)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<underlying>(value));
}

#define ENABLE_BITMASK_OPERATORS(x)                                                                                    \
    template <>                                                                                                        \
    struct EnableBitMaskOperators<x>                                                                                   \
    {                                                                                                                  \
        static const bool enable = true;                                                                               \
    };

}  // namespace Orc
