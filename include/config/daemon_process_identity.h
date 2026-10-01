#pragma once
#include "aura_version.h"
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace aura {
// Software-only identity. Current() is initialized once at daemon main entry;
// NewInstance() permits tests to model a different daemon process lifetime.
struct DaemonProcessIdentity {
    std::string process_instance_id, started_at_utc;
    static DaemonProcessIdentity NewInstance() {
        GUID guid{};
        if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot create daemon process identity");
        wchar_t wide[40]{}; StringFromGUID2(guid, wide, 40);
        std::string id;
        for (int i = 1; i < 37; ++i) id.push_back(static_cast<char>(wide[i]));
        SYSTEMTIME now{}; GetSystemTime(&now);
        char timestamp[32]{};
        std::snprintf(timestamp, sizeof(timestamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        return {id, timestamp};
    }
    static const DaemonProcessIdentity& Current() {
        static const auto instance = NewInstance(); return instance;
    }
};
} // namespace aura
