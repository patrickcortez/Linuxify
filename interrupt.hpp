// g++ -std=c++17 main.cpp -o main.exe
#ifndef LINUXIFY_INTERRUPT_HPP
#define LINUXIFY_INTERRUPT_HPP
#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <ctime>
#include <functional>
#include <atomic>
#include <vector>
#include <tlhelp32.h>
#include <winternl.h>
#ifdef __GNUC__
#define __try try
#define __except(x) catch(...)
#endif
namespace Interrupt {
    typedef struct _LDR_DATA_TABLE_ENTRY_LITE {
        LIST_ENTRY InLoadOrderLinks;
        LIST_ENTRY InMemoryOrderLinks;
        LIST_ENTRY InInitializationOrderLinks;
        PVOID DllBase;
        PVOID EntryPoint;
        ULONG SizeOfImage;
        UNICODE_STRING FullDllName;
        UNICODE_STRING BaseDllName;
    } LDR_DATA_TABLE_ENTRY_LITE, *PLDR_DATA_TABLE_ENTRY_LITE;
    typedef struct _PEB_LDR_DATA_LITE {
        ULONG Length;
        BOOLEAN Initialized;
        PVOID SsHandle;
        LIST_ENTRY InLoadOrderModuleList;
        LIST_ENTRY InMemoryOrderModuleList;
        LIST_ENTRY InInitializationOrderModuleList;
    } PEB_LDR_DATA_LITE, *PPEB_LDR_DATA_LITE;
    typedef struct _PEB_LITE {
        BOOLEAN InheritedAddressSpace;
        BOOLEAN ReadImageFileExecOptions;
        BOOLEAN BeingDebugged;
        BOOLEAN BitField;
        PVOID Mutant;
        PVOID ImageBaseAddress;
        PPEB_LDR_DATA_LITE Ldr;
    } PEB_LITE, *PPEB_LITE;
    typedef enum _UNWIND_OP_CODES {
        UWOP_PUSH_NONVOL = 0,
        UWOP_ALLOC_LARGE,
        UWOP_ALLOC_SMALL,
        UWOP_SET_FPREG,
        UWOP_SAVE_NONVOL,
        UWOP_SAVE_NONVOL_FAR, 
        UWOP_SAVE_XMM128 = 8,
        UWOP_SAVE_XMM128_FAR,
        UWOP_PUSH_MACHFRAME
    } UNWIND_OP_CODES;
    typedef union _UNWIND_CODE {
        struct {
            BYTE CodeOffset;
            BYTE UnwindOp : 4;
            BYTE OpInfo   : 4;
        };
        USHORT FrameOffset;
    } UNWIND_CODE, *PUNWIND_CODE;
    typedef struct _UNWIND_INFO {
        BYTE Version       : 3;
        BYTE Flags         : 5;
        BYTE SizeOfProlog;
        BYTE CountOfCodes;
        BYTE FrameRegister : 4;
        BYTE FrameOffset   : 4;
        UNWIND_CODE UnwindCode[1];
    } UNWIND_INFO, *PUNWIND_INFO;
    inline std::atomic<bool> g_isCrashing(false);
    inline std::function<void()> g_rescueCallback = nullptr;
    inline PVOID g_vehHandle = nullptr;
    inline std::string toHex(DWORD64 value, int width = 16) {
        std::stringstream ss;
        ss << "0x" << std::setfill('0') << std::setw(width) << std::hex << value;
        return ss.str();
    }
    inline const char* getExceptionName(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
            case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
            case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
            case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
            case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
            case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
            case EXCEPTION_INVALID_HANDLE:           return "INVALID_HANDLE";
            default:                                 return "UNKNOWN_EXCEPTION";
        }
    }
    class PEResolver {
    public:
        static uintptr_t getImageBase() {
            #ifdef _WIN64
                uintptr_t peb = __readgsqword(0x60);
                return *(uintptr_t*)(peb + 0x10);
            #else
                uintptr_t peb = __readfsdword(0x30);
                return *(uintptr_t*)(peb + 0x08);
            #endif
        }
        static bool GetModuleFromAddress(uintptr_t addr, uintptr_t& outBase, DWORD& outSize, std::string& outName) {
            PPEB_LITE peb = nullptr;
            #ifdef _WIN64
                peb = (PPEB_LITE)__readgsqword(0x60);
            #else
                peb = (PPEB_LITE)__readfsdword(0x30);
            #endif
            if (!peb || !peb->Ldr) return false;
            LIST_ENTRY* head = &peb->Ldr->InMemoryOrderModuleList;
            LIST_ENTRY* curr = head->Flink;
            int limit = 500; 
            while (curr != head && limit-- > 0) {
                 uintptr_t offset = (uintptr_t)(&((PLDR_DATA_TABLE_ENTRY_LITE)0)->InMemoryOrderLinks);
                 PLDR_DATA_TABLE_ENTRY_LITE entry = (PLDR_DATA_TABLE_ENTRY_LITE)((uintptr_t)curr - offset);
                 uintptr_t start = (uintptr_t)entry->DllBase;
                 uintptr_t end = start + entry->SizeOfImage;
                 if (addr >= start && addr < end) {
                     outBase = start;
                     outSize = entry->SizeOfImage;
                     if (entry->FullDllName.Buffer) {
                         std::wstring wname(entry->FullDllName.Buffer, entry->FullDllName.Length / 2);
                         outName = std::string(wname.begin(), wname.end());
                     }
                     return true;
                 }
                 curr = curr->Flink;
            }
            return false;
        }
        static std::string resolveSymbol(uintptr_t address) {
            uintptr_t imageBase = 0;
            DWORD imageSize = 0;
            std::string moduleName;
            if (!GetModuleFromAddress(address, imageBase, imageSize, moduleName)) {
                 return "(External/Unknown)";
            }
            PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)imageBase;
            if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return moduleName + " (Invalid DOS)";
            PIMAGE_NT_HEADERS ntHeaders = (PIMAGE_NT_HEADERS)(imageBase + dosHeader->e_lfanew);
            if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return moduleName + " (Invalid NT)";
            DWORD exportDirRVA = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
            if (exportDirRVA == 0) return moduleName + " (No Exports)";
            PIMAGE_EXPORT_DIRECTORY exportDir = (PIMAGE_EXPORT_DIRECTORY)(imageBase + exportDirRVA);
            DWORD* addressOfFunctions = (DWORD*)(imageBase + exportDir->AddressOfFunctions);
            DWORD* addressOfNames = (DWORD*)(imageBase + exportDir->AddressOfNames);
            WORD* addressOfNameOrdinals = (WORD*)(imageBase + exportDir->AddressOfNameOrdinals);
            uintptr_t bestFuncAddr = 0;
            std::string bestFuncName;
            for (DWORD i = 0; i < exportDir->NumberOfNames; i++) {
                DWORD funcRVA = addressOfFunctions[addressOfNameOrdinals[i]];
                uintptr_t funcAddr = imageBase + funcRVA;
                if (funcAddr <= address) {
                    if (funcAddr > bestFuncAddr) {
                        bestFuncAddr = funcAddr;
                        char* name = (char*)(imageBase + addressOfNames[i]);
                        bestFuncName = name;
                    }
                }
            }
            if (!bestFuncName.empty()) {
                std::stringstream ss;
                ss << bestFuncName << " + " << toHex(address - bestFuncAddr, 4);
                return ss.str();
            }
            return "(Unknown Symbol)";
        }
        static void dumpLoadedModules(std::ofstream& log) {
            log << "\nLOADED MODULES (LDR Walk):\n";
            log << std::left << std::setw(20) << "Base Address" << std::setw(12) << "Size" << "Name\n";
            log << "------------------------------------------------------------\n";
            PPEB_LITE peb = nullptr;
            #ifdef _WIN64
                peb = (PPEB_LITE)__readgsqword(0x60);
            #else
                peb = (PPEB_LITE)__readfsdword(0x30);
            #endif
            if (!peb || !peb->Ldr) return;
            LIST_ENTRY* head = &peb->Ldr->InMemoryOrderModuleList;
            LIST_ENTRY* curr = head->Flink;
            int limit = 100;
            while (curr != head && limit-- > 0) {
                uintptr_t offset = (uintptr_t)(&((PLDR_DATA_TABLE_ENTRY_LITE)0)->InMemoryOrderLinks);
                PLDR_DATA_TABLE_ENTRY_LITE entry = (PLDR_DATA_TABLE_ENTRY_LITE)((uintptr_t)curr - offset);
                if (entry->FullDllName.Buffer && entry->FullDllName.Length > 0) {
                    std::wstring wname(entry->FullDllName.Buffer, entry->FullDllName.Length / 2);
                    std::string sname(wname.begin(), wname.end());
                    log << toHex((uintptr_t)entry->DllBase, 16) << " " << toHex(entry->SizeOfImage, 8) << "   " << sname << "\n";
                }
                curr = curr->Flink;
            }
        }
    };
    inline void dumpMemory(std::ofstream& log, DWORD64 address, int range = 64);
    inline void manualWalkStack(std::ofstream& log, PCONTEXT context);
    inline void dumpRegisters(std::ofstream& log, PCONTEXT ctx);
    inline void dumpDisassembly(std::ofstream& log, DWORD64 address, int lines);
    inline void dumpAllThreads(std::ofstream& log);
    inline void DumpHungThread(HANDLE hThread) {
        CONTEXT ctx;
        ZeroMemory(&ctx, sizeof(CONTEXT));
        ctx.ContextFlags = CONTEXT_FULL;
        if (!GetThreadContext(hThread, &ctx)) {
            std::cerr << "[Interrupt] Failed to get context of hung thread.\n";
            return;
        }
        std::time_t now = std::time(nullptr);
        char logFile[64];
        std::strftime(logFile, sizeof(logFile), "hang_%Y%m%d_%H%M%S.log", std::localtime(&now));
        std::ofstream log(logFile);
        if (log.is_open()) {
            log << "LINUXIFY HANG/WATCHDOG REPORT\n=============================\n";
            log << "Thread Handle: " << toHex((uintptr_t)hThread) << "\n";
            dumpRegisters(log, &ctx);
            #ifdef _WIN64
            DWORD64 pc = ctx.Rip;
            #else
            DWORD64 pc = ctx.Eip;
            #endif
            dumpDisassembly(log, pc, 6);
            log << "Program Counter: " << toHex(pc) << " " << PEResolver::resolveSymbol((uintptr_t)pc) << "\n";
            manualWalkStack(log, &ctx);
            PEResolver::dumpLoadedModules(log);
            dumpMemory(log, pc);
            dumpAllThreads(log);
            log.close();
            std::cerr << "[Interrupt] Forensic report generated: " << logFile << "\n";
        }
    }
    inline void dumpMemory(std::ofstream& log, DWORD64 address, int range) {
        log << "\nMEMORY DUMP (" << toHex(address) << " +/- " << range << " bytes):\n";
        DWORD64 start = (address > (DWORD64)range) ? address - range : 0;
        DWORD64 end = address + range;
        unsigned char* ptr = (unsigned char*)start;
        for (DWORD64 i = start; i < end; i += 16) {
            log << toHex(i, 16) << ": ";
            for (int j = 0; j < 16; j++) {
                if (i + j >= end) break;
                __try {
                    volatile unsigned char byte = ptr[i + j - start]; 
                    log << std::setfill('0') << std::setw(2) << std::hex << (int)byte << " ";
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    log << "?? ";
                }
            }
            log << "  ";
            for (int j = 0; j < 16; j++) {
                if (i + j >= end) break;
                __try {
                    unsigned char byte = ptr[i + j - start];
                    if (byte >= 32 && byte <= 126) log << (char)byte;
                    else log << ".";
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    log << "?";
                }
            }
            log << "\n";
        }
    }
    class UnwindMachine {
    public:
        template <typename T>
        static bool SafeRead(uintptr_t addr, T& out) {
            return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, &out, sizeof(T), NULL);
        }
        static uintptr_t GetModuleBase(uintptr_t addr) {
            uintptr_t base = 0;
            DWORD size = 0;
            std::string name;
            if (PEResolver::GetModuleFromAddress(addr, base, size, name)) {
                return base;
            }
            return 0;
        }
        static bool VirtualUnwind(DWORD64& ImageBase, DWORD64& ControlPc, PCONTEXT Context) {
            ImageBase = GetModuleBase(ControlPc);
            if (ImageBase == 0) return false;
            PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)ImageBase;
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
            PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)(ImageBase + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
            DWORD dirRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress;
            DWORD dirSize = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size;
            if (dirRva == 0) return false;
            PRUNTIME_FUNCTION funcs = (PRUNTIME_FUNCTION)(ImageBase + dirRva);
            DWORD count = dirSize / sizeof(RUNTIME_FUNCTION);
            DWORD rvaPC = (DWORD)(ControlPc - ImageBase);
            PRUNTIME_FUNCTION entry = NULL;
            int low = 0, high = count - 1;
            while (low <= high) {
                int mid = (low + high) / 2;
                if (rvaPC < funcs[mid].BeginAddress) high = mid - 1;
                else if (rvaPC >= funcs[mid].EndAddress) low = mid + 1;
                else { entry = &funcs[mid]; break; }
            }
            if (!entry) {
                DWORD64 RetAddr;
                if (!SafeRead(Context->Rsp, RetAddr)) return false;
                Context->Rip = RetAddr;
                Context->Rsp += 8;
                return true; 
            }
            PUNWIND_INFO info = (PUNWIND_INFO)(ImageBase + entry->UnwindData);
            DWORD offsetInFunc = rvaPC - entry->BeginAddress;
            DWORD codeIdx = 0;
            DWORD64* IntegerRegs[] = { &Context->Rax, &Context->Rcx, &Context->Rdx, &Context->Rbx, &Context->Rsp, &Context->Rbp, &Context->Rsi, &Context->Rdi, &Context->R8, &Context->R9, &Context->R10, &Context->R11, &Context->R12, &Context->R13, &Context->R14, &Context->R15 };
            bool ripUpdated = false;
            for (codeIdx = 0; codeIdx < info->CountOfCodes; codeIdx++) {
                UNWIND_CODE code = info->UnwindCode[codeIdx];
                if (offsetInFunc < code.CodeOffset) {
                    UNWIND_OP_CODES op = (UNWIND_OP_CODES)code.UnwindOp;
                    if (op == UWOP_ALLOC_LARGE || op == UWOP_SAVE_NONVOL || op == UWOP_SAVE_XMM128) codeIdx++;
                    else if (op == UWOP_SAVE_NONVOL_FAR || op == UWOP_SAVE_XMM128_FAR) codeIdx += 2;
                    continue; 
                }
                switch (code.UnwindOp) {
                    case UWOP_PUSH_NONVOL: {
                        DWORD64 val;
                        if (SafeRead(Context->Rsp, val)) {
                            *IntegerRegs[code.OpInfo] = val;
                        }
                        Context->Rsp += 8;
                        break;
                    }
                    case UWOP_ALLOC_LARGE: {
                        DWORD size;
                        if (code.OpInfo == 0) {
                            size = info->UnwindCode[++codeIdx].FrameOffset * 8;
                        } else {
                            size = info->UnwindCode[codeIdx + 1].FrameOffset | (info->UnwindCode[codeIdx + 2].FrameOffset << 16);
                            codeIdx += 2;
                        }
                        Context->Rsp += size;
                        break;
                    }
                    case UWOP_ALLOC_SMALL: {
                        Context->Rsp += (code.OpInfo * 8) + 8;
                        break;
                    }
                    case UWOP_SET_FPREG: {
                        DWORD64 regVal = *IntegerRegs[info->FrameRegister];
                        DWORD64 offset = (DWORD64)info->FrameOffset * 16;
                        Context->Rsp = regVal - offset;
                        break;
                    }
                    case UWOP_SAVE_NONVOL: {
                        DWORD64 offset = (DWORD64)info->UnwindCode[++codeIdx].FrameOffset * 8;
                        DWORD64 val;
                        if (SafeRead(Context->Rsp + offset, val)) {
                            *IntegerRegs[code.OpInfo] = val;
                        }
                        break;
                    }
                    case UWOP_SAVE_NONVOL_FAR: {
                        DWORD32 offset = info->UnwindCode[codeIdx + 1].FrameOffset | (info->UnwindCode[codeIdx + 2].FrameOffset << 16);
                        codeIdx += 2;
                        DWORD64 val;
                        if (SafeRead(Context->Rsp + offset, val)) {
                            *IntegerRegs[code.OpInfo] = val;
                        }
                        break;
                    }
                    case UWOP_SAVE_XMM128: {
                        codeIdx++; 
                        break;
                    }
                    case UWOP_SAVE_XMM128_FAR: {
                         codeIdx += 2; 
                         break;
                    }
                    case UWOP_PUSH_MACHFRAME: {
                        DWORD64 base = Context->Rsp;
                        if (code.OpInfo == 1) {
                            base += 8; 
                        }
                        SafeRead(base, Context->Rip);
                        SafeRead(base + 24, Context->Rsp);
                        ripUpdated = true;
                        break;
                    }
                    default: 
                        break; 
                }
            }
            if (!ripUpdated) {
                DWORD64 RetAddr;
                if (!SafeRead(Context->Rsp, RetAddr)) return false;
                Context->Rip = RetAddr;
                Context->Rsp += 8;
            }
            return true;
        }
    };
    inline void manualWalkStack(std::ofstream& log, PCONTEXT initialContext) {
        log << "\nRAW STACK TRACE (Manual Walk):\n";
        #ifdef _WIN64
            CONTEXT ctx = *initialContext;
            DWORD64 ImageBase;
            log << "#00 " << toHex(ctx.Rip) << " " << PEResolver::resolveSymbol(ctx.Rip) << " (Current)\n";
            for (int i = 1; i < 64; i++) { 
                DWORD64 prevRip = ctx.Rip;
                if (!UnwindMachine::VirtualUnwind(ImageBase, prevRip, &ctx)) {
                    log << " <Unwind Failed / Chain End>\n";
                    break;
                }
                if (ctx.Rip == 0) break;
                log << "#" << std::setw(2) << std::setfill('0') << i << " " << toHex(ctx.Rip) 
                    << " " << PEResolver::resolveSymbol(ctx.Rip)
                    << " (Stack: " << toHex(ctx.Rsp) << ")\n";
            }
        #else
            DWORD* ebp = (DWORD*)initialContext->Ebp;
            log << "#00 " << toHex(initialContext->Eip) << " " << PEResolver::resolveSymbol(initialContext->Eip) << " (Current)\n";
            for (int i = 1; i < 32; i++) {
                __try {
                    if (!ebp || (DWORD)ebp & 3) break;
                    DWORD retAddr = *(ebp + 1);
                    DWORD nextEbp = *ebp;
                    if (retAddr == 0) break;
                    log << "#" << i << " " << toHex(retAddr) << " " << PEResolver::resolveSymbol(retAddr) << "\n";
                    if ((DWORD*)nextEbp <= ebp) break;
                    ebp = (DWORD*)nextEbp;
                } __except (EXCEPTION_EXECUTE_HANDLER) break;
            }
        #endif
    }
    class Disassembler {
    public:
        struct Instruction {
            std::string mnemonic;
            std::string op1;
            std::string op2;
            int length;
        };
        static Instruction DecodeStruct(DWORD64 address) {
            unsigned char buffer[16];
            if (!UnwindMachine::SafeRead(address, buffer)) {
                return { "???", "", "", 1 };
            }
            Instruction instr = { "???", "", "", 1 };
            int offset = 0;
            bool is64Bit = true; 
            bool rexW = false;
            bool prefixDone = false;
            while (offset < 15 && !prefixDone) {
                switch (buffer[offset]) {
                    case 0x66: 
                        is64Bit = false; 
                        offset++;
                        break;
                    case 0x48: rexW = true; prefixDone = true; offset++; break; 
                    case 0x40: case 0x41: case 0x42: case 0x43: 
                    case 0x44: case 0x45: case 0x46: case 0x47: 
                        prefixDone = true; offset++; break; 
                    default: prefixDone = true; break;
                }
            }
            unsigned char opcode = buffer[offset++];
            switch (opcode) {
                case 0x89: instr.mnemonic = "MOV"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x8B: instr.mnemonic = "MOV"; ParseModRM(buffer, offset, instr, rexW); break; 
                case 0xC7: instr.mnemonic = "MOV"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0xE8: instr.mnemonic = "CALL"; ParseRel32(address, buffer, offset, instr); break;
                case 0xE9: instr.mnemonic = "JMP";  ParseRel32(address, buffer, offset, instr); break;
                case 0xFF: instr.mnemonic = "CALL/JMP"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x31: case 0x33: instr.mnemonic = "XOR"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x01: case 0x03: instr.mnemonic = "ADD"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x29: case 0x2B: instr.mnemonic = "SUB"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x39: case 0x3B: instr.mnemonic = "CMP"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x85: instr.mnemonic = "TEST"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x8D: instr.mnemonic = "LEA"; ParseModRM(buffer, offset, instr, rexW); break;
                case 0x50: instr.mnemonic = "PUSH RAX"; instr.length = offset; break;
                case 0x51: instr.mnemonic = "PUSH RCX"; instr.length = offset; break;
                case 0x52: instr.mnemonic = "PUSH RDX"; instr.length = offset; break;
                case 0x53: instr.mnemonic = "PUSH RBX"; instr.length = offset; break;
                case 0x55: instr.mnemonic = "PUSH RBP"; instr.length = offset; break;
                case 0x56: instr.mnemonic = "PUSH RSI"; instr.length = offset; break;
                case 0x57: instr.mnemonic = "PUSH RDI"; instr.length = offset; break;
                case 0x58: instr.mnemonic = "POP RAX"; instr.length = offset; break; 
                case 0x59: instr.mnemonic = "POP RCX"; instr.length = offset; break;
                case 0x5B: instr.mnemonic = "POP RBX"; instr.length = offset; break;
                case 0x5D: instr.mnemonic = "POP RBP"; instr.length = offset; break;
                case 0xC3: instr.mnemonic = "RET"; instr.length = offset; break;
                case 0xCC: instr.mnemonic = "INT3"; instr.length = offset; break;
                case 0x90: instr.mnemonic = "NOP"; instr.length = offset; break;
                default: 
                    instr.mnemonic = "DB"; 
                    instr.op1 = toHex(opcode, 2); 
                    instr.length = 1;
                    break;
            }
            return instr;
        }
        static std::string Decode(DWORD64 address) {
            Instruction instr = DecodeStruct(address);
            std::string result = instr.mnemonic;
            if (!instr.op1.empty()) result += " " + instr.op1;
            if (!instr.op2.empty()) result += ", " + instr.op2;
            return result;
        }
    private:
        static void ParseRel32(DWORD64 pc, unsigned char* buf, int& offset, Instruction& ctx) {
             int rel = *(int*)(buf + offset);
             offset += 4;
             DWORD64 target = pc + offset + rel; 
             ctx.op1 = toHex(target);
             ctx.length = offset;
             std::string sym = PEResolver::resolveSymbol(target);
             if (sym.find("Unknown") == std::string::npos) {
                 ctx.op1 += " <" + sym + ">";
             }
        }
        static void ParseModRM(unsigned char* buf, int& offset, Instruction& ctx, bool rexW) {
            unsigned char modrm = buf[offset++];
            int mod = (modrm >> 6) & 3;
            int reg = (modrm >> 3) & 7;
            int rm  = modrm & 7;
            const char* regs64[] = { "RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI" };
            const char* regs32[] = { "EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI" };
            const char** regNames = rexW ? regs64 : regs32;
            ctx.op1 = regNames[reg]; 
            if (mod == 3) {
                ctx.op2 = regNames[rm];
            } else {
                std::string mem = "[";
                if (rm == 4) {
                    unsigned char sib = buf[offset++];
                    int scale = (sib >> 6) & 3;
                    int index = (sib >> 3) & 7;
                    int base  = sib & 7;
                    int scaleVal = 1 << scale;
                    if (base != 5 || mod != 0) mem += regNames[base];
                    if (index != 4) mem += " + " + std::string(regNames[index]) + "*" + std::to_string(scaleVal);
                } else if (rm == 5 && mod == 0) {
                     int disp = *(int*)(buf + offset);
                     offset += 4;
                     mem += "RIP + " + toHex(disp);
                } else {
                     mem += regNames[rm];
                }
                if (mod == 1) {
                    signed char disp = *(signed char*)(buf + offset);
                    offset++;
                    if (disp >= 0) mem += " + " + toHex(disp);
                    else mem += " - " + toHex(-disp);
                } else if (mod == 2) {
                    int disp = *(int*)(buf + offset);
                    offset += 4;
                    mem += " + " + toHex(disp);
                }
                mem += "]";
                ctx.op2 = mem;
            }
            ctx.length = offset;
        }
    };
    inline void dumpRegisters(std::ofstream& log, PCONTEXT ctx) {
        log << "\nCPU REGISTERS:\n";
        #ifdef _WIN64
            log << "RAX: " << toHex(ctx->Rax) << "  R8 : " << toHex(ctx->R8)  << "\n";
            log << "RBX: " << toHex(ctx->Rbx) << "  R9 : " << toHex(ctx->R9)  << "\n";
            log << "RCX: " << toHex(ctx->Rcx) << "  R10: " << toHex(ctx->R10) << "\n";
            log << "RDX: " << toHex(ctx->Rdx) << "  R11: " << toHex(ctx->R11) << "\n";
            log << "RSI: " << toHex(ctx->Rsi) << "  R12: " << toHex(ctx->R12) << "\n";
            log << "RDI: " << toHex(ctx->Rdi) << "  R13: " << toHex(ctx->R13) << "\n";
            log << "RBP: " << toHex(ctx->Rbp) << "  R14: " << toHex(ctx->R14) << "\n";
            log << "RSP: " << toHex(ctx->Rsp) << "  R15: " << toHex(ctx->R15) << "\n";
            log << "RIP: " << toHex(ctx->Rip) << "  EFL: " << toHex(ctx->EFlags, 8) << "\n";
        #else
            log << "EAX: " << toHex(ctx->Eax, 8) << "  ESI: " << toHex(ctx->Esi, 8) << "\n";
            log << "EBX: " << toHex(ctx->Ebx, 8) << "  EDI: " << toHex(ctx->Edi, 8) << "\n";
            log << "ECX: " << toHex(ctx->Ecx, 8) << "  EBP: " << toHex(ctx->Ebp, 8) << "\n";
            log << "EDX: " << toHex(ctx->Edx, 8) << "  ESP: " << toHex(ctx->Esp, 8) << "\n";
            log << "EIP: " << toHex(ctx->Eip, 8) << "  EFL: " << toHex(ctx->EFlags, 8) << "\n";
        #endif
    }
    inline void dumpDisassembly(std::ofstream& log, DWORD64 address, int count = 5) {
        log << "\nDISASSEMBLY (at " << toHex(address) << "):\n";
        DWORD64 curr = address;
        for (int i = 0; i < count; i++) {
             Disassembler::Instruction instr = Disassembler::DecodeStruct(curr);
             log << toHex(curr) << ": " << std::left << std::setw(6) << instr.mnemonic;
             std::string ops;
             if (!instr.op1.empty()) ops += instr.op1;
             if (!instr.op2.empty()) ops += ", " + instr.op2;
             log << ops << "\n";
             curr += instr.length;
        }
    }
    inline void dumpAllThreads(std::ofstream& log) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) {
            log << "Failed to snapshot threads (Error: " << GetLastError() << ").\n";
            return;
        }
        THREADENTRY32 te;
        te.dwSize = sizeof(THREADENTRY32);
        DWORD myPid = GetCurrentProcessId();
        DWORD myTid = GetCurrentThreadId();
        if (Thread32First(hSnap, &te)) {
            log << "\nTHREAD LIST:\n";
            do {
                if (te.th32OwnerProcessID == myPid) {
                    if (te.th32ThreadID == myTid) continue;
                    log << "THREAD ID: " << te.th32ThreadID << "\n";
                    HANDLE hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                    if (hThread) {
                        if (SuspendThread(hThread) != (DWORD)-1) {
                            CONTEXT ctx;
                            ZeroMemory(&ctx, sizeof(CONTEXT));
                            ctx.ContextFlags = CONTEXT_FULL;
                            if (GetThreadContext(hThread, &ctx)) {
                                log << "State: Suspended\n";
                                #ifdef _WIN64
                                DWORD64 pc = ctx.Rip;
                                #else
                                DWORD64 pc = ctx.Eip;
                                #endif
                                log << "RIP:   " << toHex(pc) << " " << PEResolver::resolveSymbol((uintptr_t)pc) << "\n";
                                dumpRegisters(log, &ctx);
                                manualWalkStack(log, &ctx);
                            } else {
                                log << "State: Failed to GetContext (" << GetLastError() << ")\n";
                            }
                            ResumeThread(hThread);
                        } else {
                            log << "State: Failed to Suspend (" << GetLastError() << ")\n";
                        }
                        CloseHandle(hThread);
                    } else {
                        log << "State: Failed to OpenThread (" << GetLastError() << ")\n";
                    }
                }
            } while (Thread32Next(hSnap, &te));
        }
        CloseHandle(hSnap);
    }
    inline LONG WINAPI GlobalCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (g_isCrashing.exchange(true)) return EXCEPTION_EXECUTE_HANDLER;
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        std::cerr << "\n*** KERNEL INTERRUPT ***\n";
        std::cerr << "Code: " << toHex(pExceptionInfo->ExceptionRecord->ExceptionCode, 8) << " (" 
                  << getExceptionName(pExceptionInfo->ExceptionRecord->ExceptionCode) << ")\n";
        DWORD64 crashAddr = (DWORD64)pExceptionInfo->ExceptionRecord->ExceptionAddress;
        std::cerr << "Addr: " << toHex(crashAddr) << " " << PEResolver::resolveSymbol((uintptr_t)crashAddr) << "\n";
        std::time_t now = std::time(nullptr);
        char logFile[64];
        std::strftime(logFile, sizeof(logFile), "crash_%Y%m%d_%H%M%S.log", std::localtime(&now));
        std::ofstream log(logFile);
        if (log.is_open()) {
            log << "LINUXIFY INTERRUPT REPORT\n=========================\n";
            log << "Exception: " << getExceptionName(pExceptionInfo->ExceptionRecord->ExceptionCode) << "\n";
            log << "Address:   " << toHex(crashAddr) << " " << PEResolver::resolveSymbol((uintptr_t)crashAddr) << "\n";
            dumpRegisters(log, pExceptionInfo->ContextRecord);
            #ifdef _WIN64
            dumpDisassembly(log, pExceptionInfo->ContextRecord->Rip, 6);
            #else
            dumpDisassembly(log, pExceptionInfo->ContextRecord->Eip, 6);
            #endif
            manualWalkStack(log, pExceptionInfo->ContextRecord);
            PEResolver::dumpLoadedModules(log);
            dumpMemory(log, crashAddr);
            dumpAllThreads(log);
            log.close();
        }
        if (g_rescueCallback) {
            __try {
                g_rescueCallback();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    inline LONG WINAPI VectoredHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || 
            code == EXCEPTION_STACK_OVERFLOW || 
            code == EXCEPTION_ILLEGAL_INSTRUCTION) {
            return GlobalCrashHandler(pExceptionInfo);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    inline void registerRescueCallback(std::function<void()> callback) {
        g_rescueCallback = callback;
    }
    inline void init() {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        SetUnhandledExceptionFilter(GlobalCrashHandler);
        PEResolver::resolveSymbol((uintptr_t)&init);
        g_vehHandle = AddVectoredExceptionHandler(1, VectoredHandler);
    }
} 
#endif 
