#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

enum class LogLevel
{
    Info,
    Debug,
    Error
};

class Logger
{
public:
    static Logger &Instance()
    {
        static Logger instance;
        return instance;
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void SetDebugMode(bool enabled)
    {
        debugMode = enabled;
    }

    bool DebugMode() const
    {
        return debugMode;
    }

    void SetLogFile(const std::string &filename)
    {
        std::lock_guard<std::mutex> lock(logMutex);

        if (logFile.is_open())
        {
            logFile.close();
        }

        logFile.open(filename, std::ios::app);
    }

    void Log(LogLevel level, const std::string &message)
    {
        std::lock_guard<std::mutex> lock(logMutex);

        if (logFile.is_open())
        {
            logFile << Timestamp() << " [" << LevelName(level) << "] " << message << std::endl;
        }

        if (level == LogLevel::Error)
        {
            std::cerr << Timestamp() << " [" << LevelName(level) << "] " << message << std::endl;
        }
        else if (debugMode)
        {
            std::cout << Timestamp() << " [" << LevelName(level) << "] " << message << std::endl;
        }
    }

private:
    Logger() : debugMode(false) {}

    ~Logger()
    {
        std::lock_guard<std::mutex> lock(logMutex);
        if (logFile.is_open())
        {
            logFile.close();
        }
    }

    static const char *LevelName(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Error:
            return "ERROR";
        }
        return "?????";
    }

    static std::string Timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);

        std::tm tm{};
        localtime_r(&time, &tm);

        std::ostringstream out;
        out << std::put_time(&tm, "%H:%M:%S");
        return out.str();
    }

    bool debugMode;
    std::ofstream logFile;
    std::mutex logMutex;
};

#define LOG_INFO(msg) Logger::Instance().Log(LogLevel::Info, (msg))
#define LOG_DEBUG(msg) Logger::Instance().Log(LogLevel::Debug, (msg))
#define LOG_ERROR(msg) Logger::Instance().Log(LogLevel::Error, (msg))
