// OFFLINE ONLY: mock GetRawInputData results and in-memory ABI fixtures.
// No HWND, RegisterRawInputDevices, WM_INPUT injection or OS input acquisition.
#ifdef _WIN32
#include <windows.h>
#endif
#include "input_delivery.h"
#include <iostream>
#include <stdexcept>
#include <utility>
namespace {
using namespace stage_b;
#define VERIFY(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
const InputLayout layout =
#ifdef _WIN32
    {sizeof(RAWINPUTHEADER), offsetof(RAWINPUTHEADER, hDevice), sizeof(HANDLE),
     offsetof(RAWINPUT, data.hid.dwSizeHid), offsetof(RAWINPUT, data.hid.dwCount), offsetof(RAWINPUT, data.hid.bRawData)};
#else
    {24, 8, 8, 24, 28, 32}; // Synthetic x64 ABI; no Win32 claim on other hosts.
#endif
template<class T> void put(std::vector<uint8_t>& data, size_t offset, T value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}
std::array<uint8_t, 21> rt(uint8_t state) {
    std::array<uint8_t, 21> bytes{}; bytes[0] = 3; bytes[1] = 0x76; bytes[4] = state; return bytes;
}
struct MockSource : InputSource {
    std::vector<uint8_t> packet;
    InputHeader h;
    ReadResult hr, sr, cr;
    unsigned header_calls{}, size_calls{}, copy_calls{};
    explicit MockSource(const std::array<uint8_t,21>& report, size_t padding = 0) {
        packet.resize(layout.data_offset + report.size() + padding);
        h = {2, uint32_t(packet.size()), 1};
        hr = {uint32_t(layout.header_bytes),uint32_t(layout.header_bytes),0};
        sr = {0,h.bytes,0}; cr = {h.bytes,h.bytes,0};
        put(packet,0,uint32_t(2)); put(packet,4,h.bytes);
        std::memcpy(packet.data()+layout.device_offset,&h.device,layout.device_bytes);
        put(packet,layout.hid_size_offset,uint32_t(report.size())); put(packet,layout.hid_count_offset,uint32_t(1));
        std::copy(report.begin(),report.end(),packet.begin()+layout.data_offset);
    }
    ReadResult header(InputHeader& out) override { ++header_calls; out = h; return hr; }
    ReadResult size() override { ++size_calls; return sr; }
    ReadResult copy(void* buffer, uint32_t capacity) override {
        ++copy_calls; std::memcpy(buffer,packet.data(),std::min(size_t(capacity),packet.size())); return cr;
    }
};
Observation arm() { Observation o; o.arm(Scope::Notifications,cases.back(),1,7,100,1000); return o; }
bool receive(InputDelivery& d, MockSource& s, Observation& o, uint64_t generation = 7, uint64_t now = 101) {
    return d.receive(s,layout,1,generation,now,o);
}
}
unsigned run_delivery_tests() {
    unsigned passed = 0;
    const auto test = [&passed](const char* name, auto fn) { fn(); ++passed; std::cout << "PASS " << name << '\n'; };
    test("known_RT76_contract_through_mock_acquisition", [] {
        auto o=arm(); InputDelivery d; MockSource on(rt(1)), off(rt(0));
        VERIFY(receive(d,on,o)); VERIFY(o.rt_state()==FreshState::On);
        VERIFY(receive(d,off,o,7,103)); VERIFY(o.rt_state()==FreshState::Off);
        VERIFY(o.counts().rt_on==1 && o.counts().rt_off==1 && o.counts().rt_changes==1);
        VERIFY(d.counts().target_headers==2 && d.counts().payload_batches==2);
        VERIFY(on.header_calls==1 && on.size_calls==1 && on.copy_calls==1);
        VERIFY(o.counts().rt_last_valid_state==0);
    });
    test("pure_RT_parser_null_truncated_and_unrelated_contract", [] {
        VERIFY(!aura::ParseHardwareRtGateReport(nullptr,21));
        auto r=rt(1); VERIFY(!aura::ParseHardwareRtGateReport(r.data(),1));
        VERIFY(aura::ParseHardwareRtGateReport(r.data(),20)==aura::HardwareRtGateState::Unknown);
        r[0]=0; VERIFY(!aura::ParseHardwareRtGateReport(r.data(),21));
    });
    test("RT76_duplicate_and_three_round_trips", [] {
        auto o=arm(); InputDelivery d; MockSource on(rt(1)),off(rt(0));
        for (int i=0;i<3;++i) { VERIFY(receive(d,on,o)); VERIFY(receive(d,off,o)); }
        VERIFY(receive(d,off,o));
        VERIFY(o.counts().rt_changes==5 && o.counts().rt_repeats==1 && o.counts().consecutive_duplicates==1);
        VERIFY(o.counts().rt_on==3 && o.counts().rt_off==4);
    });
    test("RT76_invalid_parameter_reserved_and_tail_unknown", [] {
        auto o=arm(); InputDelivery d;
        for (int variant=0;variant<4;++variant) {
            auto r=rt(variant==0 ? 2 : 1);
            if (variant==1) r[2]=1; if (variant==2) r[3]=1; if (variant==3) r[20]=1;
            MockSource s(r); VERIFY(receive(d,s,o)); VERIFY(o.rt_state()==FreshState::Unknown);
        }
        VERIFY(o.counts().rt_invalid==4 && o.counts().rt_on==0);
    });
    test("RT76_missing_transition_is_not_invented", [] {
        auto o=arm(); InputDelivery d; MockSource on(rt(1)); VERIFY(receive(d,on,o));
        VERIFY(o.tick(500)); VERIFY(o.counts().rt_off==0 && o.rt_state()==FreshState::On);
        VERIFY(!o.tick(1100)); VERIFY(o.rt_state()==FreshState::Stale && o.counts().rt_off==0);
    });
    test("RT76_unrelated_report_does_not_change_known_state", [] {
        auto o=arm(); InputDelivery d; MockSource on(rt(1)); VERIFY(receive(d,on,o));
        auto r=rt(0); r[1]=0x77; MockSource other(r); VERIFY(receive(d,other,o));
        VERIFY(o.rt_state()==FreshState::On && o.counts().rt_off==0);
    });
    test("SYNTHETIC_73_74_through_mock_acquisition", [] {
        auto o=arm(); InputDelivery d; auto r=rt(0); r[1]=0x73; r[2]=0x1f; r[4]=4;
        MockSource a(r); VERIFY(receive(d,a,o)); r[1]=0x74; MockSource b(r); VERIFY(receive(d,b,o));
        VERIFY(o.counts().marker73==1 && o.counts().marker74==1 && o.rt_state()==FreshState::Unknown);
    });
    test("non_target_and_non_HID_headers_never_fetch_payload", [] {
        for (int mode=0;mode<2;++mode) {
            auto o=arm(); InputDelivery d; MockSource s(rt(1));
            if (mode==0) s.h.device=99; else s.h.type=1;
            VERIFY(!receive(d,s,o)); VERIFY(s.size_calls==0 && s.copy_calls==0);
            VERIFY(o.active() && d.counts().other_headers==1 && o.counts().reports==0);
        }
    });
    test("stale_generation_never_fetches_target_payload", [] {
        auto o=arm(); InputDelivery d; MockSource s(rt(1)); VERIFY(!receive(d,s,o,6));
        VERIFY(d.counts().stale_session==1 && s.size_calls==0 && o.active());
    });
    test("acquisition_deadline_precedes_header_read", [] {
        auto o=arm(); InputDelivery d; MockSource s(rt(1)); VERIFY(!receive(d,s,o,7,1100));
        VERIFY(s.header_calls==0 && o.stop()==Stop::Timeout && o.raw_memory_clear());
    });
    test("header_read_failure_and_short_header", [] {
        for (auto returned : {api_error,uint32_t(layout.header_bytes-1)}) {
            auto o=arm(); InputDelivery d; MockSource s(rt(1)); s.hr={returned,uint32_t(layout.header_bytes),6};
            VERIFY(!receive(d,s,o)); VERIFY(s.size_calls==0 && d.counts().last_error==6);
            VERIFY(d.counts().failures[size_t(ReadFailure::HeaderRead)]==1 && o.stop()==Stop::Malformed);
        }
    });
    test("size_probe_error_and_size_bounds", [] {
        for (int mode=0;mode<3;++mode) {
            auto o=arm(); InputDelivery d; MockSource s(rt(1));
            if (mode==0) s.sr={api_error,0,6}; else s.sr.bytes=mode==1 ? uint32_t(layout.data_offset-1) : 65537;
            VERIFY(!receive(d,s,o)); VERIFY(s.copy_calls==0 && o.stop()==Stop::Malformed);
            VERIFY(d.counts().failures[size_t(mode==0 ? ReadFailure::SizeRead : ReadFailure::SizeBounds)]==1);
        }
    });
    test("copy_error_short_copy_and_changed_size", [] {
        for (int mode=0;mode<3;++mode) {
            auto o=arm(); InputDelivery d; MockSource s(rt(1));
            if (mode==0) s.cr.returned=api_error;
            else if (mode==1) --s.cr.returned; else --s.cr.bytes;
            VERIFY(!receive(d,s,o)); VERIFY(d.counts().failures[size_t(ReadFailure::CopyRead)]==1);
            VERIFY(o.counts().reports==0 && o.raw_memory_clear());
        }
    });
    test("mock_read_exception_closes_owned_subscription_and_resets_state", [] {
        struct Api : SubscriptionApi { unsigned removed{}; bool add() override { return true; }
            bool remove() override { ++removed; return true; } } api;
        struct Throwing : MockSource {
            Throwing() : MockSource(rt(1)) {}
            ReadResult copy(void*, uint32_t) override { throw std::runtime_error("SYNTHETIC read exception"); }
        } source;
        auto o=arm(); InputDelivery d;
        try { Subscription s(api); VERIFY(s.start(true)); receive(d,source,o); VERIFY(false); }
        catch (const std::runtime_error&) { o.finish(Stop::Exception); }
        VERIFY(api.removed==1 && !o.active() && o.raw_memory_clear() && o.rt_state()==FreshState::Stale);
    });
    test("changed_packet_header_rejected_before_report_parse", [] {
        for (auto offset : {size_t(0),size_t(4),layout.device_offset}) {
            auto o=arm(); InputDelivery d; MockSource s(rt(1)); s.packet[offset]^=1;
            VERIFY(!receive(d,s,o)); VERIFY(d.counts().failures[size_t(ReadFailure::HeaderChanged)]==1);
            VERIFY(o.counts().reports==0);
        }
    });
    test("truncated_HID_and_multiplication_overflow_rejected", [] {
        for (auto count : {uint32_t(2),api_error}) {
            auto o=arm(); InputDelivery d; MockSource s(rt(1)); put(s.packet,layout.hid_count_offset,count);
            VERIFY(!receive(d,s,o)); VERIFY(d.counts().failures[size_t(ReadFailure::HidBounds)]==1);
            VERIFY(o.counts().reports==0 && o.raw_memory_clear());
        }
    });
    test("trailing_RAWINPUT_padding_is_not_HID_data", [] {
        auto o=arm(); InputDelivery d; MockSource s(rt(1),7);
        std::fill(s.packet.end()-7,s.packet.end(),uint8_t(0x73));
        VERIFY(receive(d,s,o)); VERIFY(o.counts().rt_on==1 && o.counts().marker73==0);
        VERIFY(d.counts().padded_batches==1 && o.counts().reports==1);
    });
    test("unknown_report_id_and_wrong_length_have_separate_diagnostics", [] {
        auto o=arm(); InputDelivery d; auto r=rt(1); r[0]=0x99; MockSource s(r);
        VERIFY(!receive(d,s,o)); VERIFY(o.counts().invalid_report_id==1 && o.counts().report_ids[0x99]==1);
        o=arm(); MockSource short_report(rt(1)); put(short_report.packet,layout.hid_size_offset,uint32_t(20));
        VERIFY(!receive(d,short_report,o)); VERIFY(o.counts().invalid_length==1 && o.counts().report_lengths[20]==1);
    });
    test("zero_reports_stop_without_fabricated_state", [] {
        auto o=arm(); InputDelivery d; MockSource s(rt(1)); put(s.packet,layout.hid_count_offset,uint32_t(0));
        VERIFY(!receive(d,s,o)); VERIFY(o.counts().invalid_length==1 && o.counts().reports==0);
    });
    test("timer_clock_reversal_without_prior_report_stops", [] {
        auto o=arm(); VERIFY(o.tick(200)); VERIFY(!o.tick(199));
        VERIFY(o.stop()==Stop::ClockError && o.raw_memory_clear());
    });
    test("all_handler_entries_count_before_guard_or_active_filter", [] {
        uint64_t total{},inactive{},trial{};
        VERIFY(!note_input_entry(false,total,inactive,trial));
        VERIFY(note_input_entry(true,total,inactive,trial));
        VERIFY(total==2 && inactive==1 && trial==1);
    });
    test("message_loop_result_and_subscription_exit_cleanup", [] {
        struct Fake : SubscriptionApi { unsigned removed{}; bool add() override { return true; }
            bool remove() override { ++removed; return true; } } api;
        VERIFY(message_loop_end(0,true)==Stop::None);
        for (auto rc : {-1,0}) {
            auto o=arm(); Subscription s(api); VERIFY(s.start(true));
            const auto why=message_loop_end(rc,false); o.finish(why); VERIFY(s.close()); VERIFY(s.close());
            VERIFY(!o.active() && o.raw_memory_clear());
            VERIFY(why==(rc<0 ? Stop::MessageLoopError : Stop::MessageLoopExit));
        }
        VERIFY(api.removed==2);
    });
    test("disconnect_generation_reconnect_reset_stale_and_old_session", [] {
        auto o=arm(); InputDelivery d; MockSource s(rt(1)); VERIFY(receive(d,s,o));
        uint64_t generation=7;
        VERIFY(!advance_disconnected_session(1,99,generation) && generation==7);
        VERIFY(advance_disconnected_session(1,1,generation) && generation==8);
        o.finish(Stop::Disconnected); VERIFY(o.rt_state()==FreshState::Stale && o.raw_memory_clear());
        VERIFY(!advance_disconnected_session(0,1,generation));
        o.arm(Scope::Notifications,cases.back(),1,generation,200,1000);
        VERIFY(o.rt_state()==FreshState::Unknown && o.counts().reports==0);
        VERIFY(!receive(d,s,o,7,201) && s.copy_calls==1);
        VERIFY(receive(d,s,o,8,202) && o.rt_state()==FreshState::On);
    });
    test("SYNTHETIC_missing_or_reordered_73_74_never_become_key_state", [] {
        auto o=arm(); InputDelivery d; auto r=rt(0); r[1]=0x74; MockSource up(r);
        VERIFY(receive(d,up,o)); r[1]=0x73; MockSource down(r);
        VERIFY(receive(d,down,o)); VERIFY(receive(d,down,o));
        VERIFY(o.counts().marker74_before73==1 && o.counts().repeated73==1);
        VERIFY(o.rt_state()==FreshState::Unknown && o.counts().marker73==2);
        o.finish(Stop::Cancel); VERIFY(o.raw_memory_clear() && o.counts().marker74==1);
    });
    std::cout << "ABI FIXTURE: header=" << layout.header_bytes << " data_offset=" << layout.data_offset
              << " pointer_bytes=" << layout.device_bytes << "; MOCK ONLY\n";
    return passed;
}
