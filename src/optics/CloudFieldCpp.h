#pragma once

// C++ adapter for `optics/CloudField.h`.
//
// The shared field is written as plain GLSL 3.30 so a GPU driver can compile it directly. That
// leaves three things C++ does not have in the form the shared body uses, and this header supplies
// only those:
//
//   - `clamp`, which C++17 has no unqualified global version of.
//   - `max` / `min` that accept a mixed `(float, double)` argument pair, which the body uses when it
//     compares a float against a literal like `1.0e-3`. `std::max` requires both arguments to have
//     the same type.
//   - `smoothstep`, which GLSL has as a builtin and C++ does not have at all.
//
// `sqrt` and `floor` explicitly use the float overload in std:: below. The global
// C overload set is implementation-dependent and can otherwise differ by compiler.
//
// Keeping the adapter in a separate file is what lets the shared file stay free of dialect
// conditionals -- the GLSL preprocessor is a restricted subset, and every `#ifdef` inside the shared
// text is another chance for a driver to disagree with the C preprocessor about what it means.
//
// **The shims exist only around the shared body.** They are defined here, the shared header is
// included, and they are undefined again immediately after. That is not fastidiousness: `max` and
// `min` as macros break `<algorithm>` and `glm` outright, and a translation unit that includes this
// header must still be able to use both. The one condition this creates is that the shared body may
// only call names GLSL has as builtins -- which is exactly the discipline the shared file documents.
#include <algorithm>
#include <cmath>
#include "optics/CloudNoiseVolume.h"

inline float myrenderer_cloud_offline_sample(float x, float y, float z, int channel) {
    return cloud::canonicalNoiseVolume().sample(x,y,z)[static_cast<std::size_t>(channel)];
}

// `uint` is a GLSL builtin type name. C++ does not have it in the standard library, so this typedef
// supplies it -- and it is not cosmetic. The shared body needs an explicit conversion from `int` to
// an unsigned 32-bit value, and the two obvious spellings both fail: GLSL rejects the C-style
// `(unsigned int)x`, and GCC rejects the functional `unsigned int(x)` because it parses a two-word
// type name followed by a parenthesised expression as a declaration. A single-word type name works
// in both languages, so the shared body writes `uint(x)`.
using uint = unsigned int;

inline float myrenderer_cloud_smoothstep_cpp(double edge0, double edge1, double x) {
    const double span = edge1 - edge0;
    if (span == 0.0) return x < edge0 ? 0.0f : 1.0f;
    const double t = (x - edge0) / span < 0.0 ? 0.0
        : ((x - edge0) / span > 1.0 ? 1.0 : (x - edge0) / span);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

// `MYRENDERER_CLOUD_INLINE` expands to `inline` for C++ and to nothing for GLSL, which has no such
// keyword. Without it every translation unit that includes the shared body emits its own external
// definition and the link fails on duplicate symbols -- which is exactly what happened when the
// shims were simplified and this define was dropped.
#define MYRENDERER_CLOUD_INLINE inline

#define clamp(value, low, high) \
    ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define smoothstep(edge0, edge1, x) myrenderer_cloud_smoothstep_cpp((edge0), (edge1), (x))
// <cmath>'s global C overload set differs between MSVC and GCC. The shared GLSL
// field always takes float sqrt/floor; explicitly select the same C++ overloads
// so one compiler cannot evaluate Worley distance in double before quantization.
#define sqrt(value) std::sqrt(static_cast<float>(value))
#define floor(value) std::floor(static_cast<float>(value))

#include "optics/CloudField.h"

#undef clamp
#undef max
#undef min
#undef smoothstep
#undef sqrt
#undef floor
#undef MYRENDERER_CLOUD_INLINE
