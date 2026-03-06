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
#include <string>
#include <thread>
#include <string_view>
namespace SignalHandler {
    inline std::atomic<bool> g_isShuttingDown(false);
    inline std::atomic<bool> g_signalsBlocked(false); 
    inline HANDLE g_mainThreadHandle = NULL;         
    inline std::function<void()> g_cleanupCallback = nullptr;
    inline std::function<void()> g_interruptCallback = nullptr;
    inline std::function<void()> g_suspendCallback = nullptr; 

    // Custom Signals
    inline std::map<std::string, std::function<void()>> g_customSignalHandlers;
    inline std::mutex g_customSignalMutex;
    inline std::thread g_ipcListenerThread;

    inline void blockSignals() { g_signalsBlocked.store(true); }
    inline void unblockSignals() { g_signalsBlocked.store(false); }
    inline void signalHeartbeat() {
        // No-op: Watchdog removed
    }

    inline void registerCustomSignal(const std::string& signalName, std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(g_customSignalMutex);
        g_customSignalHandlers[signalName] = callback;
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
        std::function<void(int, int, int)> mouseCallback;
        std::queue<INPUT_RECORD> inputBuffer;
        std::mutex bufferMutex;
        HANDLE hStdin;
        DWORD originalMode;
        bool initialized = false;
        bool mouseEnabled = false;
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
            DWORD newMode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS | ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE | ENABLE_MOUSE_INPUT;
            SetConsoleMode(hStdin, newMode);  
        }  

        void enableMouse(bool enable) {
            mouseEnabled = enable;
            if (initialized) enableRawMode();
        }
        
        void restore() {
            if (initialized) {
                DWORD cookedMode = originalMode | ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT;
                SetConsoleMode(hStdin, cookedMode);
            }
        }
        void registerKeyHandler(WORD vk, bool ctrl, bool alt, bool shift, std::function<void()> callback) {
            keyHandlers[{vk, ctrl, alt, shift}] = callback;
        }
        void registerMouseHandler(std::function<void(int, int, int)> callback) {
            mouseCallback = callback;
            enableMouse(true);
        }
        bool poll() {
            DWORD eventsAvailable = 0;
            GetNumberOfConsoleInputEvents(hStdin, &eventsAvailable);
            if (eventsAvailable == 0) return false;
            
            // Optimization: Static buffer to avoid reallocation
            static std::vector<INPUT_RECORD> buffer;
            if (buffer.capacity() < eventsAvailable) buffer.reserve(eventsAvailable + 16);
            buffer.resize(eventsAvailable);

            DWORD eventsRead = 0;
            if (ReadConsoleInput(hStdin, buffer.data(), eventsAvailable, &eventsRead)) {
                for (DWORD i = 0; i < eventsRead; i++) {
                    INPUT_RECORD& record = buffer[i];
                    if (record.EventType == MOUSE_EVENT && mouseCallback) {
                        auto& m = record.Event.MouseEvent;
                        int evType = -1;
                        bool ctrlHeld = (m.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                        if (m.dwEventFlags == 0 && (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) && ctrlHeld) {
                            evType = 1;
                        } else if (m.dwEventFlags == MOUSE_MOVED) {
                            evType = 0;
                        }
                        if (evType >= 0) {
                            mouseCallback(m.dwMousePosition.X, m.dwMousePosition.Y, evType);
                        }
                        if (evType == 1) continue;
                    }
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
    
    inline void handleSuspend() {
        if (g_signalsBlocked.load()) return;
        if (g_suspendCallback) {
            std::cout << "^Z\n";
            g_suspendCallback();
        }
    }

    inline void registerSuspendHandler(std::function<void()> callback) {
        g_suspendCallback = callback;
        InputDispatcher::getInstance().registerKeyHandler('Z', true, false, false, handleSuspend);
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

    inline void IpcListener() {
        DWORD pid = GetCurrentProcessId();
        std::string pipeName = "\\\\.\\pipe\\linuxify_signals_" + std::to_string(pid);

        while (!g_isShuttingDown.load()) {
            HANDLE hPipe = CreateNamedPipeA(
                pipeName.c_str(),
                PIPE_ACCESS_INBOUND,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1, // Only 1 instance
                1024,
                1024,
                0,
                NULL
            );

            if (hPipe == INVALID_HANDLE_VALUE) {
                Sleep(100);
                continue;
            }

            if (ConnectNamedPipe(hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
                char buffer[256];
                DWORD bytesRead;
                if (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                    if (bytesRead > 0) {
                        buffer[bytesRead] = '\0';
                        
                        // Parse the signal - safely strip newlines
                        std::string sig(buffer);
                        size_t pos = sig.find_first_of("\r\n");
                        if (pos != std::string::npos) sig.erase(pos);
                        
                        if (g_signalsBlocked.load()) goto next_client;

                        if (sig == "SIGINT" || sig == "SIGINT\n" || sig == "INT") {
                            handleInterrupt();
                        } else if (sig == "SIGTERM" || sig == "TERM") {
                            handleTermination("SIGTERM via IPC");
                        } else if (sig == "SIGABRT" || sig == "ABRT") {
                            std::cerr << "\n[SIGABRT] Abort via IPC.\n"; ExitProcess(3);
                        } else {
                            // Custom signal
                            std::lock_guard<std::mutex> lock(g_customSignalMutex);
                            if (g_customSignalHandlers.count(sig)) {
                                g_customSignalHandlers[sig]();
                            }
                        }
                    }
                }
            }
        next_client:
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
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
        
        // Start IPC listener for custom sig command
        g_ipcListenerThread = std::thread(IpcListener);
        g_ipcListenerThread.detach(); // Allow it to run independently
    }
    inline void poll() {
        InputDispatcher::getInstance().poll();
    }
} 
#endif 
