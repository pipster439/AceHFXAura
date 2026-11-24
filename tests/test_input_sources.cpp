#include "engine/input_sources.h"
#include <iostream>
#include <vector>
#include <cmath>

using namespace aura;
int checks = 0;
void C(bool v, const char* m) {
    ++checks;
    if (!v) throw std::runtime_error(m);
}

int main() {
    try {
        AudioFeatureSource audio;
        auto disabled = audio.Read(100);
        C(disabled.health.availability == InputAvailability::Disabled, "audio default disabled");
        C(!audio.Submit({100, .5f, .2f, 1000, 0}), "disabled audio rejects samples");
        audio.Enable();
        C(audio.Submit({100, .5f, .2f, 1000, 0}), "audio fake sample accepted");
        C(audio.Read(120).health.availability == InputAvailability::Fresh, "audio fresh");
        C(audio.Read(500).health.availability == InputAvailability::Stale, "audio stale");
        C(!audio.Submit({101, std::numeric_limits<float>::quiet_NaN(), 0, 0, 0}), "invalid audio rejected");
        audio.Disable();
        C(audio.Read(120).health.availability == InputAvailability::Disabled, "audio disable");

        ScreenColorSource screen;
        screen.Enable();
        C(screen.Submit({100, {1, 2, 3}, 0}), "screen fake sample accepted");
        C(screen.Read(200).sample.color.g == 2, "screen deterministic color");
        screen.Disable();
        C(screen.Read(200).health.availability == InputAvailability::Disabled, "screen disable");

        GsiInputSource gsi;
        C(gsi.Health(0).availability == InputAvailability::Unknown, "GSI absent unknown");
        gsi.Observe(100, 4);
        C(gsi.Health(120).availability == InputAvailability::Fresh, "GSI observed fresh");
        gsi.Disable();
        C(gsi.Health(120).availability == InputAvailability::Disabled, "GSI disabled");

        KeyInputHub::Instance().SetSourceAvailable(true);
        KeyInputHub::Instance().RecordKeyEvent("W", KeyEventType::Down);
        AuraLogicalKeyboardSnapshotV1 snapshot{};
        KeyInputHub::Instance().ReadLogicalSnapshot(snapshot);
        KeyboardInputSource keyboard;
        C(keyboard.Read(snapshot.observed_at_ms + 100).health.availability == InputAvailability::Fresh, "keyboard hub snapshot fresh");
        KeyInputHub::Instance().Reset();
        KeyInputHub::Instance().SetSourceAvailable(false);

        C(audio.Metrics().accepted_samples == 1 && audio.Metrics().rejected_samples >= 2, "source metrics bounded");

#ifdef _WIN32
        // Test Windows Audio Capture Adapter with simulation seam
        AudioFeatureSource win_audio_source;
        WindowsAudioCaptureAdapter audio_adapter(win_audio_source);
        C(!audio_adapter.Enabled(), "audio adapter defaults to disabled");
        C(audio_adapter.Enable(), "audio adapter enable");
        C(audio_adapter.Enabled(), "audio adapter enabled state");

        // 480 samples of a 1kHz sine wave at 48kHz, peak amplitude 0.8
        std::vector<float> pcm(480);
        for (size_t i = 0; i < pcm.size(); ++i) {
            pcm[i] = 0.8f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
        }
        C(audio_adapter.FeedPcmSamples(pcm.data(), pcm.size(), 48000, 1000), "audio adapter feed PCM");
        auto audio_read = win_audio_source.Read(1010);
        C(audio_read.health.availability == InputAvailability::Fresh, "fed audio is fresh");
        C(std::abs(audio_read.sample.peak - 0.8f) < 0.01f, "fed audio peak accurate");
        C(std::abs(audio_read.sample.rms - 0.8f / 1.4142f) < 0.05f, "fed audio RMS accurate");
        C(audio_read.sample.spectral_centroid_hz > 500.0f && audio_read.sample.spectral_centroid_hz < 1500.0f, "fed audio centroid estimated");
        C(audio_adapter.GetMetrics().simulated_frames == 1, "audio adapter metrics count simulated frames");

        audio_adapter.Disable();
        C(!audio_adapter.Enabled(), "audio adapter disabled state");
        C(win_audio_source.Read(1010).health.availability == InputAvailability::Disabled, "source disabled after adapter disable");

        // Test Windows Screen Capture Adapter with simulation seam
        ScreenColorSource win_screen_source;
        WindowsScreenCaptureAdapter screen_adapter(win_screen_source, 20);
        C(!screen_adapter.Enabled(), "screen adapter defaults to disabled");
        C(screen_adapter.Enable(), "screen adapter enable");
        C(screen_adapter.Enabled(), "screen adapter enabled state");

        // 4x4 image, 16 pixels of BGRA (B=30, G=70, R=190, A=255)
        std::vector<uint8_t> bgra(4 * 4 * 4);
        for (size_t i = 0; i < 16; ++i) {
            bgra[i * 4 + 0] = 30;  // B
            bgra[i * 4 + 1] = 70;  // G
            bgra[i * 4 + 2] = 190; // R
            bgra[i * 4 + 3] = 255; // A
        }
        C(screen_adapter.FeedPixels(bgra.data(), 4, 4, 2000), "screen adapter feed pixels");
        auto screen_read = win_screen_source.Read(2010);
        C(screen_read.health.availability == InputAvailability::Fresh, "fed screen color is fresh");
        C(screen_read.sample.color.r == 190 && screen_read.sample.color.g == 70 && screen_read.sample.color.b == 30, "fed screen RGB accurate");
        C(screen_adapter.GetMetrics().simulated_frames == 1, "screen adapter metrics count simulated frames");

        screen_adapter.Disable();
        C(!screen_adapter.Enabled(), "screen adapter disabled state");
        C(win_screen_source.Read(2010).health.availability == InputAvailability::Disabled, "screen source disabled after adapter disable");
#endif

        std::cout << "PASS: " << checks << " input source assertions\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
