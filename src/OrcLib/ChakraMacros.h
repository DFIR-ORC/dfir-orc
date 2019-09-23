//-------------------------------------------------------------------------------------------------------
// Copyright (C) 2016 HHD Software Ltd.
// Written by Alexander Bessonov
//
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#pragma once

// Boost.Preprocessor
#include <boost/preprocessor/repetition/enum_params.hpp>

#include "ChakraBridge.h"

#pragma managed(push, off)

// Use the following macros when creating Chakra object bound to [this].
// JSC_PROP and JSC_PROP_GET bind property accessors to get_ and set_ functions
#define JSC_PROP(name)                                                                                                 \
    .property(                                                                                                         \
        L#name, [this] { return get_##name(); }, [this](const auto& v) { set_##name(v); })  // end of macro
#define JSC_PROP_GET(name) .property(L#name, [this] { return get_##name(); })  // end of macro

// JSC_METHOD binds the method. Only total number of arguments and method name must be specified.
#define JSC_METHOD(ArgCount, name)                                                                                     \
    .method<ArgCount>(L#name, [this](BOOST_PP_ENUM_PARAMS(ArgCount, const auto& p)) {                                  \
        return name(BOOST_PP_ENUM_PARAMS(ArgCount, p));                                                                \
    })  // end of macro

// Use the following macros when declaring Chakra-compatible interface:
// JSC_DECLARE_PROP and JSC_DECLARE_PROP_GET declare property accessor functions
#define JSC_DECLARE_PROP(type, name)                                                                                   \
    virtual type get_##name() = 0;                                                                                     \
    virtual void set_##name(type v) = 0  // end of macro

#define JSC_DECLARE_PROP_GET(type, name) virtual type get_##name() = 0  // end of macro

// No separate macro exists for methods, use plain C++ virtual abstract function for them
#pragma managed(pop)
