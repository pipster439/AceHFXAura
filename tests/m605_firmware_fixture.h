#pragma once
#include "aura/hardware/m605_protocol.h"

// SYNTHETIC_TEST: representative already-validated M605 1.00.59 BasicInfo.
// Tests explicitly opt in; an unimplemented Transport identity seam stays Unknown.
inline aura::m605::FirmwareCompatibility SupportedM605FirmwareFixture() {
    aura::m605::Report report{};
    report[1] = 0x12; report[5] = 0x59; report[7] = 1;
    report[9] = 6; report[11] = 1;
    return aura::m605::ParseBasicInfoFirmware(report.data(), report.size());
}
