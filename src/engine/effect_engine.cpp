#include "engine/effect_engine.h"
#include "engine/plugin_manager.h"
#include "engine/builtin_effects.h"
#include "engine/input_sources.h"
#include "gsi/automation_input.h"
#include "utils/logger.h"

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

void EffectEngine::SetInputSources(const AudioFeatureSource* audio, const ScreenColorSource* screen) {
    audio_source_ = audio; screen_source_ = screen;
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
