#include <iostream>
#include "Base/Logger.hpp"
#include "Base/Assert.hpp"
#include "Math/Fixed64.hpp"

using namespace EF;

int main() {
    // 1. 로거 초기화
    Logger::Init("Sandbox.log");
    LOG_INFO("Engine Sandbox Started!");

    // 2. Fixed64 테스트
    Fixed64 a = Fixed64::FromInt(10);
    Fixed64 b = Fixed64::FromFloat(0.5f);
    Fixed64 c = a * b; // 10 * 0.5 = 5

    LOG_INFO("Fixed64 Multiplication Test: {} * {} = {}",
        a.ToFloat(), b.ToFloat(), c.ToFloat());

    // 3. Assert 테스트 (일부러 틀려보기)
    // EF_ASSERT(c.Raw() == Fixed64::FromInt(5).Raw(), "Multiplication failed!");

    LOG_INFO("All systems green. Shutting down...");

    return 0;
}