#include "aura/hardware/m605_hall_telemetry.h"
#include "aura/hardware/m605_key_mapping.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace aura::m605::hall;

#define CHECK(expr)                                                               \
    do {                                                                          \
        if (!(expr)) {                                                            \
            std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__        \
                      << ": " << #expr << "\n";                                   \
            return 1;                                                             \
        }                                                                         \
    } while (false)

int main() {
    // 1. 68-key table alignment & uniqueness check
    CHECK(kHallKeyTable.size() == 68);
    CHECK(aura::m605::VerifiedM605PhysicalKeyCount() == 68);

    const auto a_idx = FindKeyIndexByLogicalId(0x0602);
    CHECK(a_idx.has_value());
    CHECK(std::string(kHallKeyTable[*a_idx].name) == "A");
    CHECK(kHallKeyTable[*a_idx].analog_id == 4);
    CHECK(kHallKeyTable[*a_idx].wire_id == 0x001F);

    const auto aid_idx = FindKeyIndexByAnalogId(4);
    CHECK(aid_idx == a_idx);
    CHECK(!FindKeyIndexByLogicalId(0xFFFF).has_value());
    CHECK(!FindKeyIndexByAnalogId(0).has_value());

    // 2. Regression guard on safe stop-stream packet (must be 04 04 01, NEVER 04 04 00)
    CHECK(kStopStreamReport[0] == 0x00);
    CHECK(kStopStreamReport[1] == 0x04);
    CHECK(kStopStreamReport[2] == 0x04);
    CHECK(kStopStreamReport[3] == 0x01);

    // 3. Depth conversion & clamping
    CHECK(std::abs(ConvertHallRawToDepthMm(7035, 7035.0f, 5398.0f) - 0.0f) < 1e-4f);
    CHECK(std::abs(ConvertHallRawToDepthMm(8000, 7035.0f, 5398.0f) - 0.0f) < 1e-4f);
    CHECK(std::abs(ConvertHallRawToDepthMm(1637, 7035.0f, 5398.0f) - 4.0f) < 1e-4f);
    CHECK(std::abs(ConvertHallRawToDepthMm(500, 7035.0f, 5398.0f) - 4.0f) < 1e-4f);
    const float mid = ConvertHallRawToDepthMm(7035 - 2699, 7035.0f, 5398.0f);
    CHECK(std::abs(mid - 2.0f) < 1e-3f);

    // 4. Timestamp matching
    std::vector<HallSample> samples = {
        {1.000, 7000, 0.2f},
        {1.005, 5000, 1.8f},
        {1.012, 4200, 2.4f},
    };
    const auto matched = MatchHallDepthAtTimestamp(samples, 1.006);
    CHECK(matched.has_value());
    CHECK(std::abs(*matched - 1.8) < 1e-4);
    CHECK(!MatchHallDepthAtTimestamp(samples, 1.200).has_value());

    // 5. Shared-memory ABI layout & seq-lock publish/read roundtrip
    static_assert(sizeof(AuraM605HallSharedMemory) == 1128);
    AuraM605HallSharedMemory shm{};
    std::array<AuraM605HallSharedKey, kHallKeyCount> keys{};
    for (size_t i = 0; i < kHallKeyCount; ++i) {
        keys[i].hid_usage = kHallKeyTable[i].analog_id;
        keys[i].logical_id = kHallKeyTable[i].logical_id;
        keys[i].raw = 7000;
    }
    keys[*a_idx].depth_mm = 2.5f;
    keys[*a_idx].normalized = 0.625f;
    keys[*a_idx].digital_down = 1;

    PublishSharedMemoryLocked(&shm, true, 1234567ULL, 42, keys);
    AuraM605HallSharedMemory copy{};
    CHECK(ReadSharedMemorySnapshot(&shm, copy));
    CHECK(copy.magic == kHallShmMagic);
    CHECK(copy.version == kHallShmVersion);
    CHECK(copy.active == 1);
    CHECK(copy.timestamp_us == 1234567ULL);
    CHECK(copy.packet_count == 42);
    CHECK(copy.key_count == 68);
    CHECK(copy.vid == kM605Vid && copy.pid == kM605Pid);
    CHECK(std::abs(copy.keys[*a_idx].depth_mm - 2.5f) < 1e-5f);
    CHECK(std::abs(copy.keys[*a_idx].normalized - 0.625f) < 1e-5f);
    CHECK(copy.keys[*a_idx].digital_down == 1);

    // 6. Closed-loop evaluation helpers
    const auto act_pass = EvaluateActuationTrial(1.8, 1.86);
    CHECK(act_pass.passed);
    const auto act_fail = EvaluateActuationTrial(1.8, 2.30);
    CHECK(!act_fail.passed);

    const auto [rt_rel, rt_rep] = EvaluateRapidTriggerTrial(0.3, 3.80, 3.48, 2.10, 2.42);
    CHECK(rt_rel.passed);
    CHECK(rt_rep.passed);

    const auto [rt_rel_bad, rt_rep_bad] = EvaluateRapidTriggerTrial(0.3, 3.80, 3.00, 2.10, 2.12);
    CHECK(!rt_rel_bad.passed);
    CHECK(!rt_rep_bad.passed);

    CHECK(EvaluateDeadzoneTopTrial(0.4, 0.45).passed);
    CHECK(!EvaluateDeadzoneTopTrial(0.4, 0.85).passed);

    CHECK(EvaluateDeadzoneBottomTrial(0.4, 3.95, 3.45, 0.2).passed);
    CHECK(!EvaluateDeadzoneBottomTrial(0.5, 3.95, 3.90, 0.2).passed);

    std::cout << "test_m605_hall_telemetry PASSED\n";
    return 0;
}
