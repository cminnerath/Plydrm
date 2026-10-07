#pragma once

#include <cstdio>
#include <string>

extern int plydrmTestsFailed;

#define PLYDRM_CHECK(cond, msg) \
    do { \
        if (! (cond)) \
        { \
            std::printf ("FAIL [%s:%d] %s\n", __FILE__, __LINE__, std::string (msg).c_str()); \
            ++plydrmTestsFailed; \
        } \
    } while (false)
