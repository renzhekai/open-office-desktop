#pragma once

// The pinned AGG header still uses the removed C++17 `register` storage
// specifier. Qt 6 requires C++17 and MSVC's strict parser rejects that token.
// Load only this real upstream header with the obsolete hint removed, then
// restore the preprocessor state before any Qt/wrapper implementation loads.
// This changes neither the upstream checkout nor any type/algorithm/layout.
#if defined(_MSC_VER) && __cplusplus >= 201703L
#pragma push_macro("register")
#undef register
#define register
#include <agg-2.4/include/agg_basics.h>
#undef register
#pragma pop_macro("register")
#endif
