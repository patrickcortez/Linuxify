#ifndef LINUXIFY_CRASH_HANDLER_HPP
#define LINUXIFY_CRASH_HANDLER_HPP

#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <exception>
#include <psapi.h>

#include "shell_streams.hpp"

namespace CrashHandler {

    inline std::string getExceptionName(DWORD code) {
        switch(code) {
            case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
            case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
            case EXCEPTION_FLT_DENORMAL_OPERAND:     return "FLT_DENORMAL_OPERAND";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
            case EXCEPTION_FLT_INEXACT_RESULT:       return "FLT_INEXACT_RESULT";
            case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
            case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
            case EXCEPTION_FLT_STACK_CHECK:          return "FLT_STACK_CHECK";
            case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
            case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
            case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
            case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
            case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
            case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
            case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
            case EXCEPTION_SINGLE_STEP:              return "SINGLE_STEP";
            case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
            default: return "UNKNOWN_EXCEPTION";
        }
    }

    inline void printCrashReport(const std::string& reason, PVOID address = nullptr) {
        // Force restore console mode to ensure we can see the error
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(hIn, &mode);
        mode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT;
        SetConsoleMode(hIn, mode);

        ShellIO::sout << ShellIO::endl;
        ShellIO::sout << ShellIO::Color::Bold << ShellIO::Color::Red 
                      << "==========================================" << ShellIO::endl;
        ShellIO::sout << " FATAL ERROR: LINUXIFY HAS CRASHED" << ShellIO::endl;
        ShellIO::sout << "==========================================" << ShellIO::Color::Reset << ShellIO::endl;
        
        ShellIO::sout << ShellIO::Color::LightRed << "Reason: " << reason << ShellIO::Color::Reset << ShellIO::endl;
        
        if (address) {
            std::stringstream ss;
            ss << "Address: 0x" << std::hex << std::uppercase << (uintptr_t)address;
            
            // Try to find module
            HMODULE hMod = NULL;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, 
                                   (LPCSTR)address, &hMod)) {
                char modPath[MAX_PATH];
                if (GetModuleFileNameA(hMod, modPath, MAX_PATH)) {
                    std::string modName = std::string(modPath);
                    size_t lastSlash = modName.find_last_of("\\/");
                    if (lastSlash != std::string::npos) modName = modName.substr(lastSlash + 1);
                    
                    uintptr_t offset = (uintptr_t)address - (uintptr_t)hMod;
                    ss << " (" << modName << " + 0x" << offset << ")";
                }
            }
            ShellIO::sout << ss.str() << ShellIO::endl;
        }

        ShellIO::sout << ShellIO::endl << "Stack Trace:" << ShellIO::endl;
        
        // Simple stack trace
        PVOID stack[64];
        WORD frames = CaptureStackBackTrace(0, 64, stack, NULL);
        
        for (WORD i = 0; i < frames; i++) {
            std::stringstream ss;
            ss << "[" << std::setw(2) << std::setfill('0') << i << "] 0x" 
               << std::hex << std::uppercase << (uintptr_t)stack[i];
            
            HMODULE hMod = NULL;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, 
                                   (LPCSTR)stack[i], &hMod)) {
                char modPath[MAX_PATH];
                if (GetModuleFileNameA(hMod, modPath, MAX_PATH)) {
                    std::string modName = std::string(modPath);
                    size_t lastSlash = modName.find_last_of("\\/");
                    if (lastSlash != std::string::npos) modName = modName.substr(lastSlash + 1);
                    
                    uintptr_t offset = (uintptr_t)stack[i] - (uintptr_t)hMod;
                    ss << " (" << modName << " + 0x" << offset << ")";
                }
            }
            
            ShellIO::sout << ss.str() << ShellIO::endl;
        }

        ShellIO::sout << ShellIO::endl;
        ShellIO::sout << "The application will now terminate." << ShellIO::endl;
        ShellIO::sout.flush();
    }

    inline LONG WINAPI UnhandledExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        PVOID addr = pExceptionInfo->ExceptionRecord->ExceptionAddress;
        
        std::stringstream ss;
        ss << getExceptionName(code) << " (0x" << std::hex << std::uppercase << code << ")";
        
        printCrashReport(ss.str(), addr);
        
        return EXCEPTION_EXECUTE_HANDLER; // Proceed to termination
    }

    inline void TerminateHandler() {
        std::string reason = "Unhandled C++ Exception (std::terminate)";
        std::exception_ptr ptr = std::current_exception();
        if (ptr) {
            try {
                std::rethrow_exception(ptr);
            } catch (const std::exception& e) {
                reason = std::string("Unhandled Exception: ") + e.what();
            } catch (...) {
                reason = "Unknown C++ Exception";
            }
        }
        printCrashReport(reason);
        std::abort();
    }

    inline void init() {
        SetUnhandledExceptionFilter(UnhandledExceptionHandler);
        std::set_terminate(TerminateHandler);
    }
}

#endif // LINUXIFY_CRASH_HANDLER_HPP
