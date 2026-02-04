#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <windows.h>
#include "shell_streams.hpp"

// Linuxify Error Handling Subsystem
// Provides verbose, detailed error reporting with source location and system error codes.

namespace ErrorHandling {

    enum class Level {
        Debug,
        Info,
        Warning,
        Error,
        Fatal
    };

    inline std::string levelToString(Level level) {
        switch(level) {
            case Level::Debug: return "DEBUG";
            case Level::Info: return "INFO";
            case Level::Warning: return "WARNING";
            case Level::Error: return "ERROR";
            case Level::Fatal: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    inline ShellIO::Color levelToColor(Level level) {
         switch(level) {
            case Level::Debug: return ShellIO::Color::Gray;
            case Level::Info: return ShellIO::Color::Cyan;
            case Level::Warning: return ShellIO::Color::Yellow;
            case Level::Error: return ShellIO::Color::LightRed;
            case Level::Fatal: return ShellIO::Color::Red;
            default: return ShellIO::Color::White;
        }
    }

    // Windows Error Message Formatter
    inline std::string getSystemErrorMessage(DWORD errorCode) {
        if (errorCode == 0) return "";
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL
        );

        if (size == 0 || messageBuffer == nullptr) {
            return "Unknown System Error";
        }

        std::string message(messageBuffer, size);
        LocalFree(messageBuffer);
        
        // Remove trailing newline
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
        return message;
    }

    inline void log(Level level, const std::string& message, const char* file, int line, DWORD sysError = 0) {
        // [LEVEL] Message (File:Line) [System Error: ...]
        
        ShellIO::serr << ShellIO::Color::Bold << "[" << levelToColor(level) << levelToString(level) 
                      << ShellIO::Color::Reset << ShellIO::Color::Bold << "] " << ShellIO::Color::Reset;
        
        ShellIO::serr << message;

        // Add source location for Debug, Error, Fatal
        if (level == Level::Debug || level == Level::Error || level == Level::Fatal) {
             ShellIO::serr << ShellIO::Color::Gray << " (" << file << ":" << line << ")" << ShellIO::Color::Reset;
        }

        // Add System Error if present
        if (sysError != 0) {
            std::string sysMsg = getSystemErrorMessage(sysError);
            if (!sysMsg.empty()) {
                ShellIO::serr << ShellIO::endl << "        System Error (" << sysError << "): " << sysMsg;
            }
        }
        
        ShellIO::serr << ShellIO::endl;

        if (level == Level::Fatal) {
            ShellIO::serr << "Criticial Error: Linuxify must terminate." << ShellIO::endl;
            exit(1);
        }
    }
}

// Macros for easy logging with automatic file/line capture
#define LOG_DEBUG(msg) ErrorHandling::log(ErrorHandling::Level::Debug, msg, __FILE__, __LINE__)
#define LOG_INFO(msg) ErrorHandling::log(ErrorHandling::Level::Info, msg, __FILE__, __LINE__)
#define LOG_WARNING(msg) ErrorHandling::log(ErrorHandling::Level::Warning, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) ErrorHandling::log(ErrorHandling::Level::Error, msg, __FILE__, __LINE__)
#define LOG_FATAL(msg) ErrorHandling::log(ErrorHandling::Level::Fatal, msg, __FILE__, __LINE__)

// Log an error with the current Windows GetLastError() code
#define LOG_SYS_ERROR(msg) ErrorHandling::log(ErrorHandling::Level::Error, msg, __FILE__, __LINE__, GetLastError())
