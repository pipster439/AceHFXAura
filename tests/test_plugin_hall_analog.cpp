#include "aura/hardware/m605_hall_telemetry.h"
#include "aura/keymap.h"
#include "engine/plugin_manager.h"

#include <windows.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

#define CHECK(expr)                                                               \
    do {                                                                          \
        if (!(expr)) {                                                            \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__        \
                      << ": " << #expr << "\n";                                   \
            return 1;                                                             \
        }                                                                         \
    } while (false)

int main(int argc, char** argv) {
    CHECK(argc >= 2);
    const fs::path dll_path = fs::absolute(argv[1]);
    CHECK(fs::exists(dll_path));

    // Create a scratch keymap file mapping "A" -> led_id 20 so Render sets buffer[57..59]
    const fs::path scratch = fs::temp_directory_path() / ("aura_hall_plugin_test_" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(scratch / "plugins");
    const fs::path keymap_path = scratch / "keymap.json";
    {
        std::ofstream ofs(keymap_path);
        ofs << R"({"keys":{"A":{"key_name":"A","led_id":20,"physical_row":3,"physical_col":2}}})";
    }
    aura::Keymap keymap;
    CHECK(keymap.LoadFromJson(keymap_path.string()));

    // Publish a live test snapshot into Local\AuraM605HallAnalogV1
    HANDLE h_map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(sizeof(aura::m605::hall::AuraM605HallSharedMemory)),
        aura::m605::hall::kHallShmName);
    CHECK(h_map != nullptr);
    auto* shm = static_cast<aura::m605::hall::AuraM605HallSharedMemory*>(
        MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(aura::m605::hall::AuraM605HallSharedMemory)));
    CHECK(shm != nullptr);

    std::array<aura::m605::hall::AuraM605HallSharedKey, aura::m605::hall::kHallKeyCount> keys{};
    keys[0].hid_usage = 4; // 'A'
    keys[0].logical_id = 0x0602;
    keys[0].normalized = 0.5f;
    keys[0].depth_mm = 2.0f;
    keys[0].raw = 4336;
    aura::m605::hall::PublishSharedMemoryLocked(shm, true, 1000ULL, 10, keys);

    {
        aura::PluginManager manager;
        auto inst = manager.LoadPluginInstance(dll_path.string());
        CHECK(bool(inst));
        CHECK(inst.GetGeneration()->hall_analog.version == AURA_HALL_ANALOG_VERSION);

        const auto summaries = manager.GetPluginSummaries();
        CHECK(summaries.size() == 1);
        CHECK(summaries[0].effect_name == "hall_reactive");
        CHECK(summaries[0].kind == "native_dll");
        CHECK(summaries[0].has_hall_analog);

        aura::FrameBuffer frame;
        frame.Clear();
        inst.GetEffect()->Render(100, frame, keymap);
        // LED 20 => FrameBuffer::SetKey(20, ...) writes bytes [60, 61, 62]
        const size_t offset = static_cast<size_t>(20) * 3;
        CHECK(frame.buffer[offset + 0] > 0);
        CHECK(frame.buffer[offset + 2] > 0);
    }

    UnmapViewOfFile(shm);
    CloseHandle(h_map);
    std::error_code ec;
    fs::remove_all(scratch, ec);
    std::cout << "test_plugin_hall_analog PASSED\n";
    return 0;
}
