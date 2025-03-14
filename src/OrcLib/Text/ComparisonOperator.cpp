//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright © 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#include "ComparisonOperator.h"

using namespace Orc::Text;
using namespace Orc;

using namespace std::literals::string_view_literals;

namespace {

static_assert(
    std::underlying_type_t<ComparisonOperator>(ComparisonOperator::Undefined) == 0,
    "Check enum values so they are ordered for 'kStringViewArray'");

constexpr auto enum_count = static_cast<std::underlying_type_t<ComparisonOperator>>(ComparisonOperator::EnumCount);

constexpr std::array<std::tuple<ComparisonOperator, std::string_view>, enum_count + 1> kStringViewArray = {
    std::make_tuple(ComparisonOperator::Undefined, "<Undefined>"sv),
    std::make_tuple(ComparisonOperator::LessThan, "<"sv),
    std::make_tuple(ComparisonOperator::LessThanOrEqual, "<="sv),
    std::make_tuple(ComparisonOperator::Equal, "=="sv),
    std::make_tuple(ComparisonOperator::GreaterThan, ">"sv),
    std::make_tuple(ComparisonOperator::GreaterThanOrEqual, ">="sv),
    std::make_tuple(ComparisonOperator::NotEqual, "!="sv),
    std::make_tuple(ComparisonOperator::EnumCount, "<Unknown>"sv)};

// cpp17 limitation ?
constexpr std::array<std::tuple<ComparisonOperator, std::wstring_view>, enum_count + 1> kWStringViewArray = {
    std::make_tuple(ComparisonOperator::Undefined, L"<Undefined>"sv),
    std::make_tuple(ComparisonOperator::LessThan, L"<"sv),
    std::make_tuple(ComparisonOperator::LessThanOrEqual, L"<="sv),
    std::make_tuple(ComparisonOperator::Equal, L"=="sv),
    std::make_tuple(ComparisonOperator::GreaterThan, L">"sv),
    std::make_tuple(ComparisonOperator::GreaterThanOrEqual, L">="sv),
    std::make_tuple(ComparisonOperator::NotEqual, L"!="sv),
    std::make_tuple(ComparisonOperator::EnumCount, L"<Unknown>"sv)};

template <typename T>
Result<std::pair<ComparisonOperator, std::basic_string_view<T>>>
ComparisonOperatorFromString(std::basic_string_view<T> expression, std::optional<ComparisonOperator> defaultOperator)
{
    size_t start = 0;
    while (start < expression.size() && std::isspace(expression[start]))
    {
        ++start;
    }

    expression = expression.substr(start);
    if (expression.empty())
    {
        return std::errc::invalid_argument;
    }

    ComparisonOperator op;
    uint8_t opLength;

    // Symbol based operators
    if (expression[0] == '<')
    {
        if (expression.size() > 1 && expression[1] == '=')
        {
            op = ComparisonOperator::LessThanOrEqual;
            opLength = 2;
        }
        else
        {
            op = ComparisonOperator::LessThan;
            opLength = 1;
        }
    }
    else if (expression[0] == '=')
    {
        op = ComparisonOperator::Equal;
        opLength = expression.size() > 1 && expression[1] == '=' ? 2 : 1;
    }
    else if (expression[0] == '>')
    {
        if (expression.size() > 1 && expression[1] == '=')
        {
            op = ComparisonOperator::GreaterThanOrEqual;
            opLength = 2;
        }
        else
        {
            op = ComparisonOperator::GreaterThan;
            opLength = 1;
        }
    }
    else if (expression.size() > 1 && expression[0] == '!' && expression[1] == '=')
    {
        op = ComparisonOperator::NotEqual;
        opLength = 2;
    }
    // Text based operators
    else if (
        expression.size() >= 2 && (expression[0] == 'l' || expression[0] == 'L')
        && (expression[1] == 't' || expression[1] == 'T'))
    {
        if (expression.size() >= 3 && (expression[2] == 'e' || expression[2] == 'E'))
        {
            op = ComparisonOperator::LessThanOrEqual;
            opLength = 3;
        }
        else
        {
            op = ComparisonOperator::LessThan;
            opLength = 2;
        }
    }
    else if (
        expression.size() >= 2 && (expression[0] == 'g' || expression[0] == 'G')
        && (expression[1] == 't' || expression[1] == 'T'))
    {
        if (expression.size() >= 3 && (expression[2] == 'e' || expression[2] == 'E'))
        {
            op = ComparisonOperator::GreaterThanOrEqual;
            opLength = 3;
        }
        else
        {
            op = ComparisonOperator::GreaterThan;
            opLength = 2;
        }
    }
    else if (
        expression.size() >= 2 && (expression[0] == 'e' || expression[0] == 'E')
        && (expression[1] == 'q' || expression[1] == 'Q'))
    {
        op = ComparisonOperator::Equal;
        opLength = 2;
    }
    else if (
        expression.size() >= 3 && (expression[0] == 'n' || expression[0] == 'N')
        && (expression[1] == 'e' || expression[1] == 'E') && (expression[2] == 'q' || expression[2] == 'Q'))
    {
        op = ComparisonOperator::NotEqual;
        opLength = 3;
    }
    else if (defaultOperator.has_value())
    {
        op = *defaultOperator;
        opLength = 0;
    }
    else
    {
        return std::errc::invalid_argument;
    }

    return std::make_pair(op, expression.substr(opLength));
}

}  // namespace

namespace Orc {
namespace Text {

Result<std::pair<ComparisonOperator, std::string_view>>
ComparisonOperatorFromString(std::string_view expression, std::optional<ComparisonOperator> defaultOperator)
{
    return ::ComparisonOperatorFromString<char>(expression, defaultOperator);
}

Result<std::pair<ComparisonOperator, std::wstring_view>>
ComparisonOperatorFromString(std::wstring_view expression, std::optional<ComparisonOperator> defaultOperator)
{
    return ::ComparisonOperatorFromString<wchar_t>(expression, defaultOperator);
}

std::string_view ToStringView(const ComparisonOperator type, bool displayEqual)
{
    using underlying_type = std::underlying_type_t<ComparisonOperator>;
    constexpr auto enum_count = static_cast<underlying_type>(ComparisonOperator::EnumCount);

    auto id = static_cast<underlying_type>(type);
    if (id >= enum_count)
    {
        id = static_cast<underlying_type>(ComparisonOperator::EnumCount);
    }

    if (type == ComparisonOperator::Equal && !displayEqual)
    {
        return {};
    }

    return std::get<1>(kStringViewArray[id]);
}

std::wstring_view ToWStringView(const ComparisonOperator type, bool displayEqual)
{
    using underlying_type = std::underlying_type_t<ComparisonOperator>;
    constexpr auto enum_count = static_cast<underlying_type>(ComparisonOperator::EnumCount);

    auto id = static_cast<underlying_type>(type);
    if (id >= enum_count)
    {
        id = static_cast<underlying_type>(ComparisonOperator::EnumCount);
    }

    if (type == ComparisonOperator::Equal && !displayEqual)
    {
        return {};
    }

    return std::get<1>(kWStringViewArray[id]);
}

}  // namespace Text
}  // namespace Orc
