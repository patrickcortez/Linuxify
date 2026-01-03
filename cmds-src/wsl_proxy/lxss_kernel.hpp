// LXSS Kernel Access Interface
// Include this header to communicate with the LXSS kernel driver
// Usage: #include "lxss_kernel.hpp"

#pragma once

#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>

namespace LxssKernel {

typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

#define LXSS_DEVICE_PATH L"\\Device\\lxss"
#define LXSS_SYMLINK_PATH L"\\\\.\\lxss"

#define IOCTL_LXSS_BASE 0x800
#define IOCTL_LXSS_QUERY_SUBSYSTEM CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_LXSS_BASE + 0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LXSS_ENUMERATE_INSTANCES CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_LXSS_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LXSS_CREATE_INSTANCE CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_LXSS_BASE + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LXSS_GET_INSTANCE_INFO CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_LXSS_BASE + 3, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef NTSTATUS (NTAPI *PNtCreateFile)(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer,
    ULONG EaLength
);

typedef VOID (NTAPI *PRtlInitUnicodeString)(
    PUNICODE_STRING DestinationString,
    PCWSTR SourceString
);

struct DeviceInfo {
    bool isOpen;
    DWORD lastError;
    std::wstring devicePath;
    std::wstring driverPath;
};

struct SubsystemInfo {
    bool available;
    DWORD version;
    DWORD instanceCount;
    std::vector<std::wstring> distributions;
};

class LxssDevice {
private:
    HANDLE hDevice;
    HMODULE hNtdll;
    PNtCreateFile NtCreateFile;
    PRtlInitUnicodeString RtlInitUnicodeString;
    DWORD lastError;
    
    bool InitNtFunctions() {
        hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll) hNtdll = LoadLibraryW(L"ntdll.dll");
        if (!hNtdll) return false;
        
        NtCreateFile = (PNtCreateFile)GetProcAddress(hNtdll, "NtCreateFile");
        RtlInitUnicodeString = (PRtlInitUnicodeString)GetProcAddress(hNtdll, "RtlInitUnicodeString");
        
        return (NtCreateFile && RtlInitUnicodeString);
    }
    
public:
    LxssDevice() : hDevice(INVALID_HANDLE_VALUE), hNtdll(NULL), 
                   NtCreateFile(NULL), RtlInitUnicodeString(NULL), lastError(0) {
        InitNtFunctions();
    }
    
    ~LxssDevice() {
        Close();
    }
    
    bool Open() {
        if (hDevice != INVALID_HANDLE_VALUE) return true;
        
        hDevice = CreateFileW(
            LXSS_SYMLINK_PATH,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hDevice == INVALID_HANDLE_VALUE && NtCreateFile && RtlInitUnicodeString) {
            UNICODE_STRING deviceName;
            OBJECT_ATTRIBUTES objAttr;
            IO_STATUS_BLOCK ioStatus;
            
            RtlInitUnicodeString(&deviceName, LXSS_DEVICE_PATH);
            InitializeObjectAttributes(&objAttr, &deviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
            
            NTSTATUS status = NtCreateFile(
                &hDevice,
                GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                &objAttr,
                &ioStatus,
                NULL,
                FILE_ATTRIBUTE_NORMAL,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                FILE_OPEN,
                FILE_SYNCHRONOUS_IO_NONALERT,
                NULL,
                0
            );
            
            if (!NT_SUCCESS(status)) {
                lastError = (DWORD)(status & 0xFFFF);
                return false;
            }
        }
        
        if (hDevice == INVALID_HANDLE_VALUE) {
            lastError = GetLastError();
            return false;
        }
        
        return true;
    }
    
    void Close() {
        if (hDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(hDevice);
            hDevice = INVALID_HANDLE_VALUE;
        }
    }
    
    bool IsOpen() const { 
        return hDevice != INVALID_HANDLE_VALUE; 
    }
    
    DWORD GetLastError() const { 
        return lastError; 
    }
    
    bool SendIoctl(DWORD ioctlCode, PVOID inBuf, DWORD inSize, PVOID outBuf, DWORD outSize, DWORD* bytesReturned) {
        if (!IsOpen() && !Open()) return false;
        
        DWORD returned = 0;
        BOOL result = DeviceIoControl(
            hDevice,
            ioctlCode,
            inBuf,
            inSize,
            outBuf,
            outSize,
            &returned,
            NULL
        );
        
        if (bytesReturned) *bytesReturned = returned;
        
        if (!result) {
            lastError = ::GetLastError();
        }
        
        return result != FALSE;
    }
    
    DeviceInfo GetDeviceInfo() {
        DeviceInfo info = {};
        info.isOpen = IsOpen();
        info.lastError = lastError;
        info.devicePath = LXSS_DEVICE_PATH;
        
        WCHAR driverPath[MAX_PATH];
        GetSystemDirectoryW(driverPath, MAX_PATH);
        wcscat_s(driverPath, L"\\drivers\\lxcore.sys");
        info.driverPath = driverPath;
        
        return info;
    }
    
    SubsystemInfo QuerySubsystem() {
        SubsystemInfo info = {};
        
        if (!Open()) {
            info.available = false;
            return info;
        }
        
        BYTE buffer[4096] = {0};
        DWORD bytesReturned = 0;
        
        if (SendIoctl(IOCTL_LXSS_QUERY_SUBSYSTEM, NULL, 0, buffer, sizeof(buffer), &bytesReturned)) {
            info.available = true;
            info.version = 1;
        } else {
            info.available = (hDevice != INVALID_HANDLE_VALUE);
            info.version = 0;
        }
        
        return info;
    }
};

inline bool IsWslProxyInstalled() {
    HMODULE hWslApi = LoadLibraryW(L"wslapi.dll");
    if (!hWslApi) return false;
    
    typedef BOOL (*PWslProxyIsActive)();
    PWslProxyIsActive func = (PWslProxyIsActive)GetProcAddress(hWslApi, "WslProxyIsActive");
    
    bool result = (func != NULL && func());
    FreeLibrary(hWslApi);
    return result;
}

inline std::string GetProxyVersion() {
    HMODULE hWslApi = LoadLibraryW(L"wslapi.dll");
    if (!hWslApi) return "";
    
    typedef const char* (*PWslProxyGetVersion)();
    PWslProxyGetVersion func = (PWslProxyGetVersion)GetProcAddress(hWslApi, "WslProxyGetVersion");
    
    std::string result = func ? func() : "";
    FreeLibrary(hWslApi);
    return result;
}

inline bool IsLxcoreDriverLoaded() {
    WCHAR driverPath[MAX_PATH];
    GetSystemDirectoryW(driverPath, MAX_PATH);
    wcscat_s(driverPath, L"\\drivers\\lxcore.sys");
    
    return GetFileAttributesW(driverPath) != INVALID_FILE_ATTRIBUTES;
}

}
