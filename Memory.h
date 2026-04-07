#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include <optional>

class Memory {
public:
    DWORD  process_id{ 0 };
    HANDLE process_handle{ nullptr };

    explicit Memory(const char* process_name) { UpdateProcess(process_name); }
    ~Memory() { if (process_handle) CloseHandle(process_handle); }

    void UpdateProcess(const char* process_name) {
        if (process_handle) { CloseHandle(process_handle); process_handle = nullptr; }
        process_id = 0;

        PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;

        WCHAR wname[MAX_PATH];
        if (!MultiByteToWideChar(CP_ACP, 0, process_name, -1, wname, MAX_PATH))
        {
            CloseHandle(snap); return;
        }

        if (Process32FirstW(snap, &entry)) {
            do {
                if (_wcsicmp(entry.szExeFile, wname) == 0) {
                    process_id = entry.th32ProcessID;
                    process_handle = OpenProcess(
                        PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                        PROCESS_QUERY_INFORMATION, FALSE, process_id);
                    if (!process_handle)
                        process_handle = OpenProcess(
                            PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
                            FALSE, process_id);
                    break;
                }
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);
    }

    bool IsValid() const { return process_handle != nullptr; }

    uintptr_t GetModuleAddress(const char* module_name) const {
        MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
        HANDLE snap = CreateToolhelp32Snapshot(
            TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
        if (snap == INVALID_HANDLE_VALUE) return 0;

        WCHAR wname[MAX_PATH];
        if (!MultiByteToWideChar(CP_ACP, 0, module_name, -1, wname, MAX_PATH))
        {
            CloseHandle(snap); return 0;
        }

        uintptr_t base = 0;
        if (Module32FirstW(snap, &entry)) {
            do {
                if (_wcsicmp(entry.szModule, wname) == 0) {
                    base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                    break;
                }
            } while (Module32NextW(snap, &entry));
        }
        CloseHandle(snap);
        return base;
    }

    // ── Read<T> ──────────────────────────────────────────────────────────────
    template <typename T>
    T Read(uintptr_t address) const {
        static_assert(std::is_trivially_copyable_v<T>);
        T buf{};
        if (!process_handle) return buf;
        SIZE_T n = 0;
        ReadProcessMemory(process_handle,
            reinterpret_cast<LPCVOID>(address), &buf, sizeof(T), &n);
        return buf;
    }

    template <typename T>
    std::optional<T> ReadChecked(uintptr_t address) const {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!process_handle) return std::nullopt;
        T buf{}; SIZE_T n = 0;
        if (!ReadProcessMemory(process_handle,
            reinterpret_cast<LPCVOID>(address), &buf, sizeof(T), &n)
            || n != sizeof(T))
            return std::nullopt;
        return buf;
    }

    bool ReadBytes(uintptr_t address, void* out, std::size_t size) const {
        if (!process_handle) return false;
        SIZE_T n = 0;
        return ReadProcessMemory(process_handle,
            reinterpret_cast<LPCVOID>(address), out, size, &n) != 0;
    }

    // ── Write<T> ──────────────────────────────────────────────────────────────
    // Used for features like no-flash (write over enemy/local flash values).
    // Returns true only if WriteProcessMemory reports a full write.
    template <typename T>
    bool Write(uintptr_t address, const T& value) const {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!process_handle) return false;
        SIZE_T n = 0;
        return WriteProcessMemory(process_handle,
            reinterpret_cast<LPVOID>(address),
            &value, sizeof(T), &n) && n == sizeof(T);
    }

    bool WriteBytes(uintptr_t address, const void* data, std::size_t size) const {
        if (!process_handle) return false;
        SIZE_T n = 0;
        return WriteProcessMemory(process_handle,
            reinterpret_cast<LPVOID>(address), data, size, &n) && n == size;
    }
};