#include "engine/effect_engine.h"

namespace aura {

EffectEngine::EffectEngine()
    : active_profile_(nullptr),
      start_time_(std::chrono::steady_clock::now()) {}

void EffectEngine::SetActiveProfile(const Profile* profile) {
    active_profile_.store(profile, std::memory_order_release);
}

const Profile* EffectEngine::GetActiveProfile() const {
    return active_profile_.load(std::memory_order_acquire);
}

uint64_t EffectEngine::GetElapsedMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
}

void EffectEngine::Tick(FrameBuffer& out_frame, const Keymap& keymap) {
    const Profile* profile = GetActiveProfile();
    uint64_t elapsed_ms = GetElapsedMs();

    if (profile) {
        profile->Render(elapsed_ms, out_frame, keymap);
    } else {
        out_frame.Clear();
    }
}

} // namespace aura
