// Deliberately does NOT implement the optional firmware identity transport seam.
// Compiles with the pre-Gate baseline too, where it reproduces unintended writes.
#include "aura/hardware/m605_runtime.h"
#include <iostream>
namespace aura {
struct M605RuntimeTestAccess {
    static std::unique_ptr<M605Runtime> Create(std::unique_ptr<m605::detail::Transport> io,
        std::unique_ptr<m605::detail::SettleWait> wait,
        std::unique_ptr<m605::detail::SafetyLatch> latch) {
        return std::unique_ptr<M605Runtime>(new M605Runtime(std::move(io),std::move(wait),std::move(latch)));
    }
};
}
namespace {
struct Io final : aura::m605::detail::Transport {
    bool connected=false; int stages=0, applies=0;
    bool IsConnected() const override { return connected; }
    bool IsCurrentSession() const override { return connected; }
    bool Connect() override { connected=true; return true; }
    bool WriteStage(const aura::m605::Report&) override { ++stages; return true; }
    bool WriteApply(const aura::m605::Report&) override { ++applies; return true; }
    void Disconnect() override { connected=false; }
    std::string GetLastError() const override { return "synthetic transport with no firmware authority"; }
};
struct Wait final : aura::m605::detail::SettleWait {
    void WaitBetweenDksStages() override {} void WaitBeforeApply() override {} void WaitAfterApply() override {}
};
struct Latch final : aura::m605::detail::SafetyLatch {
    bool armed=false; int arms=0;
    bool IsQuarantined() const override { return armed; }
    bool Arm() override { armed=true; ++arms; return true; }
    bool Clear() override { armed=false; return true; }
};
}
int main() {
    auto io=std::make_unique<Io>(); auto* transport=io.get();
    auto latch=std::make_unique<Latch>(); auto* safety=latch.get();
    auto runtime=aura::M605RuntimeTestAccess::Create(std::move(io),std::make_unique<Wait>(),std::move(latch));
    const bool accepted=runtime->SetPerKeyActuation(0x0402,1).get(); runtime->Stop();
    std::cout<<"unknown-seam accepted="<<accepted<<" stage="<<transport->stages<<" apply="<<transport->applies<<" latch_arms="<<safety->arms<<'\n';
    return !accepted && !transport->stages && !transport->applies && !safety->arms &&
        runtime->GetAppliedRuntimeState().per_key_actuation_raw.empty() ? 0 : 1;
}
