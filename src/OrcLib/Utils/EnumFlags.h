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

#include <type_traits>
#include <assert.h>

namespace Orc {

template <typename Enum, typename Enabler = void>
struct EnumFlagsOperator : std::false_type
{
};

#define ENABLE_BITMASK_OPERATORS(x)                                                                                    \
    template <>                                                                                                        \
    struct EnumFlagsOperator<x> : public std::true_type                                                                \
    {                                                                                                                  \
    };

template <typename Enum>
typename std::enable_if<EnumFlagsOperator<Enum>::value, Enum>::type constexpr operator&(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

template <typename Enum>
typename std::enable_if<EnumFlagsOperator<Enum>::value, Enum>::type constexpr operator&=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
    return lhs;
}

template <typename Enum>
typename std::enable_if<EnumFlagsOperator<Enum>::value, Enum>::type constexpr operator|(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

template <typename Enum>
typename std::enable_if<EnumFlagsOperator<Enum>::value, Enum>::type constexpr operator|=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
    return lhs;
}

template <typename Enum>
typename std::enable_if<EnumFlagsOperator<Enum>::value, Enum>::type constexpr operator^(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
}

template <typename Enum>
typename std::enable_if<EnumFlagsOperator<Enum>::value, Enum>::type constexpr operator^=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
    return lhs;
}

template <typename Enum>
typename std::enable_if<EnumFlagsOperator<Enum>::value, Enum>::type constexpr operator~(Enum value)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<underlying>(value));
}

template <typename Enum>
inline typename std::enable_if<EnumFlagsOperator<Enum>::value, bool>::type HasFlag(Enum value, Enum flag)
{
    using underlying = typename std::underlying_type_t<Enum>;

    //
    // It makes a little sense to compare against '0' and has been source of bug with expressions like:
    //
    //  if (HasFlag(config.Output.Type, OutputSpec::Kind::None))
    //    ...
    //
    // See also: https://github.com/DFIR-ORC/dfir-orc/pull/39
    //
    assert(static_cast<underlying>(flag) != 0);

    if (static_cast<underlying>(flag) == 0)
    {
        return value == flag;
    }

    return (value & flag) == flag;
}

template <typename Enum>
inline typename std::enable_if<EnumFlagsOperator<Enum>::value, bool>::type HasAnyFlag(Enum value, Enum flag)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return (static_cast<underlying>(value) & static_cast<underlying>(flag)) != 0;
}

}  // namespace Orc
