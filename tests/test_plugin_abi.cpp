#include "engine/plugin_manager.h"
#include "third_party/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <atomic>

namespace fs = std::filesystem;
using aura::TriggeredEffectInstance;
namespace {
unsigned checks = 0;
void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void frame_is(const TriggeredEffectInstance& instance, unsigned char expected) {
    require(bool(instance), "factory returned no instance");
    aura::FrameBuffer frame;
    aura::Keymap keymap;
    instance.GetEffect()->Render(0, frame, keymap);
    require(frame.buffer[0] == expected, "wrong generation rendered");
}
void lifecycle_is(const TriggeredEffectInstance& instance, unsigned expected) {
    const auto& caps = instance.GetGeneration()->lifecycle;
    require(caps.version == 1 && caps.is_finished && caps.get_opacity, "full lifecycle missing");
    require(caps.is_finished(instance.GetEffect().get(), expected - 1) == 0, "finished too soon/wrong DLL");
    require(caps.is_finished(instance.GetEffect().get(), expected) == 1, "completion/wrong DLL");
    require(std::abs(caps.get_opacity(instance.GetEffect().get(), 0) - expected / 100.0f) < 0.0001f,
            "opacity paired with wrong object generation");
}
void install(const fs::path& generated, const std::string& name, const fs::path& destination) {
    fs::copy_file(generated / (name + ".dll"), destination, fs::copy_options::overwrite_existing);
}
void frozen_fixtures(const fs::path& frozen) {
    nlohmann::json manifest;
    std::ifstream(frozen / "manifest.json") >> manifest;
    for (const auto& item : manifest.at("binaries")) {
        aura::PluginManager manager;
        auto instance = manager.LoadPluginInstance((frozen / item.at("file").get<std::string>()).string());
        const auto raw = item.at("expected_raw_version");
        const bool accepted = raw.is_null() || raw == 1 || raw == 65536;
        require(bool(instance) == accepted, "frozen ABI decision mismatch");
        if (instance) {
            require(instance.GetGeneration()->normalized_version == aura::PluginAbiVersion::V1_0, "normalization");
            require(instance.GetGeneration()->version_export_present == !raw.is_null(), "absence metadata");
            require(instance.GetGeneration()->api_version == (raw.is_null() ? 1u : raw.get<unsigned>()), "raw metadata");
            require(!instance.GetGeneration()->lifecycle.is_finished, "old DLL gained lifecycle");
            aura::FrameBuffer frame;
            aura::Keymap keymap;
            aura::EffectContext ctx(0, keymap);
            instance.GetEffect()->RenderWithContext(ctx, frame);
            const auto filename = item.at("file").get<std::string>();
            if (filename.find("studio") != std::string::npos) {
                require(frame.buffer[0] == 0 && frame.buffer[1] == 150 && frame.buffer[2] == 255,
                        "prebuilt Studio context ABI/output changed");
            } else if (filename.find("stress") != std::string::npos) {
                require(frame.buffer[0] == 0 && frame.buffer[1] == 100 && frame.buffer[2] == 200,
                        "prebuilt stress context ABI/output changed");
            } else require(frame.buffer[0] == 17, "prebuilt/synthetic fixture output changed");
        }
        std::cout << "EXERCISED " << item.at("file") << " [" << item.at("classification") << "] "
                  << (accepted ? "accepted" : "rejected") << '\n';
    }
    require(fs::is_empty("plugins/.cache"), "frozen fixtures leaked modules");
}

void capabilities(const fs::path& generated) {
    struct Case { const char* name; bool accepted; bool finished; bool opacity; };
    const Case cases[] = {
        {"abi_full_old", true, true, true}, {"abi_full_new", true, true, true},
        {"abi_unprefixed", true, true, true}, {"abi_absent", true, true, true},
        {"abi_finished_only", true, true, false}, {"abi_opacity_only", true, false, false},
        {"abi_no_lifecycle", true, false, false}, {"abi_lifecycle_unknown", true, false, false},
        {"abi_lifecycle_no_revision", true, false, false},
        {"abi_zero", false, false, false}, {"abi_two", false, false, false},
        {"abi_minor", false, false, false}, {"abi_missing_destroy", false, false, false},
        {"abi_factory_null", false, false, false}, {"abi_factory_throw", false, false, false},
        {"abi_version_throw", false, false, false}
    };
    for (const auto& c : cases) {
        aura::PluginManager manager;
        auto instance = manager.LoadPluginInstance((generated / (std::string(c.name) + ".dll")).string());
        require(bool(instance) == c.accepted, std::string(c.name) + " acceptance");
        require(manager.HasPlugin("abi_fixture") == c.accepted, "failed candidate published");
        if (instance) {
            const auto& caps = instance.GetGeneration()->lifecycle;
            require(bool(caps.is_finished) == c.finished && bool(caps.get_opacity) == c.opacity, "capability negotiation");
            require(instance.GetGeneration()->handle->GetDestroyFn() == instance.GetGeneration()->destroy_fn,
                    "generation destroy pointer inconsistent");
            auto fresh = manager.CreateEffectInstance("abi_fixture");
            require(fresh.GetGeneration() == instance.GetGeneration(), "factory metadata detached from registry generation");
            require(fresh.GetEffect().get() != instance.GetEffect().get(), "factory shared a mutable instance");
        }
        std::cout << "CAPABILITY " << c.name << " accepted=" << c.accepted << '\n';
    }
    require(fs::is_empty("plugins/.cache"), "capability rejection leaked module");
}

void reject_before_plugin_calls(const fs::path& generated) {
    aura::PluginManager manager;
    // Configure only this process's synthetic fixtures. No installed DLL is run.
    const auto trace = fs::absolute("export-trace.txt");
    require(SetEnvironmentVariableA("AURA_PLUGIN_FIXTURE_TRACE", trace.string().c_str()) != 0, "trace setup");
    auto rejected = manager.LoadPluginInstance((generated / "abi_two.dll").string());
    SetEnvironmentVariableA("AURA_PLUGIN_FIXTURE_TRACE", nullptr);
    require(!rejected, "unsupported ABI accepted");
    std::ifstream input(trace);
    std::stringstream contents;
    contents << input.rdbuf();
    require(contents.str() == "version\n", "unknown version reached name/lifecycle/create export");
}

void reload_and_aliases(const fs::path& generated, const fs::path& frozen) {
    unsigned destroyed = 0, unloaded = 0;
    aura::PluginManager manager;
    const fs::path installed = "plugins/effect_installed.dll";
    install(generated, "abi_full_old", installed);
    auto old = manager.LoadPluginInstance("installed");
    frame_is(old, 17); lifecycle_is(old, 17);
    const auto old_path = old.GetGeneration()->handle->GetShadowPath();
    using Observe = void (*)(unsigned*, unsigned*);
    auto observe = reinterpret_cast<Observe>(GetProcAddress(old.GetGeneration()->handle->GetModule(), "FixtureObserve"));
    require(observe != nullptr, "fixture observer missing");
    observe(&destroyed, &unloaded);

    install(generated, "abi_full_new", installed);
    require(manager.ReloadPlugin("effect_installed"), "valid reload failed");
    auto current = manager.CreateEffectInstance("abi_fixture");
    frame_is(current, 93); lifecycle_is(current, 93);
    frame_is(old, 17); lifecycle_is(old, 17);
    require(current.GetGeneration()->generation_id != old.GetGeneration()->generation_id, "generation ID reused");
    require(manager.CreateEffectInstance("installed").GetGeneration() == current.GetGeneration(), "stale short alias");
    require(manager.CreateEffectInstance("effect_installed").GetGeneration() == current.GetGeneration(), "stale stem alias");

    for (const char* bad : {"abi_zero", "abi_two", "abi_minor", "abi_missing_destroy", "abi_factory_null",
                            "abi_factory_throw", "abi_version_throw", "abi_destroy_throw"}) {
        install(generated, bad, installed);
        require(!manager.ReloadPlugin("installed"), std::string("reload accepted ") + bad);
        require(manager.CreateEffectInstance("installed").GetGeneration() == current.GetGeneration(), "failed reload replaced registry");
        frame_is(current, 93); lifecycle_is(old, 17);
    }
    fs::copy_file(frozen / "binaries/synthetic_unknown_abi.dll", installed, fs::copy_options::overwrite_existing);
    require(!manager.ReloadPlugin("installed"), "unknown frozen version reload accepted");
    { std::ofstream stream(installed, std::ios::binary | std::ios::trunc); stream << "not a DLL"; }
    require(!manager.ReloadPlugin("installed"), "corrupt reload accepted");
    require(!manager.LoadPluginInstance("installed"), "failed explicit load accepted");
    require(manager.CreateEffectInstance("installed").GetGeneration() == current.GetGeneration(), "failed load overwrote old");

    // Distinct source, identical canonical name: reject every candidate alias atomically.
    install(generated, "abi_full_old", "plugins/effect_intruder.dll");
    require(!manager.LoadPluginInstance("intruder"), "canonical-name collision accepted");
    require(!manager.HasPlugin("intruder") && !manager.HasPlugin("effect_intruder"), "partial collision publication");
    require(manager.CreateEffectInstance("abi_fixture").GetGeneration() == current.GetGeneration(), "collision stole alias");

    // Same source may rename its export; every previously published alias follows.
    install(generated, "abi_renamed", installed);
    require(manager.ReloadPlugin("installed"), "same-source rename failed");
    auto renamed = manager.CreateEffectInstance("renamed_fixture");
    require(manager.CreateEffectInstance("abi_fixture").GetGeneration() == renamed.GetGeneration(), "old export alias left stale");
    // Different export name but filename-stem alias collides with the renamed source.
    install(generated, "abi_other", "plugins/abi_fixture.dll");
    require(!manager.LoadPluginInstance("plugins/abi_fixture.dll"), "stem collision accepted");
    require(!manager.HasPlugin("other_fixture"), "collision published canonical name partially");

    // Keep only an Effect copy and even a weak reference: its deleter must pin the
    // old module through DestroyEffect, then allow unload despite surviving weak_ptr.
    auto detached = old.GetEffect();
    std::weak_ptr<aura::Effect> weak = detached;
    old = {};
    require(destroyed == 0 && unloaded == 0 && fs::exists(old_path), "old generation unloaded too early");
    detached.reset();
    require(destroyed == 1 && unloaded == 1 && !fs::exists(old_path), "destroy-before-unload/weak lifetime broken");
    require(weak.expired(), "effect survived last strong owner");
}

void manager_lifetime(const fs::path& generated) {
    TriggeredEffectInstance survivor;
    fs::path shadow;
    {
        aura::PluginManager manager;
        survivor = manager.LoadPluginInstance((generated / "abi_full_old.dll").string());
        shadow = survivor.GetGeneration()->handle->GetShadowPath();
    }
    frame_is(survivor, 17); lifecycle_is(survivor, 17);
    require(fs::exists(shadow), "manager destruction unloaded live sidecar");
    // Metadata-only ownership also keeps callbacks' module loaded.
    auto metadata = survivor.GetGeneration();
    survivor = {};
    require(fs::exists(shadow), "metadata did not pin generation");
    metadata.reset();
    require(!fs::exists(shadow), "metadata release leaked generation");
}

void concurrent_factory_reload(const fs::path& generated) {
    aura::PluginManager manager;
    const fs::path path = "plugins/effect_concurrent.dll";
    install(generated, "abi_full_old", path);
    require(bool(manager.LoadPluginInstance("concurrent")), "concurrency initial load");
    std::atomic<bool> stop{false};
    std::atomic<unsigned> reads{0}, mismatches{0};
    std::thread reader([&] {
        while (!stop.load()) {
            auto instance = manager.CreateEffectInstance("concurrent");
            if (!instance) { ++mismatches; continue; }
            aura::FrameBuffer frame;
            aura::Keymap keymap;
            instance.GetEffect()->Render(0, frame, keymap);
            const auto& caps = instance.GetGeneration()->lifecycle;
            const auto value = frame.buffer[0];
            if ((value != 17 && value != 93) || !caps.get_opacity || !caps.is_finished ||
                std::abs(caps.get_opacity(instance.GetEffect().get(), 0) - value / 100.0f) > 0.0001f ||
                caps.is_finished(instance.GetEffect().get(), value) != 1) ++mismatches;
            ++reads;
        }
    });
    bool reloads_ok = true;
    try {
        while (reads.load() == 0) std::this_thread::yield();
        for (int i = 0; i < 8; ++i) {
            install(generated, i % 2 == 0 ? "abi_full_new" : "abi_full_old", path);
            if (!manager.ReloadPlugin("concurrent")) reloads_ok = false;
        }
    } catch (...) {
        stop = true;
        reader.join();
        throw;
    }
    stop = true;
    reader.join();
    require(reloads_ok && reads > 0 && mismatches == 0, "concurrent factory detached object from its generation");
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    const auto generated = fs::absolute(argv[1]), frozen = fs::absolute(argv[2]);
    const auto previous = fs::current_path();
    const auto sandbox = fs::temp_directory_path() /
        ("aura-stage1-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    int result = 0;
    try {
        fs::create_directories(sandbox);
        fs::current_path(sandbox);
        frozen_fixtures(frozen);
        capabilities(generated);
        reject_before_plugin_calls(generated);
        reload_and_aliases(generated, frozen);
        require(fs::is_empty("plugins/.cache"), "reload tests leaked generations");
        manager_lifetime(generated);
        concurrent_factory_reload(generated);
        require(fs::is_empty("plugins/.cache"), "survivor leaked generation");
        std::cout << "PASS: " << checks << " ABI/factory/lifetime assertions\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " assertions: " << error.what() << '\n';
        result = 1;
    }
    fs::current_path(previous);
    // Only this test's unique, absolute temporary directory is removed.
    if (sandbox.parent_path() == fs::temp_directory_path() && sandbox.filename().wstring().find(L"aura-stage1-") == 0) {
        std::error_code ec;
        fs::remove_all(sandbox, ec);
    }
    return result;
}
