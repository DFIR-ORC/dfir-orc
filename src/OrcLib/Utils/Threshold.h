//
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Copyright 2025 ANSSI. All Rights Reserved.
//
// Author(s): fabienfl (ANSSI)
//

#pragma once

#include "Utils/Result.h"
#include "Utils/TypeTraits.h"
#include "Text/ByteQuantity.h"
#include "Text/ComparisonOperator.h"

namespace Orc {

template <typename T>
class Threshold
{
    template <typename, typename = std::void_t<>>
    struct IsByteQuantity : std::false_type
    {
    };

    template <typename U>
    struct IsByteQuantity<ByteQuantity<U>> : std::true_type
    {
    };

    static constexpr bool kIsIntegral = std::is_integral<T>::value;
    static constexpr bool kIsByteQuantity = IsByteQuantity<T>::value;
    static constexpr bool kIsSupportedType = kIsIntegral || kIsByteQuantity;

public:
    Threshold()
        : m_operator(ComparisonOperator::GreaterThan)
        , m_value()
    {
    }

    Threshold(ComparisonOperator op, T value)
        : m_operator(op)
        , m_value(value)
    {
    }

    Result<bool> Check(const T& value) const
    {
        switch (m_operator)
        {
            case ComparisonOperator::LessThan:
                return value < m_value;
            case ComparisonOperator::LessThanOrEqual:
                return value <= m_value;
            case ComparisonOperator::Equal:
                return value == m_value;
            case ComparisonOperator::GreaterThanOrEqual:
                return value >= m_value;
            case ComparisonOperator::GreaterThan:
                return value > m_value;
            case ComparisonOperator::NotEqual:
                return value != m_value;
            default:
                return std::errc::invalid_argument;
        }
    }

    std::string ToString(ByteQuantityBase base = ByteQuantityBase::Base2) const
    {
        static_assert(kIsSupportedType, "Threshold<T>::ToString: T must be integral or ByteQuantity<U>");

        if constexpr (kIsIntegral)
        {
            return std::string(Text::ToStringView(m_operator, false)) + std::to_string(m_value);
        }
        else if constexpr (kIsByteQuantity)
        {
            std::string s(Text::ToStringView(m_operator, false));
            Orc::Text::ToString(std::back_inserter(s), m_value, base);
            return s;
        }
    }

    std::wstring ToWString(ByteQuantityBase base = ByteQuantityBase::Base2) const
    {
        static_assert(kIsSupportedType, "Threshold<T>::ToWString: T must be integral or ByteQuantity<U>");

        if constexpr (kIsIntegral)
        {
            return std::wstring(Text::ToWStringView(m_operator, false)) + std::to_wstring(m_value);
        }
        else if constexpr (kIsByteQuantity)
        {
            std::wstring s(Text::ToWStringView(m_operator, false));
            Orc::Text::ToWString(std::back_inserter(s), m_value, base);
            return s;
        }
    }

    const T& Value() const { return m_value; }
    ComparisonOperator Operator() const { return m_operator; }

private:
    ComparisonOperator m_operator;
    T m_value;
};

}  // namespace Orc
