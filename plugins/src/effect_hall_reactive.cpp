#include "engine/effect.h"
#include "engine/plugin_interface.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

struct UsageKeyName {
    uint16_t hid_usage;
    const char* name;
};

constexpr std::array<UsageKeyName, 68> kUsageToKeyName = {{
    {41, "ESC"}, {30, "1"}, {31, "2"}, {32, "3"}, {33, "4"}, {34, "5"}, {35, "6"},
    {36, "7"}, {37, "8"}, {38, "9"}, {39, "0"}, {45, "-"}, {46, "="}, {42, "BACKSPACE"}, {73, "INS"},
    {43, "TAB"}, {20, "Q"}, {26, "W"}, {8, "E"}, {21, "R"}, {23, "T"}, {28, "Y"},
    {24, "U"}, {12, "I"}, {18, "O"}, {19, "P"}, {47, "["}, {48, "]"}, {49, "\\"}, {76, "DEL"},
    {57, "CAPS"}, {4, "A"}, {22, "S"}, {7, "D"}, {9, "F"}, {10, "G"}, {11, "H"},
    {13, "J"}, {14, "K"}, {15, "L"}, {51, ";"}, {52, "'"}, {40, "ENTER"}, {75, "PGUP"},
    {225, "L_SHIFT"}, {29, "Z"}, {27, "X"}, {6, "C"}, {25, "V"}, {5, "B"}, {17, "N"},
    {16, "M"}, {54, ","}, {55, "."}, {56, "/"}, {229, "R_SHIFT"}, {82, "UP"}, {78, "PGDN"},
    {224, "L_CTRL"}, {227, "L_WIN"}, {226, "L_ALT"}, {44, "SPACE"}, {230, "R_ALT"},
    {232, "FN"}, {228, "COPILOT"}, {80, "LEFT"}, {81, "DOWN"}, {79, "RIGHT"},
}};

const char* FindKeyNameForUsage(uint16_t hid_usage) {
    for (const auto& item : kUsageToKeyName) {
        if (item.hid_usage == hid_usage) return item.name;
    }
    return nullptr;
}

class HallReactiveEffect : public aura::Effect {
public:
    HallReactiveEffect() : aura::Effect("hall_reactive") {}

    void BindHallAnalog(const AuraHallAnalogHostV1* host) {
        if (host && host->struct_size == sizeof(AuraHallAnalogHostV1) &&
            host->version == AURA_HALL_ANALOG_VERSION && host->read_snapshot) {
            host_ = *host;
            bound_ = true;
        }
    }

    void Render(uint64_t /*elapsed_ms*/, aura::FrameBuffer& out_frame, const aura::Keymap& keymap) override {
        out_frame.Clear();
        if (!bound_) return;

        AuraHallAnalogSnapshotV1 snap{};
        if (host_.read_snapshot(host_.context, &snap, sizeof(snap)) != 1 || !snap.active) {
            return;
        }

        const uint32_t count = std::min<uint32_t>(snap.count, AURA_HALL_ANALOG_MAX_KEYS);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& k = snap.keys[i];
            if (k.depth_mm < 0.08f) continue;

            const char* name = FindKeyNameForUsage(k.hid_usage);
            int led_id = -1;
            if (!name || !keymap.FindLedId(name, led_id) || led_id <= 0) continue;

            const float t = std::clamp(k.normalized, 0.0f, 1.0f);
            // Smooth cyan -> electric blue -> magenta depth gradient
            const uint8_t r = static_cast<uint8_t>(std::round(20.0f + 220.0f * t));
            const uint8_t g = static_cast<uint8_t>(std::round(180.0f * (1.0f - 0.65f * t)));
            const uint8_t b = static_cast<uint8_t>(std::round(255.0f * (0.4f + 0.6f * t)));
            out_frame.SetKey(led_id, aura::ColorRGB(r, g, b));
        }
    }

private:
    AuraHallAnalogHostV1 host_{};
    bool bound_{false};
};

} // namespace

extern "C" {

AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() {
    return AURA_PLUGIN_API_VERSION;
}

AURA_PLUGIN_EXPORT const char* AuraGetEffectName() {
    return "hall_reactive";
}

AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect() {
    return new HallReactiveEffect();
}

AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect) {
    delete effect;
}

AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraGetHallAnalogVersion() {
    return AURA_HALL_ANALOG_VERSION;
}

AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraBindHallAnalog(
    aura::Effect* effect, const AuraHallAnalogHostV1* host) {
    auto* reactive = dynamic_cast<HallReactiveEffect*>(effect);
    if (!reactive || !host || host->version != AURA_HALL_ANALOG_VERSION || !host->read_snapshot) {
        return 0;
    }
    reactive->BindHallAnalog(host);
    return 1;
}

}
