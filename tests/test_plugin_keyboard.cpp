#include "engine/plugin_manager.h"
#include "monitor/key_input_hub.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstring>

namespace fs = std::filesystem;
namespace {
unsigned checks = 0;
void Require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
aura::FrameBuffer Render(const aura::TriggeredEffectInstance& instance) {
    Require(bool(instance), "missing effect instance");
    aura::FrameBuffer frame;
    aura::Keymap keymap;
    instance.GetEffect()->Render(0, frame, keymap);
    return frame;
}
using Counts = void (*)(unsigned*, unsigned*, unsigned*);
}

int main(int argc, char** argv) {
    try {
        Require(argc == 3, "expected fixture directory and task-local scratch root");
        const auto fixture_dir = fs::absolute(argv[1]);
        const auto scratch = fs::absolute(argv[2]) / ("keyboard-" + std::to_string(GetCurrentProcessId()));
        fs::create_directories(scratch / "plugins");
        fs::current_path(scratch);
        auto& hub = aura::KeyInputHub::Instance();
        hub.SetSourceAvailable(false); hub.Reset();
        AuraLogicalKeyboardSnapshotV1 snapshot{};
        hub.ReadLogicalSnapshot(snapshot);
        Require(!snapshot.source_available && !snapshot.count, "unavailable source must stay unknown");

        aura::TriggeredEffectInstance retained;
        {
            aura::PluginManager manager;
            const auto installed = fs::absolute("plugins/effect_keyboard.dll");
            fs::copy_file(fixture_dir / "keyboard_valid.dll", installed);
            auto first = manager.LoadPluginInstance("keyboard");
            Require(bool(first) && first.GetGeneration()->logical_keyboard.version == 1, "keyboard contract not negotiated");
            Require(first.GetGeneration()->lifecycle.is_finished(first.GetEffect().get(), 0) == 0,
                    "binder or lifecycle received a wrapper instead of raw factory object");
            Require(Render(first).buffer[1] == 0, "unavailable source became fabricated held state");
            hub.SetSourceAvailable(true);
            hub.RecordKeyEvent("W", aura::KeyEventType::Down);
            const auto down = Render(first);
            Require(down.buffer[0] == 200 && down.buffer[1] == 1 && down.buffer[2] == 17, "real DLL did not receive synthetic Down");
            hub.RecordKeyEvent("W", aura::KeyEventType::Up);
            const auto up = Render(first);
            Require(up.buffer[0] == 80 && up.buffer[3] == 1, "real DLL did not receive synthetic Up/timing");
            hub.ReadLogicalSnapshot(snapshot);
            Require(snapshot.struct_size == sizeof(snapshot) && snapshot.version == 1 && snapshot.count == 1 &&
                    std::strcmp(snapshot.keys[0].name, "W") == 0 && snapshot.sequence == snapshot.keys[0].up_sequence,
                    "named coherent snapshot contract");
            const auto epoch = snapshot.source_epoch;
            auto counts = reinterpret_cast<Counts>(GetProcAddress(first.GetGeneration()->handle->GetModule(), "KeyboardFixtureCounts"));
            Require(counts != nullptr, "fixture counts missing");
            unsigned creates, destroys, reads;
            counts(&creates, &destroys, &reads);
            Require(creates == 1 && destroys == 0 && reads == 3, "fixture must copy exactly one snapshot per render");
            auto second = manager.CreateEffectInstance("keyboard");
            Require(second.GetEffect().get() != first.GetEffect().get() &&
                    second.GetGeneration()->lifecycle.is_finished(second.GetEffect().get(), 0) == 0, "binding must be per raw instance");

            for (const char* bad : {"keyboard_unknown", "keyboard_no_bind", "keyboard_no_version", "keyboard_version_throw",
                                    "keyboard_bind_reject", "keyboard_bind_throw"}) {
                fs::copy_file(fixture_dir / (std::string(bad) + ".dll"), installed, fs::copy_options::overwrite_existing);
                Require(!manager.ReloadPlugin("keyboard"), "invalid keyboard extension accepted");
                Require(manager.GetGeneration("keyboard") == first.GetGeneration(), "failed extension replaced prior generation");
                Require(Render(first).buffer[0] == 80, "failed reload invalidated old instance callback");
            }
            for (const char* bad : {"keyboard_bind_reject", "keyboard_bind_throw"}) {
                auto source = manager.PrepareEffectGeneration((fixture_dir / (std::string(bad) + ".dll")).string());
                Require(bool(source), "expected a valid export declaration before binder failure");
                auto failed = aura::PluginManager::CreateFromGeneration(source);
                Require(!failed, "binder failure retained an effect instance");
                auto failed_counts = reinterpret_cast<Counts>(GetProcAddress(source->handle->GetModule(), "KeyboardFixtureCounts"));
                Require(failed_counts != nullptr, "binder failure counters missing");
                failed_counts(&creates, &destroys, &reads);
                Require(creates == 1 && destroys == 1, "binder rejection/throw leaked the raw factory object");
            }
            fs::copy_file(fixture_dir / "keyboard_new.dll", installed, fs::copy_options::overwrite_existing);
            Require(manager.ReloadPlugin("keyboard"), "valid keyboard reload rejected");
            auto current = manager.CreateEffectInstance("keyboard");
            Require(current.GetGeneration() != first.GetGeneration() && Render(current).buffer[2] == 93 && Render(first).buffer[2] == 17,
                    "new/old keyboard generation binding mixed");
            hub.SetSourceAvailable(false);
            Require(Render(current).buffer[1] == 0 && Render(first).buffer[1] == 0, "source stop retained known keyboard state");
            hub.SetSourceAvailable(true);
            hub.ReadLogicalSnapshot(snapshot);
            Require(snapshot.source_epoch > epoch && snapshot.count == 0, "source restart replayed stale release state");
            hub.RecordKeyEvent("W", aura::KeyEventType::Down);
            retained = first;

            auto old_abi = manager.LoadPluginInstance((fixture_dir / "plugin_fixture_v1.dll").string());
            Require(bool(old_abi) && !old_abi.GetGeneration()->logical_keyboard.bind && Render(old_abi).buffer[0] == 17,
                    "ABI-v1 plugin without extension stopped loading");
        }
        Require(Render(retained).buffer[0] == 200, "host callback/generation expired when manager retired");
        hub.Reset();
        Require(Render(retained).buffer[0] == 0, "reset replayed held/release state");
        hub.RecordKeyEvent("L_WIN", aura::KeyEventType::Down);
        hub.ReadLogicalSnapshot(snapshot);
        hub.RecordKeyEvent("COPILOT", aura::KeyEventType::Down);
        hub.RecordKeyEvent("L_WIN", aura::KeyEventType::Up);
        hub.ReadLogicalSnapshot(snapshot);
        const auto consumed_win = std::find_if(snapshot.keys, snapshot.keys + snapshot.count,
            [](const auto& state) { return std::strcmp(state.name, "L_WIN") == 0; });
        Require(consumed_win != snapshot.keys + snapshot.count && consumed_win->has_release && !consumed_win->held,
                "snapshot consumption lost the paired Copilot-prefix Up");
        hub.Reset();
        for (int i = 0; i <= 128; ++i) {
            hub.RecordKeyEvent("K" + std::to_string(i), aura::KeyEventType::Down);
            hub.RecordKeyEvent("K" + std::to_string(i), aura::KeyEventType::Up);
        }
        hub.ReadLogicalSnapshot(snapshot);
        Require(!snapshot.source_available && snapshot.count == 0, "snapshot overflow silently truncated known inputs");
        hub.Reset();
        hub.ReadLogicalSnapshot(snapshot);
        Require(snapshot.source_available && snapshot.count == 0, "explicit source reset did not recover bounded snapshot");
        hub.SetSourceAvailable(false);
        retained = {};
        Require(fs::is_empty("plugins/.cache"), "keyboard generations leaked shadow modules");
        std::cout << "PASS: " << checks << " checks; real DLL keyboard binding, source epochs, failure rollback, old ABI\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
