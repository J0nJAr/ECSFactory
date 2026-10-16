#include "Logger.hpp"

// 플랫폼 헤더를 공개 헤더에 노출하지 않는다.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace EF {
    void Logger::Init(const std::string& fileName) {
#if defined(_WIN32)
        // /utf-8 및 GCC의 UTF-8 실행 문자셋과 콘솔의 해석을 맞춘다.
        // stdout이 파일/파이프로 연결되어 있어도 출력 바이트는 UTF-8 그대로다.
        SetConsoleOutputCP(CP_UTF8);
#endif
        Get().m_fileStream.open(fileName, std::ios::out | std::ios::app);
        if (Get().m_fileStream.is_open()) {
            Log(LogLevel::Info, "--- Logger Initialized (File: {}) ---", fileName);
        }
    }
}
