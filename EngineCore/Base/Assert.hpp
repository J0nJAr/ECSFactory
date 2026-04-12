// Assert.h 

#pragma once
#include <iostream>

// 디버그 모드에서만 작동하도록 설정
#ifndef NDEBUG

// 디버그 중단 매크로
#if defined(_MSC_VER)
#define EF_BREAK() __debugbreak() // MSVC
#elif defined(__clang__) || defined(__GNUC__)
#define EF_BREAK() __builtin_trap() // Clang, GCC
#else
//#define EF_BREAK() (*(int*)0 = 0) // 최후의 수단: 강제 크래시
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

