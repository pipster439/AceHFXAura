#pragma once
#include <windows.h>
#include <third_party/json.hpp>
#include <stdexcept>
#include <string>
using Json = nlohmann::json;
constexpr DWORD MaximumWorkerFrame = 2 * 1024 * 1024;
inline void Transfer(HANDLE pipe, void* data, DWORD size, bool write) {
    auto bytes = static_cast<char*>(data);
    while (size) {
        DWORD transferred = 0;
        bool ok = write ? WriteFile(pipe, bytes, size, &transferred, nullptr) != FALSE :
                          ReadFile(pipe, bytes, size, &transferred, nullptr) != FALSE;
        if (!ok || !transferred) throw std::runtime_error("WorkerDisconnected");
        bytes += transferred; size -= transferred;
    }
}
inline Json ReadFrame() {
    DWORD size = 0; Transfer(GetStdHandle(STD_INPUT_HANDLE), &size, 4, false);
    if (!size || size > MaximumWorkerFrame) throw std::runtime_error("WorkerFrameSizeInvalid");
    std::string bytes(size, '\0'); Transfer(GetStdHandle(STD_INPUT_HANDLE), bytes.data(), size, false);
    return Json::parse(bytes);
}
inline void WriteFrame(const Json& frame) {
    auto bytes = frame.dump();
    if (bytes.empty() || bytes.size() > MaximumWorkerFrame) throw std::runtime_error("WorkerFrameTooLarge");
    auto size = static_cast<DWORD>(bytes.size());
    Transfer(GetStdHandle(STD_OUTPUT_HANDLE), &size, 4, true);
    Transfer(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(), size, true);
}
