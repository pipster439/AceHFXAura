#include "engine/effect_engine.h"
#include "engine/plugin_manager.h"
#include "engine/builtin_effects.h"
#include "engine/input_sources.h"
#include "gsi/automation_input.h"
#include "utils/logger.h"
#include <algorithm>
#include <cmath>

namespace aura {

// Input effects own a read cursor and transient visuals. Give each activation a
// fresh instance even when a cached Profile is selected again.
static std::shared_ptr<Effect> FreshInputEffect(const std::shared_ptr<Effect>& effect) {
    if (auto reactive = std::dynamic_pointer_cast<ReactiveEffect>(effect))
        return std::make_shared<ReactiveEffect>(reactive->GetBaseColor(), reactive->GetTriggerColor(), reactive->GetSpeedMs());
    if (auto ripple = std::dynamic_pointer_cast<RippleEffect>(effect))
        return std::make_shared<RippleEffect>(ripple->GetBaseColor(), ripple->GetTriggerColor(),
                                              ripple->GetSpeedMs(), ripple->GetThickness());
    if (auto plain = std::dynamic_pointer_cast<StaticEffect>(effect); plain && plain->GetAnalog())
        return std::make_shared<StaticEffect>(plain->GetColor(), true);
    return effect;
}

EffectEngine::EffectEngine()
    : start_time_(std::chrono::steady_clock::now()),
      preview_active_(false) {
    preview_frame_.Clear();
    last_frame_.Clear();
}

namespace {
struct EngineHallUsageEntry { uint16_t usage; const char* key_name; };
constexpr std::array<EngineHallUsageEntry, 68> kEngineHallKeys = {{
    {41, "ESC"}, {30, "1"}, {31, "2"}, {32, "3"}, {33, "4"}, {34, "5"}, {35, "6"},
    {36, "7"}, {37, "8"}, {38, "9"}, {39, "0"}, {45, "-"}, {46, "="}, {42, "BACKSPACE"}, {73, "INS"},
    {43, "TAB"}, {20, "Q"}, {26, "W"}, {8, "E"}, {21, "R"}, {23, "T"}, {28, "Y"},
    {24, "U"}, {12, "I"}, {18, "O"}, {19, "P"}, {47, "["}, {48, "]"}, {49, "\\"}, {76, "DEL"},
    {57, "CAPS"}, {4, "A"}, {22, "S"}, {7, "D"}, {9, "F"}, {10, "G"}, {11, "H"},
    {13, "J"}, {14, "K"}, {15, "L"}, {51, ";"}, {52, "'"}, {40, "ENTER"}, {75, "PGUP"},
    {225, "L_SHIFT"}, {29, "Z"}, {27, "X"}, {6, "C"}, {25, "V"}, {5, "B"}, {17, "N"},
    {16, "M"}, {54, ","}, {55, "."}, {56, "/"}, {229, "R_SHIFT"}, {82, "UP"}, {78, "PGDN"},
    {224, "L_CTRL"}, {227, "L_WIN"}, {226, "L_ALT"}, {44, "SPACE"}, {230, "R_ALT"},
    {232, "FN"}, {228, "COPILOT"}, {80, "LEFT"}, {81, "DOWN"}, {79, "RIGHT"}
}};

const char* EngineKeyForUsage(uint16_t hid_usage) {
    for (const auto& entry : kEngineHallKeys) {
        if (entry.usage == hid_usage) return entry.key_name;
    }
    return nullptr;
}

uint8_t ClampChannel(double v) {
    return static_cast<uint8_t>(std::clamp(std::round(v), 0.0, 255.0));
}

ColorRGB BlendHallPixel(ColorRGB base, ColorRGB target, double alpha, bool screen_add) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    if (screen_add) {
        return ColorRGB(
            ClampChannel(base.r * (1.0 - 0.25 * alpha) + target.r * alpha),
            ClampChannel(base.g * (1.0 - 0.25 * alpha) + target.g * alpha),
            ClampChannel(base.b * (1.0 - 0.25 * alpha) + target.b * alpha));
    }
    return ColorRGB(
        ClampChannel(base.r * (1.0 - alpha) + target.r * alpha),
        ClampChannel(base.g * (1.0 - alpha) + target.g * alpha),
        ClampChannel(base.b * (1.0 - alpha) + target.b * alpha));
}

void ApplyHallDepthOverlay(const HallLightingConfig& cfg, const AuraHallAnalogHostV1& hall_host,
                           const Keymap& keymap, FrameBuffer& out_frame) {
    if (!cfg.enabled || cfg.intensity <= 0.0 || !hall_host.read_snapshot) return;
    AuraHallAnalogSnapshotV1 snap{};
    if (hall_host.read_snapshot(hall_host.context, &snap, sizeof(snap)) != 1 ||
        snap.struct_size != sizeof(snap) || snap.version != AURA_HALL_ANALOG_VERSION ||
        !snap.active || snap.count == 0) {
        return;
    }

    const uint32_t count = std::min<uint32_t>(snap.count, AURA_HALL_ANALOG_MAX_KEYS);
    const double floor_mm = std::clamp(cfg.min_depth_mm, 0.05, 2.0);
    const double span = std::max(0.5, 4.0 - floor_mm);

    if (cfg.mode == "halo") {
        struct ActiveKey { double x, y, energy; };
        std::array<ActiveKey, AURA_HALL_ANALOG_MAX_KEYS> sources{};
        uint32_t source_count = 0;
        const auto& all_keys = keymap.GetAllKeys();
        for (uint32_t i = 0; i < count; ++i) {
            const auto& k = snap.keys[i];
            if (k.depth_mm < floor_mm) continue;
            const char* name = EngineKeyForUsage(k.hid_usage);
            if (!name) continue;
            auto it = all_keys.find(name);
            if (it == all_keys.end()) continue;
            const double t = std::clamp((k.depth_mm - floor_mm) / span, 0.0, 1.0);
            sources[source_count++] = {it->second.physical_x, it->second.physical_y, t};
        }
        if (source_count == 0) return;

        for (const auto& [name, info] : all_keys) {
            if (info.led_id < 0 || info.led_id >= static_cast<int>(TOTAL_LEDS)) continue;
            double best = 0.0;
            for (uint32_t s = 0; s < source_count; ++s) {
                const double dx = info.physical_x - sources[s].x;
                const double dy = info.physical_y - sources[s].y;
                const double d2 = dx * dx + dy * dy;
                const double sigma2 = 0.75 + 2.5 * sources[s].energy;
                const double contribution = sources[s].energy * std::exp(-d2 / sigma2);
                if (contribution > best) best = contribution;
            }
            if (best < 0.02) continue;
            const size_t base_idx = static_cast<size_t>(info.led_id) * 3;
            const ColorRGB base(out_frame.buffer[base_idx], out_frame.buffer[base_idx + 1], out_frame.buffer[base_idx + 2]);
            out_frame.SetKey(static_cast<size_t>(info.led_id),
                BlendHallPixel(base, cfg.color, best * cfg.intensity, true));
        }
        return;
    }

    const bool gradient = (cfg.mode == "gradient");
    for (uint32_t i = 0; i < count; ++i) {
        const auto& k = snap.keys[i];
        if (k.depth_mm < floor_mm) continue;
        const char* name = EngineKeyForUsage(k.hid_usage);
        int led_id = -1;
        if (!name || !keymap.FindLedId(name, led_id) || led_id < 0 || led_id >= static_cast<int>(TOTAL_LEDS)) continue;
        const double t = std::clamp((k.depth_mm - floor_mm) / span, 0.0, 1.0);
        const ColorRGB target = gradient
            ? ColorRGB(
                ClampChannel(cfg.color.r * (1.0 - t) + cfg.secondary_color.r * t),
                ClampChannel(cfg.color.g * (1.0 - t) + cfg.secondary_color.g * t),
                ClampChannel(cfg.color.b * (1.0 - t) + cfg.secondary_color.b * t))
            : cfg.color;
        const size_t idx = static_cast<size_t>(led_id) * 3;
        const ColorRGB base(out_frame.buffer[idx], out_frame.buffer[idx + 1], out_frame.buffer[idx + 2]);
        out_frame.SetKey(static_cast<size_t>(led_id),
            BlendHallPixel(base, target, t * cfg.intensity, !gradient));
    }
}
} // namespace

void EffectEngine::SetInputSources(const AudioFeatureSource* audio, const ScreenColorSource* screen) {
    audio_source_ = audio; screen_source_ = screen;
}
void EffectEngine::SetHallAnalogHost(const AuraHallAnalogHostV1* host) {
    std::lock_guard<std::mutex> lock(hall_config_mutex_);
    hall_host_ = host ? *host : AuraHallAnalogHostV1{};
}
void EffectEngine::SetHallLightingConfig(const HallLightingConfig& config) {
    std::lock_guard<std::mutex> lock(hall_config_mutex_);
    hall_lighting_ = config;
}
HallLightingConfig EffectEngine::GetHallLightingConfig() const {
    std::lock_guard<std::mutex> lock(hall_config_mutex_);
    return hall_lighting_;
}
bool EffectEngine::ActiveEffectUsesHall() const {
    std::lock_guard<std::mutex> lock(profile_mutex_);
    if (!active_profile_ || !active_profile_->base_effect) return false;
    if (auto prog = std::dynamic_pointer_cast<ProgramEffect>(active_profile_->base_effect)) {
        return prog->Program() && prog->Program()->UsesHall();
    }
    return false;
}

EffectEngine::RenderDiagnostics EffectEngine::GetRenderDiagnostics() const {
    std::lock_guard<std::mutex> lock(render_diagnostics_mutex_);
    return render_diagnostics_;
}

void EffectEngine::SetActiveProfile(std::shared_ptr<const Profile> profile) {
    std::lock_guard<std::mutex> lock(profile_mutex_);
    if (profile && !profile->plugin_name.empty()) {
        auto copy = std::make_shared<Profile>(*profile);
        auto instance = PluginManager::Instance().CreateEffect(profile->plugin_name);
        if (instance) copy->base_effect = instance;
        profile = copy;
    }
    if (profile && profile->base_effect) {
        auto fresh = FreshInputEffect(profile->base_effect);
        if (fresh != profile->base_effect) {
            auto copy = std::make_shared<Profile>(*profile);
            copy->base_effect = std::move(fresh);
            profile = std::move(copy);
        }
    }
    profile_started_ms_ = GetElapsedMs();
    active_profile_ = std::move(profile);
    base_identity_.clear(); attempted_base_identity_.clear();
}

bool EffectEngine::ReconcileProfile(std::shared_ptr<const Profile> profile) {
    if (!profile) return false; // retain last runnable base on failed/missing selection
    const auto plugin = ProfilePluginName(*profile);
    const auto generation = plugin.empty() ? nullptr : PluginManager::Instance().GetGeneration(plugin);
    std::string identity = profile->name + ":" + ProfileEffectIdentity(*profile) + ":" + std::to_string(profile->brightness) + ":" + std::to_string(profile->fps);
    for (const auto& key : profile->key_overrides) identity += key.key_spec + ":" + std::to_string(key.color.r) + "," + std::to_string(key.color.g) + "," + std::to_string(key.color.b);
    identity += ":generation=" + std::to_string(generation ? generation->generation_id : 0);
    { std::lock_guard<std::mutex> lock(profile_mutex_);
      if (identity == base_identity_ || identity == attempted_base_identity_) return false;
      attempted_base_identity_ = identity; }
    try {
        auto candidate = std::make_shared<Profile>(*profile);
        if (!plugin.empty()) {
            auto fresh = CreateProfileEffectInstance(*profile);
            if (!fresh) { LOG_WARN("[Automation base] Replacement unavailable; retaining prior base"); return false; }
            candidate->base_effect = fresh.GetEffect();
        }
        candidate->base_effect = FreshInputEffect(candidate->base_effect);
        std::lock_guard<std::mutex> lock(profile_mutex_);
        active_profile_ = std::move(candidate); base_identity_ = identity; profile_started_ms_ = GetElapsedMs();
        return true;
    } catch (...) { LOG_WARN("[Automation base] Replacement failed; retaining prior base"); return false; }
}

std::shared_ptr<const Profile> EffectEngine::GetActiveProfileCopy() const {
    std::lock_guard<std::mutex> lock(profile_mutex_);
    return active_profile_;
}

uint64_t EffectEngine::GetElapsedMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
}

void EffectEngine::SetPreviewFrame(const FrameBuffer& frame, uint64_t duration_ms) {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    preview_frame_ = frame;
    preview_active_ = true;
    preview_expiry_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
}

void EffectEngine::ClearPreview() {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    preview_active_ = false;
}

bool EffectEngine::HasActivePreview() const {
    std::lock_guard<std::mutex> lock(preview_mutex_);
    return preview_active_ && (std::chrono::steady_clock::now() < preview_expiry_);
}

void EffectEngine::Tick(FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi) {
    const auto elapsed = GetElapsedMs();
    TickAt(elapsed, out_frame, keymap, EffectContext(elapsed, keymap, gsi, audio_source_, screen_source_, AutomationMonotonicMs()));
}

void EffectEngine::TickAt(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi) {
    TickAt(elapsed_ms, out_frame, keymap, EffectContext(elapsed_ms, keymap, gsi, audio_source_, screen_source_));
}

void EffectEngine::TickAt(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap, const EffectContext& context) {
    const auto render_start = std::chrono::steady_clock::now();
    const auto* gsi = context.gsi;
    {
        std::lock_guard<std::mutex> lock(render_diagnostics_mutex_);
        render_diagnostics_.active_program = false;
        render_diagnostics_.program_faulted = false;
        render_diagnostics_.audio_used = false;
        render_diagnostics_.audio_available = false;
        render_diagnostics_.screen_used = false;
        render_diagnostics_.screen_available = false;
        render_diagnostics_.program_generation = 0;
        render_diagnostics_.program_error.clear();
    }

    // 1. Check edit-time preview frame
    bool is_preview = false;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        if (preview_active_) {
            if (std::chrono::steady_clock::now() < preview_expiry_) {
                out_frame = preview_frame_;
                is_preview = true;
            } else {
                preview_active_ = false;
            }
        }
    }

    // 2. Render base active profile if not in preview mode
    if (!is_preview) {
        // Copy the shared_ptr under the lock, then render without holding it:
        // the profile stays alive for the whole render even if a hot reload swaps
        // the active profile concurrently. Zero tearing, zero frame drops!
        std::shared_ptr<const Profile> profile;
        uint64_t started;
        { std::lock_guard<std::mutex> lock(profile_mutex_); profile = active_profile_; started = profile_started_ms_; }
        if (profile) {
            if(auto program=std::dynamic_pointer_cast<ProgramEffect>(profile->base_effect)) {
                program->RenderWithContext(EffectContext(elapsed_ms >= started ? elapsed_ms - started : 0,keymap,gsi,context.audio,context.screen,context.monotonic_ms),out_frame);
                const auto& status = program->Status();
                const auto generation = program->GenerationId();
                std::lock_guard<std::mutex> lock(render_diagnostics_mutex_);
                render_diagnostics_.active_program = true;
                render_diagnostics_.program_faulted = status.faulted;
                render_diagnostics_.audio_used = program->Program()->UsesAudio();
                render_diagnostics_.audio_available = status.audio_available;
                render_diagnostics_.screen_used = program->Program()->UsesScreen();
                render_diagnostics_.screen_available = status.screen_available;
                render_diagnostics_.program_generation = generation;
                render_diagnostics_.program_error = status.error.message;
                const auto opacity=program->Finished()?0.0f:program->Opacity();
                for(auto& channel:out_frame.buffer)channel=static_cast<uint8_t>(channel*opacity);
                // Profile overrides and brightness retain their established order
                // after the effect's own lifecycle opacity, without ABI changes.
                for(const auto& override:profile->key_overrides){std::vector<int> ids;if(keymap.ResolveKeys(override.key_spec,ids))
                    for(int id:ids)out_frame.SetKey(id,override.color);}
                if(profile->brightness<255)for(auto& channel:out_frame.buffer)
                    channel=static_cast<uint8_t>((static_cast<uint32_t>(channel)*profile->brightness)/255);
            } else profile->Render(elapsed_ms >= started ? elapsed_ms - started : 0, out_frame, keymap, gsi);
        } else {
            out_frame.Clear();
        }
    }

    if (!is_preview) {
        HallLightingConfig hall_cfg;
        AuraHallAnalogHostV1 hall_host{};
        {
            std::lock_guard<std::mutex> lock(hall_config_mutex_);
            hall_cfg = hall_lighting_;
            hall_host = hall_host_;
        }
        ApplyHallDepthOverlay(hall_cfg, hall_host, keymap, out_frame);
    }

    // Fixed V2 class placement: Base -> persistent -> transient.
    LayerStack persistent, transient;
    automation_effects_.RenderLayers(true, elapsed_ms, persistent, keymap, context);
    automation_effects_.RenderLayers(false, elapsed_ms, transient, keymap, context);
    const auto base = LayerPixelFrame::FromFrame(out_frame, true);
    std::bitset<TOTAL_LEDS> coverage;
    compositor_.Compose(base, persistent, transient, out_frame, coverage);
    last_frame_ = out_frame;
    const auto elapsed_us = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - render_start).count());
    std::lock_guard<std::mutex> lock(render_diagnostics_mutex_);
    ++render_diagnostics_.rendered_frames;
    render_diagnostics_.last_render_us = elapsed_us;
    render_diagnostics_.compositor_calls = compositor_.Metrics().compose_calls;
    render_diagnostics_.composited_layers = persistent.Size() + transient.Size();
    if (!render_diagnostics_.active_program) {
        render_diagnostics_.program_generation = 0;
        render_diagnostics_.program_error.clear();
    }
}

} // namespace aura
