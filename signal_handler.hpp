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
    inline std::atomic<time_t> g_lastHeartbeat(0);
    inline std::atomic<bool> g_watchdogRunning(false);
    inline HANDLE g_watchdogThreadHandle = NULL;
    inline std::function<void()> g_cleanupCallback = nullptr;
    inline std::function<void()> g_interruptCallback = nullptr; 
    inline void blockSignals() { g_signalsBlocked.store(true); }
    inline void unblockSignals() { g_signalsBlocked.store(false); }
    inline void signalHeartbeat() {
        g_lastHeartbeat.store(std::time(nullptr));
    }
    inline DWORD WINAPI WatchdogThreadRoutine(LPVOID lpParam) {
        Sleep(2000); 
        while (g_watchdogRunning.load()) {
            time_t now = std::time(nullptr);
            time_t last = g_lastHeartbeat.load();
            if (last > 0 && (now - last) > 10) {
                if (g_mainThreadHandle) SuspendThread(g_mainThreadHandle);
                HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                std::cerr << "\n\n*** SYSTEM WATCHDOG TRIGGERED ***\n";
                std::cerr << "Verification Failed: Main thread unresponsive for 10s.\n";
                if (g_mainThreadHandle) {
                    std::cerr << "[WATCHDOG] Generating forensic hang report...\n";
                    Interrupt::DumpHungThread(g_mainThreadHandle);
                }
                std::cerr << "[WATCHDOG] Terminating hung process...\n";
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                TerminateProcess(GetCurrentProcess(), 0xC000042B); 
            }
            Sleep(1000); 
        }
        return 0;
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
    public:
        static InputDispatcher& getInstance() {
            static InputDispatcher instance;
            return instance;
        }
        void init() {
            hStdin = GetStdHandle(STD_INPUT_HANDLE);
            GetConsoleMode(hStdin, &originalMode);
            DWORD newMode = originalMode & ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
            newMode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
            SetConsoleMode(hStdin, newMode);
        }
        void restore() {
            SetConsoleMode(hStdin, originalMode);
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
        if (g_signalsBlocked.load()) {
            std::cerr << "[SIGNAL] " << eventName << " blocked. Waiting for critical section...\n";
            int retries = 50; 
            while (retries-- > 0 && g_signalsBlocked.load()) Sleep(100);
            if (g_signalsBlocked.load()) {
                std::cerr << "[SIGNAL] TIMEOUT! Active intervention.\n";
                g_signalsBlocked.store(false);
                if (g_mainThreadHandle) CancelSynchronousIo(g_mainThreadHandle);
            } else {
                std::cerr << "[SIGNAL] Block released. Proceeding.\n";
            }
        } else {
            std::cerr << "[SIGNAL] " << eventName << " received.\n";
        }
        if (!g_isShuttingDown.exchange(true)) {
            g_watchdogRunning.store(false);
            if (g_cleanupCallback) {
                try {
                    g_cleanupCallback(); 
                    std::cerr << "[SIGNAL] Cleanup success.\n";
                } catch (...) {
                    std::cerr << "[SIGNAL] Cleanup failed.\n";
                }
            }
        }
    }
    inline BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
        switch (ctrlType) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
                handleInterrupt();
                return TRUE;
            case CTRL_CLOSE_EVENT:
                handleTermination("CTRL_CLOSE_EVENT");
                return FALSE; 
            case CTRL_LOGOFF_EVENT:
                handleTermination("CTRL_LOGOFF_EVENT");
                return FALSE;
            case CTRL_SHUTDOWN_EVENT:
                handleTermination("CTRL_SHUTDOWN_EVENT");
                return FALSE;
            default:
                return FALSE;
        }
    }
    inline void StandardSignalHandler(int signal) {
        if (g_signalsBlocked.load()) return;
        switch (signal) {
            case SIGINT: handleInterrupt(); break;
            case SIGTERM: handleTermination("SIGTERM"); exit(0); break;
            case SIGABRT: std::cerr << "\n[SIGABRT] Abort.\n"; exit(3); break;
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
        g_lastHeartbeat.store(std::time(nullptr));
        g_watchdogRunning.store(true);
        g_watchdogThreadHandle = CreateThread(NULL, 0, WatchdogThreadRoutine, NULL, 0, NULL);
        if (g_watchdogThreadHandle) {
            SetThreadPriority(g_watchdogThreadHandle, THREAD_PRIORITY_LOWEST);
        }
        InputDispatcher::getInstance().init();
    }
    inline void poll() {
        InputDispatcher::getInstance().poll();
    }
} 
#endif 
