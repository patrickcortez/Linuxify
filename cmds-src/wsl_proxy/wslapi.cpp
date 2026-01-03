// Compile: g++ -shared -o wslapi.dll wslapi.cpp wslapi.def -lntdll
// Install: Run 'setup integrate' command in Linuxify shell as Administrator

#include <windows.h>
#include <winternl.h>
#include <string>

#define PROXY_VERSION "1.0.0"

typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

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

typedef NTSTATUS (NTAPI *PNtDeviceIoControlFile)(
    HANDLE FileHandle,
    HANDLE Event,
    PVOID ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG IoControlCode,
    PVOID InputBuffer,
    ULONG InputBufferLength,
    PVOID OutputBuffer,
    ULONG OutputBufferLength
);

typedef VOID (NTAPI *PRtlInitUnicodeString)(
    PUNICODE_STRING DestinationString,
    PCWSTR SourceString
);

static HMODULE g_hNtdll = NULL;
static HMODULE g_hOriginalWslApi = NULL;
static PNtCreateFile g_NtCreateFile = NULL;
static PNtDeviceIoControlFile g_NtDeviceIoControlFile = NULL;
static PRtlInitUnicodeString g_RtlInitUnicodeString = NULL;

#define LXSS_DEVICE_PATH L"\\Device\\lxss"
#define LXSS_SYMLINK_PATH L"\\\\.\\lxss"

#define IOCTL_LXSS_QUERY_SUBSYSTEM CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_LXSS_ENUMERATE_INSTANCES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct LxssQueryResult {
    DWORD version;
    DWORD status;
    DWORD instanceCount;
    WCHAR driverPath[MAX_PATH];
};

static bool InitializeNtFunctions() {
    if (g_hNtdll) return true;
    
    g_hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!g_hNtdll) {
        g_hNtdll = LoadLibraryW(L"ntdll.dll");
    }
    if (!g_hNtdll) return false;
    
    g_NtCreateFile = (PNtCreateFile)GetProcAddress(g_hNtdll, "NtCreateFile");
    g_NtDeviceIoControlFile = (PNtDeviceIoControlFile)GetProcAddress(g_hNtdll, "NtDeviceIoControlFile");
    g_RtlInitUnicodeString = (PRtlInitUnicodeString)GetProcAddress(g_hNtdll, "RtlInitUnicodeString");
    
    return (g_NtCreateFile && g_NtDeviceIoControlFile && g_RtlInitUnicodeString);
}

static bool LoadOriginalWslApi() {
    if (g_hOriginalWslApi) return true;
    
    WCHAR systemPath[MAX_PATH];
    GetSystemDirectoryW(systemPath, MAX_PATH);
    std::wstring origPath = std::wstring(systemPath) + L"\\wslapi_orig.dll";
    
    g_hOriginalWslApi = LoadLibraryW(origPath.c_str());
    return (g_hOriginalWslApi != NULL);
}

static HANDLE OpenLxssDevice() {
    if (!InitializeNtFunctions()) return INVALID_HANDLE_VALUE;
    
    HANDLE hDevice = CreateFileW(
        LXSS_SYMLINK_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        UNICODE_STRING deviceName;
        OBJECT_ATTRIBUTES objAttr;
        IO_STATUS_BLOCK ioStatus;
        
        g_RtlInitUnicodeString(&deviceName, LXSS_DEVICE_PATH);
        InitializeObjectAttributes(&objAttr, &deviceName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        
        NTSTATUS status = g_NtCreateFile(
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
            return INVALID_HANDLE_VALUE;
        }
    }
    
    return hDevice;
}

extern "C" {

__declspec(dllexport) const char* WslProxyGetVersion() {
    return PROXY_VERSION;
}

__declspec(dllexport) BOOL WslProxyIsActive() {
    return TRUE;
}

__declspec(dllexport) DWORD LxssKernelQuery(LxssQueryResult* result) {
    if (!result) return ERROR_INVALID_PARAMETER;
    
    ZeroMemory(result, sizeof(LxssQueryResult));
    result->version = 1;
    
    HANDLE hDevice = OpenLxssDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        result->status = GetLastError();
        return result->status;
    }
    
    DWORD bytesReturned = 0;
    BYTE outputBuffer[1024] = {0};
    
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_LXSS_QUERY_SUBSYSTEM,
        NULL,
        0,
        outputBuffer,
        sizeof(outputBuffer),
        &bytesReturned,
        NULL
    );
    
    if (success) {
        result->status = STATUS_SUCCESS;
        result->instanceCount = bytesReturned > 0 ? 1 : 0;
    } else {
        result->status = GetLastError();
    }
    
    WCHAR driverPath[MAX_PATH];
    GetSystemDirectoryW(driverPath, MAX_PATH);
    wcscat_s(driverPath, L"\\drivers\\lxcore.sys");
    wcscpy_s(result->driverPath, driverPath);
    
    CloseHandle(hDevice);
    return result->status;
}

}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            InitializeNtFunctions();
            LoadOriginalWslApi();
            break;
            
        case DLL_PROCESS_DETACH:
            if (g_hOriginalWslApi) {
                FreeLibrary(g_hOriginalWslApi);
                g_hOriginalWslApi = NULL;
            }
            break;
    }
    return TRUE;
}
