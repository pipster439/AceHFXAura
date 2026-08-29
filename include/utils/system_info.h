#pragma once

#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "psapi.lib")

namespace aura {

struct ProcessMemoryStats {
    uint64_t working_set_bytes = 0;
    uint64_t peak_working_set_bytes = 0;
    uint64_t private_bytes = 0; // PagefileUsage in PROCESS_MEMORY_COUNTERS_EX
};

inline bool GetCurrentProcessMemory(ProcessMemoryStats& stats) {
    HANDLE hProcess = GetCurrentProcess();
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);

    if (GetProcessMemoryInfo(hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        stats.working_set_bytes = pmc.WorkingSetSize;
        stats.peak_working_set_bytes = pmc.PeakWorkingSetSize;
        stats.private_bytes = pmc.PrivateUsage;
        return true;
    }
    return false;
}

inline std::string FormatBytes(uint64_t bytes) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    if (bytes >= 1024 * 1024 * 1024) {
        oss << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else if (bytes >= 1024 * 1024) {
        oss << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else if (bytes >= 1024) {
        oss << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else {
        oss << bytes << " B";
    }
    return oss.str();
}

} // namespace aura
