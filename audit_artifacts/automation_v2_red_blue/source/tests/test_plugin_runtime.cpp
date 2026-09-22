#include "engine/plugin_manager.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void expect_frame(const std::shared_ptr<aura::Effect>& effect, unsigned char value) {
    require(bool(effect), "effect factory returned null");
    aura::FrameBuffer frame;
    aura::Keymap keymap;
    effect->Render(0, frame, keymap);
    require(frame.buffer[0] == value, "DLL rendered the wrong version");
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    const auto v1 = fs::absolute(argv[1]);
    const auto v2 = fs::absolute(argv[2]);
    const auto previous = fs::current_path();
    const auto sandbox = fs::temp_directory_path() /
        ("aura-plugin-regression-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    int result = 0;
    try {
        fs::create_directories(sandbox);
        fs::current_path(sandbox);
        // Call the real DLL exports, not a locally assigned Python constant.
        HMODULE module = LoadLibraryW(v1.c_str());
        require(module != nullptr, "fixture DLL did not load");
        auto version = reinterpret_cast<PfnAuraGetPluginApiVersion>(GetProcAddress(module, "AuraGetPluginApiVersion"));
        const bool abi_ok = version && version() == 1 && version() == AURA_PLUGIN_API_VERSION;
        FreeLibrary(module);
        require(abi_ok, "plugin ABI export disagrees with the supported v1 contract");
        {
            aura::PluginManager manager;
            const auto installed = fs::path("plugins/effect_reload_fixture.dll");
            fs::copy_file(v1, installed);
            auto old_effect = manager.LoadPlugin("reload_fixture");
            expect_frame(old_effect, 17);
            require(!fs::is_empty("plugins/.cache"), "manager did not create a shadow DLL");

            // Replacing the original must work while the old instance is alive.
            fs::copy_file(v2, installed, fs::copy_options::overwrite_existing);
            require(manager.ReloadPlugin("reload_fixture"), "reload rejected valid v2");
            auto new_effect = manager.CreateEffect("reload_fixture");
            expect_frame(new_effect, 93);
            expect_frame(old_effect, 17);

            // Failed replacement must preserve both the registered v2 and old v1 instance.
            { std::ofstream corrupt(installed, std::ios::binary | std::ios::trunc); corrupt << "not a DLL"; }
            require(!manager.ReloadPlugin("reload_fixture"), "corrupt reload unexpectedly succeeded");
            expect_frame(manager.CreateEffect("reload_fixture"), 93);
            expect_frame(old_effect, 17);
            expect_frame(new_effect, 93);
        }
        require(fs::is_empty("plugins/.cache"), "shadow DLLs leaked after manager/instances were destroyed");
        std::cout << "PASS: real DLL ABI, shadow load, version replacement, old instance lifetime, failed reload, cleanup\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    fs::current_path(previous);
    std::error_code ec;
    fs::remove_all(sandbox, ec);
    return result;
}
