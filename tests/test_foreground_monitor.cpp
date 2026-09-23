#include "monitor/foreground_monitor.h"
#include "config/rule_engine.h"
#include "gsi/gsi_adapter.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace aura {
class ForegroundMonitorTestPeer {
public:
    static void Event(ForegroundMonitor& monitor, HWND hwnd) { monitor.ObserveForegroundEvent(hwnd); }
    static void Reconcile(ForegroundMonitor& monitor) { monitor.ReconcileForeground(); }
};
}

namespace {
using namespace aura;
using namespace std::chrono_literals;

HWND Window(std::uintptr_t id) { return reinterpret_cast<HWND>(id); }
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Resolve(HWND hwnd) {
    if (hwnd == Window(1)) return "cs2.exe";
    if (hwnd == Window(2)) return "explorer.exe";
    if (hwnd == Window(3)) return "GameOverlayUI.exe";
    if (hwnd == Window(4)) return "CS2.EXE";
    return ""; // destroyed/unresolvable HWND
}

void StateTransitions() {
    HWND authoritative = Window(1);
    std::vector<std::string> callbacks;
    ForegroundMonitor monitor([&] { return authoritative; }, Resolve);
    monitor.SetCallback([&](const std::string& process, HWND) { callbacks.push_back(process); });

    ForegroundMonitorTestPeer::Event(monitor, Window(1));
    Check(monitor.GetCurrentProcessName() == "cs2.exe" && callbacks.size() == 1, "A: initial CS2 event");
    authoritative = Window(2);
    ForegroundMonitorTestPeer::Event(monitor, Window(2));
    Check(monitor.GetCurrentProcessName() == "explorer.exe" && callbacks.size() == 2, "B: explorer event");

    authoritative = Window(1); // deliberately omit the recovery WinEvent
    Check(monitor.GetCurrentProcessName() == "explorer.exe", "C: missed event leaves stale cache until reconciliation");
    ForegroundMonitorTestPeer::Reconcile(monitor);
    Check(monitor.GetCurrentProcessName() == "cs2.exe" && callbacks.size() == 3, "C: reconciliation recovers CS2");

    ForegroundMonitorTestPeer::Event(monitor, Window(99));
    ForegroundMonitorTestPeer::Event(monitor, nullptr);
    Check(monitor.GetCurrentProcessName() == "cs2.exe" && callbacks.size() == 3, "D: transient invalid events cannot erase CS2");

    authoritative = nullptr;
    ForegroundMonitorTestPeer::Reconcile(monitor);
    Check(monitor.GetCurrentProcessName() == "cs2.exe", "E: one missing authoritative sample is transient");
    ForegroundMonitorTestPeer::Reconcile(monitor);
    Check(monitor.GetCurrentProcessName().empty() && callbacks.size() == 4, "E: sustained no-foreground converges to unknown");
    ForegroundMonitorTestPeer::Reconcile(monitor);
    Check(callbacks.size() == 4, "E: unknown does not repeat callback");

    authoritative = Window(1);
    ForegroundMonitorTestPeer::Reconcile(monitor);
    const auto before_duplicate = callbacks.size();
    authoritative = Window(4);
    ForegroundMonitorTestPeer::Reconcile(monitor);
    ForegroundMonitorTestPeer::Event(monitor, Window(4));
    Check(callbacks.size() == before_duplicate && monitor.GetCurrentProcessName() == "cs2.exe", "F: canonical identity suppresses duplicate callback");

    ForegroundMonitorTestPeer::Event(monitor, Window(3));
    Check(monitor.GetCurrentProcessName() == "GameOverlayUI.exe", "G: overlay event observed");
    authoritative = Window(1); // recovery event lost
    ForegroundMonitorTestPeer::Reconcile(monitor);
    Check(monitor.GetCurrentProcessName() == "cs2.exe" && callbacks.back() == "cs2.exe", "G: overlay cannot remain stuck");
}

void ThreadLifecycle() {
    std::atomic<std::uintptr_t> authoritative{1};
    const DWORD caller_thread = GetCurrentThreadId();
    std::atomic<DWORD> provider_thread{0};
    std::atomic<int> callbacks{0};
    int stopped_count = 0;
    {
        ForegroundMonitor monitor([&] {
            provider_thread.store(GetCurrentThreadId());
            return Window(authoritative.load());
        }, Resolve);
        monitor.SetCallback([&](const std::string&, HWND) { ++callbacks; });
        Check(monitor.Start(), "H: start");
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (monitor.GetCurrentProcessName() != "cs2.exe" && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
        Check(monitor.GetCurrentProcessName() == "cs2.exe", "H: initial monitor-thread sample");
        Check(provider_thread.load() != 0 && provider_thread.load() != caller_thread,
              "H: foreground query stays off the caller/render thread");
        authoritative.store(2);
        while (monitor.GetCurrentProcessName() != "explorer.exe" && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(10ms);
        }
        Check(monitor.GetCurrentProcessName() == "explorer.exe", "H: periodic wait runs on message-pump thread");
        monitor.Stop();
        monitor.Stop();
        stopped_count = callbacks.load();
    }
    authoritative.store(1);
    std::this_thread::sleep_for(450ms);
    Check(callbacks.load() == stopped_count, "H: no callback after destruction");

    // Stop may race thread queue creation; it must still join promptly.
    for (int attempt = 0; attempt < 8; ++attempt) {
        ForegroundMonitor quick([&] { return Window(authoritative.load()); }, Resolve);
        Check(quick.Start(), "H: rapid start");
        quick.Stop();
    }
}

void AutomationIntegration() {
    const auto dir = std::filesystem::path("audit_artifacts") / "foreground_monitor_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "config.json";
    std::ofstream(path) << R"({"default_profile":"desktop","profiles":{"desktop":{"type":"static"},"cs2":{"type":"static"}},"orchestration":{"rules":[{"id":"foreground","model":"automation_v2","when":{"mode":"state","condition":{"field":"process","op":"==","value":"cs2.exe"}},"action":{"type":"activate_profile","profile":"cs2"}}]}})";
    RuleEngine engine;
    Check(engine.LoadConfig(path.string()), "I: load automation state rule");
    GsiState gsi;
    HWND authoritative = Window(1);
    ForegroundMonitor monitor([&] { return authoritative; }, Resolve);
    ForegroundMonitorTestPeer::Event(monitor, Window(1));
    auto evaluate = [&](uint64_t now) {
        return engine.EvaluateAutomation(gsi, [&] { return monitor.GetCurrentProcessName(); }, now);
    };
    Check(evaluate(100).profile->name == "cs2", "I: CS2 selects profile");
    ForegroundMonitorTestPeer::Event(monitor, Window(3));
    Check(evaluate(200).profile->name == "desktop", "I: overlay clears CS2 profile");
    authoritative = Window(1); // no recovery WinEvent
    ForegroundMonitorTestPeer::Reconcile(monitor);
    const auto recovered = evaluate(300);
    Check(recovered.profile->name == "cs2" && recovered.foreground_process == monitor.GetCurrentProcessName(),
          "I: reconciliation restores profile from the same monitor snapshot");
}
}

int main() {
    try {
        StateTransitions();
        ThreadLifecycle();
        AutomationIntegration();
        std::cout << "foreground monitor A-I passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
