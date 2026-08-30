#include "engine/effect_engine.h"

namespace aura {

EffectEngine::EffectEngine()
    : start_time_(std::chrono::steady_clock::now()) {}

void EffectEngine::SetActiveProfile(std::shared_ptr<const Profile> profile) {
    std::lock_guard<std::mutex> lock(profile_mutex_);
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

void EffectEngine::Tick(FrameBuffer& out_frame, const Keymap& keymap) {
    // Copy the shared_ptr under the lock, then render without holding it:
    // the profile stays alive for the whole render even if a hot reload swaps
    // the active profile concurrently.
    std::shared_ptr<const Profile> profile = GetActiveProfileCopy();
    uint64_t elapsed_ms = GetElapsedMs();

    if (profile) {
        profile->Render(elapsed_ms, out_frame, keymap);
    } else {
        out_frame.Clear();
    }
}

} // namespace aura
