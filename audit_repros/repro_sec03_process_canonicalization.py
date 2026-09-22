#!/usr/bin/env python3
"""
Reproduction script for Finding SEC-03 (LOW):
Process Name Canonicalizer Over-Rejects Valid Executables with Interior Dots.

Directly tests the logic implemented in CanonicalizeProcessName (automation_service.cpp:48-56):
```cpp
    static const std::string kExeSuffix = ".exe";
    size_t dot_pos = filename.rfind('.');
    if (dot_pos == std::string::npos) {
        filename += kExeSuffix;
    } else {
        std::string ext = filename.substr(dot_pos);
        if (ext != kExeSuffix) {
            return "";
        }
    }
```
"""

def canonicalize_process_name(raw: str) -> str:
    # 1. Trim whitespace
    s = raw.strip()
    if not s:
        return ""
    
    # 2. Extract basename
    import os
    filename = os.path.basename(s).lower()
    if not filename:
        return ""
    
    # 3. Production logic from automation_service.cpp:48-56:
    kExeSuffix = ".exe"
    dot_pos = filename.rfind('.')
    if dot_pos == -1: # std::string::npos
        filename += kExeSuffix
    else:
        ext = filename[dot_pos:]
        if ext != kExeSuffix:
            return "" # REJECTS!
            
    if filename == kExeSuffix:
        return ""
    return filename

def test():
    test_cases = [
        ("cs2", "cs2.exe", True),
        ("cs2.exe", "cs2.exe", True),
        ("foo.dll", "", False), # correctly rejected non-exe
        ("ModernWarfare.v1", "modernwarfare.v1.exe", True), # VALID GAME EXECUTABLE BASENAME!
        ("Game.Shipping", "game.shipping.exe", True),       # COMMON UNREAL ENGINE BINARY!
        ("System.CommandLine", "system.commandline.exe", True), # .NET BINARY!
    ]

    failed = []
    for raw, expected, should_accept in test_cases:
        actual = canonicalize_process_name(raw)
        accepted = (actual != "")
        print(f"Input: {raw!r:25} -> Output: {actual!r:25} (Expected: {expected!r:25})")
        if accepted != should_accept or (accepted and actual != expected):
            failed.append((raw, actual, expected))

    if failed:
        print(f"\n[VULNERABILITY CONFIRMED] {len(failed)} valid process names with interior dots were incorrectly rejected:")
        for raw, actual, expected in failed:
            print(f"  - Input: {raw!r} returned empty string (rejected) instead of {expected!r}")
        return False
    return True

if __name__ == "__main__":
    ok = test()
    exit(0 if not ok else 1)
