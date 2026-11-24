#pragma once

#include "engine/input_sources.h"
#include "third_party/httplib.h"
#include "third_party/json.hpp"
#include <mutex>

namespace aura {

// Capture consent is daemon-session scoped and defaults to disabled. No user
// configuration or OS privacy setting is modified by this service.
class InputCaptureService {
public:
    InputCaptureService();
    ~InputCaptureService();
    InputCaptureService(const InputCaptureService&) = delete;
    InputCaptureService& operator=(const InputCaptureService&) = delete;

    const AudioFeatureSource& Audio() const { return audio_; }
    const ScreenColorSource& Screen() const { return screen_; }
    nlohmann::json Status(uint64_t now_ms) const;
    nlohmann::json SetEnabled(const nlohmann::json& request, uint64_t now_ms);
    void RegisterRoutes(httplib::Server& server);
    void Shutdown();

private:
    nlohmann::json StatusLocked(uint64_t now_ms) const;
    mutable std::mutex mutex_;
    AudioFeatureSource audio_;
    ScreenColorSource screen_;
    WindowsAudioCaptureAdapter audio_adapter_;
    WindowsScreenCaptureAdapter screen_adapter_;
};

} // namespace aura
