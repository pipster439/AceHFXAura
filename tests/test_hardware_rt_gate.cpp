#include "aura/hardware/m605_rt_gate.h"
#include <array>
#include <iostream>
#include "monitor/hardware_rt_gate_monitor.h"
#include <condition_variable>
int main(int argc, char** argv) {
    using namespace aura;
    // Opt-in passive registration check; never runs in CTest or changes hardware.
    if (argc == 2 && std::string(argv[1]) == "--passive-probe") {
        std::mutex mutex; std::condition_variable changed; bool registered = false;
        HardwareRtGateMonitor monitor([&](const auto& value) {
            std::lock_guard<std::mutex> lock(mutex);
            std::cout << "state=" << HardwareRtGateName(value.state) << " connected=" << value.input_collection_connected
                << " input_generation=" << value.input_session_generation << " reason=" << value.unavailable_reason << '\n';
            registered = registered || value.input_collection_connected; changed.notify_all();
        });
        monitor.Start();
        { std::unique_lock<std::mutex> lock(mutex); changed.wait_for(lock, std::chrono::seconds(2), [&] { return registered; }); }
        monitor.Stop(); return registered ? 0 : 1;
    }
    // USB capture frames 107399 (ON) and 108494 (OFF), firmware 1.00.59.
    std::array<uint8_t, 21> report{3, 0x76, 0, 0, 1};
    bool okay = ParseHardwareRtGateReport(report.data(), report.size()) == HardwareRtGateState::On;
    report[4] = 0;
    okay &= ParseHardwareRtGateReport(report.data(), report.size()) == HardwareRtGateState::Off;
    report[4] = 2;
    okay &= ParseHardwareRtGateReport(report.data(), report.size()) == HardwareRtGateState::Unknown;
    report[4] = 1; report[20] = 1;
    okay &= ParseHardwareRtGateReport(report.data(), report.size()) == HardwareRtGateState::Unknown;
    report[20] = 0;
    okay &= ParseHardwareRtGateReport(report.data(), 20) == HardwareRtGateState::Unknown;
    okay &= ParseHardwareRtGateReport(report.data(), 2) == HardwareRtGateState::Unknown;
    report[1] = 0x77;
    okay &= !ParseHardwareRtGateReport(report.data(), report.size()).has_value();
    okay &= !ParseHardwareRtGateReport(nullptr, 21).has_value();
    for (bool enabled : {false, true}) {
        okay &= DeriveEffectiveRt(HardwareRtGateState::Off, enabled) == false;
        okay &= DeriveEffectiveRt(HardwareRtGateState::On, enabled) == enabled;
    }
    okay &= !DeriveEffectiveRt(HardwareRtGateState::Unknown, true).has_value();
    okay &= DeriveEffectiveRt(HardwareRtGateState::Unknown, false) == false;
    okay &= !DeriveEffectiveRt(HardwareRtGateState::On, std::nullopt).has_value();
    if (!okay) std::cerr << "RT gate parser/state contract failed\n";
    return okay ? 0 : 1;
}
