# Hardware Protocol and Binary Reverse Engineering Guidelines

This document outlines the official reverse engineering methodology, toolchain architecture, and audit rules for analyzing proprietary HAL libraries and hardware communication protocols in AceHFXAura.

---

## 1. Ethical, Legal, and Architectural Boundaries

1. **Clean-Room Interoperability Goal**: Reverse engineering is conducted solely to achieve device compatibility (e.g., USB HID report formatting, per-key RGB streaming, and electrical pin matrix decoding) for open-source software interoperability.
2. **Zero Commercial Infringement**: No cracking of license verification, no circumvention of digital rights management (DRM), no bypassing of security features, and no binary patching of commercial software.
3. **Zero Production Code Tampering**: Never modify production project code (`src/`, `include/`, `winui/`, `frontend/`) as part of an investigation.

---

## 2. Dual-MCP (Static + Dynamic) Evidence Standard

To prevent false hypotheses or unverified assumptions from entering documentation, all findings must be established through a synchronized dual-MCP evidence chain:

```
               ┌────────────────────────────────────────────────────────┐
               │              Target Binary: AacKbHal_x64.dll           │
               └───────────┬────────────────────────────────┬───────────┘
                           │                                │
                 Static Decompilation             Dynamic Debugging & Verification
                           │                                │
                           ▼                                ▼
                  [Ghidra MCP Server]              [x64dbg MCP Server]
                  - Function AST & CFG             - Live instruction disassembly
                  - VTable offset arrays           - Dynamic ASLR module base
                  - Mathematical formulas         - Buffer memory dumps
                  - Struct member offsets          - Register value tracking
                           │                                │
                           └───────────────┬────────────────┘
                                           │
                                           ▼
                       [Auditable Protocol Evidence Chain]
                       (e.g., docs/hardware/HAL_REVERSE_ENGINEERING_EVIDENCE.md)
```

### Static Analysis Protocol (Ghidra MCP)
- **Ghidra Headless Ingestion**: Ghidra MCP requires an analyzed `.gpr` project. Binaries must first be imported using `analyzeHeadless.bat` into a dedicated project folder (e.g. `C:\Users\ROG\ghidra_projects`).
- **Decompilation and Struct Mapping**: Decompile target functions (e.g. `InitTable`, `Set_L_STD_SINGLE_XY`) to reconstruct data types, loop bounds, and hardware routing formulas.

### Dynamic Verification Protocol (x64dbg MCP)
- **Direct PE Loading**: Load target binaries directly via x64dbg MCP `load_executable`.
- **Address Resolution**: Calculate live runtime addresses by adding function RVAs to the dynamic module base obtained from `get_modules`:
  $$\text{Address}_{\text{runtime}} = \text{Module Base}_{\text{ASLR}} + \text{RVA}$$
- **Live Memory and Assembly Dumps**: Disassemble the actual executing instructions, dump packet buffers, and capture live register states.

---

## 3. Python ctypes Test Harness Standards

When developing standalone verification harnesses:

1. **Mandatory 64-bit Return Type**:
   On 64-bit Windows, `LoadLibraryW.restype` must be explicitly declared as `ctypes.c_void_p`. Omitting this causes 32-bit truncation of module handles, leading to `0xC0000005` access violations.
2. **DLL Directory Resolution**:
   Always execute `kernel32.SetDllDirectoryW(os.path.dirname(dll_path))` before loading libraries with sibling dependencies.
3. **Vendor Fail-Fast Handling**:
   Identify and neutralize vendor diagnostics in volatile memory (e.g., zeroing `EnableLog` at RVA `0x1CB85C` or patching `Logger::Log` at RVA `0x07ABE0` to `0xC3` (`RET`)) to prevent crashes when vendor diagnostic services are absent.

---

## 4. Documentation and Output Artifacts

- **No Root Pollution**: Never commit temporary scripts, disassembly text dumps, or scratch logs to the repository root.
- **Auditable Reports**: Curate permanent reverse engineering conclusions into [`docs/hardware/`](file:///g:/Aura/docs/hardware) (e.g., [`HAL_REVERSE_ENGINEERING_EVIDENCE.md`](file:///g:/Aura/docs/hardware/HAL_REVERSE_ENGINEERING_EVIDENCE.md)).
- **Skill Reuse**: Use the workspace skill [dual-mcp-reverse-engineering](../../.agents/skills/dual-mcp-reverse-engineering/SKILL.md) to automate and replicate this analysis procedure.
