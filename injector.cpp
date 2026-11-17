#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

// Hilfsfunktion für Case-Insensitive Teilstring-Suche (findet sowohl GTA5_Enhanced.exe als auch GTA5_Enhanced.e)
bool ContainsIgnoreCase(std::string str, std::string substr) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    std::transform(substr.begin(), substr.end(), substr.begin(), ::tolower);
    return str.find(substr) != std::string::npos;
}

// Prototyp für Fallback-Methode NtCreateThreadEx (undokumentierte Native API in ntdll.dll)
typedef NTSTATUS(NTAPI* pfnNtCreateThreadEx)(
    OUT PHANDLE hThread,
    IN ACCESS_MASK DesiredAccess,
    IN PVOID ObjectAttributes,
    IN HANDLE ProcessHandle,
    IN PVOID lpStartAddress,
    IN PVOID lpParameter,
    IN ULONG Flags,
    IN SIZE_T StackZeroBits,
    IN SIZE_T SizeOfStackCommit,
    IN SIZE_T SizeOfStackReserve,
    OUT PVOID lpBytesBuffer
);

DWORD FindTargetProcess(const char* searchName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        std::cerr << "[!] CreateToolhelp32Snapshot fehlgeschlagen. Error: " << GetLastError() << "\n";
        return 0;
    }

    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    DWORD exactPid = 0;
    DWORD partialPid = 0;
    std::vector<std::string> visibleProcesses;

    if (Process32First(hSnap, &pe32)) {
        do {
            std::string exeName = pe32.szExeFile;
            visibleProcesses.push_back(exeName + " (PID: " + std::to_string(pe32.th32ProcessID) + ")");

            // Ignoriere den Injector selbst und den Launcher
            if (ContainsIgnoreCase(exeName, "injector") || ContainsIgnoreCase(exeName, "PlayGTAV")) {
                continue;
            }

            // 1. Priorität: Exakter Treffer
            if (_stricmp(pe32.szExeFile, searchName) == 0) {
                exactPid = pe32.th32ProcessID;
                break;
            }

            // 2. Priorität (Auto-Match): Enthält den Suchbegriff oder "GTA5"
            if (ContainsIgnoreCase(exeName, searchName) || ContainsIgnoreCase(exeName, "GTA5")) {
                partialPid = pe32.th32ProcessID;
            }
        } while (Process32Next(hSnap, &pe32));
    }

    CloseHandle(hSnap);

    if (exactPid != 0) return exactPid;
    if (partialPid != 0) return partialPid;

    // Diagnose-Ausgabe, falls wir im falschen Wine-Server gelandet sind
    std::cerr << "[!] Zielprozess nicht gefunden. Aktuell sichtbare Prozesse in dieser Wine-Instanz:\n";
    for (const auto& proc : visibleProcesses) {
        std::cerr << "    - " << proc << "\n";
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Verwendung: injector.exe <Prozessname> <DLL-Pfad>\n";
        return 1;
    }

    const char* targetName = argv[1];
    const char* rawDllPath = argv[2];

    char fullDllPath[MAX_PATH] = {0};
    if (!GetFullPathNameA(rawDllPath, MAX_PATH, fullDllPath, nullptr)) {
        std::cerr << "[!] Pfadauflösung fehlgeschlagen. Error: " << GetLastError() << "\n";
        return 1;
    }

    if (GetFileAttributesA(fullDllPath) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "[!] DLL nicht gefunden unter: " << fullDllPath << "\n";
        return 1;
    }

    std::cout << "[*] Suche Prozess '" << targetName << "'...\n";
    DWORD pid = FindTargetProcess(targetName);
    if (pid == 0) {
        return 2; // Exit-Code 2 signalisiert dem Bash-Skript: Fallback starten!
    }

    std::cout << "[+] Zielprozess gefunden! Wine-PID: " << pid << "\n";
    std::cout << "[*] Injiziere: " << fullDllPath << "\n";

    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid
    );

    if (!hProcess) {
        std::cerr << "[!] OpenProcess fehlgeschlagen. Error: " << GetLastError() << "\n";
        return 3;
    }

    size_t pathLen = strlen(fullDllPath) + 1;
    LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, pathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        std::cerr << "[!] VirtualAllocEx fehlgeschlagen. Error: " << GetLastError() << "\n";
        CloseHandle(hProcess);
        return 3;
    }

    if (!WriteProcessMemory(hProcess, remoteMem, fullDllPath, pathLen, nullptr)) {
        std::cerr << "[!] WriteProcessMemory fehlgeschlagen. Error: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 3;
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    LPVOID loadLibAddr = reinterpret_cast<LPVOID>(GetProcAddress(hKernel32, "LoadLibraryA"));

    // --- MAIN METHODE: CreateRemoteThread ---
    std::cout << "[*] Starte Main-Injektion (CreateRemoteThread -> LoadLibraryA)...\n";
    HANDLE hThread = CreateRemoteThread(
        hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibAddr),
                                        remoteMem, 0, nullptr
    );

    // --- FALLBACK METHODE IN C++: NtCreateThreadEx ---
    if (!hThread) {
        std::cout << "[!] CreateRemoteThread fehlgeschlagen (Error " << GetLastError()
        << "). Nutze Fallback (NtCreateThreadEx)...\n";
        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        auto NtCreateThreadEx = reinterpret_cast<pfnNtCreateThreadEx>(GetProcAddress(hNtdll, "NtCreateThreadEx"));
        if (NtCreateThreadEx) {
            NTSTATUS status = NtCreateThreadEx(
                &hThread, THREAD_ALL_ACCESS, nullptr, hProcess,
                loadLibAddr, remoteMem, 0, 0, 0, 0, nullptr
            );
            if (status != 0) hThread = nullptr;
        }
    }

    if (!hThread) {
        std::cerr << "[!] Alle Thread-Erstellungsmethoden fehlgeschlagen!\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 4;
    }

    WaitForSingleObject(hThread, 10000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    std::cout << "[+] Injektion erfolgreich ausgeführt! (Thread-ExitCode: 0x"
    << std::hex << exitCode << std::dec << ")\n";

    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return 0;
}
