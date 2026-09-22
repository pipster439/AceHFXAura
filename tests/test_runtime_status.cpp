#include "test_util.h"
#include "aura/runtime_status.h"
#include "aura/aura_adapter.h"
#include "aura/aura_types.h"

#include <thread>
#include <vector>
#include <atomic>
#include <future>

int main() {
    int failures = 0;

    std::cout << "[Test 1] RuntimeStatusSnapshot 默认构造值校验\n";
    {
        aura::RuntimeStatusSnapshot snap;
        CHECK(snap.hardware_connected == false, "hardware_connected 默认为 false");
        CHECK(snap.adapter_state == "uninitialized", "adapter_state 默认为 uninitialized");
        CHECK(snap.configured_backend == "auto", "configured_backend 默认为 auto");
        CHECK(snap.active_backend == "unknown", "active_backend 默认为 unknown");
        CHECK(snap.device_path.empty(), "device_path 默认为空");
        CHECK(snap.last_hardware_error.empty(), "last_hardware_error 默认为空");
        CHECK(snap.active_profile == "(None)", "active_profile 默认为 (None)");
        CHECK(snap.target_fps == 25, "target_fps 默认为 25");
        CHECK(snap.dry_run == false, "dry_run 默认为 false");
        CHECK(snap.gsi_active == false, "gsi_active 默认为 false");
        CHECK(snap.foreground_process.empty(), "foreground_process 默认为空");
    }

    std::cout << "[Test 2] AdapterStateToString 与 HardwareBackendToString 枚举映射校验\n";
    {
        CHECK(std::string(aura::AdapterStateToString(aura::AdapterState::Uninitialized)) == "uninitialized", "AdapterState::Uninitialized -> uninitialized");
        CHECK(std::string(aura::AdapterStateToString(aura::AdapterState::Connecting)) == "connecting", "AdapterState::Connecting -> connecting");
        CHECK(std::string(aura::AdapterStateToString(aura::AdapterState::Connected)) == "connected", "AdapterState::Connected -> connected");
        CHECK(std::string(aura::AdapterStateToString(aura::AdapterState::Disconnected)) == "disconnected", "AdapterState::Disconnected -> disconnected");
        CHECK(std::string(aura::AdapterStateToString(aura::AdapterState::BackoffWait)) == "backoff_wait", "AdapterState::BackoffWait -> backoff_wait");
        CHECK(std::string(aura::AdapterStateToString(aura::AdapterState::Error)) == "error", "AdapterState::Error -> error");

        CHECK(std::string(aura::HardwareBackendToString(aura::HardwareBackend::Auto)) == "auto", "HardwareBackend::Auto -> auto");
        CHECK(std::string(aura::HardwareBackendToString(aura::HardwareBackend::NativeHid)) == "native_hid", "HardwareBackend::NativeHid -> native_hid");
        CHECK(std::string(aura::HardwareBackendToString(aura::HardwareBackend::LegacyHal)) == "legacy_hal", "HardwareBackend::LegacyHal -> legacy_hal");
    }

    std::cout << "[Test 3] RuntimeStatusStore 多线程并发读写安全压力测试\n";
    {
        aura::RuntimeStatusStore store;
        constexpr int reader_count = 4, writer_count = 2;
        constexpr int reads_per_thread = 4000, writes_per_thread = 2000;
        std::atomic<uint64_t> read_count{0}, write_count{0}, incoherent_count{0};
        std::atomic<int> ready{0};
        std::promise<void> start;
        const auto gate = start.get_future().share();

        auto make_snapshot = [](int writer, int phase) {
            aura::RuntimeStatusSnapshot snap;
            snap.hardware_connected = (phase % 2 == 0);
            snap.adapter_state = snap.hardware_connected ? "connected" : "disconnected";
            snap.active_backend = writer == 0 ? "native_hid" : "legacy_hal";
            snap.active_profile = "profile_" + std::to_string(phase % 10);
            snap.target_fps = 20 + phase;
            snap.dry_run = writer == 1;
            snap.gsi_active = phase % 3 == 0;
            snap.foreground_process = "app_" + std::to_string(phase) + ".exe";
            return snap;
        };
        // Seed a valid tuple so readers can verify every snapshot, even before
        // either writer is scheduled. No throughput assumption about CI hosts.
        store.Update(make_snapshot(0, 0));
        auto coherent = [&](const aura::RuntimeStatusSnapshot& snap) {
            const int phase = snap.target_fps - 20;
            if (phase < 0 || phase >= 40) return false;
            const auto expected = make_snapshot(snap.dry_run ? 1 : 0, phase);
            return snap.hardware_connected == expected.hardware_connected &&
                snap.adapter_state == expected.adapter_state &&
                snap.active_backend == expected.active_backend &&
                snap.active_profile == expected.active_profile &&
                snap.gsi_active == expected.gsi_active &&
                snap.foreground_process == expected.foreground_process;
        };

        std::vector<std::thread> readers, writers;
        for (int i = 0; i < reader_count; ++i) {
            readers.emplace_back([&]() {
                ++ready;
                gate.wait();
                for (int read = 0; read < reads_per_thread; ++read) {
                    if (!coherent(store.GetSnapshot())) ++incoherent_count;
                    ++read_count;
                    std::this_thread::yield();
                }
            });
        }
        for (int i = 0; i < writer_count; ++i) {
            writers.emplace_back([&, i]() {
                ++ready;
                gate.wait();
                for (int tick = 0; tick < writes_per_thread; ++tick) {
                    store.Update(make_snapshot(i, tick % 40));
                    ++write_count;
                    std::this_thread::yield();
                }
            });
        }
        while (ready.load() != reader_count + writer_count) std::this_thread::yield();
        start.set_value();
        for (auto& r : readers) r.join();
        for (auto& w : writers) w.join();

        CHECK(read_count.load() == reader_count * reads_per_thread, "all scheduled snapshot reads complete");
        CHECK(write_count.load() == writer_count * writes_per_thread, "all scheduled snapshot writes complete");
        CHECK(incoherent_count.load() == 0, "every concurrent snapshot contains a coherent field tuple");
        const auto final = store.GetSnapshot();
        CHECK(coherent(final) && final.target_fps == 20 + (writes_per_thread - 1) % 40,
              "final snapshot contains a completed writer's last update");
    }

    std::cout << "[Test 4] JSON Schema 基本字段与结构校验\n";
    {
        aura::RuntimeStatusSnapshot snap;
        snap.hardware_connected = true;
        snap.adapter_state = "connected";
        snap.configured_backend = "auto";
        snap.active_backend = "native_hid";
        snap.device_path = "\\\\?\\hid#vid_0b05&pid_1a38#...";
        snap.last_hardware_error = "";
        snap.active_profile = "desktop";
        snap.target_fps = 25;
        snap.dry_run = false;
        snap.gsi_active = false;
        snap.foreground_process = "explorer.exe";

        auto j = snap.ToJson();
        CHECK(j.contains("status") && j["status"] == "ok", "JSON status 存在且为 ok");
        CHECK(j.contains("api_version") && j["api_version"] == 1, "JSON api_version 存在且为 1");

        CHECK(j.contains("hardware") && j["hardware"].is_object(), "JSON 包含 hardware 对象");
        CHECK(j["hardware"].value("connected", false) == true, "hardware.connected 字段正确");
        CHECK(j["hardware"].value("state", "") == "connected", "hardware.state 字段正确");
        CHECK(j["hardware"].value("configured_backend", "") == "auto", "hardware.configured_backend 字段正确");
        CHECK(j["hardware"].value("active_backend", "") == "native_hid", "hardware.active_backend 字段正确");
        CHECK(!j["hardware"].value("device_path", "").empty(), "hardware.device_path 字段非空");
        CHECK(j["hardware"].value("last_error", "") == "", "hardware.last_error 字段正确");

        CHECK(j.contains("runtime") && j["runtime"].is_object(), "JSON 包含 runtime 对象");
        CHECK(j["runtime"].value("active_profile", "") == "desktop", "runtime.active_profile 字段正确");
        CHECK(j["runtime"].value("fps", 0) == 25, "runtime.fps 字段正确");
        CHECK(j["runtime"].value("dry_run", true) == false, "runtime.dry_run 字段正确");
        CHECK(j["runtime"].value("foreground_process", "") == "explorer.exe", "runtime.foreground_process 字段正确");

        CHECK(j.contains("gsi") && j["gsi"].is_object(), "JSON 包含 gsi 对象");
        CHECK(j["gsi"].value("active", true) == false, "gsi.active 字段正确");
    }

    if (failures == 0) {
        std::cout << "\n[PASS] 所有 RuntimeStatus 测试均已成功通过！\n";
        return 0;
    } else {
        std::cerr << "\n[FAIL] 存在 " << failures << " 个测试断言失败！\n";
        return 1;
    }
}
