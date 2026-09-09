#include "core/logging/logger.hpp"
#include "core/container/string.hpp"
#include <cstdio>
#include <cstdarg>

namespace zq::log {

static ILogger* g_logger = nullptr;
static LogLevel g_min_level = LogLevel::Info;

void SetLogger(ILogger* logger) {
    g_logger = logger;
}

void SetMinLevel(LogLevel level) {
    g_min_level = level;
}

ILogger* GetLogger() {
    return g_logger;
}

void ConsoleLogger::Log(LogLevel level, std::string_view message) {
    const char* prefix;
    FILE* stream = stdout;
    
    switch (level) {
        case LogLevel::Debug: prefix = "[DEBUG] "; break;
        case LogLevel::Info:  prefix = "[INFO]  "; break;
        case LogLevel::Warning: prefix = "[WARN]  "; stream = stderr; break;
        case LogLevel::Error: prefix = "[ERROR] "; stream = stderr; break;
        case LogLevel::Fatal: prefix = "[FATAL] "; stream = stderr; break;
        default: prefix = "[?????] "; stream = stderr; break;
    }
    
    fprintf(stream, "%s%s\n", prefix, message.data());
}

ConsoleLogger& ConsoleLogger::Instance() {
    static ConsoleLogger instance;
    return instance;
}

void CallbackLogger::SetCallback(std::function<void(LogLevel, std::string_view)> callback) {
    callback_ = callback;
}

void CallbackLogger::Log(LogLevel level, std::string_view message) {
    if (callback_) {
        callback_(level, message);
    }
}

// Global logging functions
void Debug(const String& message) {
    if (g_logger && g_min_level <= LogLevel::Debug) {
        g_logger->Log(LogLevel::Debug, std::string_view(message.data(), message.size()));
    }
}

void Info(const String& message) {
    if (g_logger && g_min_level <= LogLevel::Info) {
        g_logger->Log(LogLevel::Info, std::string_view(message.data(), message.size()));
    }
}

void Warn(const String& message) {
    if (g_logger && g_min_level <= LogLevel::Warning) {
        g_logger->Log(LogLevel::Warning, std::string_view(message.data(), message.size()));
    }
}

void Error(const String& message) {
    if (g_logger && g_min_level <= LogLevel::Error) {
        g_logger->Log(LogLevel::Error, std::string_view(message.data(), message.size()));
    }
}

void Fatal(const String& message) {
    if (g_logger && g_min_level <= LogLevel::Fatal) {
        g_logger->Log(LogLevel::Fatal, std::string_view(message.data(), message.size()));
    }
}

void Debug(const char* message) {
    if (g_logger && g_min_level <= LogLevel::Debug) {
        g_logger->Log(LogLevel::Debug, std::string_view(message));
    }
}

void Info(const char* message) {
    if (g_logger && g_min_level <= LogLevel::Info) {
        g_logger->Log(LogLevel::Info, std::string_view(message));
    }
}

void Warn(const char* message) {
    if (g_logger && g_min_level <= LogLevel::Warning) {
        g_logger->Log(LogLevel::Warning, std::string_view(message));
    }
}

void Error(const char* message) {
    if (g_logger && g_min_level <= LogLevel::Error) {
        g_logger->Log(LogLevel::Error, std::string_view(message));
    }
}

void Fatal(const char* message) {
    if (g_logger && g_min_level <= LogLevel::Fatal) {
        g_logger->Log(LogLevel::Fatal, std::string_view(message));
    }
}

} // namespace zq::log
