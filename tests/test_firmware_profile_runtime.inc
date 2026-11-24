// Linked production Profile Runtime/coordinator and shared mutation gate.
// Only the existing synthetic Io is reachable; isolated fixture documents.
namespace {
bool TestFirmwareProfileAdmission() {
    for (const bool hardware : {false, true}) {
        Phase4BFixture f(false, "cs2.exe", hardware, true);
        const auto revision = f.profiles->State().at("document_revision").get<int64_t>();
        const auto original = Json::parse(std::ifstream(f.path));
        f.io->firmware = {aura::m605::FirmwareState::Unsupported, "1.00.58", "FirmwareUnsupported: synthetic unverified firmware"};
        int frames = 0;
        if (!f.profiles->WithAuraLightingOwnership([&] { ++frames; return true; })) return false;
        bool rejected = false;
        try { f.profiles->ActivateProfile(B, "Manual", revision); }
        catch (const std::exception& error) { rejected = std::string(error.what()).find("FirmwareUnsupported") != std::string::npos; }
        if (!rejected || Json::parse(std::ifstream(f.path)) != original || f.io->stages || f.io->applies || f.latch->arms || f.latch->armed)
            return false;
        if (!f.profiles->WithAuraLightingOwnership([&] { ++frames; return true; }) || frames != 2) return false;
        httplib::Server server; f.profiles->RegisterRoutes(server);
        const auto port=server.bind_to_any_port("127.0.0.1"); if(port<=0) return false;
        std::thread serving([&] { server.listen_after_bind(); });
        bool api_denied=false;
        try {
            httplib::Client client("127.0.0.1",port);
            const auto response=client.Post("/api/device-profiles/activate",Json{{"profile_id",B},{"reason","Manual"},{"expected_revision",revision}}.dump(),"application/json");
            api_denied=response && response->status==409 && Json::parse(response->body).at("error_code")=="FirmwareAdmissionDenied";
        } catch(...) { api_denied=false; }
        server.stop(); serving.join();
        if(!api_denied || f.io->stages || f.io->applies || f.latch->arms || Json::parse(std::ifstream(f.path))!=original) return false;
        // Real production foreground coordinator must not bypass the same gate.
        f.Stable("cs2.exe");
        if (f.io->stages || f.io->applies || f.latch->arms || Json::parse(std::ifstream(f.path)) != original ||
            !f.profiles->State().at("active_profile_id").is_null()) return false;
        for (int repeat=0; repeat<30; ++repeat) {
            std::atomic<bool> profile_rejected{false}, manual_rejected{false};
            std::thread profile([&] {
                try { f.profiles->ActivateProfile(B, "Manual", revision); }
                catch(const std::exception& error) { profile_rejected=std::string(error.what()).find("FirmwareUnsupported")!=std::string::npos; }
            });
            std::thread manual([&] {
                std::lock_guard<std::mutex> lock(f.gate);
                manual_rejected=!f.device->SetPerKeyActuation(0x0402,1).get();
            });
            profile.join(); manual.join();
            if (!profile_rejected || !manual_rejected || f.io->stages || f.io->applies || f.latch->arms ||
                Json::parse(std::ifstream(f.path)) != original) return false;
        }
        // A subsequent genuinely supported observation restores normal admission.
        f.io->firmware = SupportedM605FirmwareFixture();
        const auto supported = f.profiles->ActivateProfile(B, "Manual", revision);
        if (supported.at("outcome") != "succeeded" || supported.at("active_profile_id") != B || f.io->stages == 0) return false;
    }
    return true;
}
}
