#include <windows.h>
#include <iostream>
#include <string>
#include <tlhelp32.h>
#include <vector>

// Включаем поддержку filesystem для C++17
#ifdef __cplusplus
#if __cplusplus >= 201703L
#include <filesystem>
#endif
#endif

class D3D12Injector {
private:
    HANDLE hProcess;
    DWORD processId;

    DWORD GetProcessIdByName(const std::string& processName) {
        DWORD pid = 0;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe;
            pe.dwSize = sizeof(PROCESSENTRY32);

            if (Process32First(snapshot, &pe)) {
                do {
                    std::string currentProcess(pe.szExeFile);
                    if (currentProcess == processName) {
                        pid = pe.th32ProcessID;
                        break;
                    }
                } while (Process32Next(snapshot, &pe));
            }
            CloseHandle(snapshot);
        }
        return pid;
    }

    bool EnableDebugPrivilege() {
        HANDLE hToken;
        TOKEN_PRIVILEGES tkp;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
            return false;

        // Используем широкие строки для Unicode
        if (!LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &tkp.Privileges[0].Luid)) {
            CloseHandle(hToken);
            return false;
        }

        tkp.PrivilegeCount = 1;
        tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        bool result = AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, NULL, 0);
        CloseHandle(hToken);

        return result && GetLastError() == ERROR_SUCCESS;
    }

public:
    D3D12Injector() : hProcess(nullptr), processId(0) {}

    bool Initialize(const std::string& processName) {
        if (!EnableDebugPrivilege()) {
            std::cerr << "[-] Failed to enable debug privilege!" << std::endl;
            return false;
        }

        DWORD processId;
        while (true) {
            processId = GetProcessIdByName(processName);
            if (processId != 0) {
                break;
            }
            std::cerr << "[-] Process not found: " << processName << std::endl;
            Sleep(1000);
        }

        std::cout << "[+] Found process '" << processName << "' with PID: " << processId << std::endl;
        Sleep(5000);
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
        if (!hProcess) {
            std::cerr << "[-] Failed to open process! Error: " << GetLastError() << std::endl;
            return false;
        }

        std::cout << "[+] Successfully opened process" << std::endl;
        return true;
    }

    bool Inject(const std::string& dllPath) {
        if (!hProcess) {
            std::cerr << "[-] Process not initialized!" << std::endl;
            return false;
        }

        // Проверка существования DLL без filesystem
        DWORD fileAttrib = GetFileAttributesA(dllPath.c_str());
        if (fileAttrib == INVALID_FILE_ATTRIBUTES) {
            std::cerr << "[-] DLL file not found: " << dllPath << std::endl;
            return false;
        }

        // Получаем абсолютный путь
        char absolutePath[MAX_PATH];
        if (!GetFullPathNameA(dllPath.c_str(), MAX_PATH, absolutePath, NULL)) {
            std::cerr << "[-] Failed to get absolute path!" << std::endl;
            return false;
        }

        std::string finalPath(absolutePath);
        std::cout << "[+] Using DLL: " << finalPath << std::endl;

        // Allocate memory in target process
        size_t dllPathSize = finalPath.length() + 1;
        LPVOID remoteMemory = VirtualAllocEx(hProcess, NULL, dllPathSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (!remoteMemory) {
            std::cerr << "[-] Failed to allocate memory in target process! Error: " << GetLastError() << std::endl;
            return false;
        }

        std::cout << "[+] Allocated memory at: 0x" << std::hex << (uintptr_t)remoteMemory << std::dec << std::endl;

        // Write DLL path to allocated memory
        if (!WriteProcessMemory(hProcess, remoteMemory, finalPath.c_str(), dllPathSize, NULL)) {
            std::cerr << "[-] Failed to write to process memory! Error: " << GetLastError() << std::endl;
            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
            return false;
        }

        std::cout << "[+] DLL path written to target process" << std::endl;

        // Get LoadLibraryA address
        HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
        if (!kernel32) {
            std::cerr << "[-] Failed to get kernel32 module handle!" << std::endl;
            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
            return false;
        }

        LPVOID loadLibraryAddr = (LPVOID)GetProcAddress(kernel32, "LoadLibraryA");

        if (!loadLibraryAddr) {
            std::cerr << "[-] Failed to get LoadLibraryA address!" << std::endl;
            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
            return false;
        }

        std::cout << "[+] LoadLibraryA address: 0x" << std::hex << (uintptr_t)loadLibraryAddr << std::dec << std::endl;

        // Create remote thread to load DLL
        HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)loadLibraryAddr,
            remoteMemory, 0, NULL);

        if (!hThread) {
            std::cerr << "[-] Failed to create remote thread! Error: " << GetLastError() << std::endl;
            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
            return false;
        }

        std::cout << "[+] Remote thread created, waiting for injection..." << std::endl;

        // Wait for thread to complete with timeout
        DWORD waitResult = WaitForSingleObject(hThread, 10000);

        if (waitResult == WAIT_TIMEOUT) {
            std::cerr << "[-] Injection timeout!" << std::endl;
            TerminateThread(hThread, 0);
            CloseHandle(hThread);
            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
            return false;
        }

        // Get thread exit code
        DWORD exitCode;
        GetExitCodeThread(hThread, &exitCode);

        if (exitCode == 0) {
            std::cerr << "[-] LoadLibraryA failed in target process!" << std::endl;
            CloseHandle(hThread);
            VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);
            return false;
        }

        std::cout << "[+] DLL loaded successfully! Module base: 0x" << std::hex << exitCode << std::dec << std::endl;

        // Cleanup
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, remoteMemory, 0, MEM_RELEASE);

        return true;
    }

    void Cleanup() {
        if (hProcess) {
            CloseHandle(hProcess);
            hProcess = nullptr;
        }
    }

    ~D3D12Injector() {
        Cleanup();
    }
};

void PrintUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <process_name> <dll_path>" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << programName << " MyDX12Game.exe C:\\DLLs\\myhook.dll" << std::endl;
    std::cout << "  " << programName << " notepad.exe .\\test.dll" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help     Show this help message" << std::endl;
    std::cout << "  -l             List running processes" << std::endl;
}

void ListProcesses() {
    std::cout << "\nRunning processes:" << std::endl;
    std::cout << "-----------------" << std::endl;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(snapshot, &pe)) {
            int count = 0;
            do {
                std::cout << "[" << pe.th32ProcessID << "] " << pe.szExeFile << std::endl;
                count++;
            } while (Process32Next(snapshot, &pe));

            std::cout << "\nTotal processes: " << count << std::endl;
        }
        CloseHandle(snapshot);
    }
}

int main(int argc, char* argv[]) {
    // Parse arguments
    //if (argc < 2) {
    //    PrintUsage(argv[0]);
    //    std::cout << "\nPress Enter to exit...";
    //    std::cin.get();
    //    return 1;
    //}

    //std::string arg1 = argv[1];

    //if (arg1 == "-h" || arg1 == "--help") {
    //    PrintUsage(argv[0]);
    //    std::cout << "\nPress Enter to exit...";
    //    std::cin.get();
    //    return 0;
    //}

    //if (arg1 == "-l") {
    //    ListProcesses();
    //    std::cout << "\nPress Enter to exit...";
    //    std::cin.get();
    //    return 0;
    //}

    //if (argc < 3) {
    //    std::cerr << "Error: Missing DLL path!" << std::endl;
    //    PrintUsage(argv[0]);
    //    std::cout << "\nPress Enter to exit...";
    //    std::cin.get();
    //    return 1;
    //}

    //std::string processName = argv[1];
    //std::string dllPath = argv[2];

    std::string processName = "Polaris-Win64-Shipping.exe";
    std::string dllPath = ".\\dll.dll";

    std::cout << "========================================" << std::endl;
    std::cout << "    DX12 Injector v1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[+] Target process: " << processName << std::endl;
    std::cout << "[+] DLL path: " << dllPath << std::endl;
    std::cout << std::endl;

    // Check if running as admin
    BOOL isElevated = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, size, &size)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }

    if (!isElevated) {
        std::cerr << "[-] Warning: Not running as administrator!" << std::endl;
        std::cerr << "[-] Injection may fail. Restart as admin for best results." << std::endl;
        std::cout << std::endl;
    }

    D3D12Injector injector;

    if (injector.Initialize(processName)) {
        if (injector.Inject(dllPath)) {
            std::cout << std::endl;
            std::cout << "========================================" << std::endl;
            std::cout << "[+] SUCCESS: DLL injected!" << std::endl;
            std::cout << "========================================" << std::endl;
        }
        else {
            std::cerr << std::endl;
            std::cerr << "========================================" << std::endl;
            std::cerr << "[-] FAILED: Injection failed!" << std::endl;
            std::cerr << "========================================" << std::endl;
        }
    }
    else {
        std::cerr << std::endl;
        std::cerr << "========================================" << std::endl;
        std::cerr << "[-] FAILED: Could not initialize!" << std::endl;
        std::cerr << "========================================" << std::endl;
    }

    injector.Cleanup();

    std::cout << "\nPress Enter to exit...";
    std::cin.get();

    return 0;
}