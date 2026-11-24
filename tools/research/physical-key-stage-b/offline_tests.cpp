#include "observation_core.h"
#include "startup_checks.h"
#include <utility>
#include <iostream>
#include <stdexcept>

using namespace stage_b;
unsigned run_delivery_tests();
static unsigned passed = 0;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
template<class F> void test(const char* name, F fn) { fn(); ++passed; std::cout << "PASS " << name << '\n'; }
std::array<uint8_t, 21> notification(uint8_t marker, uint16_t field = 0x1f) {
    std::array<uint8_t, 21> r{}; r[0] = 3; r[1] = marker;
    r[2] = uint8_t(field); r[3] = uint8_t(field >> 8); return r;
}
Observation armed() { Observation o; o.arm(Scope::Notifications, cases[0], 1, 2, 100, 1000); return o; }
struct FakeApi : SubscriptionApi {
    unsigned adds{}, removes{}; bool add_ok = true, remove_ok = true;
    bool add() override { ++adds; return add_ok; }
    bool remove() override { ++removes; return remove_ok; }
};
int main() {
    try {
        test("empty_registration_success_keeps_capacity_one", [] {
            CHECK(empty_registration_check({0,1,0}).ready());
        });
        test("empty_registration_success_zero_capacity", [] {
            CHECK(empty_registration_check({0,0,0}).ready());
        });
        test("actual_process_registration_still_blocks", [] {
            const auto c=empty_registration_check({1,1,0});
            CHECK(!c.ready() && std::string(c.reason)=="existing_process_registration");
        });
        test("insufficient_registration_buffer_still_blocks", [] {
            const auto c=empty_registration_check({0xffffffffu,2,122});
            CHECK(!c.ready() && c.win32_error==122 && std::string(c.api)=="GetRegisteredRawInputDevices");
        });
        test("registration_api_failure_preserves_error", [] {
            const auto c=empty_registration_check({0xffffffffu,1,5});
            CHECK(!c.ready() && c.win32_error==5 && std::string(c.reason)=="enumeration_failed");
        });
        test("shared_startup_gate_ready_with_unchanged_capacity", [] {
            CHECK(startup_check({},1,{0,1,0}).ready());
            CHECK(!startup_check({},1,{1,1,0}).ready());
        });
        test("locked_remote_or_unknown_session_gate_not_bypassed", [] {
            for (const auto why : {"session_locked_or_unknown","remote_protocol_rejected","not_active_local_console"}) {
                const StartupCheck c{"session",why,"none",0};
                const auto result=startup_check(c,1,{0,1,0});
                CHECK(!result.ready() && std::string(result.reason)==why);
            }
        });
        test("desktop_api_failure_precedes_other_startup_gates", [] {
            const StartupCheck c{"desktop","input_desktop_open_failed","OpenInputDesktop",5};
            const auto result=startup_check(c,0,{0xffffffffu,2,122});
            CHECK(!result.ready() && result.win32_error==5 && std::string(result.api)=="OpenInputDesktop");
        });
        test("missing_and_ambiguous_target_diagnostics", [] {
            CHECK(std::string(startup_check({},0,{0,1,0}).reason)=="no_matching_collection");
            CHECK(std::string(startup_check({},2,{0,1,0}).reason)=="ambiguous_matching_collections");
        });
        test("registration_snapshot_accepts_observed_devnotify_omission", [] {
            CHECK(registration_snapshot_matches(Scope::Notifications,1,1,0xffc0,1,0,77,77));
            CHECK(registration_snapshot_matches(Scope::Notifications,1,1,0xffc0,1,0x2000,77,77));
        });
        test("registration_snapshot_rejects_background_and_other_flags", [] {
            for (const auto flags : {0x100u,0x30u,0x20u,0x3000u,0xffffffffu})
                CHECK(!registration_snapshot_matches(Scope::Notifications,1,1,0xffc0,1,flags,77,77));
        });
        test("registration_snapshot_rejects_wrong_collection_or_window", [] {
            CHECK(!registration_snapshot_matches(Scope::Control,1,1,0xffc0,1,0,77,77));
            CHECK(!registration_snapshot_matches(Scope::Control,1,1,0xff00,2,0,77,77));
            CHECK(!registration_snapshot_matches(Scope::Control,1,1,0xff00,1,0,78,77));
            CHECK(!registration_snapshot_matches(Scope::Control,1,1,0xff00,1,0,0,0));
        });
        test("registration_snapshot_rejects_api_errors_and_ambiguous_counts", [] {
            CHECK(!registration_snapshot_matches(Scope::Notifications,0xffffffff,1,0xffc0,1,0,77,77));
            CHECK(!registration_snapshot_matches(Scope::Notifications,1,2,0xffc0,1,0,77,77));
            CHECK(!registration_snapshot_matches(Scope::Notifications,0,0,0xffc0,1,0,77,77));
        });
        test("synthetic_markers_are_not_press_release", [] {
            auto o = armed(); auto a = notification(0x73), b = notification(0x74);
            CHECK(o.batch(1, 2, a.data(), a.size(), 21, 1, 101));
            CHECK(o.batch(1, 2, b.data(), b.size(), 21, 1, 104));
            CHECK(o.counts().marker73 == 1 && o.counts().marker74 == 1);
            CHECK(o.counts().field_hypothesis_selected == 2);
            CHECK(o.counts().interval_sum_ms == 3);
            CHECK(o.stop() == Stop::None);
        });
        test("payload_73_is_not_header_marker", [] {
            auto o = armed(); auto r = notification(0x76); r[5] = 0x73;
            CHECK(o.batch(1, 2, r.data(), r.size(), 21, 1, 101));
            CHECK(o.counts().marker73 == 0 && o.counts().other == 1);
        });
        test("parameter_histogram_remains_a_layout_hypothesis", [] {
            auto o = armed(); auto r = notification(0x73); r[4] = 4;
            CHECK(o.batch(1, 2, r.data(), r.size(), 21, 1, 101)); r[4] = 255;
            CHECK(o.batch(1, 2, r.data(), r.size(), 21, 1, 102));
            CHECK(o.counts().parameter_hypothesis[4] == 1 && o.counts().parameter_hypothesis[5] == 1);
        });
        test("wrong_device_never_dereferences_payload", [] {
            auto o = armed(); CHECK(!o.batch(99, 2, nullptr, 999999, 0, 0, 101));
            CHECK(o.active() && o.counts().reports == 0);
        });
        test("old_generation_is_rejected", [] {
            auto o = armed(); CHECK(!o.batch(1, 1, nullptr, 999999, 0, 0, 101)); CHECK(o.active());
        });
        test("duplicate_report_counts_without_history", [] {
            auto o = armed(); auto r = notification(0x73);
            CHECK(o.batch(1, 2, r.data(), r.size(), 21, 1, 101));
            CHECK(o.batch(1, 2, r.data(), r.size(), 21, 1, 102));
            CHECK(o.counts().consecutive_duplicates == 1 && o.counts().repeated73 == 1);
            o.finish(Stop::Cancel); CHECK(o.raw_memory_clear());
        });
        test("74_before73_has_no_up_semantics", [] {
            auto o = armed(); auto r = notification(0x74, 0xffff);
            CHECK(o.batch(1, 2, r.data(), r.size(), 21, 1, 101));
            CHECK(o.counts().marker74_before73 == 1 && o.counts().field_hypothesis_other == 1);
        });
        test("batch_processes_every_report", [] {
            auto o = armed(); auto a = notification(0x73), b = notification(0x74);
            std::array<uint8_t, 42> batch{};
            std::copy(a.begin(), a.end(), batch.begin()); std::copy(b.begin(), b.end(), batch.begin()+21);
            CHECK(o.batch(1, 2, batch.data(), batch.size(), 21, 2, 101)); CHECK(o.counts().reports == 2);
        });
        test("malformed_second_report_rejects_whole_batch", [] {
            auto o = armed(); std::array<uint8_t, 42> batch{}; batch[0] = 3; batch[21] = 0;
            CHECK(!o.batch(1, 2, batch.data(), batch.size(), 21, 2, 101));
            CHECK(o.counts().reports == 0 && o.stop() == Stop::Malformed);
        });
        test("truncated_oversized_zero_and_overflow_batches", [] {
            const auto r = notification(0x73);
            for (const auto sizes : {std::pair<uint32_t,uint32_t>{21,0}, {20,1}, {21,0xffffffff}, {0xffffffff,0xffffffff}}) {
                auto o = armed(); CHECK(!o.batch(1, 2, r.data(), 21, sizes.first, sizes.second, 101));
                CHECK(o.stop() == Stop::Malformed && o.raw_memory_clear());
                CHECK(o.counts().last_batch_report_bytes == sizes.first && o.counts().last_batch_report_count == sizes.second);
            }
            auto o = armed(); CHECK(!o.batch(1,2,r.data(),65537,21,1,101));
        });
        test("clock_reversal_cancels_and_clears", [] {
            auto o = armed(); auto r = notification(0x73); CHECK(o.batch(1,2,r.data(),21,21,1,110));
            CHECK(!o.batch(1,2,r.data(),21,21,1,109)); CHECK(o.stop()==Stop::ClockError && o.raw_memory_clear());
        });
        test("deadline_checked_before_payload", [] {
            auto o = armed(); CHECK(!o.batch(1,2,nullptr,0,0,0,1100)); CHECK(o.stop()==Stop::Timeout);
        });
        test("disconnect_reset_rearm_discards_old_payload", [] {
            auto o = armed(); auto r = notification(0x73); CHECK(o.batch(1,2,r.data(),21,21,1,101));
            o.finish(Stop::Disconnected); CHECK(o.raw_memory_clear());
            o.arm(Scope::Notifications,cases[0],3,3,200,1000);
            CHECK(!o.batch(1,2,r.data(),21,21,1,201)); CHECK(o.counts().reports==0);
            CHECK(o.batch(3,3,r.data(),21,21,1,201));
        });
        test("control65_and_notification21_do_not_mix", [] {
            Observation o; o.arm(Scope::Control,cases[0],1,1,100,1000);
            std::array<uint8_t,65> r{}; r[1]=0x73; CHECK(o.batch(1,1,r.data(),65,65,1,101));
            auto n=notification(0x73); CHECK(!o.batch(1,1,n.data(),21,21,1,102)); CHECK(o.stop()==Stop::Malformed);
        });
        test("all_stop_causes_clear_short_raw_memory", [] {
            for (const auto why : {Stop::Locked,Stop::FocusLost,Stop::Disconnected,Stop::Exception,Stop::Budget}) {
                auto o=armed(); auto r=notification(0x73); CHECK(o.batch(1,2,r.data(),21,21,1,101));
                o.finish(why); CHECK(!o.active() && o.raw_memory_clear()); CHECK(!o.batch(1,2,r.data(),21,21,1,102));
            }
        });
        test("finite_report_budget_stops_before_overrun", [] {
            auto o = armed(); auto r = notification(0x73);
            for (unsigned i = 0; i < 200000; ++i) CHECK(o.batch(1,2,r.data(),21,21,1,101));
            CHECK(!o.batch(1,2,r.data(),21,21,1,101));
            CHECK(o.counts().reports == 200000 && o.stop() == Stop::Budget && o.raw_memory_clear());
        });
        test("unsafe_start_does_not_register", [] {
            FakeApi api; Subscription s(api); CHECK(!s.start(false)); CHECK(api.adds==0 && api.removes==0);
        });
        test("register_failure_has_no_owned_subscription", [] {
            FakeApi api; api.add_ok=false; Subscription s(api); CHECK(!s.start(true)); CHECK(s.close()); CHECK(api.removes==0);
        });
        test("double_start_and_double_cleanup_are_idempotent", [] {
            FakeApi api; Subscription s(api); CHECK(s.start(true)); CHECK(!s.start(true)); CHECK(s.close()); CHECK(s.close());
            CHECK(api.adds==1 && api.removes==1);
        });
        test("exception_unwinds_subscription", [] {
            FakeApi api;
            try { Subscription s(api); CHECK(s.start(true)); throw std::runtime_error("synthetic failure"); }
            catch(const std::runtime_error&) {}
            CHECK(api.adds==1 && api.removes==1);
        });
        test("unregister_failure_is_not_reported_as_success", [] {
            FakeApi api; api.remove_ok=false; Subscription s(api); CHECK(s.start(true)); CHECK(!s.close()); CHECK(!s.registered());
        });
        passed += run_delivery_tests();
    } catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
    std::cout << "OFFLINE TEST: " << passed << " passed; SYNTHETIC fixtures; PHYSICAL NOT RUN\n";
}
