// Logger.hpp

#pragma once
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string_view>

namespace EF {
    enum class LogLevel {
        Info, Warn, Error, Critical
    };

    class Logger {
    public:
        // Initialize once during program startup.
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

            // Format the message and timestamp first.
            std::string message = std::vformat(fmt, std::make_format_args(args...));
            std::string timeStr = std::format("{:%H:%M:%S}", time);

            // Resolve level-specific prefix and color.
            std::string_view prefix;
            std::string_view color;
            switch (level) {
            case LogLevel::Info:     prefix = "[INFO]";     color = "\033[32m"; break;
            case LogLevel::Warn:     prefix = "[WARN]";     color = "\033[33m"; break;
            case LogLevel::Error:    prefix = "[ERROR]";    color = "\033[31m"; break;
            case LogLevel::Critical: prefix = "[CRITICAL]"; color = "\033[41m"; break;
            }

            // Console output with ANSI color.
            std::cout << color << timeStr << " " << prefix << " " << message << "\033[0m" << std::endl;

            // File output without color codes.
            if (Get().m_fileStream.is_open()) {
                Get().m_fileStream << timeStr << " " << prefix << " " << message << "\n";

                // Flush errors eagerly to reduce log loss on crashes.
                if (level >= LogLevel::Error) {
                    Get().m_fileStream.flush();
                }
            }
        }

    private:
        Logger() = default;
        ~Logger() {
            if (m_fileStream.is_open()) {
                m_fileStream.close();
            }
        }

        // Return the singleton instance.
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
