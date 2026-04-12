// Logger.h 

#pragma once
#include <iostream>
#include <string_view>
#include <format>
#include <chrono>
#include <fstream>
#include <filesystem>

namespace EF {
    enum class LogLevel {
        Info, Warn, Error, Critical
    };

    class Logger {
    public:
        // 로거 초기화: 프로그램 시작 시 한 번 호출
        static void Init(const std::string& fileName = "engine.log") {
            Get().m_fileStream.open(fileName, std::ios::out | std::ios::app);
            if (Get().m_fileStream.is_open()) {
                Log(LogLevel::Info, "--- Logger Initialized (File: {}) ---", fileName);
            }
        }

        template<typename... Args>
        static void Log(LogLevel level, std::string_view fmt, Args&&... args) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::current_zone()->to_local(now);

            // 1. 메시지 포맷팅
            std::string message = std::vformat(fmt, std::make_format_args(args...));
            std::string timeStr = std::format("{:%H:%M:%S}", time);

            // 2. 레벨별 설정
            std::string_view prefix;
            std::string_view color;
            switch (level) {
            case LogLevel::Info:     prefix = "[INFO]";     color = "\033[32m"; break;
            case LogLevel::Warn:     prefix = "[WARN]";     color = "\033[33m"; break;
            case LogLevel::Error:    prefix = "[ERROR]";    color = "\033[31m"; break;
            case LogLevel::Critical: prefix = "[CRITICAL]"; color = "\033[41m"; break;
            }

            // 콘솔 출력 (색상 포함)
            std::cout << color << timeStr << " " << prefix << " " << message << "\033[0m" << std::endl;

            // 파일 출력 (색상 제외)
            if (Get().m_fileStream.is_open()) {
                Get().m_fileStream << timeStr << " " << prefix << " " << message << "\n";

                //  크래시 시 로그 유실을 막으려면 해주는 게 좋음
                if (level >= LogLevel::Error) {
                    Get().m_fileStream.flush();
                }
            }
        }

    private:
        Logger() = default;
        ~Logger() {
            if (m_fileStream.is_open()) m_fileStream.close();
        }

        // 싱글톤 인스턴스 반환
        static Logger& Get() {
            static Logger instance;
            return instance;
        }

        std::ofstream m_fileStream;
    };
}

#define LOG_INFO(fmt, ...)     ::EF::Logger::Log(::EF::LogLevel::Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)     ::EF::Logger::Log(::EF::LogLevel::Warn, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)    ::EF::Logger::Log(::EF::LogLevel::Error, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) ::EF::Logger::Log(::EF::LogLevel::Critical, fmt, ##__VA_ARGS__)