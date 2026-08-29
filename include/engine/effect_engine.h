#pragma once

#include "engine/effect.h"
#include "aura/aura_types.h"
#include "aura/keymap.h"
#include <atomic>
#include <chrono>

namespace aura {

class EffectEngine {
public:
    EffectEngine();

    void SetActiveProfile(const Profile* profile);
    const Profile* GetActiveProfile() const;

    // Zero-allocation render tick for the current elapsed time
    void Tick(FrameBuffer& out_frame, const Keymap& keymap);

    uint64_t GetElapsedMs() const;

private:
    std::atomic<const Profile*> active_profile_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace aura
