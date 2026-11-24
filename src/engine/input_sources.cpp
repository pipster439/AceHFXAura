#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <chrono>
#include <vector>
#endif
#include "engine/input_sources.h"
#include <cstring>
#include <cmath>

namespace aura {
namespace {
uint32_t Bits(float value) { uint32_t bits{}; std::memcpy(&bits, &value, sizeof(bits)); return bits; }
float Float(uint32_t value) { float result{}; std::memcpy(&result, &value, sizeof(result)); return result; }
uint32_t Packed(ColorRGB color) { return static_cast<uint32_t>(color.r) | (static_cast<uint32_t>(color.g) << 8) | (static_cast<uint32_t>(color.b) << 16); }
ColorRGB Unpacked(uint32_t value) { return ColorRGB(static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16)); }
float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
}

InputHealth EvaluateInputHealth(bool enabled, uint64_t observed_at_ms, uint64_t sequence,
                                uint64_t now_ms, uint64_t freshness_ms) {
    InputHealth health{InputAvailability::Unknown, observed_at_ms, 0, sequence};
    if (!enabled) { health.availability = InputAvailability::Disabled; return health; }
    if (!observed_at_ms || now_ms < observed_at_ms) return health;
    health.age_ms = now_ms - observed_at_ms;
    health.availability = health.age_ms <= freshness_ms ? InputAvailability::Fresh : InputAvailability::Stale;
    return health;
}

void AudioFeatureSource::Enable() { enabled_.store(true, std::memory_order_release); }
void AudioFeatureSource::Disable() { enabled_.store(false, std::memory_order_release); }
bool AudioFeatureSource::Valid(const AudioFeatureSample& sample) {
    return sample.observed_at_ms && std::isfinite(sample.rms) && std::isfinite(sample.peak) &&
        std::isfinite(sample.spectral_centroid_hz) && sample.rms >= 0.0f && sample.rms <= 1.0f &&
        sample.peak >= 0.0f && sample.peak <= 1.0f && sample.spectral_centroid_hz >= 0.0f &&
        sample.spectral_centroid_hz <= 24000.0f;
}
bool AudioFeatureSource::Submit(AudioFeatureSample sample) {
    if (!Enabled() || !Valid(sample)) { rejected_.fetch_add(1, std::memory_order_relaxed); return false; }
    const auto next = sequence_.load(std::memory_order_relaxed) + 1;
    sample.rms = Clamp01(sample.rms); sample.peak = Clamp01(sample.peak); sample.sequence = next;
    rms_bits_.store(Bits(sample.rms), std::memory_order_relaxed); peak_bits_.store(Bits(sample.peak), std::memory_order_relaxed);
    centroid_bits_.store(Bits(sample.spectral_centroid_hz), std::memory_order_relaxed);
    observed_at_ms_.store(sample.observed_at_ms, std::memory_order_release); sequence_.store(next, std::memory_order_release);
    accepted_.fetch_add(1, std::memory_order_relaxed); return true;
}
AudioFeatureReading AudioFeatureSource::Read(uint64_t now_ms) const {
    reads_.fetch_add(1, std::memory_order_relaxed);
    AudioFeatureReading reading;
    const auto observed = observed_at_ms_.load(std::memory_order_acquire), sequence = sequence_.load(std::memory_order_acquire);
    reading.health = EvaluateInputHealth(Enabled(), observed, sequence, now_ms, freshness_ms_);
    reading.sample = {observed, Float(rms_bits_.load(std::memory_order_relaxed)), Float(peak_bits_.load(std::memory_order_relaxed)),
        Float(centroid_bits_.load(std::memory_order_relaxed)), sequence};
    if (reading.health.availability != InputAvailability::Fresh) reading.sample = {};
    return reading;
}
InputSourceMetrics AudioFeatureSource::Metrics() const { return {accepted_.load(), rejected_.load(), reads_.load()}; }

void ScreenColorSource::Enable() { enabled_.store(true, std::memory_order_release); }
void ScreenColorSource::Disable() { enabled_.store(false, std::memory_order_release); }
bool ScreenColorSource::Valid(const ScreenColorSample& sample) { return sample.observed_at_ms != 0; }
bool ScreenColorSource::Submit(ScreenColorSample sample) {
    if (!Enabled() || !Valid(sample)) { rejected_.fetch_add(1, std::memory_order_relaxed); return false; }
    const auto next = sequence_.load(std::memory_order_relaxed) + 1; sample.sequence = next;
    color_.store(Packed(sample.color), std::memory_order_relaxed); observed_at_ms_.store(sample.observed_at_ms, std::memory_order_release);
    sequence_.store(next, std::memory_order_release); accepted_.fetch_add(1, std::memory_order_relaxed); return true;
}
ScreenColorReading ScreenColorSource::Read(uint64_t now_ms) const {
    reads_.fetch_add(1, std::memory_order_relaxed);
    const auto observed = observed_at_ms_.load(std::memory_order_acquire), sequence = sequence_.load(std::memory_order_acquire);
    ScreenColorReading reading; reading.health = EvaluateInputHealth(Enabled(), observed, sequence, now_ms, freshness_ms_);
    reading.sample = {observed, Unpacked(color_.load(std::memory_order_relaxed)), sequence};
    if (reading.health.availability != InputAvailability::Fresh) reading.sample = {};
    return reading;
}
InputSourceMetrics ScreenColorSource::Metrics() const { return {accepted_.load(), rejected_.load(), reads_.load()}; }

void GsiInputSource::Observe(uint64_t observed_at_ms, uint64_t sequence) {
    if (!observed_at_ms) return;
    observed_at_ms_.store(observed_at_ms, std::memory_order_release); sequence_.store(sequence, std::memory_order_release);
}
InputHealth GsiInputSource::Health(uint64_t now_ms) const {
    const auto observed = observed_at_ms_.load(std::memory_order_acquire), sequence = sequence_.load(std::memory_order_acquire);
    return EvaluateInputHealth(enabled_.load(std::memory_order_acquire), observed, sequence, now_ms, freshness_ms_);
}
KeyboardInputReading KeyboardInputSource::Read(uint64_t now_ms) const {
    KeyboardInputReading reading; KeyInputHub::Instance().ReadLogicalSnapshot(reading.snapshot);
    reading.health = EvaluateInputHealth(reading.snapshot.source_available != 0, reading.snapshot.observed_at_ms,
        reading.snapshot.sequence, now_ms, freshness_ms_);
    if (reading.health.availability != InputAvailability::Fresh) reading.snapshot = {};
    return reading;
}

#ifdef _WIN32
WindowsAudioCaptureAdapter::WindowsAudioCaptureAdapter(AudioFeatureSource& target)
    : target_(target) {}

WindowsAudioCaptureAdapter::~WindowsAudioCaptureAdapter() {
    Disable();
}

bool WindowsAudioCaptureAdapter::Enable() {
    if (enabled_.load(std::memory_order_acquire)) return true;
    enabled_.store(true, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    target_.Enable();
    try {
        worker_ = std::thread(&WindowsAudioCaptureAdapter::WorkerLoop, this);
    } catch (...) {
        enabled_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void WindowsAudioCaptureAdapter::Disable() {
    if (!enabled_.load(std::memory_order_acquire) && !running_.load(std::memory_order_acquire)) return;
    enabled_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    target_.Disable();
    if (worker_.joinable()) {
        try { worker_.join(); } catch (...) {}
    }
}

bool WindowsAudioCaptureAdapter::FeedPcmSamples(const float* samples, size_t count, uint32_t sample_rate, uint64_t now_ms) {
    if (!samples || count == 0) return false;
    double sum_sq = 0.0;
    float peak = 0.0f;
    size_t zero_crossings = 0;
    for (size_t i = 0; i < count; ++i) {
        float val = samples[i];
        if (!std::isfinite(val)) val = 0.0f;
        float abs_val = std::fabs(val);
        if (abs_val > peak) peak = abs_val;
        sum_sq += static_cast<double>(val) * val;
        if (i > 0 && ((samples[i] >= 0.0f && samples[i-1] < 0.0f) || (samples[i] < 0.0f && samples[i-1] >= 0.0f))) {
            ++zero_crossings;
        }
    }
    float rms = static_cast<float>(std::sqrt(sum_sq / count));
    float centroid = 0.0f;
    if (sample_rate > 0 && count > 1) {
        centroid = (static_cast<float>(zero_crossings) / (2.0f * count)) * sample_rate;
    }
    AudioFeatureSample sample;
    sample.observed_at_ms = now_ms;
    sample.rms = std::clamp(rms, 0.0f, 1.0f);
    sample.peak = std::clamp(peak, 0.0f, 1.0f);
    sample.spectral_centroid_hz = std::clamp(centroid, 0.0f, 24000.0f);
    simulated_frames_.fetch_add(1, std::memory_order_relaxed);
    return target_.Submit(sample);
}

WindowsAudioCaptureAdapter::Metrics WindowsAudioCaptureAdapter::GetMetrics() const {
    return {frames_captured_.load(std::memory_order_relaxed),
            capture_errors_.load(std::memory_order_relaxed),
            simulated_frames_.load(std::memory_order_relaxed)};
}

void WindowsAudioCaptureAdapter::WorkerLoop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnumerator));
    if (FAILED(hr) || !pEnumerator) {
        capture_errors_.fetch_add(1, std::memory_order_relaxed);
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CoUninitialize();
        return;
    }

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnumerator->Release();
    if (FAILED(hr) || !pDevice) {
        capture_errors_.fetch_add(1, std::memory_order_relaxed);
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CoUninitialize();
        return;
    }

    IAudioClient* pAudioClient = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&pAudioClient));
    pDevice->Release();
    if (FAILED(hr) || !pAudioClient) {
        capture_errors_.fetch_add(1, std::memory_order_relaxed);
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CoUninitialize();
        return;
    }

    WAVEFORMATEX* pwfx = nullptr;
    hr = pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr) || !pwfx) {
        capture_errors_.fetch_add(1, std::memory_order_relaxed);
        pAudioClient->Release();
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CoUninitialize();
        return;
    }

    REFERENCE_TIME hnsBufferDuration = 1000000;
    hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                  hnsBufferDuration, 0, pwfx, nullptr);
    if (FAILED(hr)) {
        capture_errors_.fetch_add(1, std::memory_order_relaxed);
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CoUninitialize();
        return;
    }

    IAudioCaptureClient* pCaptureClient = nullptr;
    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&pCaptureClient));
    if (FAILED(hr) || !pCaptureClient) {
        capture_errors_.fetch_add(1, std::memory_order_relaxed);
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CoUninitialize();
        return;
    }

    hr = pAudioClient->Start();
    if (FAILED(hr)) {
        capture_errors_.fetch_add(1, std::memory_order_relaxed);
        pCaptureClient->Release();
        CoTaskMemFree(pwfx);
        pAudioClient->Release();
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        CoUninitialize();
        return;
    }

    const uint32_t sample_rate = pwfx->nSamplesPerSec;
    const WORD channels = pwfx->nChannels;
    const WORD bits_per_sample = pwfx->wBitsPerSample;
    const bool is_float = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
        (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    std::vector<float> pcm_scratch;
    pcm_scratch.reserve(4096);

    while (running_.load(std::memory_order_acquire) && enabled_.load(std::memory_order_acquire)) {
        UINT32 packet_length = 0;
        hr = pCaptureClient->GetNextPacketSize(&packet_length);
        if (FAILED(hr) || packet_length == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        BYTE* pData = nullptr;
        UINT32 num_frames = 0;
        DWORD flags = 0;
        hr = pCaptureClient->GetBuffer(&pData, &num_frames, &flags, nullptr, nullptr);
        if (SUCCEEDED(hr) && pData && num_frames > 0) {
            pcm_scratch.resize(num_frames);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill(pcm_scratch.begin(), pcm_scratch.end(), 0.0f);
            } else if (is_float && bits_per_sample == 32) {
                const float* in = reinterpret_cast<const float*>(pData);
                for (UINT32 f = 0; f < num_frames; ++f) {
                    float sum = 0.0f;
                    for (WORD c = 0; c < channels; ++c) sum += in[f * channels + c];
                    pcm_scratch[f] = sum / (channels > 0 ? channels : 1);
                }
            } else if (bits_per_sample == 16) {
                const int16_t* in = reinterpret_cast<const int16_t*>(pData);
                for (UINT32 f = 0; f < num_frames; ++f) {
                    float sum = 0.0f;
                    for (WORD c = 0; c < channels; ++c) sum += in[f * channels + c] / 32768.0f;
                    pcm_scratch[f] = sum / (channels > 0 ? channels : 1);
                }
            } else {
                std::fill(pcm_scratch.begin(), pcm_scratch.end(), 0.0f);
            }

            const uint64_t now_ms = static_cast<uint64_t>(GetTickCount64());
            double sum_sq = 0.0;
            float peak = 0.0f;
            size_t zero_crossings = 0;
            for (size_t i = 0; i < pcm_scratch.size(); ++i) {
                float val = pcm_scratch[i];
                if (!std::isfinite(val)) val = 0.0f;
                float abs_val = std::fabs(val);
                if (abs_val > peak) peak = abs_val;
                sum_sq += static_cast<double>(val) * val;
                if (i > 0 && ((pcm_scratch[i] >= 0.0f && pcm_scratch[i-1] < 0.0f) || (pcm_scratch[i] < 0.0f && pcm_scratch[i-1] >= 0.0f))) {
                    ++zero_crossings;
                }
            }
            float rms = static_cast<float>(std::sqrt(sum_sq / (pcm_scratch.empty() ? 1 : pcm_scratch.size())));
            float centroid = 0.0f;
            if (sample_rate > 0 && pcm_scratch.size() > 1) {
                centroid = (static_cast<float>(zero_crossings) / (2.0f * pcm_scratch.size())) * sample_rate;
            }
            AudioFeatureSample sample;
            sample.observed_at_ms = now_ms;
            sample.rms = std::clamp(rms, 0.0f, 1.0f);
            sample.peak = std::clamp(peak, 0.0f, 1.0f);
            sample.spectral_centroid_hz = std::clamp(centroid, 0.0f, 24000.0f);
            target_.Submit(sample);
            frames_captured_.fetch_add(1, std::memory_order_relaxed);
            pCaptureClient->ReleaseBuffer(num_frames);
        } else {
            capture_errors_.fetch_add(1, std::memory_order_relaxed);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pAudioClient->Stop();
    pCaptureClient->Release();
    CoTaskMemFree(pwfx);
    pAudioClient->Release();
    CoUninitialize();
}

WindowsScreenCaptureAdapter::WindowsScreenCaptureAdapter(ScreenColorSource& target, uint32_t sample_interval_ms)
    : target_(target), sample_interval_ms_(sample_interval_ms) {}

WindowsScreenCaptureAdapter::~WindowsScreenCaptureAdapter() {
    Disable();
}

bool WindowsScreenCaptureAdapter::Enable() {
    if (enabled_.load(std::memory_order_acquire)) return true;
    enabled_.store(true, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    target_.Enable();
    try {
        worker_ = std::thread(&WindowsScreenCaptureAdapter::WorkerLoop, this);
    } catch (...) {
        enabled_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void WindowsScreenCaptureAdapter::Disable() {
    if (!enabled_.load(std::memory_order_acquire) && !running_.load(std::memory_order_acquire)) return;
    enabled_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    target_.Disable();
    if (worker_.joinable()) {
        try { worker_.join(); } catch (...) {}
    }
}

bool WindowsScreenCaptureAdapter::FeedPixels(const uint8_t* bgra, int width, int height, uint64_t now_ms) {
    if (!bgra || width <= 0 || height <= 0) return false;
    uint64_t total_r = 0, total_g = 0, total_b = 0;
    const size_t pixel_count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixel_count; ++i) {
        total_b += bgra[i * 4 + 0];
        total_g += bgra[i * 4 + 1];
        total_r += bgra[i * 4 + 2];
    }
    uint8_t avg_r = static_cast<uint8_t>(total_r / pixel_count);
    uint8_t avg_g = static_cast<uint8_t>(total_g / pixel_count);
    uint8_t avg_b = static_cast<uint8_t>(total_b / pixel_count);
    ScreenColorSample sample;
    sample.observed_at_ms = now_ms;
    sample.color = ColorRGB(avg_r, avg_g, avg_b);
    simulated_frames_.fetch_add(1, std::memory_order_relaxed);
    return target_.Submit(sample);
}

WindowsScreenCaptureAdapter::Metrics WindowsScreenCaptureAdapter::GetMetrics() const {
    return {frames_captured_.load(std::memory_order_relaxed),
            capture_errors_.load(std::memory_order_relaxed),
            simulated_frames_.load(std::memory_order_relaxed)};
}

void WindowsScreenCaptureAdapter::WorkerLoop() {
    while (running_.load(std::memory_order_acquire) && enabled_.load(std::memory_order_acquire)) {
        HDC hdcScreen = GetDC(NULL);
        if (!hdcScreen) {
            capture_errors_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms_));
            continue;
        }

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (screenW <= 0 || screenH <= 0) {
            ReleaseDC(NULL, hdcScreen);
            capture_errors_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms_));
            continue;
        }

        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            ReleaseDC(NULL, hdcScreen);
            capture_errors_.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms_));
            continue;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 16;
        bmi.bmiHeader.biHeight = -16;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pBits = nullptr;
        HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (hbm && pBits) {
            HGDIOBJ oldBmp = SelectObject(hdcMem, hbm);
            SetStretchBltMode(hdcMem, COLORONCOLOR);
            StretchBlt(hdcMem, 0, 0, 16, 16, hdcScreen, 0, 0, screenW, screenH, SRCCOPY);

            const uint64_t now_ms = static_cast<uint64_t>(GetTickCount64());
            const uint8_t* bgra = static_cast<const uint8_t*>(pBits);
            uint64_t total_r = 0, total_g = 0, total_b = 0;
            for (size_t i = 0; i < 256; ++i) {
                total_b += bgra[i * 4 + 0];
                total_g += bgra[i * 4 + 1];
                total_r += bgra[i * 4 + 2];
            }
            ScreenColorSample sample;
            sample.observed_at_ms = now_ms;
            sample.color = ColorRGB(static_cast<uint8_t>(total_r / 256), static_cast<uint8_t>(total_g / 256), static_cast<uint8_t>(total_b / 256));
            target_.Submit(sample);
            frames_captured_.fetch_add(1, std::memory_order_relaxed);

            SelectObject(hdcMem, oldBmp);
            DeleteObject(hbm);
        } else {
            capture_errors_.fetch_add(1, std::memory_order_relaxed);
        }

        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms_));
    }
}
#endif
} // namespace aura
