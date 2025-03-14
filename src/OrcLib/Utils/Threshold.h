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

public:
    Threshold()
        : m_operator(GreaterThan)
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
        if constexpr (std::is_integral<T>::value)
        {
            return Text::ToStringView(m_operator) + std::to_wstring(m_value);
        }
        else if constexpr (IsByteQuantity<T>::value)
        {
            std::string s(Text::ToStringView(m_operator, false));
            Text::ToString(std::back_inserter(s), m_value, base);
            return s;
        }
    }

    std::wstring ToWString(ByteQuantityBase base = ByteQuantityBase::Base2) const
    {
        if constexpr (std::is_integral<T>::value)
        {
            return Text::ToWStringView(m_operator) + std::to_wstring(m_value);
        }
        else if constexpr (IsByteQuantity<T>::value)
        {
            std::wstring s(Text::ToWStringView(m_operator, false));
            Text::ToString(std::back_inserter(s), m_value, base);
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
