#include "engine/effect_engine.h"
#include "engine/plugin_manager.h"

namespace aura {

EffectEngine::EffectEngine()
    : start_time_(std::chrono::steady_clock::now()),
      preview_active_(false) {
    preview_frame_.Clear();
}

void EffectEngine::SetActiveProfile(std::shared_ptr<const Profile> profile) {
    std::lock_guard<std::mutex> lock(profile_mutex_);
    if (profile && !profile->plugin_name.empty()) {
        auto copy = std::make_shared<Profile>(*profile);
        auto instance = PluginManager::Instance().CreateEffect(profile->plugin_name);
        if (instance) copy->base_effect = instance;
        profile = copy;
    }
    profile_started_ms_ = GetElapsedMs();
    active_profile_ = std::move(profile);
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
    uint64_t elapsed_ms = GetElapsedMs();

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
            profile->Render(elapsed_ms >= started ? elapsed_ms - started : 0, out_frame, keymap, gsi);
        } else {
            out_frame.Clear();
        }
    }

    // 3. Blend active CS2 transient event overlays (0 heap allocation)
    overlay_manager_.ApplyOverlays(elapsed_ms, out_frame, keymap, gsi);
}

} // namespace aura
