#include <exception>
#include <string_view>
#include <iostream>

#include "TestHarness.hpp"

int main(int argc, char** argv) {
    std::string_view filter;
    if (argc > 1) {
        filter = argv[1];
    }

    std::size_t passed = 0;
    std::size_t selected = 0;

    for (const auto& testCase : Test::Registry()) {
        const std::string fullName = std::string(testCase.suite) + "." + testCase.name;
        if (!filter.empty() && filter != fullName) {
            continue;
        }

        ++selected;

        try {
            testCase.func();
            ++passed;
            std::cout << "[PASS] " << fullName << "\n";
        } catch (const std::exception& ex) {
            std::cerr << "[FAIL] " << fullName << " - " << ex.what() << "\n";
            return 1;
        } catch (...) {
            std::cerr << "[FAIL] " << fullName << " - unknown exception\n";
            return 1;
        }
    }

    if (!filter.empty() && selected == 0) {
        std::cerr << "[FAIL] No test matched filter: " << filter << "\n";
        return 1;
    }

    std::cout << passed << " tests passed\n";
    return 0;
}
