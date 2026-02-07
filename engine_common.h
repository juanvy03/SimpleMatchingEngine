#pragma once
#include <cstdlib>

#ifdef ENGINE_PERF_MODE
    #define ENGINE_ABORT(msg) ((void)0)
#else
    #define ENGINE_ABORT(msg) std::abort()
#endif