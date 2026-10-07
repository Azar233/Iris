#pragma once

// MinGW GCC 15 jump threading miscompiles stb_image's flat RGBE row loop
// (stbi__hdr_load, which also has a legacy RLE goto). A 2x2 image loses the
// second row at -O2/-O3. Restrict the workaround to this third-party code;
// renderer/integrator optimization and all image acceptance thresholds stay intact.
#if defined(__MINGW32__) && defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 15
#pragma GCC push_options
#pragma GCC optimize ("no-thread-jumps")
#endif

#include <stb_image.h>

#if defined(__MINGW32__) && defined(__GNUC__) && !defined(__clang__) && __GNUC__ == 15
#pragma GCC pop_options
#endif
