#pragma once

#include <string_view>
#include <cstdint>
#include <functional>

// Forward declaration
namespace zq { class String; }

namespace zq::log {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void Log(LogLevel level, std::string_view message) = 0;
};

// Console logger - writes to stdout/stderr
class ConsoleLogger : public ILogger {
public:
    void Log(LogLevel level, std::string_view message) override;
    static ConsoleLogger& Instance();
    
private:
    LogLevel min_level_ = LogLevel::Info;
};

// Callback-based logger - allows custom output destinations
class CallbackLogger : public ILogger {
public:
    void SetCallback(std::function<void(LogLevel, std::string_view)> callback);
    void Log(LogLevel level, std::string_view message) override;
    
private:
    std::function<void(LogLevel, std::string_view)> callback_;
};

// Global logging API
void SetLogger(ILogger* logger);
void SetMinLevel(LogLevel level);
ILogger* GetLogger();

void Debug(const char* message);
void Info(const char* message);
void Warn(const char* message);
void Error(const char* message);
void Fatal(const char* message);

void Debug(const String& message);
void Info(const String& message);
void Warn(const String& message);
void Error(const String& message);
void Fatal(const String& message);

} // namespace zq::log
