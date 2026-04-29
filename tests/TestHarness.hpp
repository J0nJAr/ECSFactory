#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Test {
    struct Failure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    struct Case {
        const char* suite;
        const char* name;
        void (*func)();
    };

    inline std::vector<Case>& Registry() {
        static std::vector<Case> cases;
        return cases;
    }

    struct Registrar {
        Registrar(const char* suite, const char* name, void (*func)()) {
            Registry().push_back(Case{suite, name, func});
        }
    };

    template<typename Left, typename Right>
    [[noreturn]] inline void FailEqual(
        const char* leftExpr,
        const char* rightExpr,
        const Left& left,
        const Right& right,
        const char* file,
        int line) {
        std::ostringstream oss;
        oss << file << ":" << line << " Expected equality: "
            << leftExpr << " == " << rightExpr
            << ", actual " << left << " vs " << right;
        throw Failure(oss.str());
    }

    [[noreturn]] inline void FailPredicate(
        const char* expr,
        const char* file,
        int line) {
        std::ostringstream oss;
        oss << file << ":" << line << " Expected true: " << expr;
        throw Failure(oss.str());
    }
}

#define TEST_CASE(suite_name, test_name)                                                   \
    static void suite_name##_##test_name();                                                \
    static ::Test::Registrar suite_name##_##test_name##_registrar(                         \
        #suite_name,                                                                        \
        #test_name,                                                                         \
        &suite_name##_##test_name);                                                         \
    static void suite_name##_##test_name()

#define EXPECT_TRUE(expr)                                                                   \
    do {                                                                                    \
        if (!(expr)) {                                                                      \
            ::Test::FailPredicate(#expr, __FILE__, __LINE__);                               \
        }                                                                                   \
    } while (false)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_EQ(left, right)                                                              \
    do {                                                                                    \
        auto&& lhs = (left);                                                                \
        auto&& rhs = (right);                                                               \
        if (!(lhs == rhs)) {                                                                \
            ::Test::FailEqual(#left, #right, lhs, rhs, __FILE__, __LINE__);                \
        }                                                                                   \
    } while (false)
