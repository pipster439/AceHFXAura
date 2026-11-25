#pragma once
#include "engine/effect.h"
#include "engine/input_sources.h"
#include "third_party/json.hpp"
#include <bitset>
#include <memory>
#include <stdexcept>

namespace aura {
struct EffectProgramError {
    std::string code;
    std::string path;
    std::string node; // optional originating Blockly block ID, never executable.
    std::string message;
};
class EffectProgramException : public std::runtime_error {
public:
    explicit EffectProgramException(EffectProgramError error)
        : std::runtime_error(error.message), error_(std::move(error)) {}
    const EffectProgramError& Error() const { return error_; }
private:
    EffectProgramError error_;
};

// Immutable, validated restricted program. Native DLL execution remains a separate
// trusted developer tier. Compilation never publishes or replaces another program.
class EffectProgram {
public:
    static constexpr uint32_t MaxInstructions = 2048, MaxExpressions = 4096;
    static constexpr uint32_t MaxVariables = 64, MaxSlots = 64, FrameBudget = 4096;
    static constexpr size_t MaxJsonBytes = 1024 * 1024;
    static std::shared_ptr<const EffectProgram> Compile(const nlohmann::json& document);
    static std::shared_ptr<const EffectProgram> Parse(const std::string& document);
    bool UsesKeyboard() const;
    bool UsesGsi() const;
    bool UsesAudio() const;
    bool UsesScreen() const;
    bool UsesHall() const;
    uint32_t InstructionCount() const;
    struct Impl;
private:
    explicit EffectProgram(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<const Impl> impl_;
    friend class ProgramEffect;
};

struct EffectProgramStatus {
    bool faulted{false}, yielded{false}, waiting{false}, terminal{false};
    bool keyboard_available{false};
    bool audio_available{false};
    bool screen_available{false};
    bool hall_available{false};
    uint32_t instructions{0}, work_units{0}, pc{0};
    EffectProgramError error;
};

// Per-instance state; caller owns admission limits and stable Keymap lifetime.
// Render resolves the standard layout once per Keymap object, then uses bounded
// arrays and no interpreter-owned heap allocations on the successful hot path.
class ProgramEffect final : public Effect {
public:
    explicit ProgramEffect(std::shared_ptr<const EffectProgram> program,
                           const AuraLogicalKeyboardHostV1* keyboard = nullptr,
                           const AuraHallAnalogHostV1* hall = nullptr);
    ~ProgramEffect() override;
    ProgramEffect(const ProgramEffect&) = delete;
    ProgramEffect& operator=(const ProgramEffect&) = delete;
    void Render(uint64_t elapsed_ms, FrameBuffer& output, const Keymap& keymap) override;
    void RenderWithContext(const EffectContext& context, FrameBuffer& output) override;
    void Reset();
    bool Finished() const;
    float Opacity() const;
    const std::bitset<TOTAL_LEDS>& Coverage() const;
    const EffectProgramStatus& Status() const;
    const std::shared_ptr<const EffectProgram>& Program() const { return program_; }
    uint64_t GenerationId() const;
    void SetGenerationId(uint64_t generation_id);
private:
    struct Runtime;
    std::unique_ptr<Runtime> runtime_;
    std::shared_ptr<const EffectProgram> program_;
};
} // namespace aura
