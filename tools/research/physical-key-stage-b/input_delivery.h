#pragma once
#include "observation_core.h"
#include <vector>

namespace stage_b {
constexpr uint32_t api_error = 0xffffffffu;
struct ReadResult { uint32_t returned{}, bytes{}, error{}; };
struct InputHeader { uint32_t type{}, bytes{}; uintptr_t device{}; };
// Win32 adapter supplies actual sizeof/offsetof; tests supply a synthetic ABI.
struct InputLayout {
    size_t header_bytes{}, device_offset{}, device_bytes{}, hid_size_offset{}, hid_count_offset{}, data_offset{};
};
struct InputSource {
    virtual ~InputSource() = default;
    virtual ReadResult header(InputHeader&) = 0;
    virtual ReadResult size() = 0;
    virtual ReadResult copy(void* buffer, uint32_t capacity) = 0;
};
enum class ReadFailure { HeaderRead, SizeRead, SizeBounds, CopyRead, HeaderChanged, HidBounds, Count };
struct DeliveryCounts {
    uint64_t target_headers{}, other_headers{}, stale_session{}, payload_batches{}, padded_batches{};
    uint32_t last_error{};
    std::array<uint64_t, size_t(ReadFailure::Count)> failures{};
};
class InputDelivery {
public:
    const DeliveryCounts& counts() const { return counts_; }
    void reset() { counts_ = {}; }
    // Called only from a guarded active trial. No payload fetch for another
    // source or old generation. All returned buffers are wiped at scope exit.
    bool receive(InputSource& source, const InputLayout& layout, uintptr_t target,
                 uint64_t generation, uint64_t now, Observation& observation) {
        if (!observation.tick(now)) return false;
        InputHeader header;
        auto result = source.header(header);
        if (result.returned != layout.header_bytes || result.bytes != layout.header_bytes)
            return fail(ReadFailure::HeaderRead, result.error, observation);
        if (header.type != 2 || header.device != target) { ++counts_.other_headers; return false; }
        ++counts_.target_headers;
        if (!observation.accepts(header.device, generation)) { ++counts_.stale_session; return false; }
        result = source.size();
        if (result.returned != 0) return fail(ReadFailure::SizeRead, result.error, observation);
        const uint32_t expected = result.bytes;
        if (expected < layout.data_offset || expected > 65536 ||
            layout.header_bytes < 8 || layout.header_bytes > layout.data_offset || !layout.device_bytes || layout.device_bytes > sizeof(uintptr_t) ||
            layout.device_offset + layout.device_bytes > layout.header_bytes ||
            layout.hid_size_offset + 4 > layout.data_offset || layout.hid_count_offset + 4 > layout.data_offset)
            return fail(ReadFailure::SizeBounds, 0, observation);
        struct Buffer {
            std::vector<uint64_t> words;
            explicit Buffer(size_t bytes) : words((bytes + 7) / 8) {}
            ~Buffer() {
                volatile uint64_t* p = words.data();
                for (size_t i = 0; i < words.size(); ++i) p[i] = 0;
            }
        } buffer(expected);
        result = source.copy(buffer.words.data(), expected);
        if (result.returned != expected || result.bytes != expected)
            return fail(ReadFailure::CopyRead, result.error, observation);
        const auto* bytes = reinterpret_cast<const uint8_t*>(buffer.words.data());
        uint32_t type{}, total{}, report_size{}, count{}; uintptr_t device{};
        std::memcpy(&type, bytes, sizeof(type)); std::memcpy(&total, bytes + 4, sizeof(total));
        std::memcpy(&device, bytes + layout.device_offset, layout.device_bytes);
        if (type != header.type || total != expected || total != header.bytes || device != header.device)
            return fail(ReadFailure::HeaderChanged, 0, observation);
        std::memcpy(&report_size, bytes + layout.hid_size_offset, 4);
        std::memcpy(&count, bytes + layout.hid_count_offset, 4);
        const uint64_t length = uint64_t(report_size) * count;
        if (length > expected - layout.data_offset)
            return fail(ReadFailure::HidBounds, 0, observation);
        // RAWINPUT structure may have trailing alignment padding. Never parse
        // that padding as another report. Core still enforces exact HID length.
        if (length < expected - layout.data_offset) ++counts_.padded_batches;
        ++counts_.payload_batches;
        return observation.batch(device, generation, bytes + layout.data_offset,
                                 size_t(length), report_size, count, now);
    }
private:
    bool fail(ReadFailure kind, uint32_t error, Observation& observation) {
        ++counts_.failures[size_t(kind)]; counts_.last_error = error;
        observation.finish(Stop::Malformed); return false;
    }
    DeliveryCounts counts_{};
};
} // namespace stage_b
