#pragma once

#include "engine/effect.h"
#include "aura/aura_types.h"
#include "aura/keymap.h"
#include <memory>
#include <mutex>
#include <chrono>

namespace aura {

class EffectEngine {
public:
    EffectEngine();

    // Holds a shared_ptr copy so a concurrent config hot reload can never
    // invalidate the profile currently being rendered (no dangling pointers).
    void SetActiveProfile(std::shared_ptr<const Profile> profile);

    // Zero-allocation render tick for the current elapsed time
    void Tick(FrameBuffer& out_frame, const Keymap& keymap);

    uint64_t GetElapsedMs() const;

private:
    std::shared_ptr<const Profile> GetActiveProfileCopy() const;

    mutable std::mutex profile_mutex_;
    std::shared_ptr<const Profile> active_profile_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace aura
