#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace aura::m605::hall {

inline constexpr size_t kHallKeyCount = 68;
inline constexpr uint16_t kM605Vid = 0x0B05;
inline constexpr uint16_t kM605Pid = 0x1B7E;
inline constexpr uint16_t kHallUsagePage = 0xFF00;
inline constexpr uint16_t kEventUsagePage = 0xFFC0;
inline constexpr size_t kHallReportBytes = 65;
inline constexpr size_t kEventReportBytes = 21;
inline constexpr float kTravelMm = 4.0f;
inline constexpr float kDefaultSpan = 4920.0f;

// Safe stop-stream report: ReportID=0x00, 0x04 0x04 0x01 (FA_HA_MFG(1)).
// NEVER send bare 0x04 0x04 0x00, which sets firmware mfg_block_keys = 1.
inline constexpr std::array<uint8_t, kHallReportBytes> kStopStreamReport = {
    0x00, 0x04, 0x04, 0x01
};

inline constexpr const wchar_t* kHallShmName = L"Local\\AuraM605HallAnalogV1";
inline constexpr const char* kHallShmNameUtf8 = "Local\\AuraM605HallAnalogV1";
inline constexpr uint32_t kHallShmMagic = 0x414D3635u; // "AM65"
inline constexpr uint32_t kHallShmVersion = 1u;

struct HallKeySpec {
    uint16_t logical_id;
    uint16_t wire_id;
    uint8_t analog_id; // USB HID Usage ID (Usage Page 0x07), 232 for FN
    const char* name;
    float default_idle;
    float default_span;
};

// Audited 68-key mapping aligned 1:1 with aura::m605::detail::kVerifiedPhysicalKeys
// and calibrated Hall baselines from G:\Gearlink\_re\keymap_final.json.
inline constexpr std::array<HallKeySpec, kHallKeyCount> kHallKeyTable{{
    // Row 0 (15 keys)
    {0x0100, 0x0001, 41,  "ESC",       7066.0f, 5303.0f},
    {0x0600, 0x0002, 30,  "1",         7044.0f, 5182.0f},
    {0x0700, 0x0003, 31,  "2",         7040.0f, 4885.0f},
    {0x0101, 0x0004, 32,  "3",         7092.0f, 5438.0f},
    {0x0102, 0x0005, 33,  "4",         6976.0f, 4876.0f},
    {0x0103, 0x0006, 34,  "5",         7025.0f, 4712.0f},
    {0x0104, 0x0007, 35,  "6",         7029.0f, 5345.0f},
    {0x0105, 0x0008, 36,  "7",         7073.0f, 4915.0f},
    {0x0106, 0x0009, 37,  "8",         7040.0f, 5014.0f},
    {0x0107, 0x000a, 38,  "9",         7025.0f, 4942.0f},
    {0x0108, 0x000b, 39,  "0",         7086.0f, 5158.0f},
    {0x0109, 0x000c, 45,  "-",         7047.0f, 5094.0f},
    {0x010a, 0x000d, 46,  "=",         7066.0f, 4913.0f},
    {0x0706, 0x000f, 42,  "BACKSPACE", 7081.0f, 5588.0f},
    {0x0608, 0x004b, 73,  "INS",       7062.0f, 4979.0f},

    // Row 1 (15 keys)
    {0x0200, 0x0010, 43,  "TAB",       7042.0f, 4758.0f},
    {0x0601, 0x0011, 20,  "Q",         7036.0f, 5398.0f},
    {0x0701, 0x0012, 26,  "W",         7053.0f, 4887.0f},
    {0x0201, 0x0013, 8,   "E",         7054.0f, 4921.0f},
    {0x0202, 0x0014, 21,  "R",         7002.0f, 4976.0f},
    {0x0203, 0x0015, 23,  "T",         6999.0f, 4796.0f},
    {0x0204, 0x0016, 28,  "Y",         7039.0f, 5577.0f},
    {0x0205, 0x0017, 24,  "U",         7040.0f, 4766.0f},
    {0x0206, 0x0018, 12,  "I",         7050.0f, 5298.0f},
    {0x0207, 0x0019, 18,  "O",         7062.0f, 4843.0f},
    {0x0208, 0x001a, 19,  "P",         7052.0f, 4805.0f},
    {0x0209, 0x001b, 47,  "[",         7075.0f, 4815.0f},
    {0x020a, 0x001c, 48,  "]",         7073.0f, 4843.0f},
    {0x0607, 0x001d, 49,  "\\",        7078.0f, 4920.0f},
    {0x0708, 0x004c, 76,  "DEL",       7038.0f, 4938.0f},

    // Row 2 (14 keys)
    {0x0300, 0x001e, 57,  "CAPS",      7029.0f, 4972.0f},
    {0x0602, 0x001f, 4,   "A",         7035.0f, 5398.0f},
    {0x0702, 0x0020, 22,  "S",         7050.0f, 5381.0f},
    {0x0301, 0x0021, 7,   "D",         7047.0f, 4757.0f},
    {0x0302, 0x0022, 9,   "F",         6965.0f, 4837.0f},
    {0x0303, 0x0023, 10,  "G",         7075.0f, 4892.0f},
    {0x0304, 0x0024, 11,  "H",         7019.0f, 5529.0f},
    {0x0305, 0x0025, 13,  "J",         7054.0f, 4910.0f},
    {0x0306, 0x0026, 14,  "K",         7061.0f, 4845.0f},
    {0x0307, 0x0027, 15,  "L",         7067.0f, 5149.0f},
    {0x0308, 0x0028, 51,  ";",         7029.0f, 5290.0f},
    {0x0309, 0x0029, 52,  "'",         7041.0f, 5180.0f},
    {0x0707, 0x002b, 40,  "ENTER",     7071.0f, 4855.0f},
    {0x060b, 0x0055, 75,  "PGUP",      7085.0f, 5090.0f},

    // Row 3 (14 keys)
    {0x0400, 0x002c, 225, "L_SHIFT",   6934.0f, 4811.0f},
    {0x0703, 0x002e, 29,  "Z",         7058.0f, 4822.0f},
    {0x0401, 0x002f, 27,  "X",         7071.0f, 4990.0f},
    {0x0501, 0x0030, 6,   "C",         7042.0f, 4926.0f},
    {0x0402, 0x0031, 25,  "V",         6974.0f, 4839.0f},
    {0x0403, 0x0032, 5,   "B",         7072.0f, 4893.0f},
    {0x0404, 0x0033, 17,  "N",         7066.0f, 4799.0f},
    {0x0405, 0x0034, 16,  "M",         7075.0f, 4897.0f},
    {0x0406, 0x0035, 54,  ",",         7041.0f, 4723.0f},
    {0x0407, 0x0036, 55,  ".",         7049.0f, 4715.0f},
    {0x0408, 0x0037, 56,  "/",         7042.0f, 5040.0f},
    {0x040a, 0x0039, 229, "R_SHIFT",   7051.0f, 4748.0f},
    {0x070a, 0x0053, 82,  "UP",        7081.0f, 5173.0f},
    {0x070b, 0x0056, 78,  "PGDN",      7041.0f, 5131.0f},

    // Row 4 (10 keys)
    {0x0500, 0x003a, 224, "L_CTRL",    7040.0f, 4791.0f},
    {0x0604, 0x003b, 227, "L_WIN",     7060.0f, 5375.0f},
    {0x0704, 0x003c, 226, "L_ALT",     7078.0f, 4654.0f},
    {0x0503, 0x003d, 44,  "SPACE",     7082.0f, 5458.0f},
    {0x0507, 0x003e, 230, "R_ALT",     7042.0f, 5426.0f},
    {0x0508, 0x009f, 232, "FN",        7087.0f, 4920.5f},
    {0x050a, 0x0040, 228, "COPILOT",   7048.0f, 5074.0f},
    {0x060a, 0x004f, 80,  "LEFT",      7099.0f, 4852.0f},
    {0x050b, 0x0054, 81,  "DOWN",      7052.0f, 5415.0f},
    {0x040b, 0x0059, 79,  "RIGHT",     7080.0f, 4813.0f},
}};

constexpr bool ValidateHallKeyTable() {
    for (size_t i = 0; i < kHallKeyTable.size(); ++i) {
        if (kHallKeyTable[i].logical_id == 0 || kHallKeyTable[i].wire_id == 0 ||
            kHallKeyTable[i].analog_id == 0 || kHallKeyTable[i].default_span <= 1000.0f) {
            return false;
        }
        for (size_t j = i + 1; j < kHallKeyTable.size(); ++j) {
            if (kHallKeyTable[i].logical_id == kHallKeyTable[j].logical_id ||
                kHallKeyTable[i].wire_id == kHallKeyTable[j].wire_id ||
                kHallKeyTable[i].analog_id == kHallKeyTable[j].analog_id) {
                return false;
            }
        }
    }
    return true;
}
static_assert(ValidateHallKeyTable(), "Hall key table must have 68 unique keys");

// Natural-aligned C++/Rust shared memory ABI for Local\AuraM605HallAnalogV1.
struct AuraM605HallSharedKey {
    uint16_t hid_usage;    // USB HID Usage ID (Page 0x07), 232 for FN
    uint16_t logical_id;   // Aura logical key ID
    float    normalized;   // 0.0f .. 1.0f
    float    depth_mm;     // 0.0f .. 4.0f
    uint16_t raw;          // Raw Hall ADC value
    uint8_t  digital_down; // 1 if pressed according to bound 0x71 bitmap bit
    uint8_t  reserved;
};
static_assert(sizeof(AuraM605HallSharedKey) == 16, "AuraM605HallSharedKey must be 16 bytes");

struct AuraM605HallSharedMemory {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint32_t> seq; // odd = write in progress, even = stable
    uint32_t active;           // 1 while stream running, 0 otherwise
    uint64_t timestamp_us;
    uint32_t packet_count;
    uint32_t key_count;        // 68
    uint16_t vid;              // 0x0B05
    uint16_t pid;              // 0x1B7E
    uint32_t reserved;
    AuraM605HallSharedKey keys[kHallKeyCount];
};
static_assert(sizeof(std::atomic<uint32_t>) == 4 && alignof(std::atomic<uint32_t>) == 4,
              "atomic<uint32_t> must be 4 bytes for FFI layout");
static_assert(sizeof(AuraM605HallSharedMemory) == 40 + kHallKeyCount * 16,
              "AuraM605HallSharedMemory layout size mismatch");

struct HallSample {
    double ts_sec = 0.0;
    uint16_t raw = 0;
    float depth_mm = 0.0f;
};

struct BitmapEdge {
    double ts_sec = 0.0;
    uint8_t byte_idx = 0;
    uint8_t bit_idx = 0;
    uint8_t state = 0;
};

struct HallKeySnapshot {
    uint16_t logical_id = 0;
    uint8_t hid_usage = 0;
    std::string name;
    uint16_t raw = 0;
    float depth_mm = 0.0f;
    float normalized = 0.0f;
    bool digital_down = false;
};

struct HallTelemetrySnapshot {
    bool active = false;
    bool event_channel_ok = false;
    bool shared_memory_ok = false;
    uint64_t packet_count = 0;
    double packet_rate_hz = 0.0;
    std::string last_error;
    std::vector<HallKeySnapshot> keys;
};

struct VerifyTrialResult {
    std::string kind;   // "actuation", "rt_release", "rt_repress", "deadzone_top", "deadzone_bottom"
    std::string label;
    double target_mm = 0.0;
    double measured_mm = 0.0;
    double error_mm = 0.0;
    bool passed = false;
    std::string detail;
};

struct VerifyRequest {
    uint16_t logical_id = 0;
    std::string mode = "actuation"; // "actuation", "rt", "deadzone"
    std::vector<double> actuation_points{0.8, 2.0, 3.2};
    std::vector<double> rt_points{0.3};
    double dz_top_mm = 0.4;
    double dz_bottom_mm = 0.4;
    double dz_rt_mm = 0.2;
    double restore_actuation_mm = 2.0;
    double inherited_rt_press_mm = 0.2;
    double inherited_rt_release_mm = 0.2;
    bool unlock_fa = false;
};

struct VerifyStatusSnapshot {
    std::string state = "Idle";
    uint16_t logical_id = 0;
    std::string key_name;
    std::string mode;
    std::string instruction;
    int step_index = 0;
    int step_total = 0;
    double live_depth_mm = 0.0;
    uint16_t live_raw = 0;
    bool digital_down = false;
    std::string bound_bit;
    std::string error;
    std::vector<VerifyTrialResult> results;
};

struct VerifyHardwareWriter {
    std::function<bool(uint16_t logical_id, double mm)> set_actuation;
    std::function<bool(uint16_t logical_id, double press_mm, double release_mm)> set_rt;
    std::function<bool(uint16_t logical_id, double inherited_press_mm, double inherited_release_mm)> disable_rt;
    std::function<bool(uint16_t logical_id, double top_mm, double bottom_mm)> set_deadzone;
};

// Pure helpers exported for deterministic unit tests (no hardware required).
std::optional<size_t> FindKeyIndexByLogicalId(uint16_t logical_id);
std::optional<size_t> FindKeyIndexByAnalogId(uint8_t analog_id);
float ConvertHallRawToDepthMm(uint16_t raw, float idle, float span);
std::optional<double> MatchHallDepthAtTimestamp(const std::vector<HallSample>& samples, double edge_ts_sec,
                                                double max_delta_sec = 0.05);
VerifyTrialResult EvaluateActuationTrial(double target_mm, double measured_mm, double tolerance_mm = 0.25);
std::pair<VerifyTrialResult, VerifyTrialResult> EvaluateRapidTriggerTrial(
    double target_rt_mm, double peak_mm, double rel_mm, double valley_mm, double repress_mm,
    double tolerance_mm = 0.25);
VerifyTrialResult EvaluateDeadzoneTopTrial(double expected_mm, double measured_mm, double tolerance_mm = 0.25);
VerifyTrialResult EvaluateDeadzoneBottomTrial(double bottom_mm, double peak_mm, double rel_mm, double rt_mm,
                                              double tolerance_mm = 0.30);

// Seq-lock helper usable in tests to publish and read back SharedMemory safely.
void PublishSharedMemoryLocked(AuraM605HallSharedMemory* shm, bool active, uint64_t timestamp_us,
                               uint32_t packet_count,
                               const std::array<AuraM605HallSharedKey, kHallKeyCount>& keys);
bool ReadSharedMemorySnapshot(const AuraM605HallSharedMemory* shm,
                              AuraM605HallSharedMemory& out_copy);

class M605HallTelemetry {
public:
    explicit M605HallTelemetry(std::filesystem::path calibration_path = {});
    ~M605HallTelemetry();

    M605HallTelemetry(const M605HallTelemetry&) = delete;
    M605HallTelemetry& operator=(const M605HallTelemetry&) = delete;

    // Pauses active Hall streaming cleanly (sending 04 04 01) for the lifetime
    // of a hardware write or 12 00 query, then resumes it on destruction.
    class ScopedPause {
    public:
        explicit ScopedPause(M605HallTelemetry* owner);
        ~ScopedPause();
        ScopedPause(const ScopedPause&) = delete;
        ScopedPause& operator=(const ScopedPause&) = delete;
        ScopedPause(ScopedPause&& other) noexcept;
    private:
        M605HallTelemetry* owner_ = nullptr;
        bool was_paused_ = false;
    };

    ScopedPause PauseForTransaction();

    bool Start(bool unlock_fa = false);
    void Stop();
    bool IsActive() const;
    HallTelemetrySnapshot GetSnapshot() const;

    bool StartVerify(const VerifyRequest& req, VerifyHardwareWriter writer);
    void StopVerify();
    VerifyStatusSnapshot GetVerifyStatus() const;

private:
    bool StartStreamInternal(bool unlock_fa, bool from_resume);
    void StopStreamInternal(bool keep_shared_mapping);
    void HallThreadProc();
    void EventThreadProc();
    void VerifyThreadProc(VerifyRequest req, VerifyHardwareWriter writer);

    void LoadCalibrationOverrides();
    bool OpenSharedMemory();
    void CloseSharedMemory();
    void SyncSharedMemory(bool active, uint64_t timestamp_us, uint32_t packet_count);

    bool WaitForEdge(uint8_t byte_idx, uint8_t bit_idx, uint8_t desired_state,
                     double timeout_sec, BitmapEdge& out_edge);
    void ClearEdges();
    std::vector<BitmapEdge> PopAllEdges();
    void StartRecordingSamples(uint8_t analog_id);
    void StopRecordingSamples();
    std::optional<double> DepthAtEdge(double edge_ts_sec) const;
    void GetLatestTargetState(uint8_t analog_id, double& depth_mm, uint16_t& raw) const;

    std::filesystem::path calibration_path_;

    mutable std::mutex ctrl_mutex_;
    int pause_depth_ = 0;
    bool stream_running_ = false;
    bool paused_while_active_ = false;
    bool unlock_fa_ = false;
    bool event_channel_ok_ = false;

    void* h_hall_ = reinterpret_cast<void*>(-1);  // INVALID_HANDLE_VALUE
    void* h_event_ = reinterpret_cast<void*>(-1);
    void* shm_handle_ = nullptr;
    AuraM605HallSharedMemory* shm_view_ = nullptr;

    std::atomic<bool> stop_threads_{false};
    std::thread hall_thread_;
    std::thread event_thread_;

    mutable std::mutex data_mutex_;
    std::array<float, kHallKeyCount> key_idle_{};
    std::array<float, kHallKeyCount> key_span_{};
    std::array<AuraM605HallSharedKey, kHallKeyCount> shared_keys_{};
    uint64_t packet_count_ = 0;
    double packet_rate_hz_ = 0.0;
    std::chrono::steady_clock::time_point rate_window_start_{};
    uint64_t rate_window_packets_ = 0;
    std::string last_error_;

    // Sample recording buffer for closed-loop edge matching
    bool record_samples_ = false;
    uint8_t record_analog_id_ = 0;
    double target_override_idle_ = 0.0;
    double target_override_span_ = 0.0;
    std::deque<HallSample> recorded_samples_;

    // 0xFFC0 0x71 bitmap state
    std::array<uint8_t, 17> bitmap_{};
    std::deque<BitmapEdge> edges_;
    std::optional<std::pair<uint8_t, uint8_t>> bound_bit_;

    // Verification worker state
    mutable std::mutex verify_mutex_;
    std::thread verify_thread_;
    std::atomic<bool> verify_cancel_{false};
    bool verify_running_ = false;
    bool started_stream_for_verify_ = false;
    VerifyStatusSnapshot verify_status_;
};

} // namespace aura::m605::hall
