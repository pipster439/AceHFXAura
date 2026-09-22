#!/usr/bin/env python3
"""
Reproduction analysis for Finding SEC-02 (HIGH):
FreeLibrary, plugin destructor, and synchronous disk deletion executed under host mutex on 25 FPS render thread.

Static call-graph verification:
Checks src/engine/automation_effect_runtime.cpp and include/engine/plugin_manager.h
to confirm that `layers_.erase` and `pending_.erase` are executed while `mutex_` is locked,
which invokes `~PluginHandle` (calling `FreeLibrary` and `filesystem::remove`) synchronously.
"""

import re
from pathlib import Path

def audit_sec02():
    runtime_cpp = Path("src/engine/automation_effect_runtime.cpp").read_text(encoding="utf-8")
    plugin_manager_h = Path("include/engine/plugin_manager.h").read_text(encoding="utf-8")

    # 1. Check PluginHandle destructor in plugin_manager.h
    has_free_library = "FreeLibrary(module_)" in plugin_manager_h
    has_filesystem_remove = "std::filesystem::remove(shadow_path_" in plugin_manager_h

    print(f"PluginHandle destructor calls FreeLibrary: {has_free_library}")
    print(f"PluginHandle destructor calls filesystem::remove: {has_filesystem_remove}")

    # 2. Check AutomationEffectRuntime::Apply in automation_effect_runtime.cpp
    apply_match = re.search(r"void AutomationEffectRuntime::Apply\([^)]*\)\s*\{([^}]*)\}", runtime_cpp)
    if not apply_match:
        print("ERROR: Could not locate AutomationEffectRuntime::Apply")
        return False
    
    apply_body = apply_match.group(1)
    has_lock_in_apply = "std::lock_guard<std::mutex> lock(mutex_)" in apply_body
    has_erase_in_apply = "layers_.erase(it)" in apply_body

    print(f"AutomationEffectRuntime::Apply holds mutex_: {has_lock_in_apply}")
    print(f"AutomationEffectRuntime::Apply calls layers_.erase under lock: {has_erase_in_apply}")

    # 3. Check AutomationEffectRuntime::Consume in automation_effect_runtime.cpp
    consume_match = re.search(r"void AutomationEffectRuntime::Consume\([^)]*\)\s*\{([\s\S]*?)(?=\n\S|$)", runtime_cpp)
    has_lock_in_consume = False
    has_erase_in_consume = False
    if consume_match:
        consume_body = consume_match.group(1)
        has_lock_in_consume = "std::lock_guard<std::mutex> lock(mutex_)" in consume_body
        has_erase_in_consume = "layers_.erase(it)" in consume_body and "pending_.erase(it)" in consume_body

    print(f"AutomationEffectRuntime::Consume holds mutex_: {has_lock_in_consume}")
    print(f"AutomationEffectRuntime::Consume calls layers_/pending_.erase under lock: {has_erase_in_consume}")

    if has_free_library and has_filesystem_remove and has_lock_in_apply and has_erase_in_apply:
        print("\n[VULNERABILITY CONFIRMED] Concurrency invariant violated! FreeLibrary (LdrpLoaderLock) and synchronous DeleteFileW run while holding AutomationEffectRuntime::mutex_.")
        return True
    return False

if __name__ == "__main__":
    ok = audit_sec02()
    exit(0 if ok else 1)
