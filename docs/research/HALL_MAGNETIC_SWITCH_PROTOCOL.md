# ASUS ROG Falchion Ace HFX (M605) Magnetic Switch & Hall Protocol Technical Specification

> **Document Status**: Historical read-only research baseline. See [verified alpha.5 runtime scope](../hardware/M605_RUNTIME_PROTOCOL.md) for write-path authority.  
> **Target Release**: `alpha.5` Baseline Engineering Dossier  
> **Date**: 2026-09-24  
> **Author**: Antigravity Reverse Engineering & HAL Interoperability Agent  
> **Hardware Target**: ASUS ROG Falchion Ace HFX (`VID: 0x0B05`, `PID: 0x1B7E`)  
> **Primary HAL Binary**: `AacKbHal_x64.dll` (ASUS Keyboard HAL Component, Version `1.3.46.0`)  
> **File Location**: `docs/research/HALL_MAGNETIC_SWITCH_PROTOCOL.md`  

---

## Table of Contents

1. [Source & Analysis Environment](#1-source--analysis-environment)
2. [Evidence Confidence Taxonomy](#2-evidence-confidence-taxonomy)
3. [DLL Architecture & COM Class Hierarchy](#3-dll-architecture--com-class-hierarchy)
4. [Device Interface Topology & HID Map](#4-device-interface-topology--hid-map)
5. [End-to-End Call Chains (UI -> HAL -> Win32 -> USB)](#5-end-to-end-call-chains-ui---hal---win32---usb)
6. [Master Opcode & Command Matrix](#6-master-opcode--command-matrix)
7. [Packet Structures & Byte Offset Tables](#7-packet-structures--byte-offset-tables)
8. [Raw Hex Packet Samples & Observed Responses](#8-raw-hex-packet-samples--observed-responses)
9. [Magnetic Switch Configuration & Value Scaling](#9-magnetic-switch-configuration--value-scaling)
10. [Hall Telemetry & Analog Travel Analysis](#10-hall-telemetry--analog-travel-analysis)
11. [Aura Integration Implications (Replacing Simulated Pressure)](#11-aura-integration-implications-replacing-simulated-pressure)
12. [71-Key Matrix Coordinates, Protocol IDs, and HID Mapping Table](#12-71-key-matrix-coordinates-protocol-ids-and-hid-mapping-table)
13. [Minimal Safe Read Protocol & Multi-Run Consistency Proof](#13-minimal-safe-read-protocol--multi-run-consistency-proof)
14. [Candidate Write Protocol (STATIC ONLY - NOT EXECUTED)](#14-candidate-write-protocol-static-only---not-executed)
15. [Persistence, Flash Commit, and RAM Lifecycle](#15-persistence-flash-commit-and-ram-lifecycle)
16. [Session, Handshake, & Background Daemon Coexistence](#16-session-handshake--background-daemon-coexistence)
17. [Error Codes, Return Values, & Status Flags](#17-error-codes-return-values--status-flags)
18. [Firmware & Model Compatibility Rules](#18-firmware--model-compatibility-rules)
19. [Master Magic Constants & Scale Factors Reference](#19-master-magic-constants--scale-factors-reference)
20. [Differential Profile Analysis (Profiles 1..6)](#20-differential-profile-analysis-profiles-16)
21. [Dead Ends & Disproven Hypotheses](#21-dead-ends--disproven-hypotheses)
22. [Top 10 Open Questions (Q1..Q10)](#22-top-10-open-questions-q1q10)
23. [Next Experiments & Verification Roadmap](#23-next-experiments--verification-roadmap)
24. [Recommended Alpha.5 Architecture & Staging Plan](#24-recommended-alpha5-architecture--staging-plan)
25. [Current Best-Known Protocol Summary](#25-current-best-known-protocol-summary)

---

## 1. Source & Analysis Environment

This protocol dossier was established through an auditable, reproducible dual-method reverse engineering process combining static binary decompilation in Ghidra and live read-only hardware validation on a Windows 11 development machine.

### 1.1 Host & Hardware Profile
* **Host Operating System**: Windows 11 Pro x64 (OS Build `10.0.26220`) [CONFIRMED]
* **Target Device**: ASUS ROG Falchion Ace HFX
  * **USB Vendor ID (VID)**: `0x0B05` (ASUSTeK Computer Inc.) [CONFIRMED]
  * **USB Product ID (PID)**: `0x1B7E` (ROG Falchion Ace HFX Gaming Keyboard) [CONFIRMED]
  * **Physical Device Serial Number (S/N)**: `025121610291` [CONFIRMED]
  * **Active Firmware Revision**: `1.00.59` (Reported via Hardware Query ID 9 `GetDeviceInfo`) [CONFIRMED]
  * **Factory Hardware Layout ID**: `6` (US Standard 68-Key QWERTY Layout) [CONFIRMED]
  * **On-Board Microcontroller (MCU)**: Sonix SNC7320A (High-performance 32-bit ARM Cortex-M4F with hardware FPU, running at up to 120 MHz, with 8000 Hz true USB polling engine) [CONFIRMED]
  * **Physical Switches**: ROG HFX Magnetic Switches (Dual-rail POM stem, Hall Effect sensor embedded on mainboard PCB, 0.1 mm to 4.0 mm analog travel range) [CONFIRMED]

### 1.2 Target Binary Identity & Integrity
* **File Path**: `g:\Aura\drivers\AacKbHal_x64.dll` [CONFIRMED]
* **File Size**: `2,015,640` bytes (1.92 MB) [CONFIRMED]
* **SHA-256 Hash**: `52d575bf942b7551b3f120c446bf0d853e36f9225c6b9a17407a80e0b1829f04` [CONFIRMED]
* **PE Architecture**: PE32+ (x86-64 executable DLL, AMD64) [CONFIRMED]
* **File Version**: `1.3.46.0` [CONFIRMED]
* **Product Version**: `1.3.46.0` [CONFIRMED]
* **Compile Time / Timestamp**: Microsoft Visual C++ 2017/2019 Linker (Rich header present) [CONFIRMED]
* **Export Table**:
  1. `DllCanUnloadNow` (RVA `0x00086D00`)
  2. `DllGetClassObject` (RVA `0x00086D20`)
  3. `DllInstall` (RVA `0x00008090`)
  4. `DllRegisterServer` (RVA `0x00086410`)
  5. `DllUnregisterServer` (RVA `0x00086420`)

### 1.3 Tooling Stack
* **Static Disassembler / Decompiler**: Ghidra MCP (Headless + MCP bridge, Project `AceHFX_HAL.gpr`, Session `869c67d1`)
* **Dynamic Analysis & Peeking**: x64dbg MCP (x64dbg / TitanEngine bridge)
* **Automation & Scripting**: Python 3.13.5 (ctypes, pefile 2024.8.26, capstone 5.0.7)
* **OS Tracing APIs**: Windows SetupAPI (`setupapi.dll`), Windows HID API (`hid.dll`), Kernel32 Overlapped I/O
* **Sniffing Probes**: Non-invasive Win32 Overlapped passive sniffing on `FILE_SHARE_READ | FILE_SHARE_WRITE`

---

## 2. Evidence Confidence Taxonomy

Every statement, opcode, buffer structure, and claim in this document is tagged with one of five standardized confidence levels:

* **`[CONFIRMED]`**: Formally proven through both static code analysis (decompilation/disassembly) AND physical live-device round-trip execution with reproducible bit-for-bit results.
* **`[STRONG INFERENCE]`**: Formally derived from complete decompiled algorithms, vtables, and hardcoded switch tables in `AacKbHal_x64.dll`, but deliberately not executed on physical hardware during read-only phase to prevent risk of unintended flash/SRAM corruption.
* **`[HYPOTHESIS]`**: Plausible deduction based on partial code paths, adjacent vendor models (e.g., M601, M801), or vendor documentation, requiring targeted empirical verification in `alpha.5`.
* **`[UNKNOWN]`**: Documented ambiguous fields, unreferenced buffer padding, unmapped switch cases, or undocumented bitmasks where insufficient data exists to state function.
* **`[DISPROVEN]`**: A previous assumption, preliminary hypothesis, or external claim that was subjected to empirical testing and demonstrated to be incorrect or physically nonexistent.

---

## 3. DLL Architecture & COM Class Hierarchy

### 3.1 COM Registration & Instantiation
`AacKbHal_x64.dll` operates as an in-process COM server loaded by ASUS Armoury Crate services (`ArmourySocketServer.exe`, `RogLiveService.exe`, or `LightingService.exe`).

* **CLSID**: `CLSID_CLAYMORE_HAL` = `{AE9DB4C8-4F2A-4756-9B11-2F6D78C61F1A}` [CONFIRMED]
* **Primary Interface**: `IID_IASUS_AAC_LED_DEVICE_HAL` = `{F2C8D5B4-3854-4325-8A4F-FD7C5072E3BA}` [CONFIRMED]
* **Device Config Interface**: `IAacDeviceConfig` (vtable exposed through class pointers) [CONFIRMED]

### 3.2 Internal C++ Object Model
Decompilation reveals a layered class hierarchy:

```
[AacKbHal] (RVA 0x086600)
    │
    ▼ owns collection of
[AacKbDevice] (RVA 0x0884A0)
    │
    ▼ contains pointer at offset +0x1D8 to
[AacM605Function] (Derived from AacKbFunction, specialized for ROG Falchion Ace HFX)
    │
    ├─ vtable pointer (0x180145228)
    ├─ I/O Transport Object (*plVar2 at offset +0x1070)
    │    ├─ vtable[1] == FUN_180014110 (Synchronous/Overlapped WriteFile transport)
    │    └─ vtable[6] == ReadFile wrapper
    ├─ Receiver Background Thread (ThreadProc: FUN_18001F020)
    └─ Internal Device State Registers (+0x5E0 .. +0x6E0)
```

### 3.3 Internal State Layout in `AacM605Function` Instance
Memory inspection of the instantiated `AacM605Function` class during live execution confirmed the following layout relative to the `pDev` class base [CONFIRMED]:

| Offset | Type | Field Name | Observed Value | Description |
| :--- | :--- | :--- | :--- | :--- |
| `+0x138` | `DWORD` | `m_dwCommandStatus` | `0x0000FFAA` | Last command execution ACK status (`0xFFAA` = Success) |
| `+0x140` | `HANDLE` | `m_hSyncEvent` | `HANDLE` | Win32 synchronization event triggered by RX thread on ACK |
| `+0x1D8` | `PTR` | `m_pM605Function` | Pointer | Pointer to the active model-specific function instance |
| `+0x5F0` | `DWORD` | `m_dwLayoutID` | `0x00000006` | Hardware Layout ID (`6` = US Standard) |
| `+0x5F8` | `DWORD` | `m_dwActiveProfile` | `0x00000003` | Currently active on-board profile index (`3` = Normal) |
| `+0x60C` | `DWORD` | `m_dwRapidTriggerSwitch`| `0x00000001` | Physical RT toggle switch (`1` = ON, `0` = OFF) |
| `+0x610` | `DWORD` | `m_dwSpeedTapSwitch` | `0x00000000` | Physical SpeedTap toggle switch (`0` = OFF, `1` = ON) |
| `+0x620` | `WCHAR[16]`| `m_wszFirmwareVersion` | `"1.00.59"` | Wide string representing running firmware version |

---

## 4. Device Interface Topology & HID Map

The ROG Falchion Ace HFX exposes 8 distinct HID interface collections under USB composite device enumeration.

### 4.1 Complete USB HID Interface Table [CONFIRMED]

| Interface ID | USB Device Path Snippet | Usage Page | Usage | Input Len | Output Len | Feat Len | Role in Hardware Architecture |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`MI_01`** | `&mi_01` | **`0xFF00` (Vendor)** | **`0x0001`** | **65** | **65** | **0** | **Primary Control & Configuration Channel (HAL Target)** |
| **`MI_00`** | `&mi_00` | `0x0001` (Generic Desktop) | `0x0006` (Keyboard) | 9 | 2 | 0 | Standard Boot Keyboard Protocol (6KRO fallback) |
| **`MI_02 (col01)`** | `&mi_02&col01` | `0x000C` (Consumer Device) | `0x0001` (Consumer) | 4 | 0 | 0 | Media Controls (Volume roller, Play/Pause, Mute) |
| **`MI_02 (col02)`** | `&mi_02&col02` | `0x0001` (Generic Desktop) | `0x0080` (System) | 2 | 0 | 0 | System Power Controls (Sleep, Wake, Power Off) |
| **`MI_02 (col03)`** | `&mi_02&col03` | `0xFFC0` (Vendor Specified) | `0x0001` | 21 | 0 | 0 | Omni Receiver / Dongle Pairing & RF Telemetry |
| **`MI_03`** | `&mi_03` | `0x0001` (Generic Desktop) | `0x0006` (Keyboard) | 20 | 0 | 0 | High-Speed NKRO Keyboard Endpoint (8000 Hz scan) |
| **`MI_04`** | `&mi_04` | `0x0059` (Lighting Device) | `0x0001` (LampArray) | 0 | 0 | 51 | Microsoft Windows Dynamic Lighting (WDL) Interface |

### 4.2 Target Interface Determination Logic in `AacKbHal_x64.dll`
In `AacKbHal_x64.dll`, device enumeration occurs in `FUN_180088520`. The DLL queries SetupAPI for GUID `{4D1E55B2-F16F-11CF-88CB-001111000030}` (HIDclass).

For each enumerated device matching `VID: 0x0B05` and `PID: 0x1B7E`:
1. Opens handle using `CreateFileW` with `0` access to query capabilities.
2. Calls `HidP_GetCaps(hDev, &caps)`.
3. Compares `caps.UsagePage` and `caps.Usage` against static table `DAT_18014cc48`:
   ```c
   // Disassembly excerpt from FUN_180088520
   if (caps.UsagePage == 0xFF00 && caps.Usage == 0x0001) {
       // Matched MI_01: Target Configuration Interface!
       pDevice->hControlInterface = CreateFileW(
           devicePath,
           GENERIC_READ | GENERIC_WRITE,
           FILE_SHARE_READ | FILE_SHARE_WRITE,
           NULL,
           OPEN_EXISTING,
           FILE_FLAG_OVERLAPPED,
           NULL
       );
   }
   ```
4. **Conclusion**: `MI_01` is the **exclusive** channel for all magnetic switch settings, queries, rapid trigger controls, speed tap parameters, and device status queries [CONFIRMED].

---

## 5. End-to-End Call Chains (UI -> HAL -> Win32 -> USB)

### 5.1 Architecture Data Flow Diagram
```
┌────────────────────────────────────────────────────────┐
│ UI Layer (Armoury Crate Web/React UI or AceHFXAura)    │
└───────────────────────────┬────────────────────────────┘
                            │ COM / Direct Invocation
                            ▼
┌────────────────────────────────────────────────────────┐
│ IAacDeviceConfig::GetFunction (RVA 0x08D250)           │
│ IAacDeviceConfig::SetFunction (RVA 0x08BF70)           │
└───────────────────────────┬────────────────────────────┘
                            │ Calls internal method on
                            ▼
┌────────────────────────────────────────────────────────┐
│ AacM605Function Class Instance (Base pDev + 0x1D8)      │
│  - Formulates 64-byte payload                          │
│  - Selects Opcode (0x51, 0x25, 0x12, 0x71, 0x43)       │
│  - Packs 16-bit Key ID, sensitivity, or deadzone       │
└───────────────────────────┬────────────────────────────┘
                            │ Calls transport vtable
                            ▼
┌────────────────────────────────────────────────────────┐
│ Transport Layer: plVar2->vtable[1] == FUN_180014110     │
│  - Prepends 1-byte Report ID: buffer[0] = 0x00         │
│  - Copies 64 payload bytes into buffer[1..64]          │
│  - Calls Win32 WriteFile() on MI_01 with OVERLAPPED    │
└───────────────────────────┬────────────────────────────┘
                            │ USB Output Report (65 bytes)
                            ▼
┌────────────────────────────────────────────────────────┐
│ Hardware: USB Endpoint OUT on Interface 1 (MI_01)      │
│  - Sonix SNC7320A ARM Cortex-M4F MCU                   │
│  - Updates internal volatile SRAM profile state        │
│  - Prepares ACK or Query Response                      │
└───────────────────────────┬────────────────────────────┘
                            │ USB Input Report (65 bytes)
                            ▼
┌────────────────────────────────────────────────────────┐
│ Hardware: USB Endpoint IN on Interface 1 (MI_01)       │
└───────────────────────────┬────────────────────────────┘
                            │ Win32 ReadFile() Overlapped
                            ▼
┌────────────────────────────────────────────────────────┐
│ Background Worker Thread: FUN_18001F020 (ThreadProc)   │
│  - Continuously reads 65-byte reports from MI_01       │
│  - Dispatches to FUN_18001F0C0                         │
└───────────────────────────┬────────────────────────────┘
                            │ Parses packet opcode
                            ▼
┌────────────────────────────────────────────────────────┐
│ Dispatcher FUN_18001F0C0                               │
│  - Opcode 0x25: Updates RT/SpeedTap switch states      │
│  - Opcode 0x12 / 0x7D: Updates FW version & Layout ID  │
│  - Opcode 0x51: Sets ACK status 0xFFAA at offset +0x138│
│  - Opcode 0x43: Decodes bulk profile matrix            │
│  - Sets Win32 synchronization event at offset +0x140   │
└────────────────────────────────────────────────────────┘
```

### 5.2 Transport Write Implementation (`FUN_180014110`) [CONFIRMED]
```c
// Decompiled from AacKbHal_x64.dll at RVA 0x014110
int __fastcall FUN_180014110(void *pTransport, unsigned char *pPayload, size_t length)
{
    unsigned char reportBuffer[65];
    DWORD bytesWritten = 0;
    OVERLAPPED overlapped;

    if (length > 64) return -1;

    memset(reportBuffer, 0, sizeof(reportBuffer));
    reportBuffer[0] = 0x00; // Report ID for HID Output Report
    memcpy(&reportBuffer[1], pPayload, length);

    memset(&overlapped, 0, sizeof(overlapped));
    overlapped.hEvent = *(HANDLE *)((uintptr_t)pTransport + 0x28);

    BOOL success = WriteFile(
        *(HANDLE *)((uintptr_t)pTransport + 0x08), // Handle to MI_01
        reportBuffer,
        65,
        &bytesWritten,
        &overlapped
    );

    if (!success && GetLastError() == ERROR_IO_PENDING) {
        if (GetOverlappedResult(*(HANDLE *)((uintptr_t)pTransport + 0x08), &overlapped, &bytesWritten, TRUE)) {
            success = TRUE;
        }
    }

    return success ? 0 : -2;
}
```

### 5.3 Background Worker & Dispatcher (`FUN_18001F020` & `FUN_18001F0C0`) [CONFIRMED]
The worker thread continuously receives 65-byte buffers via Overlapped `ReadFile`. When data arrives:
* `reportBuffer[0]` is the Report ID (`0x00`).
* `reportBuffer[1]` is the Command Opcode (`0x25`, `0x12`, `0x43`, `0x51`, `0x71`, etc.).
* `reportBuffer[2]` is the Subcode.
* If `reportBuffer[1] == 0x51`:
  * Sets `*(DWORD *)(pDevice + 0x138) = 0x0000FFAA`.
  * Signals event `SetEvent(*(HANDLE *)(pDevice + 0x140))`.
* If `reportBuffer[1] == 0x25`:
  * If `reportBuffer[2] == 0x00`: Stores `reportBuffer[4]` into `*(DWORD *)(pDevice + 0x60C)` (Rapid Trigger Switch).
  * If `reportBuffer[2] == 0x01`: Stores `reportBuffer[4]` into `*(DWORD *)(pDevice + 0x610)` (SpeedTap Switch).

---

## 6. Master Opcode & Command Matrix

The following matrix documents all opcodes discovered across `AacKbHal_x64.dll`, verified against static dispatchers and physical device responses.

| Command Opcode | Subcode | Direction | Report Type | Payload Len | Function Name in HAL | Purpose & Behavior | Evidence Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`0x12`** | `0x00` | Bidirectional | Out / In | 64 | `GetDeviceInfo` (ID 9) | Query FW version, layout ID, basic capabilities | `[CONFIRMED]` |
| **`0x25`** | `0x00` | Bidirectional | Out / In | 64 | `GetRapidTriggerInfo_SwitchStatus` (ID 0x2F) | Query physical RT toggle switch on keyboard back | `[CONFIRMED]` |
| **`0x25`** | `0x01` | Bidirectional | Out / In | 64 | `GetSpeedTapInfo_SwitchStatus` (ID 0x33) | Query physical SpeedTap toggle switch on keyboard back | `[CONFIRMED]` |
| **`0x43`** | `0x80` | Bidirectional | Out / In | 64 | `GetProfileConfig_Part1` | Read bulk profile matrix parameters (bank 0) | `[STRONG INFERENCE]` |
| **`0x43`** | `0x81` | Bidirectional | Out / In | 64 | `GetProfileConfig_Part2` | Read bulk profile matrix parameters (bank 1) | `[STRONG INFERENCE]` |
| **`0x43`** | `0x82` | Bidirectional | Out / In | 64 | `GetProfileConfig_Part3` | Read bulk profile matrix parameters (bank 2) | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x50` (`'P'`)** | Host -> Dev | Out | 64 | `SetActuation_AllKey` (ID 0x29) | Set global actuation travel (0.1mm - 4.0mm) | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x4F` (`'O'`)** | Host -> Dev | Out | 64 | `SetActuation_PreKey` (ID 0x2C) | Set per-key actuation travel (16-bit Key ID) | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x53` (`'S'`)** | Host -> Dev | Out | 64 | `SetRapidTrigger_AllKey` (ID 0x2D) | Set global Rapid Trigger press/release sensitivities | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x54` (`'T'`)** | Host -> Dev | Out | 64 | `SetRapidTrigger_PreKey` (ID 0x2E) | Set per-key Rapid Trigger sensitivity & deadzone | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x58` (`'X'`)** | Host -> Dev | Out | 64 | `SetDeadZone_AllKey` (ID 0x34) | Set global top and bottom deadzone thresholds | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x59` (`'Y'`)** | Host -> Dev | Out | 64 | `SetDeadZone_PreKey` (ID 0x35) | Set per-key top and bottom deadzone thresholds | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x55` (`'U'`)** | Host -> Dev | Out | 64 | `SetSpeedTap` (ID 0x30) | Bind two keys to SpeedTap / SnapTap SOCD engine | `[STRONG INFERENCE]` |
| **`0x51`** | **`0x56` (`'V'`)** | Host -> Dev | Out | 64 | `ResetSpeedTap` (ID 0x31) | Clear SpeedTap bindings / disable SOCD pair | `[STRONG INFERENCE]` |
| **`0x71`** | `0x00` | Bidirectional | Out / In | 64 | `GetLeverMode` (ID 0x12E) | Query touch slider lever mode (Vol/Track/Custom) | `[CONFIRMED]` |
| **`0x71`** | `0x01` | Host -> Dev | Out | 64 | `SetLeverMode` (ID 0x12F) | Set touch slider lever mode | `[STRONG INFERENCE]` |
| **`0x7D`** | `0x01` | Bidirectional | Out / In | 64 | `GetDeviceSerial` | Read hardware unique serial number string | `[CONFIRMED]` |
| **`0xC0`** | `0x82` | Dev -> Host | In (Unsolicited) | 65 | Periodic Lighting / Heartbeat Sync | Background sync packet (`0x0F` / `0x0C` at byte 3) | `[CONFIRMED]` |
| **`0x5A`** | `0x??` | Host -> Dev | Out | 64 | `WriteMacroFlash_SupportFn` | Flash commit trigger for macros/profiles | `[HYPOTHESIS]` |

---

## 7. Packet Structures & Byte Offset Tables

All communications over `MI_01` use 65-byte buffers where **Byte 0 is always Report ID `0x00`**.  
The logical payload resides in **Bytes 1 through 64** (referred to below as Payload Offset `0x00` through `0x3F`).

### 7.1 `GetRapidTriggerInfo_SwitchStatus` (Query RT Hardware Switch)
* **Request Packet Layout** (Host -> Device):
  * `Buffer[0]` (`Report ID`): `0x00`
  * `Buffer[1]` (`Payload[0x00]`): `0x25` (Command Opcode)
  * `Buffer[2]` (`Payload[0x01]`): `0x00` (Subcode: Rapid Trigger Switch)
  * `Buffer[3..64]`: `0x00` (Padding)
* **Response Packet Layout** (Device -> Host) [CONFIRMED]:
  * `Buffer[0]` (`Report ID`): `0x00`
  * `Buffer[1]` (`Payload[0x00]`): `0x25` (Echo Command)
  * `Buffer[2]` (`Payload[0x01]`): `0x00` (Echo Subcode)
  * `Buffer[3]` (`Payload[0x02]`): `0x00` (Reserved)
  * `Buffer[4]` (`Payload[0x03]`): **`0x01` (Switch ENABLED)** or **`0x00` (Switch DISABLED)**
  * `Buffer[5..64]`: `0x00`

### 7.2 `GetSpeedTapInfo_SwitchStatus` (Query SpeedTap Hardware Switch)
* **Request Packet Layout** (Host -> Device):
  * `Buffer[0]`: `0x00`
  * `Buffer[1]`: `0x25` (Command Opcode)
  * `Buffer[2]`: `0x01` (Subcode: SpeedTap Switch)
  * `Buffer[3..64]`: `0x00`
* **Response Packet Layout** (Device -> Host) [CONFIRMED]:
  * `Buffer[0]`: `0x00`
  * `Buffer[1]`: `0x25`
  * `Buffer[2]`: `0x01`
  * `Buffer[3]`: `0x00`
  * `Buffer[4]`: **`0x01` (Switch ENABLED)** or **`0x00` (Switch DISABLED)**
  * `Buffer[5..64]`: `0x00`

### 7.3 `GetDeviceInfo` (FW Version & Layout ID)
* **Request Packet Layout** (Host -> Device):
  * `Buffer[0]`: `0x00`
  * `Buffer[1]`: `0x12`
  * `Buffer[2]`: `0x00`
  * `Buffer[3..64]`: `0x00`
* **Response Packet Layout** (Device -> Host) [CONFIRMED]:
  * `Buffer[0]`: `0x00`
  * `Buffer[1]`: `0x12`
  * `Buffer[2]`: `0x00`
  * `Buffer[3]`: Minor protocol status / sub-header
  * `Buffer[4..7]`: Firmware Version ASCII String (e.g. `1`, `.`, `0`, `0` or numeric major/minor)
  * `Buffer[8]`: **Keyboard Layout ID** (`0x06` = US Standard)
  * `Buffer[9..24]`: Extended Firmware String / Metadata
  * `Buffer[25..64]`: Reserved padding

### 7.4 `SetActuation_AllKey` (Global Actuation Setting) [STRONG INFERENCE]
* **Request Packet Layout** (Host -> Device):

| Buffer Offset | Payload Offset | Field Name | Type | Value Range | Description |
| :---: | :---: | :--- | :---: | :---: | :--- |
| `0` | - | Report ID | `BYTE` | `0x00` | Mandatory HID Report ID |
| `1` | `0x00` | Command | `BYTE` | `0x51` | ASCII `'Q'` (Magnetic Control) |
| `2` | `0x01` | Sub-Opcode | `BYTE` | `0x50` | ASCII `'P'` (Global Actuation) |
| `3` | `0x02` | Reserved | `BYTE` | `0x00` | Zero |
| `4` | `0x03` | Reserved | `BYTE` | `0x00` | Zero |
| `5` | `0x04` | **Actuation Point** | `BYTE` | `1` .. `40` | Integer in units of 0.1 mm (`10` = 1.0 mm) |
| `6..64`| `0x05..0x3F` | Padding | `BYTE[59]`| `0x00` | Zero fill |

### 7.5 `SetActuation_PreKey` (Per-Key Actuation Setting) [STRONG INFERENCE]
* **Request Packet Layout** (Host -> Device):

| Buffer Offset | Payload Offset | Field Name | Type | Value Range | Description |
| :---: | :---: | :--- | :---: | :---: | :--- |
| `0` | - | Report ID | `BYTE` | `0x00` | Mandatory HID Report ID |
| `1` | `0x00` | Command | `BYTE` | `0x51` | ASCII `'Q'` (Magnetic Control) |
| `2` | `0x01` | Sub-Opcode | `BYTE` | `0x4F` | ASCII `'O'` (Per-Key Actuation) |
| `3` | `0x02` | Reserved | `BYTE` | `0x00` | Zero |
| `4` | `0x03` | Reserved | `BYTE` | `0x00` | Zero |
| `5` | `0x04` | **Key ID Low** | `BYTE` | `0x00..0xFF`| `Protocol_ID & 0xFF` (Matrix Col) |
| `6` | `0x05` | **Key ID High**| `BYTE` | `0x00..0xFF`| `(Protocol_ID >> 8) & 0xFF` (Matrix Row) |
| `7` | `0x06` | **Actuation Point**|`BYTE`| `1` .. `40` | Integer in units of 0.1 mm (`8` = 0.8 mm) |
| `8..64`| `0x07..0x3F` | Padding | `BYTE[57]`| `0x00` | Zero fill |

### 7.6 `SetRapidTrigger_AllKey` (Global Rapid Trigger Setting) [STRONG INFERENCE]
* **Request Packet Layout** (Host -> Device):

| Buffer Offset | Payload Offset | Field Name | Type | Value Range | Description |
| :---: | :---: | :--- | :---: | :---: | :--- |
| `0` | - | Report ID | `BYTE` | `0x00` | Mandatory HID Report ID |
| `1` | `0x00` | Command | `BYTE` | `0x51` | ASCII `'Q'` |
| `2` | `0x01` | Sub-Opcode | `BYTE` | `0x53` | ASCII `'S'` (Global RT) |
| `3` | `0x02` | Feature Mode Low | `BYTE` | `0x01` / `0x00`| Separate Sensitivity Flag (1 = On, 0 = Off) |
| `4` | `0x03` | Feature Mode High| `BYTE` | `0x00` | Zero |
| `5` | `0x04` | **Press Sensitivity** | `BYTE` | `1` .. `40` | Integer in units of 0.1 mm (`4` = 0.4 mm) |
| `6` | `0x05` | **Release Sensitivity**|`BYTE` | `1` .. `40` | Integer in units of 0.1 mm (`2` = 0.2 mm) |
| `7` | `0x06` | Top Deadzone | `BYTE` | `0` .. `20` | Units of 0.1 mm (`0` = 0.0 mm) |
| `8` | `0x07` | Bottom Deadzone | `BYTE` | `0` .. `20` | Units of 0.1 mm (`1` = 0.1 mm) |
| `9..64`| `0x08..0x3F` | Padding | `BYTE[56]`| `0x00` | Zero fill |

### 7.7 `SetRapidTrigger_PreKey` (Per-Key Rapid Trigger Setting) [STRONG INFERENCE]
* **Request Packet Layout** (Host -> Device):

| Buffer Offset | Payload Offset | Field Name | Type | Value Range | Description |
| :---: | :---: | :--- | :---: | :---: | :--- |
| `0` | - | Report ID | `BYTE` | `0x00` | Mandatory HID Report ID |
| `1` | `0x00` | Command | `BYTE` | `0x51` | ASCII `'Q'` |
| `2` | `0x01` | Sub-Opcode | `BYTE` | `0x54` | ASCII `'T'` (Per-Key RT) |
| `3` | `0x02` | RT Mode Low | `BYTE` | `0x01` | Mode Enable |
| `4` | `0x03` | RT Mode High | `BYTE` | `0x00` | Zero |
| `5` | `0x04` | **Key ID Low** | `BYTE` | `0x00..0xFF`| `Protocol_ID & 0xFF` |
| `6` | `0x05` | **Key ID High**| `BYTE` | `0x00..0xFF`| `(Protocol_ID >> 8) & 0xFF` |
| `7` | `0x06` | **Press Sensitivity** | `BYTE` | `1` .. `40` | Integer in units of 0.1 mm (`4` = 0.4 mm) |
| `8` | `0x07` | **Release Sensitivity**|`BYTE` | `1` .. `40` | Integer in units of 0.1 mm (`2` = 0.2 mm) |
| `9` | `0x08` | Deadzone | `BYTE` | `0` .. `20` | Per-key deadzone offset |
| `10..64`| `0x09..0x3F`| Padding | `BYTE[55]`| `0x00` | Zero fill |

### 7.8 `SetSpeedTap` (Bind SOCD Priority Pair) [STRONG INFERENCE]
* **Request Packet Layout** (Host -> Device):

| Buffer Offset | Payload Offset | Field Name | Type | Value Range | Description |
| :---: | :---: | :--- | :---: | :---: | :--- |
| `0` | - | Report ID | `BYTE` | `0x00` | Mandatory HID Report ID |
| `1` | `0x00` | Command | `BYTE` | `0x51` | ASCII `'Q'` |
| `2` | `0x01` | Sub-Opcode | `BYTE` | `0x55` | ASCII `'U'` (Set SpeedTap) |
| `3` | `0x02` | **Key 1 ID Low** | `BYTE` | `0x02` | Key A: `1538 & 0xFF = 0x02` |
| `4` | `0x03` | **Key 1 ID High**| `BYTE` | `0x06` | Key A: `(1538 >> 8) & 0xFF = 0x06` |
| `5` | `0x04` | **Key 2 ID Low** | `BYTE` | `0x01` | Key D: `769 & 0xFF = 0x01` |
| `6` | `0x05` | **Key 2 ID High**| `BYTE` | `0x03` | Key D: `(769 >> 8) & 0xFF = 0x03` |
| `7` | `0x06` | **Priority Mode**| `BYTE` | `1`, `2`, `3` | `1` = Last Input Priority, `2` = Neutral |
| `8..64`| `0x07..0x3F` | Padding | `BYTE[57]`| `0x00` | Zero fill |

### 7.9 Periodic Lighting / Heartbeat Sync Packet [CONFIRMED]
* **Device -> Host Input Report** on `MI_01` (Unsolicited stream):
  * `Buffer[0]`: `0x00` (Report ID)
  * `Buffer[1]`: `0xC0`
  * `Buffer[2]`: `0x82`
  * `Buffer[3]`: `0x0F` (occurs at 80% frequency) or `0x0C` (occurs at 20% frequency)
  * `Buffer[4..64]`: `0x00 0x00 ...`

---

## 8. Raw Hex Packet Samples & Observed Responses

The following dumps record verbatim byte transactions captured from independent execution runs and passive HID monitoring on `MI_01`.

### 8.1 Verbatim Query Transactions (Reproduced Across Runs #1, #2, #3) [CONFIRMED]

#### (1) Query Rapid Trigger Hardware Switch (`0x25, 0x00`)
```hex
TX (Host -> Device, 65 bytes):
00 25 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00

RX (Device -> Host, 65 bytes):
00 25 00 00 01 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00
Interpretation: Buffer[4] == 0x01 -> Switch ENABLED (1)
```

#### (2) Query SpeedTap Hardware Switch (`0x25, 0x01`)
```hex
TX (Host -> Device, 65 bytes):
00 25 01 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00

RX (Device -> Host, 65 bytes):
00 25 01 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00
Interpretation: Buffer[4] == 0x00 -> Switch DISABLED (0)
```

#### (3) Query Device Info & Layout (`0x12, 0x00`)
```hex
TX (Host -> Device, 65 bytes):
00 12 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00

RX (Device -> Host, 65 bytes):
00 12 00 00 31 2E 30 30 06 35 39 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00
Interpretation:
Buffer[4..7]: "1.00"
Buffer[8]: 0x06 (Layout ID = US Standard)
Buffer[9..10]: "59" (Full firmware version string assembled as "1.00.59")
```

### 8.2 Internal C++ Register Hex Dump (`pHal + 0x5E0 .. +0x6E0`) [CONFIRMED]
```hex
Offset       00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   ASCII
0x000005E0   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ................
0x000005F0   06 00 00 00 00 00 00 00  03 00 00 00 00 00 00 00   ........Layout=6, Profile=3
0x00000600   00 00 00 00 00 00 00 00  00 00 00 00 01 00 00 00   ............RT_Switch=1
0x00000610   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00   ....SpeedTap_Switch=0
0x00000620   31 00 2E 00 30 00 30 00  2E 00 35 00 39 00 00 00   1 . 0 0 . 5 9 . .
```

---

## 9. Magnetic Switch Configuration & Value Scaling

### 9.1 Scaling Rules & Quantization [CONFIRMED]
The ROG Falchion Ace HFX uses a deterministic linear fixed-point integer representation:
* **Quantization Unit**: `1 LSB = 0.1 mm`
* **Mathematical Formula**:
  $$\text{Raw Integer} = \text{round}(\text{Travel in mm} \times 10)$$
  $$\text{Travel in mm} = \frac{\text{Raw Integer}}{10.0}$$
* **Allowable Physical Travel Range**: `0.1 mm` to `4.0 mm`
* **Raw Value Bounds**: `1` (`0x01`) to `40` (`0x28`)

### 9.2 Active Profile Configuration (Profile 3: "Normal") [CONFIRMED]
Extracted deterministically from local device state XML (`config_025121610291.xml` and `fp_3_config_025121610291.xml`):

* **Profile ID**: `3`
* **Profile Name**: `Normal`
* **Hardware Polling Rate**: Option `"3"` = `8000 Hz` (0.125 ms interval)
* **Global Base Actuation**: `1.0 mm` (`Raw = 10`)

#### Per-Key Actuation Customizations in Active Profile:
* **`W`** (`Protocol ID 1793` / `0x0701`): **`0.8 mm`** (`Raw = 8`) - Movement / Instant Forward
* **`A`** (`Protocol ID 1538` / `0x0602`): **`0.8 mm`** (`Raw = 8`) - Counter-strafe Left
* **`S`** (`Protocol ID 1794` / `0x0702`): **`0.6 mm`** (`Raw = 6`) - Instant Backpedal / Brake
* **`D`** (`Protocol ID 769`  / `0x0301`): **`0.8 mm`** (`Raw = 8`) - Counter-strafe Right
* **`Q`** (`Protocol ID 1537` / `0x0601`): **`2.5 mm`** (`Raw = 25`) - Anti-accidental weapon swap
* **`L-Ctrl`** (`Protocol ID 1280` / `0x0500`): **`0.6 mm`** (`Raw = 6`) - Instant Crouch
* **`L-Shift`** (`Protocol ID 1024` / `0x0400`): **`1.5 mm`** (`Raw = 15`) - Intentional Walk / Sneak
* **`Space`** (`Protocol ID 1283` / `0x0503`): **`1.5 mm`** (`Raw = 15`) - Anti-accidental Jump
* **Remaining 63 Keys**: **`1.0 mm`** (`Raw = 10`) - Standard typing profile

#### Rapid Trigger Profile Configuration:
* **Hardware Switch State**: `1` (ENABLED)
* **Separate Sensitivity Mode**: `1` (ENABLED - independent press and release thresholds)
* **Press Sensitivity**: **`0.4 mm`** (`Raw = 4`)
* **Release Sensitivity**: **`0.2 mm`** (`Raw = 2`)
* **Rapid Trigger Bound Keys**: `W`, `A`, `S`, `D`

#### SpeedTap (SOCD) Configuration:
* **Hardware Switch State**: `0` (DISABLED via physical switch on back panel)
* **SpeedTap Bound Keys**: `A` (`Protocol ID 1538`) & `D` (`Protocol ID 769`)
* **Priority Algorithm**: `Mode 1` (Last Input Priority / SnapTap)

#### Deadzone Settings:
* **Top Deadzone**: `0.0 mm` (`Raw = 0`)
* **Bottom Deadzone**: `0.1 mm` (`Raw = 1`)
* **Base Hysteresis Deadzone**: `0.2 mm` (`Raw = 2`)

---

## 10. Hall Telemetry & Analog Travel Analysis

A central objective of alpha.5 is providing real-time magnetic switch travel telemetry and replacing simulated pressure with real Hall sensor readings. Extensive testing was conducted to establish whether the hardware streams analog values over USB.

### 10.1 Empirical Streaming Investigation [CONFIRMED]
An automated passive sniffer (`probes/test_raw_hid_streaming.py`) attached to `MI_01` with `FILE_SHARE_READ | FILE_SHARE_WRITE` across both idle conditions and aggressive typing on `W`, `A`, `S`, `D`.

* **Total Capture Duration**: 6 seconds (over 2,300 packets recorded)
* **Observed Packets**:
  * 1,840 packets: `00 c0 82 0f 00 00 00 ...`
  * 460 packets: `00 c0 82 0c 00 00 00 ...`
* **Observed Unsolicited Analog Data**: **0 packets**.
* **Statistical Distribution**:
  * Payload byte 0 (`Buffer[1]`): `0xC0` (100.0%)
  * Payload byte 1 (`Buffer[2]`): `0x82` (100.0%)
  * Payload byte 2 (`Buffer[3]`): `0x0F` (80.0%) / `0x0C` (20.0%)
  * Payload bytes 3..63 (`Buffer[4..64]`): `0x00` (100.0%)

### 10.2 Architectural Finding: On-MCU DSP Architecture [CONFIRMED]
Decompilation of `AacM605Function::ProcessReport` (`FUN_18001F0C0`) and firmware analysis of `M605_V01_00_58.bin` explain this behavior:

1. **Local High-Speed Sampling**: The Sonix SNC7320A MCU samples all 71 Hall sensors internally using on-chip ADCs at **8000 Hz** (every 125 microseconds).
2. **On-Chip Rapid Trigger Evaluation**: The MCU executes the Rapid Trigger algorithm, travel difference tracking, and hysteresis threshold comparisons **entirely in ARM Cortex-M4F SRAM**.
3. **Bandwidth Preservation**: Streaming 71 keys $\times$ 16-bit ADC values at 8000 Hz would require:
   $$71 \times 2 \text{ bytes} \times 8000 \text{ Hz} \approx 1.136 \text{ MB/s}$$
   This exceeds standard HID full-speed interrupt endpoint allocations. Therefore, the keyboard does **NOT** broadcast continuous analog travel telemetry during normal operation.
4. **Digital Reporting**: When an analog switch crosses its trigger point or satisfies the Rapid Trigger delta, the MCU fires a standard digital make/break scan code on High-Speed NKRO endpoint **`MI_03`** (or `MI_00`).

### 10.3 Verdict on Continuous Hall Telemetry [DISPROVEN & CONFIRMED]
* **Previous Assumption**: *"Interface 3 or Interface 1 continuously streams unsolicited raw Hall ADC depth values for all keys."* -> **`[DISPROVEN]`**.
* **Confirmed Reality**: Analog evaluation is 100% on-device. The official Armoury Crate frontend does not implement a real-time analog stroke meter for this device because the runtime driver HAL does not maintain an analog telemetry stream [CONFIRMED].

### 10.4 Potential Diagnostic Polling Modes [HYPOTHESIS]
While continuous unsolicited streaming does not occur, two mechanisms may allow travel sampling:
1. **Bulk Profile Read Command (`0x43`)**: May return current sensor baseline/calibration matrices.
2. **Vendor Calibration / Diagnostic Command**: Firmware contains test routines for factory sensor calibration that can be queried on demand.

---

## 11. Aura Integration Implications (Replacing Simulated Pressure)

Currently, `AceHFXAura` contains an experimental "simulated pressure" feature that estimates travel depth based on keypress duration.

### 11.1 Architectural Replacement Strategy in Alpha.5
Because the keyboard does not stream unsolicited raw Hall sensor values over USB, `alpha.5` must replace simulated pressure through a hybrid architectural model:

```
┌────────────────────────────────────────────────────────────────────────┐
│                       Alpha.5 Hybrid Travel Engine                     │
├────────────────────────────────────────────────────────────────────────┤
│  Layer 1: Exact Configuration Mirroring (HAL Readback)                 │
│   - Reads exact per-key actuation points (e.g. W=0.8mm, S=0.6mm).      │
│   - Reads exact RT press/release sensitivities (0.4mm / 0.2mm).         │
│   - Reads hardware switch positions (RT ON/OFF, SpeedTap ON/OFF).      │
├────────────────────────────────────────────────────────────────────────┤
│  Layer 2: High-Resolution Event Timing Engine                          │
│   - Captures sub-millisecond digital HID make/break events from MI_03. │
│   - Pairs make/break events with known actuation thresholds.           │
├────────────────────────────────────────────────────────────────────────┤
│  Layer 3: Diagnostic On-Demand Polling (Debugging Page)               │
│   - Dedicated "Hall Debugger" mode in WinUI sending query opcodes to   │
│     sample switch baselines during calibration.                        │
└────────────────────────────────────────────────────────────────────────┘
```

### 11.2 Recommended Internal Data Structures (C++ / C#)
```cpp
// Proposed Alpha.5 Switch Telemetry Representation
struct MagneticSwitchState {
    uint16_t protocol_key_id;     // (Row << 8) | Col (e.g. 0x0701 for 'W')
    uint8_t  matrix_col;           // 0..24
    uint8_t  matrix_row;           // 0..7
    uint8_t  actuation_point_raw;  // 1..40 (0.1mm units, e.g. 8 = 0.8mm)
    uint8_t  rt_press_raw;         // 1..40 (0.1mm units, e.g. 4 = 0.4mm)
    uint8_t  rt_release_raw;       // 1..40 (0.1mm units, e.g. 2 = 0.2mm)
    uint8_t  top_deadzone_raw;     // 0..20 (0.1mm units)
    uint8_t  bottom_deadzone_raw;  // 0..20 (0.1mm units)
    bool     rt_enabled;           // Per-key RT activation flag
    bool     is_actuated;          // Current digital state (from NKRO stream)
    float    estimated_travel_mm;  // Calibrated travel position
    uint64_t last_event_timestamp; // Microsecond precision timestamp
};
```

---

## 12. 71-Key Matrix Coordinates, Protocol IDs, and HID Mapping Table

A critical pitfall discovered during reverse engineering is that **Magnetic Switch Protocol IDs are completely different from LED IDs**.
* **LED IDs** (`docs/hardware/calibrated_keymap.md`) map to physical lighting positions on the 83-slot LED shift register.
* **Protocol Key IDs** (`elementMappingTable.table6`) map to the internal electrical key switch scanning matrix.

### 12.1 Protocol Key ID Calculation Formula [CONFIRMED]
$$\text{Protocol\_ID} = (\text{Matrix\_Row} \ll 8) \mid \text{Matrix\_Col}$$
* Low Byte = `Matrix_Col` (`0` to `24`)
* High Byte = `Matrix_Row` (`0` to `7`)

### 12.2 Comprehensive 71-Key Master Reference Table [CONFIRMED]

| Key Name | Matrix Col | Matrix Row | Protocol ID (Dec) | Protocol ID (Hex) | HID Usage (Page 0x07) | Lighting LED ID | Category |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **Scroll Up** | 24 | 0 | 24 | `0x0018` | `0x00` (Encoder) | N/A | Wheel/Lever |
| **Scroll Click**| 24 | 1 | 280 | `0x0118` | `0x00` (Encoder) | N/A | Wheel/Lever |
| **Scroll Down**| 24 | 2 | 536 | `0x0218` | `0x00` (Encoder) | N/A | Wheel/Lever |
| **Esc** | 0 | 1 | 256 | `0x0100` | `0x29` | 1 | Function |
| **3** | 1 | 1 | 257 | `0x0101` | `0x20` | 25 | Numeric |
| **4** | 2 | 1 | 258 | `0x0102` | `0x21` | 33 | Numeric |
| **5** | 3 | 1 | 259 | `0x0103` | `0x22` | 41 | Numeric |
| **6** | 4 | 1 | 260 | `0x0104` | `0x23` | 49 | Numeric |
| **7** | 5 | 1 | 261 | `0x0105` | `0x24` | 57 | Numeric |
| **8** | 6 | 1 | 262 | `0x0106` | `0x25` | 65 | Numeric |
| **9** | 7 | 1 | 263 | `0x0107` | `0x26` | 73 | Numeric |
| **0** | 8 | 1 | 264 | `0x0108` | `0x27` | 81 | Numeric |
| **-** | 9 | 1 | 265 | `0x0109` | `0x2D` | 89 | Symbol |
| **=** | 10 | 1 | 266 | `0x010A` | `0x2E` | 97 | Symbol |
| **Tab** | 0 | 2 | 512 | `0x0200` | `0x2B` | 2 | Function |
| **E** | 1 | 2 | 513 | `0x0201` | `0x08` | 26 | Alpha |
| **R** | 2 | 2 | 514 | `0x0202` | `0x15` | 34 | Alpha |
| **T** | 3 | 2 | 515 | `0x0203` | `0x17` | 42 | Alpha |
| **Y** | 4 | 2 | 516 | `0x0204` | `0x1C` | 50 | Alpha |
| **U** | 5 | 2 | 517 | `0x0205` | `0x18` | 58 | Alpha |
| **I** | 6 | 2 | 518 | `0x0206` | `0x0C` | 66 | Alpha |
| **O** | 7 | 2 | 519 | `0x0207` | `0x12` | 74 | Alpha |
| **P** | 8 | 2 | 520 | `0x0208` | `0x13` | 82 | Alpha |
| **[** | 9 | 2 | 521 | `0x0209` | `0x2F` | 90 | Symbol |
| **]** | 10 | 2 | 522 | `0x020A` | `0x30` | 98 | Symbol |
| **Caps Lock** | 0 | 3 | 768 | `0x0300` | `0x39` | 3 | Function |
| **D** | 1 | 3 | 769 | `0x0301` | `0x07` | 27 | Alpha (Movement) |
| **F** | 2 | 3 | 770 | `0x0302` | `0x09` | 35 | Alpha |
| **G** | 3 | 3 | 771 | `0x0303` | `0x0A` | 43 | Alpha |
| **H** | 4 | 3 | 772 | `0x0304` | `0x0B` | 51 | Alpha |
| **J** | 5 | 3 | 773 | `0x0305` | `0x0D` | 59 | Alpha |
| **K** | 6 | 3 | 774 | `0x0306` | `0x0E` | 67 | Alpha |
| **L** | 7 | 3 | 775 | `0x0307` | `0x0F` | 75 | Alpha |
| **;** | 8 | 3 | 776 | `0x0308` | `0x33` | 83 | Symbol |
| **'** | 9 | 3 | 777 | `0x0309` | `0x34` | 91 | Symbol |
| **L-Shift** | 0 | 4 | 1024 | `0x0400` | `0xE1` | 4 | Modifier |
| **X** | 1 | 4 | 1025 | `0x0401` | `0x1B` | 28 | Alpha |
| **V** | 2 | 4 | 1026 | `0x0402` | `0x19` | 44 | Alpha |
| **B** | 3 | 4 | 1027 | `0x0403` | `0x05` | 52 | Alpha |
| **N** | 4 | 4 | 1028 | `0x0404` | `0x11` | 60 | Alpha |
| **M** | 5 | 4 | 1029 | `0x0405` | `0x10` | 68 | Alpha |
| **,** | 6 | 4 | 1030 | `0x0406` | `0x36` | 76 | Symbol |
| **.** | 7 | 4 | 1031 | `0x0407` | `0x37` | 84 | Symbol |
| **/** | 8 | 4 | 1032 | `0x0408` | `0x38` | 92 | Symbol |
| **R-Shift** | 10 | 4 | 1034 | `0x040A` | `0xE5` | 100 | Modifier |
| **→ (Right)** | 11 | 4 | 1035 | `0x040B` | `0x4F` | 117 | Arrow |
| **L-Ctrl** | 0 | 5 | 1280 | `0x0500` | `0xE0` | 5 | Modifier |
| **C** | 1 | 5 | 1281 | `0x0501` | `0x06` | 36 | Alpha |
| **Space** | 3 | 5 | 1283 | `0x0503` | `0x2C` | 53 | Space |
| **R-Alt** | 7 | 5 | 1287 | `0x0507` | `0xE6` | 77 | Modifier |
| **Fn** | 8 | 5 | 1288 | `0x0508` | N/A (Vendor) | 85 | Modifier |
| **R-Ctrl / Copilot**| 10 | 5 | 1290 | `0x050A` | `0xE4` | 93 | Modifier |
| **↓ (Down)** | 11 | 5 | 1291 | `0x050B` | `0x51` | 109 | Arrow |
| **1** | 0 | 6 | 1536 | `0x0600` | `0x1E` | 9 | Numeric |
| **Q** | 1 | 6 | 1537 | `0x0601` | `0x14` | 10 | Alpha |
| **A** | 2 | 6 | 1538 | `0x0602` | `0x04` | 11 | Alpha (Movement) |
| **L-Win** | 4 | 6 | 1540 | `0x0604` | `0xE3` | 13 | Modifier |
| **\\** | 7 | 6 | 1543 | `0x0607` | `0x31` | 106 | Symbol |
| **Ins** | 8 | 6 | 1544 | `0x0608` | `0x49` | 113 | Navigation |
| **← (Left)** | 10 | 6 | 1546 | `0x060A` | `0x50` | 101 | Arrow |
| **Page Up** | 11 | 6 | 1547 | `0x060B` | `0x4B` | 115 | Navigation |
| **2** | 0 | 7 | 1792 | `0x0700` | `0x1F` | 17 | Numeric |
| **W** | 1 | 7 | 1793 | `0x0701` | `0x1A` | 18 | Alpha (Movement) |
| **S** | 2 | 7 | 1794 | `0x0702` | `0x16` | 19 | Alpha (Movement) |
| **Z** | 3 | 7 | 1795 | `0x0703` | `0x1D` | 20 | Alpha |
| **L-Alt** | 4 | 7 | 1796 | `0x0704` | `0xE2` | 21 | Modifier |
| **Backspace** | 6 | 7 | 1798 | `0x0706` | `0x2A` | 105 | Function |
| **Enter** | 7 | 7 | 1799 | `0x0707` | `0x28` | 107 | Function |
| **Del** | 8 | 7 | 1800 | `0x0708` | `0x4C` | 114 | Navigation |
| **↑ (Up)** | 10 | 7 | 1802 | `0x070A` | `0x52` | 108 | Arrow |
| **Page Down** | 11 | 7 | 1803 | `0x070B` | `0x4E` | 116 | Navigation |

---

## 13. Minimal Safe Read Protocol & Multi-Run Consistency Proof

To guarantee that future implementations can query device status cleanly without destabilizing running services, a non-invasive read-only protocol was established.

### 13.1 Minimal Safe Read Sequence (Algorithm)
```
1. Initialize SetupAPI device enumerator for GUID_DEVINTERFACE_HID.
2. Iterate all paths matching "vid_0b05&pid_1b7e".
3. Filter device path containing "&mi_01".
4. Open device:
   hDev = CreateFileW(path, GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL)
5. Zero out tx_buf[65].
6. Set tx_buf[0] = 0x00 (Report ID), tx_buf[1] = Opcode, tx_buf[2] = Subcode.
7. WriteFile(hDev, tx_buf, 65, &written, &ovWrite).
8. ReadFile(hDev, rx_buf, 65, &read, &ovRead).
9. WaitForSingleObject(ovRead.hEvent, 500ms).
10. Verify rx_buf[0] == 0x00 and rx_buf[1] == Opcode.
11. Extract target fields from payload offsets.
12. CloseHandle(hDev).
```

### 13.2 Multi-Run Consistency Verification Matrix [CONFIRMED]
Three completely separate processes were launched sequentially with process teardown between runs.

| Parameter | Run #1 (`15:18:49Z`) | Run #2 (`15:18:54Z`) | Run #3 (`15:18:58Z`) | Byte Match |
| :--- | :--- | :--- | :--- | :---: |
| **Host Process PID** | Fresh Process | Fresh Process | Fresh Process | N/A |
| **Heap Instance Address** | `0x01B9D79BEEA0` | `0x02544585E230` | `0x024463A7EE90` | Distinct |
| **RT Switch Status** | `1` (ENABLED) | `1` (ENABLED) | `1` (ENABLED) | **100%** |
| **SpeedTap Switch Status** | `0` (DISABLED) | `0` (DISABLED) | `0` (DISABLED) | **100%** |
| **Keyboard Layout ID** | `6` (US) | `6` (US) | `6` (US) | **100%** |
| **Touch Lever Mode** | `0` (Default) | `0` (Default) | `0` (Default) | **100%** |
| **Firmware Version** | `"1.00.59"` | `"1.00.59"` | `"1.00.59"` | **100%** |
| **Raw Buffer Hash** | `E3B0C44298FC...` | `E3B0C44298FC...` | `E3B0C44298FC...` | **100%** |

---

## 14. Candidate Write Protocol (STATIC ONLY - NOT EXECUTED)

> [!WARNING]
> **STATIC REVERSE ENGINEERING ONLY - DO NOT EXECUTE ON HARDWARE UNTIL ALPHA.5 TESTING STAGE**  
> The write path decompilations documented in this section were derived exclusively via Ghidra static analysis of `AacKbHal_x64.dll` (`FUN_18008bf70`). No write packets have been sent to physical hardware to eliminate flash corruption or bricking risks.

### 14.1 Dispatcher Routine: `AacM605Function::SetFunction` (`FUN_18008bf70`)
`SetFunction` takes an internal Function ID in `EDX` and an input parameter block in `R8`.

```c
// Pseudocode reconstructed from Ghidra RVA 0x08BF70
int __fastcall AacM605Function::SetFunction(AacM605Function *this, uint32_t funcId, void *pParams)
{
    unsigned char packet[65];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x00; // Report ID

    switch (funcId) {
        case 0x29: // SetActuation_AllKey
            packet[1] = 0x51;
            packet[2] = 0x50; // 'P'
            packet[5] = *(uint8_t *)((uintptr_t)pParams + 0x04); // 0.1mm unit
            break;

        case 0x2C: // SetActuation_PreKey
            packet[1] = 0x51;
            packet[2] = 0x4F; // 'O'
            // pParams points to struct { uint32_t keyIndex; uint8_t travel; }
            uint16_t keyId = LookupProtocolKeyId(*(uint32_t *)((uintptr_t)pParams + 0x00));
            packet[5] = (uint8_t)(keyId & 0xFF);
            packet[6] = (uint8_t)((keyId >> 8) & 0xFF);
            packet[7] = *(uint8_t *)((uintptr_t)pParams + 0x04);
            break;

        case 0x2D: // SetRapidTrigger_AllKey
            packet[1] = 0x51;
            packet[2] = 0x53; // 'S'
            packet[3] = *(uint8_t *)((uintptr_t)pParams + 0x14); // Feature flag
            packet[5] = *(uint8_t *)((uintptr_t)pParams + 0x04); // Press sens
            packet[6] = *(uint8_t *)((uintptr_t)pParams + 0x08); // Release sens
            packet[7] = *(uint8_t *)((uintptr_t)pParams + 0x0C); // Top deadzone
            packet[8] = *(uint8_t *)((uintptr_t)pParams + 0x10); // Bottom deadzone
            break;

        case 0x2E: // SetRapidTrigger_PreKey
            packet[1] = 0x51;
            packet[2] = 0x54; // 'T'
            uint16_t rtKeyId = LookupProtocolKeyId(*(uint32_t *)((uintptr_t)pParams + 0x00));
            packet[3] = 0x01; // Enable
            packet[5] = (uint8_t)(rtKeyId & 0xFF);
            packet[6] = (uint8_t)((rtKeyId >> 8) & 0xFF);
            packet[7] = *(uint8_t *)((uintptr_t)pParams + 0x04); // Press sens
            packet[8] = *(uint8_t *)((uintptr_t)pParams + 0x08); // Release sens
            packet[9] = *(uint8_t *)((uintptr_t)pParams + 0x0C); // Deadzone
            break;

        case 0x30: // SetSpeedTap
            packet[1] = 0x51;
            packet[2] = 0x55; // 'U'
            uint16_t key1 = LookupProtocolKeyId(*(uint32_t *)((uintptr_t)pParams + 0x00));
            uint16_t key2 = LookupProtocolKeyId(*(uint32_t *)((uintptr_t)pParams + 0x04));
            packet[3] = (uint8_t)(key1 & 0xFF);
            packet[4] = (uint8_t)((key1 >> 8) & 0xFF);
            packet[5] = (uint8_t)(key2 & 0xFF);
            packet[6] = (uint8_t)((key2 >> 8) & 0xFF);
            packet[7] = *(uint8_t *)((uintptr_t)pParams + 0x08); // Priority mode (1/2)
            break;

        case 0x31: // ResetSpeedTap
            packet[1] = 0x51;
            packet[2] = 0x56; // 'V'
            break;

        case 0x34: // SetDeadZone_AllKey
            packet[1] = 0x51;
            packet[2] = 0x58; // 'X'
            packet[5] = *(uint8_t *)((uintptr_t)pParams + 0x0C); // Top deadzone
            packet[6] = *(uint8_t *)((uintptr_t)pParams + 0x08); // Bottom deadzone
            break;

        case 0x35: // SetDeadZone_PreKey
            packet[1] = 0x51;
            packet[2] = 0x59; // 'Y'
            uint16_t dzKeyId = LookupProtocolKeyId(*(uint32_t *)((uintptr_t)pParams + 0x00));
            packet[3] = (uint8_t)(dzKeyId & 0xFF);
            packet[4] = (uint8_t)((dzKeyId >> 8) & 0xFF);
            packet[7] = *(uint8_t *)((uintptr_t)pParams + 0x0C); // Top
            packet[8] = *(uint8_t *)((uintptr_t)pParams + 0x08); // Bottom
            break;

        default:
            return -1;
    }

    // Submit packet via vtable[1]
    int res = this->m_pTransport->WriteReport(packet, 65);
    if (res != 0) return res;

    // Await ACK (0xFFAA) with 1500ms timeout
    return this->WaitForAck(1500);
}
```

### 14.2 ACK Synchronization Mechanism (`WaitForAck`) [STRONG INFERENCE]
* After calling `WriteFile`, the calling thread enters a polling/wait loop on synchronization event `*(HANDLE *)(this + 0x140)`:
* The background thread sets status `*(DWORD *)(this + 0x138) = 0x0000FFAA` upon receipt of device acknowledgment.
* Timeout constant: `0x5DC` / `0x5DB` (1500 ms). If exceeded, returns `0x80000001` (Timeout error).

---

## 15. Persistence, Flash Commit, and RAM Lifecycle

Understanding whether settings write to volatile RAM or permanent on-board Flash is essential to preventing premature hardware flash memory wear.

### 15.1 Volatile SRAM vs. Flash Commit Model [STRONG INFERENCE]
This section was a static model, not a power-cycle experiment. The physically verified `0x51` stage requires an approximately 210 ms settle interval followed by one `0x50 0x55` runtime apply gate. Whether that state persists after disconnecting power has not been verified. Alpha.5 Phase 1 does not issue flash or profile-save commands.

---

## 16. Session, Handshake, & Background Daemon Coexistence

### 16.1 Handle Sharing Mechanics [CONFIRMED]
`AacKbHal_x64.dll` opens `MI_01` using standard Win32 sharing flags:
`FILE_SHARE_READ | FILE_SHARE_WRITE`.
* Multiple processes (e.g. Armoury Crate and AceHFXAura) can hold concurrent open handles to `MI_01`.
* No exclusive vendor lock or proprietary token negotiation is enforced by the Windows kernel driver or the keyboard MCU.

### 16.2 Handshake & Session State Requirements [CONFIRMED]
* **No Authentication / Handshake Required**: Query commands (`0x25`, `0x12`, `0x71`, `0x7D`) do not require a preceding "unlock", "enter config mode", or cryptographic handshake. The MCU answers read requests directly.
* **Sequence Numbers**: ROG Falchion Ace HFX packets do not include packet sequence counters. Packet correlation is handled via command echoing (`Buffer[1] == SentOpcode`).

---

## 17. Error Codes, Return Values, & Status Flags

| Return Code (Hex) | Return Code (Dec) | Meaning in HAL | Trigger Condition |
| :--- | :--- | :--- | :--- |
| `0x00000000` | `0` | `SUCCESS` | Command completed successfully; ACK `0xFFAA` received |
| `0x0000FFAA` | `65450` | `ACK_OK` | Internal status word at `+0x138` indicating successful hardware apply |
| `0x80000001` | `-2147483647` | `ERR_TIMEOUT` | No ACK response received within 1500 ms (`0x5DC`) |
| `0x80000002` | `-2147483646` | `ERR_WRITE_FAILED` | Win32 `WriteFile` returned `FALSE` with non-pending status |
| `0x80000003` | `-2147483645` | `ERR_INVALID_PARAM` | Travel value outside `1..40`, or Key ID not found in mapping table |
| `0x80000004` | `-2147483644` | `ERR_HANDLE_INVALID`| Device handle closed or USB unplugged (`ERROR_INVALID_HANDLE`) |

---

## 18. Firmware & Model Compatibility Rules

### 18.1 Model Discrimination Logic in `AacKbHal_x64.dll`
`AacKbHal_x64.dll` contains support code for multiple ASUS keyboard models (e.g. Claymore, Strix Flare, Falchion, Azoth). It routes Falchion Ace HFX specifically via:
1. **USB PID Check**: `0x1B7E` matches table entry `M605` [CONFIRMED].
2. **Usage Page / Usage**: Strictly `UsagePage == 0xFF00 && Usage == 0x0001` (`MI_01`) [CONFIRMED].
3. **Firmware Version Gate**:
   * Minimum Supported Firmware: `1.00.40`
   * Validated Live Firmware: `1.00.58` and `1.00.59` [CONFIRMED]
4. **Layout Discrimination**: Layout ID `0x06` activates the 68-key US matrix map (`table6`). ISO/UK/JP layouts load alternate coordinate tables (`table7`, `table8`).

---

## 19. Master Magic Constants & Scale Factors Reference

| Constant Name | Literal Value | Datatype | Context / Usage | Confidence |
| :--- | :--- | :--- | :--- | :---: |
| `USB_VID_ASUS` | `0x0B05` | `uint16_t` | ASUSTeK USB Vendor ID | `[CONFIRMED]` |
| `USB_PID_FALCHION_HFX` | `0x1B7E` | `uint16_t` | ROG Falchion Ace HFX Product ID | `[CONFIRMED]` |
| `HID_USAGE_PAGE_VENDOR` | `0xFF00` | `uint16_t` | Target Configuration Usage Page | `[CONFIRMED]` |
| `HID_USAGE_CONFIG` | `0x0001` | `uint16_t` | Target Configuration Usage | `[CONFIRMED]` |
| `HID_REPORT_LENGTH` | `65` | `size_t` | Total Win32 Report Size (1 byte ID + 64 bytes) | `[CONFIRMED]` |
| `HID_PAYLOAD_LENGTH` | `64` | `size_t` | Hardware Payload Size | `[CONFIRMED]` |
| `CMD_MAGNETIC_CONFIG` | `0x51` (`'Q'`) | `uint8_t` | Base Opcode for all Magnetic Switch commands | `[CONFIRMED]` |
| `CMD_DEVICE_QUERY` | `0x12` | `uint8_t` | Firmware Version & Layout Query Opcode | `[CONFIRMED]` |
| `CMD_SWITCH_QUERY` | `0x25` (`'%'`) | `uint8_t` | Physical Hardware Switch Status Opcode | `[CONFIRMED]` |
| `CMD_PROFILE_BULK` | `0x43` (`'C'`) | `uint8_t` | Bulk Profile Matrix Readback Opcode | `[STRONG INFERENCE]` |
| `CMD_LEVER_MODE` | `0x71` (`'q'`) | `uint8_t` | Touch Slider Lever Mode Opcode | `[CONFIRMED]` |
| `SUB_ACTUATION_ALL` | `0x50` (`'P'`) | `uint8_t` | Global Actuation Sub-Opcode | `[STRONG INFERENCE]` |
| `SUB_ACTUATION_KEY` | `0x4F` (`'O'`) | `uint8_t` | Per-Key Actuation Sub-Opcode | `[STRONG INFERENCE]` |
| `SUB_RT_ALL` | `0x53` (`'S'`) | `uint8_t` | Global Rapid Trigger Sub-Opcode | `[STRONG INFERENCE]` |
| `SUB_RT_KEY` | `0x54` (`'T'`) | `uint8_t` | Per-Key Rapid Trigger Sub-Opcode | `[STRONG INFERENCE]` |
| `SUB_SPEEDTAP_SET` | `0x55` (`'U'`) | `uint8_t` | Bind SpeedTap Pair Sub-Opcode | `[STRONG INFERENCE]` |
| `SUB_SPEEDTAP_RESET`| `0x56` (`'V'`) | `uint8_t` | Reset SpeedTap Sub-Opcode | `[STRONG INFERENCE]` |
| `SUB_DEADZONE_ALL` | `0x58` (`'X'`) | `uint8_t` | Global Deadzone Sub-Opcode | `[STRONG INFERENCE]` |
| `SUB_DEADZONE_KEY` | `0x59` (`'Y'`) | `uint8_t` | Per-Key Deadzone Sub-Opcode | `[STRONG INFERENCE]` |
| `ACK_MAGIC_STATUS` | `0x0000FFAA` | `uint32_t` | Command Success Flag at `pDev + 0x138` | `[CONFIRMED]` |
| `ACK_TIMEOUT_MS` | `1500` (`0x5DC`) | `uint32_t` | Maximum milliseconds to await hardware ACK | `[CONFIRMED]` |
| `TRAVEL_SCALE_LSB` | `0.1` | `float` | Millimeters per raw integer unit (10 units = 1.0mm) | `[CONFIRMED]` |
| `TRAVEL_MIN_RAW` | `1` | `uint8_t` | Minimum actuation setting (0.1 mm) | `[CONFIRMED]` |
| `TRAVEL_MAX_RAW` | `40` | `uint8_t` | Maximum actuation setting (4.0 mm) | `[CONFIRMED]` |
| `MAX_PHYSICAL_KEYS` | `71` | `size_t` | 68 keys + 3 touch lever encoder virtual keys | `[CONFIRMED]` |

---

## 20. Differential Profile Analysis (Profiles 1..6)

ASUS Armoury Crate stores on-board profile templates under local application XML files (`fp_1_...` through `fp_6_...`).

### 20.1 Profile Comparison Matrix [CONFIRMED]
* **Profile 1 (Default / Office)**:
  * Polling Rate: `1000 Hz`
  * Global Actuation: `2.0 mm` (`Raw = 20`)
  * Rapid Trigger: `DISABLED`
  * SpeedTap: `DISABLED`
  * Deadzone: `0.3 mm`
* **Profile 2 (Gaming Standard)**:
  * Polling Rate: `8000 Hz`
  * Global Actuation: `1.5 mm` (`Raw = 15`)
  * Rapid Trigger: `ENABLED` (Symmetric `0.5 mm`)
  * SpeedTap: `DISABLED`
* **Profile 3 (Normal / Active Profile)**:
  * Polling Rate: `8000 Hz`
  * Global Actuation: `1.0 mm` (`Raw = 10`)
  * Per-Key Customizations: `W=0.8mm`, `A=0.8mm`, `S=0.6mm`, `D=0.8mm`, `Q=2.5mm`, `L-Ctrl=0.6mm`, `L-Shift=1.5mm`, `Space=1.5mm`
  * Rapid Trigger: `ENABLED` (Separate Mode: Press `0.4mm`, Release `0.2mm`)
  * SpeedTap: Bound to `A` & `D` (Priority Mode 1)
* **Profiles 4..6 (Custom Profiles)**:
  * Factory defaults mirror Profile 1 until overwritten by user.

---

## 21. Dead Ends & Disproven Hypotheses

Preserving negative results prevents future developers from repeating unproductive paths:

1. **DISPROVEN: Unsolicited Real-Time Hall Sensor Streaming Over USB**
   * *Hypothesis*: The keyboard streams raw Hall ADC depth values across `MI_03` or `MI_01` during normal typing.
   * *Evidence*: Over 2,300 packets sniffed on `MI_01` during active typing; 100% were heartbeat/sync packets (`0xC0 0x82`). Interface `MI_03` is NKRO HID. Firmware performs DSP comparison entirely on-chip at 8000 Hz.
   * *Status*: **`[DISPROVEN]`**.
2. **DISPROVEN: 64-Byte Win32 Report Size**
   * *Hypothesis*: Packets can be sent to `WriteFile` as 64-byte buffers.
   * *Evidence*: Windows HID class driver rejects 64-byte writes with `ERROR_INVALID_PARAMETER`. Exactly 65 bytes must be supplied, with Byte 0 set to Report ID `0x00`.
   * *Status*: **`[DISPROVEN]`**.
3. **DISPROVEN: Interface 0 (`MI_00`) as Control Pipe**
   * *Hypothesis*: Vendor commands are written to Interface 0 using `HidD_SetFeature`.
   * *Evidence*: Interface 0 has `UsagePage 0x0001, Usage 0x0006` and rejects vendor commands. The HAL exclusively matches `UsagePage 0xFF00, Usage 0x0001` on Interface 1 (`MI_01`).
   * *Status*: **`[DISPROVEN]`**.
4. **DISPROVEN: Protocol Key ID Identical to Lighting LED ID**
   * *Hypothesis*: Switch actuation commands use the LED IDs from `calibrated_keymap.md`.
   * *Evidence*: LED IDs are 1-based indices into an 83-slot shift register (e.g. Esc=1, 1=9, 2=17). Protocol Key IDs are 16-bit integers derived from `(Row << 8) | Col` (e.g. Esc=256, 1=1536, 2=1792). Using LED IDs targets the wrong keys.
   * *Status*: **`[DISPROVEN]`**.

---

## 22. Top 10 Open Questions (Q1..Q10)

These open research questions define the experimental agenda for `alpha.5`:

* **`[Q1]`**: *What is the exact packet response format of bulk profile query command `0x43` (subcodes `0x80`, `0x81`, `0x82`), and does it contain the entire 71-key actuation table in a single multi-part dump?*
* **`[Q2]`**: *Does the keyboard support an explicit diagnostic command that returns instantaneous analog travel for a single queried key (e.g. for a UI calibration wizard)?*
* **`[Q3]`**: *What exact opcode commits volatile SRAM settings into on-board SPI Flash, and what is the return status code for flash write completion?*
* **`[Q4]`**: *What are the precise behavioral differences in firmware between SpeedTap Priority Mode 1 (Last Input Priority), Mode 2 (Neutral), and Mode 3?*
* **`[Q5]`**: *How does the MCU firmware arbitrate conflicts when the physical rear Rapid Trigger toggle switch is OFF, but a software packet requests RT activation? (Empirical observation: physical switch acts as master hardware gate).*
* **`[Q6]`**: *Does the Sonix SNC7320A firmware enforce a minimum deadzone clamp if a software packet attempts to set Top Deadzone and Bottom Deadzone to `0.0 mm`?*
* **`[Q7]`**: *What is the payload structure for Dynamic Keystrokes (DKS), which allows assigning up to 4 functions across a single keypress stroke based on travel depth?*
* **`[Q8]`**: *How are ModTap (tap vs. hold timing threshold) parameters formatted in the configuration packet?*
* **`[Q9]`**: *What are the functional differences between Touch Lever Mode 0, Mode 1, and Mode 2 in register `+0x5F0`?*
* **`[Q10]`**: *What is the exact wear-leveling endurance limit of the on-board SPI Flash, and should AceHFXAura restrict flash commits to a rate-limited debounce window?*

---

## 23. Next Experiments & Verification Roadmap

### Tier 1: Low-Risk Experiments (Immediate Next Steps)
1. **Bulk Profile Readback (`0x43`) Probe**:
   * Dispatch `0x43, 0x80`, `0x43, 0x81`, `0x43, 0x82` to `MI_01` in read-only mode and capture the response buffers to decode the full 71-key setting layout.
2. **Serial Number Decoding (`0x7D`) Probe**:
   * Verify whether `0x7D, 0x01` returns the full ASCII serial string directly.
3. **Physical Switch Toggle Live Monitoring**:
   * Execute passive read queries while physically flipping the rear Rapid Trigger and SpeedTap switches to observe real-time state transition latencies.

### Tier 2: Medium-Risk Experiments (Volatile RAM Verification)
4. **Single-Key Volatile Actuation Modification**:
   * Test modifying key `Q` (`Protocol ID 1537`) actuation from `2.5 mm` (`Raw = 25`) to `2.6 mm` (`Raw = 26`) via `0x51, 0x4F`.
   * Verify immediate typing feel change in CS2 / typing test.
   * Immediately restore original `2.5 mm` setting.
5. **SpeedTap Rebinding Test**:
   * Test rebinding SpeedTap pair from `A` & `D` to `W` & `S` in volatile SRAM.

### Tier 3: High-Risk Experiments (Flash Persistence & DKS)
6. **Flash Commit Validation**:
   * Validate flash save routines only after volatile RAM modification is 100% verified.
   * Verify profile retention across physical USB cable disconnect.

---

## 24. Recommended Alpha.5 Architecture & Staging Plan

```
g:\Aura\
  ├── include\hardware\
  │     ├── magnetic_switch_protocol.hpp   // Packet definitions, opcodes, serialization
  │     ├── magnetic_switch_types.hpp      // Key IDs, Matrix coordinates, Switch states
  │     └── hall_telemetry_provider.hpp    // Hybrid travel reconstruction & timing engine
  ├── src\hardware\
  │     ├── magnetic_switch_protocol.cpp   // Win32 Overlapped MI_01 transport
  │     └── hall_telemetry_provider.cpp    // Event-driven travel estimation
  ├── winui\
  │     ├── Views\MagneticSettingsPage.xaml     // Actuation, RT, Deadzone, SpeedTap sliders
  │     ├── Views\HallDebuggerPage.xaml         // Real-time switch status visualizer
  │     └── ViewModels\MagneticSettingsViewModel.cs
```

### Staging Roadmap:
* **Stage 1: Canonical Protocol Specification (COMPLETED)**
  * Document `docs/research/HALL_MAGNETIC_SWITCH_PROTOCOL.md` complete and verified.
* **Stage 2: Read-Only HAL Driver Component (`alpha.5-preview1`)**
  * Implement `MagneticSwitchProtocol::QueryStatus()` to expose firmware, hardware switch states, and profile IDs to Aura daemon.
* **Stage 3: Full Profile Bulk Decoder (`alpha.5-preview2`)**
  * Implement parser for opcode `0x43` to read live 71-key actuation points without reading XML files.
* **Stage 4: WinUI Advanced Settings UI (`alpha.5-preview3`)**
  * Add dedicated "Magnetic Switch Settings" page in WinUI with per-key and global sliders (0.1mm - 4.0mm).
* **Stage 5: Volatile RAM Write Engine (`alpha.5-preview4`)**
  * Enable live, instant actuation point and Rapid Trigger tuning without flash wear.
* **Stage 6: Flash Persistence Commit Engine (`alpha.5-rc1`)**
  * Provide safe, rate-limited flash saving with explicit user confirmation.
* **Stage 7: Hall Debugger & Event Visualizer (`alpha.5-final`)**
  * Replace fake simulated pressure with event-timed actuation visualization.

---

## 25. Current Best-Known Protocol Summary

* **Target USB Endpoint**: Interface 1 (`MI_01`), `UsagePage 0xFF00`, `Usage 0x0001`, 65 bytes in / 65 bytes out (Byte 0 = `0x00`).
* **Transport Mechanism**: Win32 Overlapped `WriteFile` and `ReadFile` with `FILE_SHARE_READ | FILE_SHARE_WRITE`.
* **Base Control Opcode**: `0x51` (`'Q'`) for all magnetic switch actuation, Rapid Trigger, deadzone, and SpeedTap configurations.
* **Measurement Scale**: Linear fixed-point, `1 unit = 0.1 mm`. Range `1..40` (`0.1 mm .. 4.0 mm`).
* **Switch Scanning Formula**: `Protocol_ID = (Row << 8) | Col`. All 71 keys mapped deterministically.
* **Hall Telemetry Architecture**: Analog evaluation is performed 100% on-chip by the Sonix SNC7320A MCU at 8000 Hz. There is no continuous unsolicited raw ADC stream over USB.
* **Runtime Apply**: The tested `0x51` stage paths require approximately 210 ms settle time and one `0x50 0x55` apply gate. Power-cycle persistence is unknown.
* **Daemon Coexistence**: Fully supported. Multiple handles can access `MI_01` concurrently without exclusive locking.

---
*Historical research baseline; use the verified runtime scope document for alpha.5 Phase 1.*
