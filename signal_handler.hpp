// g++ -std=c++17 main.cpp -o main.exe
#ifndef LINUXIFY_SIGNAL_HANDLER_HPP
#define LINUXIFY_SIGNAL_HANDLER_HPP
#include "interrupt.hpp"
#include <windows.h>
#include <functional>
#include <iostream>
#include <csignal>
#include <atomic>
#include <mutex>
#include <ctime>
#include <vector>
#include <map>
#include <queue>
namespace SignalHandler {
    inline std::atomic<bool> g_isShuttingDown(false);
    inline std::atomic<bool> g_signalsBlocked(false); 
    inline HANDLE g_mainThreadHandle = NULL;         
    inline std::function<void()> g_cleanupCallback = nullptr;
    inline std::function<void()> g_interruptCallback = nullptr; 

    inline void blockSignals() { g_signalsBlocked.store(true); }
    inline void unblockSignals() { g_signalsBlocked.store(false); }
    inline void signalHeartbeat() {
        // No-op: Watchdog removed
    }

    struct KeyCombo {
        WORD vk;
        bool ctrl;
        bool alt;
        bool shift;
        bool operator<(const KeyCombo& other) const {
            if (vk != other.vk) return vk < other.vk;
            if (ctrl != other.ctrl) return ctrl < other.ctrl;
            if (alt != other.alt) return alt < other.alt;
            return shift < other.shift;
        }
    };

    class InputDispatcher {
    private:
        std::map<KeyCombo, std::function<void()>> keyHandlers;
        std::queue<INPUT_RECORD> inputBuffer;
        std::mutex bufferMutex;
        HANDLE hStdin;
        DWORD originalMode;
        bool initialized = false;
    public:
        static InputDispatcher& getInstance() {
            static InputDispatcher instance;
            return instance;
        }
        void init() {
            if (!initialized) {
                hStdin = GetStdHandle(STD_INPUT_HANDLE);
                GetConsoleMode(hStdin, &originalMode);
                initialized = true;
            }
            enableRawMode();
        }
        void enableRawMode() {
            if (!initialized) return;
            DWORD newMode = originalMode & ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            newMode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
            SetConsoleMode(hStdin, newMode);
        }
        void restore() {
            if (initialized) {
                // Force cooked mode flags to ensure child processes work
                DWORD cookedMode = originalMode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
                SetConsoleMode(hStdin, cookedMode);
            }
        }
        void registerKeyHandler(WORD vk, bool ctrl, bool alt, bool shift, std::function<void()> callback) {
            keyHandlers[{vk, ctrl, alt, shift}] = callback;
        }
        bool poll() {
            DWORD eventsAvailable = 0;
            GetNumberOfConsoleInputEvents(hStdin, &eventsAvailable);
            if (eventsAvailable == 0) return false;
            std::vector<INPUT_RECORD> buffer(eventsAvailable);
            DWORD eventsRead = 0;
            if (ReadConsoleInput(hStdin, buffer.data(), eventsAvailable, &eventsRead)) {
                for (DWORD i = 0; i < eventsRead; i++) {
                    INPUT_RECORD& record = buffer[i];
                    if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
                        bool ctrl = (record.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                        bool alt = (record.Event.KeyEvent.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
                        bool shift = (record.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED) != 0;
                        WORD vk = record.Event.KeyEvent.wVirtualKeyCode;
                        KeyCombo combo = {vk, ctrl, alt, shift};
                        if (keyHandlers.count(combo)) {
                            keyHandlers[combo]();
                            continue; 
                        }
                    }
                    std::lock_guard<std::mutex> lock(bufferMutex);
                    inputBuffer.push(record);
                }
            }
            return true;
        }
        bool getNextBufferedEvent(INPUT_RECORD& out) {
            std::lock_guard<std::mutex> lock(bufferMutex);
            if (inputBuffer.empty()) return false;
            out = inputBuffer.front();
            inputBuffer.pop();
            return true;
        }
    };

    inline void registerCleanupHandler(std::function<void()> callback) {
        g_cleanupCallback = callback;
    }
    inline void registerInterruptHandler(std::function<void()> callback) {
        g_interruptCallback = callback;
        InputDispatcher::getInstance().registerKeyHandler('C', true, false, false, callback);
    }
    inline void registerKeyBinding(WORD vk, bool ctrl, bool alt, bool shift, std::function<void()> callback) {
        InputDispatcher::getInstance().registerKeyHandler(vk, ctrl, alt, shift, callback);
    }
    inline void handleInterrupt() {
        if (g_signalsBlocked.load()) return;
        if (g_interruptCallback) g_interruptCallback();
        else std::cout << "^C\n";
    }

    inline void handleTermination(const char* eventName) {
        if (g_isShuttingDown.exchange(true)) return;
        
        // Simple, clean shutdown. Don't fight for locks.
        if (g_cleanupCallback) {
            try {
                g_cleanupCallback(); 
            } catch (...) {}
        }
        
        InputDispatcher::getInstance().restore();
        ExitProcess(0);
    }

    inline BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
        switch (ctrlType) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
                handleInterrupt();
                return TRUE;
            case CTRL_CLOSE_EVENT:
                // Handle close immediately and return TRUE so Windows doesn't show "Terminate Batch Job?"
                handleTermination("CTRL_CLOSE_EVENT");
                return TRUE; 
            case CTRL_LOGOFF_EVENT:
                handleTermination("CTRL_LOGOFF_EVENT");
                return TRUE;
            case CTRL_SHUTDOWN_EVENT:
                handleTermination("CTRL_SHUTDOWN_EVENT");
                return TRUE;
            default:
                return FALSE;
        }
    }

    inline void StandardSignalHandler(int signal) {
        if (g_signalsBlocked.load()) return;
        switch (signal) {
            case SIGINT: handleInterrupt(); break;
            case SIGTERM: handleTermination("SIGTERM"); break;
            case SIGABRT: std::cerr << "\n[SIGABRT] Abort.\n"; ExitProcess(3); break;
        }
    }

    inline void init() {
        HANDLE hPseudo = GetCurrentThread();
        DuplicateHandle(GetCurrentProcess(), hPseudo, GetCurrentProcess(), &g_mainThreadHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);
        if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
            std::cerr << "Warning: Failed to set console control handler.\n";
        }
        signal(SIGINT, StandardSignalHandler);
        signal(SIGTERM, StandardSignalHandler);
        signal(SIGABRT, StandardSignalHandler);
        
        // No Watchdog Init
        InputDispatcher::getInstance().init();
    }
    inline void poll() {
        InputDispatcher::getInstance().poll();
    }
} 
#endif 
