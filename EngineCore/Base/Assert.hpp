// Assert.hpp

#pragma once
#include <iostream>

// Active only in debug builds.
#ifndef NDEBUG

// Platform-specific debug break.
#if defined(_MSC_VER)
#define EF_BREAK() __debugbreak() // MSVC
#elif defined(__clang__) || defined(__GNUC__)
#define EF_BREAK() __builtin_trap() // Clang, GCC
#else
//#define EF_BREAK() (*(int*)0 = 0) // Last-resort hard crash
#endif

#define EF_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "Assertion Failed: " << message << "\n" \
                      << "File: " << __FILE__ << "\n" \
                      << "Line: " << __LINE__ << std::endl; \
            EF_BREAK(); \
        } \
    } while (false)
#else
#define EF_ASSERT(condition, message) ((void)0)
#endif
