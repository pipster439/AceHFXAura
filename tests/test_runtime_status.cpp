#include "test_util.h"
#include "aura/runtime_status.h"
#include "aura/aura_adapter.h"
#include "aura/aura_types.h"

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

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
        std::atomic<bool> run{true};
        std::atomic<uint64_t> read_count{0};
        std::atomic<uint64_t> write_count{0};

        // 4 个 reader 线程
        std::vector<std::thread> readers;
        for (int i = 0; i < 4; ++i) {
            readers.emplace_back([&]() {
                while (run.load(std::memory_order_relaxed)) {
                    auto snap = store.GetSnapshot();
                    // 校验读取出的数据逻辑自洽 (非零未受损)
                    if (snap.target_fps >= 10 && snap.target_fps <= 100) {
                        read_count.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            });
        }

        // 2 个 writer 线程
        std::vector<std::thread> writers;
        for (int i = 0; i < 2; ++i) {
            writers.emplace_back([&, i]() {
                int tick = 0;
                while (run.load(std::memory_order_relaxed)) {
                    aura::RuntimeStatusSnapshot snap;
                    snap.hardware_connected = (tick % 2 == 0);
                    snap.adapter_state = (tick % 2 == 0) ? "connected" : "disconnected";
                    snap.active_backend = (i == 0) ? "native_hid" : "legacy_hal";
                    snap.active_profile = "profile_" + std::to_string(tick % 10);
                    snap.target_fps = 20 + (tick % 40);
                    snap.dry_run = (i == 1);
                    snap.gsi_active = (tick % 3 == 0);
                    snap.foreground_process = "app_" + std::to_string(tick) + ".exe";
                    store.Update(snap);
                    write_count.fetch_add(1, std::memory_order_relaxed);
                    ++tick;
                    std::this_thread::yield();
                }
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        run.store(false, std::memory_order_relaxed);

        for (auto& r : readers) r.join();
        for (auto& w : writers) w.join();

        CHECK(read_count.load() > 1000, "多线程高频读取正常执行 (count > 1000)");
        CHECK(write_count.load() > 100, "多线程高频写入正常执行 (count > 100)");
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
