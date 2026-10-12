#pragma once

#include <stdarg.h>
#include <stdio.h>

// Same shape as legacy_log/hal_log: "mtk/<scope>: ..." on stdout, which the
// Java controller pipes to logcat.
static inline void mtk_log(const char* scope, const char* fmt, ...) {
    printf("mtk/%-7s: ", scope);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
}
