#include <fmt/format.h>

template <typename T>
struct EscapeQuote
{
    EscapeQuote(const T& v)
        : value {v}
    {
    }
    const T& value;
};

template <size_t N>
struct EscapeQuote<wchar_t[N]>
{
    EscapeQuote(const wchar_t v[N])
        : value {v}
    {
    }
    const wchar_t* value;
};

template <typename T>
struct fmt::formatter<EscapeQuote<T>, wchar_t>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        // TODO: could eventually forward the format string to 'format_to' call
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const EscapeQuote<T>& value, FormatContext& ctx)
    {
        return fmt::format_to(ctx.out(), L"{}", value.value);
    }
};
