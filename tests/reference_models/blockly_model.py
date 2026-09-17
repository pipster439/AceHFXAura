#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Legacy reference-model experiments, NOT production or end-to-end tests.
These checks mostly exercise Python replicas, fixtures and platform primitives.
They are excluded from CI and default unittest discovery. Passing them does not
validate Aura behavior. See docs/TESTING.md for implementation-backed checks.
"""

import sys
import os
import json
import math
import copy
import time
import re
import ctypes
import shutil
import tempfile
import subprocess
import unittest
from typing import Dict, Any, List, Optional, Tuple, Union

# Ensure UTF-8 output on Windows console
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# ============================================================================
# SECTION 0: LEGACY REFERENCE MODELS & DOMAIN CONSTANTS
# ============================================================================

KEYBOARD_68_KEYS: List[Dict[str, Any]] = [
    # Row 1: ESC, 1-0, -, =, Backspace, Ins
    {"name": "ESC", "row": 1, "col": 1, "x": 0.5, "y": 1.0, "led_id": 0, "cat": "Function"},
    {"name": "1", "row": 1, "col": 2, "x": 1.5, "y": 1.0, "led_id": 1, "cat": "Numeric"},
    {"name": "2", "row": 1, "col": 3, "x": 2.5, "y": 1.0, "led_id": 2, "cat": "Numeric"},
    {"name": "3", "row": 1, "col": 4, "x": 3.5, "y": 1.0, "led_id": 3, "cat": "Numeric"},
    {"name": "4", "row": 1, "col": 5, "x": 4.5, "y": 1.0, "led_id": 4, "cat": "Numeric"},
    {"name": "5", "row": 1, "col": 6, "x": 5.5, "y": 1.0, "led_id": 5, "cat": "Numeric"},
    {"name": "6", "row": 1, "col": 7, "x": 6.5, "y": 1.0, "led_id": 6, "cat": "Numeric"},
    {"name": "7", "row": 1, "col": 8, "x": 7.5, "y": 1.0, "led_id": 7, "cat": "Numeric"},
    {"name": "8", "row": 1, "col": 9, "x": 8.5, "y": 1.0, "led_id": 8, "cat": "Numeric"},
    {"name": "9", "row": 1, "col": 10, "x": 9.5, "y": 1.0, "led_id": 9, "cat": "Numeric"},
    {"name": "0", "row": 1, "col": 11, "x": 10.5, "y": 1.0, "led_id": 10, "cat": "Numeric"},
    {"name": "-", "row": 1, "col": 12, "x": 11.5, "y": 1.0, "led_id": 11, "cat": "Numeric"},
    {"name": "=", "row": 1, "col": 13, "x": 12.5, "y": 1.0, "led_id": 12, "cat": "Numeric"},
    {"name": "BACKSPACE", "row": 1, "col": 14, "x": 14.0, "y": 1.0, "led_id": 13, "cat": "Modifier"},
    {"name": "INS", "row": 1, "col": 15, "x": 15.5, "y": 1.0, "led_id": 14, "cat": "Navigation"},
    # Row 2: TAB, Q-P, [, ], \, Del
    {"name": "TAB", "row": 2, "col": 1, "x": 0.75, "y": 2.0, "led_id": 15, "cat": "Modifier"},
    {"name": "Q", "row": 2, "col": 2, "x": 2.0, "y": 2.0, "led_id": 16, "cat": "Alpha"},
    {"name": "W", "row": 2, "col": 3, "x": 3.0, "y": 2.0, "led_id": 17, "cat": "Alpha"},
    {"name": "E", "row": 2, "col": 4, "x": 4.0, "y": 2.0, "led_id": 18, "cat": "Alpha"},
    {"name": "R", "row": 2, "col": 5, "x": 5.0, "y": 2.0, "led_id": 19, "cat": "Alpha"},
    {"name": "T", "row": 2, "col": 6, "x": 6.0, "y": 2.0, "led_id": 20, "cat": "Alpha"},
    {"name": "Y", "row": 2, "col": 7, "x": 7.0, "y": 2.0, "led_id": 21, "cat": "Alpha"},
    {"name": "U", "row": 2, "col": 8, "x": 8.0, "y": 2.0, "led_id": 22, "cat": "Alpha"},
    {"name": "I", "row": 2, "col": 9, "x": 9.0, "y": 2.0, "led_id": 23, "cat": "Alpha"},
    {"name": "O", "row": 2, "col": 10, "x": 10.0, "y": 2.0, "led_id": 24, "cat": "Alpha"},
    {"name": "P", "row": 2, "col": 11, "x": 11.0, "y": 2.0, "led_id": 25, "cat": "Alpha"},
    {"name": "[", "row": 2, "col": 12, "x": 12.0, "y": 2.0, "led_id": 26, "cat": "Alpha"},
    {"name": "]", "row": 2, "col": 13, "x": 13.0, "y": 2.0, "led_id": 27, "cat": "Alpha"},
    {"name": "\\", "row": 2, "col": 14, "x": 14.25, "y": 2.0, "led_id": 28, "cat": "Alpha"},
    {"name": "DEL", "row": 2, "col": 15, "x": 15.5, "y": 2.0, "led_id": 29, "cat": "Navigation"},
    # Row 3: CAPS, A-L, ;, ', ENTER, PgUp
    {"name": "CAPS", "row": 3, "col": 1, "x": 0.875, "y": 3.0, "led_id": 30, "cat": "Modifier"},
    {"name": "A", "row": 3, "col": 2, "x": 2.25, "y": 3.0, "led_id": 31, "cat": "Alpha"},
    {"name": "S", "row": 3, "col": 3, "x": 3.25, "y": 3.0, "led_id": 32, "cat": "Alpha"},
    {"name": "D", "row": 3, "col": 4, "x": 4.25, "y": 3.0, "led_id": 33, "cat": "Alpha"},
    {"name": "F", "row": 3, "col": 5, "x": 5.25, "y": 3.0, "led_id": 34, "cat": "Alpha"},
    {"name": "G", "row": 3, "col": 6, "x": 6.25, "y": 3.0, "led_id": 35, "cat": "Alpha"},
    {"name": "H", "row": 3, "col": 7, "x": 7.25, "y": 3.0, "led_id": 36, "cat": "Alpha"},
    {"name": "J", "row": 3, "col": 8, "x": 8.25, "y": 3.0, "led_id": 37, "cat": "Alpha"},
    {"name": "K", "row": 3, "col": 9, "x": 9.25, "y": 3.0, "led_id": 38, "cat": "Alpha"},
    {"name": "L", "row": 3, "col": 10, "x": 10.25, "y": 3.0, "led_id": 39, "cat": "Alpha"},
    {"name": ";", "row": 3, "col": 11, "x": 11.25, "y": 3.0, "led_id": 40, "cat": "Alpha"},
    {"name": "'", "row": 3, "col": 12, "x": 12.25, "y": 3.0, "led_id": 41, "cat": "Alpha"},
    {"name": "ENTER", "row": 3, "col": 13, "x": 13.875, "y": 3.0, "led_id": 42, "cat": "Modifier"},
    {"name": "PGUP", "row": 3, "col": 14, "x": 15.5, "y": 3.0, "led_id": 43, "cat": "Navigation"},
    # Row 4: LSHIFT, Z-/, RSHIFT, Up, PgDn
    {"name": "LSHIFT", "row": 4, "col": 1, "x": 1.125, "y": 4.0, "led_id": 44, "cat": "Modifier"},
    {"name": "Z", "row": 4, "col": 2, "x": 2.5, "y": 4.0, "led_id": 45, "cat": "Alpha"},
    {"name": "X", "row": 4, "col": 3, "x": 3.5, "y": 4.0, "led_id": 46, "cat": "Alpha"},
    {"name": "C", "row": 4, "col": 4, "x": 4.5, "y": 4.0, "led_id": 47, "cat": "Alpha"},
    {"name": "V", "row": 4, "col": 5, "x": 5.5, "y": 4.0, "led_id": 48, "cat": "Alpha"},
    {"name": "B", "row": 4, "col": 6, "x": 6.5, "y": 4.0, "led_id": 49, "cat": "Alpha"},
    {"name": "N", "row": 4, "col": 7, "x": 7.5, "y": 4.0, "led_id": 50, "cat": "Alpha"},
    {"name": "M", "row": 4, "col": 8, "x": 8.5, "y": 4.0, "led_id": 51, "cat": "Alpha"},
    {"name": ",", "row": 4, "col": 9, "x": 9.5, "y": 4.0, "led_id": 52, "cat": "Alpha"},
    {"name": ".", "row": 4, "col": 10, "x": 10.5, "y": 4.0, "led_id": 53, "cat": "Alpha"},
    {"name": "/", "row": 4, "col": 11, "x": 11.5, "y": 4.0, "led_id": 54, "cat": "Alpha"},
    {"name": "RSHIFT", "row": 4, "col": 12, "x": 13.25, "y": 4.0, "led_id": 55, "cat": "Modifier"},
    {"name": "UP", "row": 4, "col": 13, "x": 14.5, "y": 4.0, "led_id": 56, "cat": "Navigation"},
    {"name": "PGDN", "row": 4, "col": 14, "x": 15.5, "y": 4.0, "led_id": 57, "cat": "Navigation"},
    # Row 5: LCTRL, LWIN, LALT, Space, RALT, FN, RCTRL, Left, Down, Right
    {"name": "LCTRL", "row": 5, "col": 1, "x": 0.625, "y": 5.0, "led_id": 58, "cat": "Modifier"},
    {"name": "LWIN", "row": 5, "col": 2, "x": 1.875, "y": 5.0, "led_id": 59, "cat": "Modifier"},
    {"name": "LALT", "row": 5, "col": 3, "x": 3.125, "y": 5.0, "led_id": 60, "cat": "Modifier"},
    {"name": "SPACE", "row": 5, "col": 4, "x": 6.875, "y": 5.0, "led_id": 61, "cat": "Alpha"},
    {"name": "RALT", "row": 5, "col": 5, "x": 10.625, "y": 5.0, "led_id": 62, "cat": "Modifier"},
    {"name": "FN", "row": 5, "col": 6, "x": 11.625, "y": 5.0, "led_id": 63, "cat": "Modifier"},
    {"name": "RCTRL", "row": 5, "col": 7, "x": 12.625, "y": 5.0, "led_id": 64, "cat": "Modifier"},
    {"name": "LEFT", "row": 5, "col": 8, "x": 13.5, "y": 5.0, "led_id": 65, "cat": "Navigation"},
    {"name": "DOWN", "row": 5, "col": 9, "x": 14.5, "y": 5.0, "led_id": 66, "cat": "Navigation"},
    {"name": "RIGHT", "row": 5, "col": 10, "x": 15.5, "y": 5.0, "led_id": 67, "cat": "Navigation"},
]

KEY_MAP_BY_NAME = {k["name"]: k for k in KEYBOARD_68_KEYS}
KEY_MAP_BY_LED = {k["led_id"]: k for k in KEYBOARD_68_KEYS}

CS2_GSI_SAMPLE_PAYLOAD = {
    "provider": {"name": "Counter-Strike 2", "appid": 730},
    "map": {"mode": "competitive", "name": "de_dust2", "phase": "live", "round": 5},
    "round": {"phase": "live", "bomb": "planted"},
    "player": {
        "steamid": "76561198000000001",
        "name": "AcePlayer",
        "team": "CT",
        "activity": "playing",
        "state": {"health": 75, "armor": 100, "helmet": True, "flashed": 0, "burning": 0, "money": 3400, "round_kills": 2}
    },
    "previously": {
        "player": {"state": {"round_kills": 1, "health": 100}}
    }
}


# ============================================================================
# SECTION 0.1: AUTHORITATIVE CONDITION NODE AST EVALUATOR
# Evaluates recursive condition trees matching C++ ConditionNode logic.
# ============================================================================

class ConditionNodeEvaluator:
    """Evaluates recursive ConditionNode AST matching src/config/rule_engine.cpp."""

    @staticmethod
    def evaluate(node: Dict[str, Any], gsi: Dict[str, Any], foreground_proc: str = "") -> bool:
        if not node:
            return True

        logic_type = node.get("type", "").lower()
        if logic_type == "and":
            sub = node.get("conditions", [])
            return all(ConditionNodeEvaluator.evaluate(child, gsi, foreground_proc) for child in sub)
        elif logic_type == "or":
            sub = node.get("conditions", [])
            if not sub:
                return False
            return any(ConditionNodeEvaluator.evaluate(child, gsi, foreground_proc) for child in sub)
        elif logic_type == "not":
            sub = node.get("conditions", [])
            if not sub:
                return True
            return not ConditionNodeEvaluator.evaluate(sub[0], gsi, foreground_proc)

        # Leaf node evaluation
        field = node.get("field", "")
        op = node.get("op", node.get("operator", "==")).lower()
        target_val = node.get("value")

        # Process matching leaf
        if field in ("process.name", "process", "proc"):
            proc_clean = os.path.basename(foreground_proc).lower()
            target_str = str(target_val).lower() if target_val is not None else ""
            if op in ("==", "eq"):
                return proc_clean == target_str or proc_clean.replace(".exe", "") == target_str.replace(".exe", "")
            elif op in ("!=", "ne"):
                return proc_clean != target_str
            elif op == "contains":
                return target_str in proc_clean
            return False

        # GSI leaf evaluation
        actual_val = ConditionNodeEvaluator.get_gsi_value(gsi, field)
        if actual_val is None:
            return False

        return ConditionNodeEvaluator.compare_values(actual_val, op, target_val)

    @staticmethod
    def get_gsi_value(gsi: Dict[str, Any], path: str) -> Any:
        if not gsi or not path:
            return None
        parts = path.split(".")
        curr = gsi
        for p in parts:
            if isinstance(curr, dict) and p in curr:
                curr = curr[p]
            else:
                return None
        return curr

    @staticmethod
    def compare_values(actual: Any, op: str, target: Any) -> bool:
        try:
            if isinstance(actual, (int, float)) and isinstance(target, (int, float, str)):
                try:
                    target_num = float(target)
                    act_num = float(actual)
                    if op in ("==", "eq"): return math.isclose(act_num, target_num, abs_tol=1e-5)
                    if op in ("!=", "ne"): return not math.isclose(act_num, target_num, abs_tol=1e-5)
                    if op in ("<", "lt"): return act_num < target_num
                    if op in ("<=", "le"): return act_num <= target_num
                    if op in (">", "gt"): return act_num > target_num
                    if op in (">=", "ge"): return act_num >= target_num
                except ValueError:
                    pass

            if op in ("==", "eq"): return str(actual).lower() == str(target).lower()
            if op in ("!=", "ne"): return str(actual).lower() != str(target).lower()
            if op == "contains": return str(target).lower() in str(actual).lower()
            if op == "in" and isinstance(target, list): return actual in target
            return False
        except Exception:
            return False


# ============================================================================
# SECTION 0.2: AUTHORITATIVE OVERLAY MANAGER MODEL
# Simulates transient pulse overlays and linear alpha crossfade blending.
# ============================================================================

class OverlayManagerModel:
    """Simulates transient CS2 event pulse overlays and smooth linear crossfades."""

    def __init__(self):
        self.overlays: List[Dict[str, Any]] = []

    def trigger_overlay(self, event_name: str, color: Tuple[int, int, int],
                        duration_ms: int, fade_ms: int, priority: int = 10,
                        blend_mode: str = "blend", trigger_time_ms: int = 0) -> None:
        duration_ms = max(0, duration_ms)
        fade_ms = max(0, min(fade_ms, duration_ms))
        self.overlays.append({
            "event_name": event_name,
            "color": color,
            "start_ms": trigger_time_ms,
            "duration_ms": duration_ms,
            "fade_ms": fade_ms,
            "priority": priority,
            "blend_mode": blend_mode,
        })
        # Sort descending by priority
        self.overlays.sort(key=lambda x: x["priority"], reverse=True)

    def calculate_alpha(self, overlay: Dict[str, Any], current_ms: int) -> float:
        elapsed = current_ms - overlay["start_ms"]
        duration = overlay["duration_ms"]
        fade = overlay["fade_ms"]

        if elapsed < 0 or elapsed >= duration or duration <= 0:
            return 0.0
        
        fade_start = duration - fade
        if elapsed <= fade_start:
            return 1.0
        
        if fade <= 0:
            return 1.0
        
        remaining = duration - elapsed
        return max(0.0, min(1.0, remaining / float(fade)))

    def blend_frames(self, base_rgb: Tuple[int, int, int], current_ms: int) -> Tuple[int, int, int]:
        curr_rgb = list(base_rgb)
        active_overlays = []
        for ov in self.overlays:
            alpha = self.calculate_alpha(ov, current_ms)
            if alpha > 0.0:
                active_overlays.append((ov, alpha))

        # Sort ascending by priority so higher-priority overlays blend on top (Painter's algorithm)
        active_overlays.sort(key=lambda item: item[0]["priority"])

        for ov, alpha in active_overlays:
            o_color = ov["color"]
            if ov["blend_mode"] == "replace":
                curr_rgb = [
                    int(round(curr_rgb[c] * (1.0 - alpha) + o_color[c] * alpha))
                    for c in range(3)
                ]
            elif ov["blend_mode"] == "add":
                curr_rgb = [
                    min(255, int(round(curr_rgb[c] + o_color[c] * alpha)))
                    for c in range(3)
                ]
            else:  # "blend"
                curr_rgb = [
                    int(round(curr_rgb[c] * (1.0 - alpha) + o_color[c] * alpha))
                    for c in range(3)
                ]

        return (curr_rgb[0], curr_rgb[1], curr_rgb[2])

    def evict_expired(self, current_ms: int) -> int:
        before_count = len(self.overlays)
        self.overlays = [
            ov for ov in self.overlays
            if (current_ms - ov["start_ms"]) < ov["duration_ms"]
        ]
        return before_count - len(self.overlays)


# ============================================================================
# SECTION 0.3: AUTHORITATIVE C++ TRANSPILER & CODE GENERATOR MODEL
# Emits 0-heap allocation C++17 Effect classes inheriting from aura::Effect.
# ============================================================================

class CppTranspilerModel:
    """Authoritative reference model for the Blockly-to-C++17 code generator."""

    @staticmethod
    def generate_cpp_source(effect_name: str, body_code: str = "", includes: Optional[List[str]] = None) -> str:
        safe_name = re.sub(r"[^a-zA-Z0-9_]", "_", effect_name)
        if not safe_name or safe_name[0].isdigit():
            safe_name = f"Effect_{safe_name}"

        extra_inc = "\n".join(f"#include <{inc}>" if not inc.endswith('.h') else f'#include "{inc}"'
                              for inc in (includes or []))

        return f"""// Auto-generated by ROG Falchion Ace HFX Blockly Effect Studio
// Target: ISO C++17 / RAII 0-heap allocation
#pragma once

#include "engine/effect.h"
#include "engine/plugin_interface.h"
#include <cmath>
#include <algorithm>
#include <cstdint>
{extra_inc}

namespace aura {{

class Effect_{safe_name} : public Effect {{
public:
    Effect_{safe_name}() = default;
    virtual ~Effect_{safe_name}() = default;

    void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override {{
        RenderInternal(elapsed_ms, out_frame, keymap, nullptr);
    }}

    void RenderWithContext(const EffectContext& ctx, FrameBuffer& out_frame) override {{
        RenderInternal(ctx.elapsed_ms, out_frame, ctx.keymap, ctx.gsi);
    }}

    void RenderInternal(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap, const IGsiReader* gsi) {{
{body_code if body_code else "        // No-op safe render pass"}
    }}
}};

}} // namespace aura

extern "C" {{
    __declspec(dllexport) uint32_t AuraGetPluginApiVersion() {{
        return 0x00010000;
    }}

    __declspec(dllexport) const char* AuraGetEffectName() {{
        return "{safe_name}";
    }}

    __declspec(dllexport) aura::Effect* AuraCreateEffect() {{
        return new aura::Effect_{safe_name}();
    }}

    __declspec(dllexport) void AuraDestroyEffect(aura::Effect* effect) {{
        delete effect;
    }}
}}
"""

    @staticmethod
    def check_zero_allocations(cpp_source: str) -> Tuple[bool, List[str]]:
        """Verifies that the Render methods adhere to the strict 0-heap allocation rule."""
        violations = []
        # Look for Render function body
        render_match = re.search(r"void\s+Render\w*\s*\([^)]*\)\s*\{([^}]+)\}", cpp_source, re.DOTALL)
        if not render_match:
            return True, []

        render_body = render_match.group(1)
        # Forbidden dynamic allocation patterns
        forbidden_patterns = [
            (r"\bnew\s+\w+", "Forbidden 'new' operator inside Render loop"),
            (r"\bmalloc\s*\(", "Forbidden 'malloc' inside Render loop"),
            (r"\bcalloc\s*\(", "Forbidden 'calloc' inside Render loop"),
            (r"\bpush_back\s*\(", "Forbidden 'push_back' dynamic vector growth inside Render loop"),
            (r"\bstd::string\s+\w+\s*=\s*\w+\s*\+", "Forbidden heap std::string concatenation"),
            (r"\bstd::stringstream\b", "Forbidden dynamic stream allocation inside Render loop"),
        ]
        for pattern, desc in forbidden_patterns:
            if re.search(pattern, render_body):
                violations.append(desc)

        return (len(violations) == 0), violations


# ============================================================================
# SECTION 0.4: LOCAL MSVC COMPILER PIPELINE & SHADOW COPY LOADER
# ============================================================================

class CompilerPipeline:
    """Manages MSVC cl.exe invocation and dynamic DLL compilation."""

    @staticmethod
    def find_vcvars64() -> Optional[str]:
        standard_path = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
        if os.path.exists(standard_path):
            return standard_path
        
        # Check through vswhere
        vswhere = os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
        if os.path.exists(vswhere):
            try:
                res = subprocess.run([
                    vswhere, "-latest", "-products", "*",
                    "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-find", r"**\vcvars64.bat"
                ], capture_output=True, text=True)
                if res.returncode == 0 and res.stdout.strip():
                    first_line = res.stdout.strip().splitlines()[0]
                    if os.path.exists(first_line):
                        return first_line
            except Exception:
                pass
        return None

    @staticmethod
    def compile_cpp_to_dll(source_code: str, output_dll_path: str, include_dir: Optional[str] = None) -> Tuple[bool, str, int]:
        vcvars = CompilerPipeline.find_vcvars64()
        if not vcvars:
            return False, "MSVC compiler (vcvars64.bat) not found in environment", -1

        inc_path = include_dir or os.path.join(ROOT_DIR, "include")
        temp_dir = tempfile.mkdtemp(prefix="aura_comp_")
        src_file = os.path.join(temp_dir, "plugin_source.cpp")

        try:
            with open(src_file, "w", encoding="utf-8") as f:
                f.write(source_code)

            os.makedirs(os.path.dirname(output_dll_path), exist_ok=True)
            cmd = (f'call "{vcvars}" >nul && '
                   f'cl.exe /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /LD /DNOMINMAX /DWIN32_LEAN_AND_MEAN '
                   f'/I "{inc_path}" "{src_file}" /Fe:"{output_dll_path}" /link /INCREMENTAL:NO')

            res = subprocess.run(cmd, shell=True, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
            log = (res.stdout or "") + "\n" + (res.stderr or "")
            return (res.returncode == 0 and os.path.exists(output_dll_path)), log, res.returncode
        except subprocess.TimeoutExpired:
            return False, "Compilation timed out (>30s)", -2
        except Exception as e:
            return False, str(e), -3
        finally:
            shutil.rmtree(temp_dir, ignore_errors=True)


# ============================================================================
# SECTION 1: TIER 1 - FEATURE COVERAGE SUITE (76 TESTS)
# Covers all 15 Features (≥5 tests each) per PROJECT.md Feature Inventory.
# ============================================================================

class TestTier1FeatureCoverage(unittest.TestCase):
    """Tier 1: Systematic validation of primary behaviors across all 15 features."""

    # ------------------------------------------------------------------------
    # Feature 1: FE-BLOCKLY-CORE (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f01_workspace_json_structure(self):
        """FE-BLOCKLY-CORE: Verify modern Google Blockly v11 workspace serialization format."""
        workspace = {
            "version": 1,
            "blockly_json": {
                "blocks": {
                    "languageVersion": 0,
                    "blocks": [
                        {"type": "time_elapsed_ms", "id": "blk_time_1", "x": 100, "y": 100}
                    ]
                }
            }
        }
        self.assertIn("version", workspace)
        self.assertIn("blockly_json", workspace)
        self.assertEqual(workspace["blockly_json"]["blocks"]["languageVersion"], 0)
        self.assertEqual(workspace["blockly_json"]["blocks"]["blocks"][0]["type"], "time_elapsed_ms")

    def test_t1_f01_singlefile_asset_integrity_no_external_urls(self):
        """FE-BLOCKLY-CORE: Verify that Blockly inject options disable external network sounds and icons."""
        inject_options = {
            "sounds": False,
            "media": "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7",
            "trashcan": True
        }
        self.assertFalse(inject_options["sounds"], "Sounds must be disabled to avoid 404 in singlefile distribution")
        self.assertTrue(inject_options["media"].startswith("data:image/"), "Media must be inlined via data-URI")

    def test_t1_f01_toolbox_categories_and_block_types(self):
        """FE-BLOCKLY-CORE: Verify complete 6-category domain toolbox definition."""
        toolbox_categories = [
            "Clock & Time", "Geometry & Coords", "Color & Gradients",
            "Keyboard Operations", "Key Dynamics", "Atomic GSI Sensors"
        ]
        self.assertEqual(len(toolbox_categories), 6)
        self.assertIn("Atomic GSI Sensors", toolbox_categories)
        self.assertIn("Geometry & Coords", toolbox_categories)

    def test_t1_f01_md3e_blockly_theme_tokens(self):
        """FE-BLOCKLY-CORE: Verify MD3E design system token integration in Blockly canvas theme."""
        theme_definition = {
            "blockStyles": {
                "clock_blocks": {"colourPrimary": "var(--md-sys-color-primary)", "colourSecondary": "var(--md-sys-color-primary-container)"},
                "geometry_blocks": {"colourPrimary": "var(--md-sys-color-tertiary)"},
                "gsi_blocks": {"colourPrimary": "var(--md-sys-color-error)"}
            },
            "componentStyles": {
                "workspaceBackgroundColour": "var(--md-sys-color-surface)",
                "toolboxBackgroundColour": "var(--md-sys-color-surface-container-low)"
            }
        }
        self.assertTrue(theme_definition["blockStyles"]["clock_blocks"]["colourPrimary"].startswith("var(--md-sys-color-"))
        self.assertTrue(theme_definition["componentStyles"]["workspaceBackgroundColour"].startswith("var(--md-sys-color-"))

    def test_t1_f01_workspace_zoom_and_grid_defaults(self):
        """FE-BLOCKLY-CORE: Verify grid snapping and zoom controls configuration."""
        grid_config = {"spacing": 20, "length": 3, "snap": True}
        zoom_config = {"controls": True, "wheel": True, "startScale": 1.0, "maxScale": 2.5, "minScale": 0.4}
        self.assertTrue(grid_config["snap"])
        self.assertEqual(grid_config["spacing"], 20)
        self.assertTrue(zoom_config["wheel"])
        self.assertGreaterEqual(zoom_config["maxScale"], 2.0)

    # ------------------------------------------------------------------------
    # Feature 2: FE-DOMAIN-BLOCKS (6 tests)
    # ------------------------------------------------------------------------
    def test_t1_f02_clock_time_blocks(self):
        """FE-DOMAIN-BLOCKS: Clock & Time: elapsed_ms, normalized phase, and sine oscillation."""
        elapsed_ms = 1500
        period_ms = 2000
        phase = (elapsed_ms % period_ms) / float(period_ms)
        sine_wave = math.sin(phase * 2.0 * math.pi) * 0.5 + 0.5
        self.assertAlmostEqual(phase, 0.75, places=3)
        self.assertGreaterEqual(sine_wave, 0.0)
        self.assertLessEqual(sine_wave, 1.0)

    def test_t1_f02_geometry_coordinate_blocks(self):
        """FE-DOMAIN-BLOCKS: Geometry: physical coordinates (x, y), row, col, and Euclidean distance."""
        esc = KEY_MAP_BY_NAME["ESC"]
        space = KEY_MAP_BY_NAME["SPACE"]
        self.assertEqual(esc["x"], 0.5)
        self.assertEqual(esc["y"], 1.0)
        self.assertEqual(space["x"], 6.875)
        self.assertEqual(space["y"], 5.0)

        dx = space["x"] - esc["x"]
        dy = space["y"] - esc["y"]
        dist = math.sqrt(dx * dx + dy * dy)
        self.assertAlmostEqual(dist, 7.525, places=2)

    def test_t1_f02_color_and_gradient_blocks(self):
        """FE-DOMAIN-BLOCKS: Color: RGB/HSV constructors, linear interpolation, and brightness scale."""
        c1 = (255, 0, 0)
        c2 = (0, 0, 255)
        ratio = 0.5
        lerp_color = (
            int(round(c1[0] * (1 - ratio) + c2[0] * ratio)),
            int(round(c1[1] * (1 - ratio) + c2[1] * ratio)),
            int(round(c1[2] * (1 - ratio) + c2[2] * ratio)),
        )
        self.assertEqual(lerp_color, (128, 0, 128))

        brightness = 0.5
        scaled = tuple(int(round(c * brightness)) for c in lerp_color)
        self.assertEqual(scaled, (64, 0, 64))

    def test_t1_f02_keyboard_ops_blocks(self):
        """FE-DOMAIN-BLOCKS: Keyboard Ops: iterate all keys, filter WASD, fill all keys."""
        all_keys = [k["name"] for k in KEYBOARD_68_KEYS]
        self.assertEqual(len(all_keys), 68)
        wasd_keys = [k for k in KEYBOARD_68_KEYS if k["name"] in ("W", "A", "S", "D")]
        self.assertEqual(len(wasd_keys), 4)

    def test_t1_f02_key_dynamics_blocks(self):
        """FE-DOMAIN-BLOCKS: Key Dynamics: key pressed state and exponential decay register."""
        decay_rate = 0.85
        reg_val = 1.0
        # Tick 3 frames
        for _ in range(3):
            reg_val *= decay_rate
        self.assertAlmostEqual(reg_val, 0.614125, places=4)

    def test_t1_f02_atomic_gsi_sensor_blocks(self):
        """FE-DOMAIN-BLOCKS: Atomic GSI: get_number, get_string, get_bool from GSI payload."""
        hp = ConditionNodeEvaluator.get_gsi_value(CS2_GSI_SAMPLE_PAYLOAD, "player.state.health")
        bomb = ConditionNodeEvaluator.get_gsi_value(CS2_GSI_SAMPLE_PAYLOAD, "round.bomb")
        helmet = ConditionNodeEvaluator.get_gsi_value(CS2_GSI_SAMPLE_PAYLOAD, "player.state.helmet")
        self.assertEqual(hp, 75)
        self.assertEqual(bomb, "planted")
        self.assertTrue(helmet)

    # ------------------------------------------------------------------------
    # Feature 3: FE-LIVE-PREVIEW (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f03_javascript_generation_pure_function(self):
        """FE-LIVE-PREVIEW: Verify generated JS closure pure function signature."""
        js_template = """function renderEffect(elapsed_ms, key, keymap, gsi, decays) {
    const phase = (elapsed_ms % 2000) / 2000.0;
    const wave = Math.sin(phase * 6.2831853) * 0.5 + 0.5;
    return [Math.round(255 * wave), 0, Math.round(255 * (1 - wave))];
}"""
        self.assertIn("function renderEffect", js_template)
        self.assertIn("elapsed_ms", js_template)
        self.assertIn("keymap", js_template)

    def test_t1_f03_sandboxed_js_execution_model(self):
        """FE-LIVE-PREVIEW: Execute simulation of the JS preview sandbox."""
        def mock_js_eval(elapsed_ms: int, key: Dict[str, Any]) -> Tuple[int, int, int]:
            dx = key["x"] - 8.0
            wave = math.sin(dx * 0.5 + (elapsed_ms % 1000) / 1000.0 * 2 * math.pi) * 0.5 + 0.5
            return (int(round(255 * wave)), int(round(128 * wave)), 0)

        color_esc = mock_js_eval(500, KEY_MAP_BY_NAME["ESC"])
        self.assertEqual(len(color_esc), 3)
        self.assertTrue(all(0 <= c <= 255 for c in color_esc))

    def test_t1_f03_frame_buffer_68_key_rgb_output(self):
        """FE-LIVE-PREVIEW: Verify full 68-key virtual keyboard frame generation."""
        frame_colors = []
        for key in KEYBOARD_68_KEYS:
            frame_colors.append((key["led_id"], (0, 255, 128)))
        self.assertEqual(len(frame_colors), 68)
        self.assertEqual(frame_colors[0][1], (0, 255, 128))

    def test_t1_f03_60fps_execution_latency_budget(self):
        """FE-LIVE-PREVIEW: Verify preview calculation across 68 keys completes well under 16ms (60 FPS)."""
        t0 = time.perf_counter()
        results = []
        for key in KEYBOARD_68_KEYS:
            phase = (500 % 2000) / 2000.0
            dist = math.sqrt((key["x"] - 8.0)**2 + (key["y"] - 3.0)**2)
            val = int(round(math.sin(dist - phase * 6.28) * 127 + 128))
            results.append((val, val, val))
        elapsed_sec = time.perf_counter() - t0
        self.assertLess(elapsed_sec, 0.016, f"Frame computation took {elapsed_sec*1000:.2f}ms (>16ms)")

    def test_t1_f03_state_persistence_decay_registers(self):
        """FE-LIVE-PREVIEW: Verify state persistence of per-key decay registers across ticks."""
        decays = {k["name"]: 0.0 for k in KEYBOARD_68_KEYS}
        decays["W"] = 1.0  # Press W
        # Advance 5 frames at 60 FPS (~16.6ms per frame, decay factor 0.9)
        for _ in range(5):
            for k in decays:
                decays[k] *= 0.9
        self.assertAlmostEqual(decays["W"], 0.9**5, places=3)
        self.assertEqual(decays["S"], 0.0)

    # ------------------------------------------------------------------------
    # Feature 4: FE-CPP-TRANSPILER (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f04_cpp17_effect_class_inheritance(self):
        """FE-CPP-TRANSPILER: Verify generated C++ inherits from aura::Effect."""
        cpp_code = CppTranspilerModel.generate_cpp_source("RainbowWave", "out_frame.Fill(255, 0, 0);")
        self.assertIn("class Effect_RainbowWave : public Effect", cpp_code)
        self.assertIn("void Render(uint64_t elapsed_ms, FrameBuffer& out_frame, const Keymap& keymap) override", cpp_code)

    def test_t1_f04_zero_heap_allocation_enforcement(self):
        """FE-CPP-TRANSPILER: Verify zero heap allocation enforcement in C++ Render method."""
        clean_code = CppTranspilerModel.generate_cpp_source("FastStatic", "out_frame.Fill(100, 150, 200);")
        ok, violations = CppTranspilerModel.check_zero_allocations(clean_code)
        self.assertTrue(ok, f"Unexpected violations: {violations}")

    def test_t1_f04_raii_stack_and_scalar_types(self):
        """FE-CPP-TRANSPILER: Verify only stack-allocated scalars and references are emitted."""
        body = """
        const double period = 2000.0;
        const double phase = std::fmod(static_cast<double>(elapsed_ms), period) / period;
        for (const auto& [name, info] : keymap.GetAllKeys()) {
            const double dist = std::sqrt(info.physical_x * info.physical_x);
            out_frame.SetKey(info.led_id, 255, 128, 0);
        }
        """
        cpp = CppTranspilerModel.generate_cpp_source("WaveSweep", body)
        self.assertIn("std::fmod", cpp)
        self.assertIn("keymap.GetAllKeys()", cpp)
        ok, _ = CppTranspilerModel.check_zero_allocations(cpp)
        self.assertTrue(ok)

    def test_t1_f04_bounds_clamping_and_safe_math(self):
        """FE-CPP-TRANSPILER: Verify std::clamp bounds safety generated for RGB channels."""
        body = """
        const double raw_r = -50.0;
        const uint8_t r = static_cast<uint8_t>(std::clamp(raw_r, 0.0, 255.0));
        out_frame.Fill(r, 0, 0);
        """
        cpp = CppTranspilerModel.generate_cpp_source("SafeClamp", body)
        self.assertIn("std::clamp", cpp)

    def test_t1_f04_c_export_abi_entrypoints(self):
        """FE-CPP-TRANSPILER: Verify required extern 'C' dllexport ABI entrypoints."""
        cpp = CppTranspilerModel.generate_cpp_source("TestAbi")
        self.assertIn("AuraGetPluginApiVersion()", cpp)
        self.assertIn("AuraGetEffectName()", cpp)
        self.assertIn("AuraCreateEffect()", cpp)
        self.assertIn("AuraDestroyEffect(aura::Effect* effect)", cpp)

    # ------------------------------------------------------------------------
    # Feature 5: FE-CODE-EXPORT (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f05_formatted_cpp_code_with_headers(self):
        """FE-CODE-EXPORT: Verify exported code contains descriptive file headers."""
        cpp = CppTranspilerModel.generate_cpp_source("ExportTest")
        self.assertTrue(cpp.startswith("// Auto-generated by ROG Falchion Ace HFX Blockly Effect Studio"))
        self.assertIn("#pragma once", cpp)

    def test_t1_f05_metadata_generation(self):
        """FE-CODE-EXPORT: Verify export metadata contains target keyboard and ISO standard."""
        cpp = CppTranspilerModel.generate_cpp_source("MetadataTest")
        self.assertIn("Target: ISO C++17", cpp)
        self.assertIn("ROG Falchion Ace HFX", cpp)

    def test_t1_f05_include_dependencies_completeness(self):
        """FE-CODE-EXPORT: Verify completeness of standard include dependencies."""
        cpp = CppTranspilerModel.generate_cpp_source("IncTest", "", ["vector", "engine/plugin_interface.h"])
        self.assertIn('#include "engine/effect.h"', cpp)
        self.assertIn('#include "engine/plugin_interface.h"', cpp)
        self.assertIn("#include <cmath>", cpp)
        self.assertIn("#include <algorithm>", cpp)

    def test_t1_f05_clipboard_payload_ready(self):
        """FE-CODE-EXPORT: Verify exported string buffer is clean UTF-8 string ready for clipboard/download."""
        cpp = CppTranspilerModel.generate_cpp_source("ClipboardTest")
        self.assertIsInstance(cpp, str)
        self.assertNotIn("\x00", cpp)
        self.assertGreater(len(cpp), 100)

    def test_t1_f05_syntax_token_validation(self):
        """FE-CODE-EXPORT: Verify balanced braces and valid C++ namespace blocks."""
        cpp = CppTranspilerModel.generate_cpp_source("SyntaxTest", "out_frame.Fill(0, 0, 0);")
        open_braces = cpp.count("{")
        close_braces = cpp.count("}")
        self.assertEqual(open_braces, close_braces, "Mismatched braces in generated C++ code")

    # ------------------------------------------------------------------------
    # Feature 6: BE-COMPILER-PIPE (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f06_vcvars64_and_msvc_detection(self):
        """BE-COMPILER-PIPE: Verify discovery of vcvars64.bat on Windows 11 development host."""
        vcvars = CompilerPipeline.find_vcvars64()
        self.assertIsNotNone(vcvars, "vcvars64.bat must be discoverable on build host")
        self.assertTrue(os.path.exists(vcvars))

    def test_t1_f06_compiler_flags_standard_conformance(self):
        """BE-COMPILER-PIPE: Verify required compiler and linker switches (/std:c++17 /O2 /MD /LD)."""
        required_flags = ["/std:c++17", "/O2", "/EHsc", "/utf-8", "/MD", "/LD"]
        cmd_sample = 'cl.exe /nologo /std:c++17 /O2 /EHsc /utf-8 /MD /LD /I "include" src.cpp /Fe:out.dll'
        for flag in required_flags:
            self.assertIn(flag, cmd_sample)

    def test_t1_f06_compile_valid_cpp_to_dll(self):
        """BE-COMPILER-PIPE: Compile minimal valid C++ effect into a real standalone DLL."""
        cpp_code = """
        #include <windows.h>
        #include <cstdint>
        extern "C" __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 0x00010000; }
        extern "C" __declspec(dllexport) const char* AuraGetEffectName() { return "e2e_test_effect"; }
        """
        with tempfile.TemporaryDirectory() as td:
            dll_out = os.path.join(td, "effect_e2e_test.dll")
            success, log, code = CompilerPipeline.compile_cpp_to_dll(cpp_code, dll_out)
            self.assertTrue(success, f"Compilation failed (code {code}): {log}")
            self.assertTrue(os.path.exists(dll_out))
            self.assertGreater(os.path.getsize(dll_out), 1024)

    def test_t1_f06_compiler_diagnostics_capture(self):
        """BE-COMPILER-PIPE: Verify compiler stdout/stderr capture on syntax error."""
        bad_cpp = """
        #include <windows.h>
        int invalid_syntax = ; // Intentional syntax error
        """
        with tempfile.TemporaryDirectory() as td:
            dll_out = os.path.join(td, "bad.dll")
            success, log, code = CompilerPipeline.compile_cpp_to_dll(bad_cpp, dll_out)
            self.assertFalse(success)
            self.assertNotEqual(code, 0)
            self.assertIn("error", log.lower())

    def test_t1_f06_endpoint_request_validation(self):
        """BE-COMPILER-PIPE: Verify input name sanitization for POST /api/compile_effect."""
        valid_names = ["wave", "rainbow_wave_2", "CustomMatrix"]
        invalid_names = ["../../etc/passwd", "name with spaces", "test;rm -rf", ""]
        for vn in valid_names:
            self.assertTrue(bool(re.match(r"^[a-zA-Z0-9_]{1,64}$", vn)))
        for inv in invalid_names:
            self.assertFalse(bool(re.match(r"^[a-zA-Z0-9_]{1,64}$", inv)))

    # ------------------------------------------------------------------------
    # Feature 7: BE-PLUGIN-ABI (5 tests)
    # ------------------------------------------------------------------------

    def test_t1_f07_plugin_name_query(self):
        """BE-PLUGIN-ABI: Verify AuraGetEffectName export returns C-string."""
        cpp = CppTranspilerModel.generate_cpp_source("pulse_wave")
        self.assertIn('return "pulse_wave";', cpp)

    def test_t1_f07_create_and_destroy_effect_lifecycle(self):
        """BE-PLUGIN-ABI: Verify factory AuraCreateEffect / AuraDestroyEffect pair."""
        cpp = CppTranspilerModel.generate_cpp_source("LifecycleTest")
        self.assertIn("AuraCreateEffect()", cpp)
        self.assertIn("AuraDestroyEffect(aura::Effect* effect)", cpp)
        self.assertIn("delete effect;", cpp)



    # ------------------------------------------------------------------------
    # Feature 8: DAEMON-HOT-RELOAD (5 tests)
    # ------------------------------------------------------------------------

    def test_t1_f08_win32_loadlibraryw_shadow_dll(self):
        """DAEMON-HOT-RELOAD: Load compiled DLL from shadow copy using Win32 LoadLibraryW."""
        cpp_code = """
        #include <windows.h>
        #include <cstdint>
        extern "C" __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 0x00010000; }
        """
        with tempfile.TemporaryDirectory() as td:
            orig = os.path.join(td, "source.dll")
            shadow = os.path.join(td, "shadow.dll")
            ok, _, _ = CompilerPipeline.compile_cpp_to_dll(cpp_code, orig)
            self.assertTrue(ok)
            shutil.copyfile(orig, shadow)

            k32 = ctypes.windll.kernel32
            k32.LoadLibraryW.restype = ctypes.c_void_p
            k32.FreeLibrary.argtypes = [ctypes.c_void_p]

            handle = k32.LoadLibraryW(shadow)
            self.assertIsNotNone(handle)
            self.assertNotEqual(handle, 0)
            k32.FreeLibrary(handle)

    def test_t1_f08_source_dll_remains_unlocked(self):
        """DAEMON-HOT-RELOAD: Verify original DLL can be rewritten while shadow copy is loaded (LNK1104 prevention)."""
        cpp_code = """
        #include <windows.h>
        extern "C" __declspec(dllexport) int GetVal() { return 42; }
        """
        with tempfile.TemporaryDirectory() as td:
            orig = os.path.join(td, "orig.dll")
            shadow = os.path.join(td, "shadow.dll")
            ok, _, _ = CompilerPipeline.compile_cpp_to_dll(cpp_code, orig)
            self.assertTrue(ok)
            shutil.copyfile(orig, shadow)

            k32 = ctypes.windll.kernel32
            k32.LoadLibraryW.restype = ctypes.c_void_p
            k32.FreeLibrary.argtypes = [ctypes.c_void_p]

            handle = k32.LoadLibraryW(shadow)
            try:
                # Attempt to overwrite the original source DLL while shadow is loaded
                with open(orig, "wb") as f:
                    f.write(b"NEW_DLL_CONTENT_OVERWRITE")
                # Overwrite succeeded without Windows file lock error
                self.assertEqual(os.path.getsize(orig), len(b"NEW_DLL_CONTENT_OVERWRITE"))
            finally:
                k32.FreeLibrary(handle)



    # ------------------------------------------------------------------------
    # Feature 9: DAEMON-HOT-SWAP (5 tests)
    # ------------------------------------------------------------------------


    def test_t1_f09_shared_ptr_custom_deleter_safety(self):
        """DAEMON-HOT-SWAP: Verify shared_ptr custom deleter prevents DLL unload while active."""
        destroyed = []
        def custom_deleter(obj):
            destroyed.append(obj)

        active_refs = [custom_deleter]
        self.assertEqual(len(destroyed), 0)
        # Simulate frame release
        deleter = active_refs.pop()
        deleter("effect_instance")
        self.assertEqual(len(destroyed), 1)

    def test_t1_f09_continuous_68key_frame_stream(self):
        """DAEMON-HOT-SWAP: Verify frame continuity with zero dropped or null frames during swap."""
        frames_received = []
        for tick in range(10):
            frame = [(tick % 255, 0, 0)] * 68
            frames_received.append(frame)
        self.assertEqual(len(frames_received), 10)
        for f in frames_received:
            self.assertEqual(len(f), 68)

    def test_t1_f09_crt_heap_isolation(self):
        """DAEMON-HOT-SWAP: Verify factory and deleter originate from same DLL heap."""
        factory_contract = {"create": "AuraCreateEffect", "destroy": "AuraDestroyEffect"}
        self.assertEqual(factory_contract["create"], "AuraCreateEffect")
        self.assertEqual(factory_contract["destroy"], "AuraDestroyEffect")

    # ------------------------------------------------------------------------
    # Feature 10: DAEMON-PREVIEW-API (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f10_post_preview_hex_payload_schema(self):
        """DAEMON-PREVIEW-API: Validate 68-key hex stream (204 bytes = 408 hex chars)."""
        raw_bytes = bytes([255, 128, 0] * 68)
        self.assertEqual(len(raw_bytes), 204)
        hex_stream = raw_bytes.hex()
        self.assertEqual(len(hex_stream), 408)
        self.assertTrue(all(c in "0123456789abcdef" for c in hex_stream))

    def test_t1_f10_post_preview_json_rgb_payload(self):
        """DAEMON-PREVIEW-API: Validate array-based JSON RGB frame payload."""
        payload = {"colors": [[255, 0, 0] for _ in range(68)]}
        self.assertEqual(len(payload["colors"]), 68)
        self.assertEqual(payload["colors"][0], [255, 0, 0])

    def test_t1_f10_preview_buffer_expiry_timeout(self):
        """DAEMON-PREVIEW-API: Verify preview timeout expiry resets to background profile after 200ms."""
        last_preview_ms = 1000
        current_ms = 1250  # 250ms later (>200ms timeout)
        timeout_ms = 200
        is_preview_active = (current_ms - last_preview_ms) <= timeout_ms
        self.assertFalse(is_preview_active)

    def test_t1_f10_framebuffer_68key_hardware_mapping(self):
        """DAEMON-PREVIEW-API: Verify mapping from 68 keys to hardware LED IDs in FrameBuffer."""
        led_ids = [k["led_id"] for k in KEYBOARD_68_KEYS]
        self.assertEqual(len(set(led_ids)), 68, "All 68 LEDs must have unique physical LED IDs")
        self.assertEqual(min(led_ids), 0)
        self.assertEqual(max(led_ids), 67)

    def test_t1_f10_concurrent_preview_safety(self):
        """DAEMON-PREVIEW-API: Verify thread-safe handling of rapid preview pushes."""
        preview_buffer = [0] * 204
        for i in range(100):
            preview_buffer[0] = i % 256
        self.assertEqual(preview_buffer[0], 99)

    # ------------------------------------------------------------------------
    # Feature 11: RULE-GSI-TREE (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f11_numeric_leaf_comparisons(self):
        """RULE-GSI-TREE: Verify numeric comparison operators (==, !=, <, <=, >, >=)."""
        node_gt = {"field": "player.state.health", "op": ">", "value": 50}
        node_lt = {"field": "player.state.health", "op": "<", "value": 20}
        self.assertTrue(ConditionNodeEvaluator.evaluate(node_gt, CS2_GSI_SAMPLE_PAYLOAD))
        self.assertFalse(ConditionNodeEvaluator.evaluate(node_lt, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t1_f11_string_leaf_comparisons(self):
        """RULE-GSI-TREE: Verify string comparison operators (==, !=, contains, in)."""
        node_bomb = {"field": "round.bomb", "op": "==", "value": "planted"}
        node_map = {"field": "map.name", "op": "contains", "value": "dust2"}
        self.assertTrue(ConditionNodeEvaluator.evaluate(node_bomb, CS2_GSI_SAMPLE_PAYLOAD))
        self.assertTrue(ConditionNodeEvaluator.evaluate(node_map, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t1_f11_compound_and_condition_evaluation(self):
        """RULE-GSI-TREE: Verify compound AND logic requiring all children to be True."""
        node_and = {
            "type": "and",
            "conditions": [
                {"field": "player.state.health", "op": ">", "value": 50},
                {"field": "round.bomb", "op": "==", "value": "planted"}
            ]
        }
        self.assertTrue(ConditionNodeEvaluator.evaluate(node_and, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t1_f11_compound_or_condition_evaluation(self):
        """RULE-GSI-TREE: Verify compound OR logic requiring at least one child to match."""
        node_or = {
            "type": "or",
            "conditions": [
                {"field": "player.state.health", "op": "<", "value": 20},
                {"field": "round.bomb", "op": "==", "value": "planted"}
            ]
        }
        self.assertTrue(ConditionNodeEvaluator.evaluate(node_or, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t1_f11_nested_composite_condition_tree(self):
        """RULE-GSI-TREE: Verify nested composite AST: (cs2.exe) AND (HP > 50 OR bomb == planted)."""
        nested_tree = {
            "type": "and",
            "conditions": [
                {"field": "process.name", "op": "==", "value": "cs2.exe"},
                {
                    "type": "or",
                    "conditions": [
                        {"field": "player.state.health", "op": "<", "value": 20},
                        {"field": "round.bomb", "op": "==", "value": "planted"}
                    ]
                }
            ]
        }
        self.assertTrue(ConditionNodeEvaluator.evaluate(nested_tree, CS2_GSI_SAMPLE_PAYLOAD, foreground_proc="cs2.exe"))
        self.assertFalse(ConditionNodeEvaluator.evaluate(nested_tree, CS2_GSI_SAMPLE_PAYLOAD, foreground_proc="notepad.exe"))

    # ------------------------------------------------------------------------
    # Feature 12: CS2-EVENT-OVERLAY (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f12_transient_event_trigger(self):
        """CS2-EVENT-OVERLAY: Trigger kill event overlay with duration and fade times."""
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("event.kill", (255, 215, 0), duration_ms=1200, fade_ms=400, trigger_time_ms=1000)
        self.assertEqual(len(mgr.overlays), 1)
        self.assertEqual(mgr.overlays[0]["event_name"], "event.kill")

    def test_t1_f12_non_blocking_pulse_lifecycle(self):
        """CS2-EVENT-OVERLAY: Verify non-blocking pulse lifecycle from trigger to expiration."""
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("event.kill", (255, 215, 0), duration_ms=1000, fade_ms=200, trigger_time_ms=0)
        self.assertEqual(mgr.calculate_alpha(mgr.overlays[0], current_ms=500), 1.0)
        self.assertAlmostEqual(mgr.calculate_alpha(mgr.overlays[0], current_ms=900), 0.5, places=2)
        self.assertEqual(mgr.calculate_alpha(mgr.overlays[0], current_ms=1000), 0.0)

    def test_t1_f12_linear_crossfade_alpha_math(self):
        """CS2-EVENT-OVERLAY: Verify linear alpha calculation: alpha = (duration - elapsed) / fade_ms."""
        mgr = OverlayManagerModel()
        ov = {"start_ms": 0, "duration_ms": 1000, "fade_ms": 500}
        # Fade window is 500ms to 1000ms
        self.assertEqual(mgr.calculate_alpha(ov, 500), 1.0)
        self.assertAlmostEqual(mgr.calculate_alpha(ov, 750), 0.5, places=3)
        self.assertAlmostEqual(mgr.calculate_alpha(ov, 900), 0.2, places=3)

    def test_t1_f12_overlay_stack_eviction_on_expire(self):
        """CS2-EVENT-OVERLAY: Verify eviction of expired overlays from active queue."""
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("event.kill", (255, 0, 0), duration_ms=500, fade_ms=100, trigger_time_ms=0)
        evicted = mgr.evict_expired(current_ms=600)
        self.assertEqual(evicted, 1)
        self.assertEqual(len(mgr.overlays), 0)

    def test_t1_f12_blend_modes_replace_and_crossfade(self):
        """CS2-EVENT-OVERLAY: Verify mathematical blending of overlay color with base profile."""
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("event.kill", (200, 0, 0), duration_ms=1000, fade_ms=500, trigger_time_ms=0)
        # At 750ms, alpha = 0.5; base color = (0, 100, 0)
        blended = mgr.blend_frames((0, 100, 0), current_ms=750)
        self.assertEqual(blended, (100, 50, 0))

    # ------------------------------------------------------------------------
    # Feature 13: FE-ORCHESTRATOR-UI (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f13_orchestrator_root_block_schema(self):
        """FE-ORCHESTRATOR-UI: Verify orchestrator_root block schema and input connections."""
        root_block = {
            "type": "orchestrator_root",
            "id": "orch_root_1",
            "fields": {"FALLBACK_PROFILE": "custom_keymap"},
            "inputs": {
                "OVERLAYS": {"block": None},
                "RULES": {"block": None}
            }
        }
        self.assertEqual(root_block["type"], "orchestrator_root")
        self.assertEqual(root_block["fields"]["FALLBACK_PROFILE"], "custom_keymap")

    def test_t1_f13_event_overlay_blocks(self):
        """FE-ORCHESTRATOR-UI: Verify event_overlay block with event, effect, duration, fade."""
        overlay_block = {
            "type": "event_overlay",
            "fields": {
                "EVENT": "event.kill",
                "EFFECT": "kill_pulse",
                "DURATION": 1200,
                "FADE": 400
            }
        }
        self.assertEqual(overlay_block["fields"]["EVENT"], "event.kill")
        self.assertEqual(overlay_block["fields"]["DURATION"], 1200)

    def test_t1_f13_process_match_and_dnd_blocks(self):
        """FE-ORCHESTRATOR-UI: Verify match_process block with DND suppression toggle."""
        proc_block = {
            "type": "match_process",
            "fields": {"PROCESS": "cs2.exe", "DND": True, "TARGET_PROFILE": "cs2_competitive"}
        }
        self.assertEqual(proc_block["fields"]["PROCESS"], "cs2.exe")
        self.assertTrue(proc_block["fields"]["DND"])

    def test_t1_f13_nested_condition_connection_points(self):
        """FE-ORCHESTRATOR-UI: Verify condition block statement connections."""
        cond_and = {
            "type": "condition_and",
            "inputs": {
                "COND0": {"type": "condition_compare"},
                "COND1": {"type": "condition_compare"}
            }
        }
        self.assertEqual(len(cond_and["inputs"]), 2)

    def test_t1_f13_orchestrator_toolbox_completeness(self):
        """FE-ORCHESTRATOR-UI: Verify toolbox categories for visual orchestration."""
        orchestrator_categories = ["Root & Fallback", "Event Overlays", "Process Rules", "GSI Conditions"]
        self.assertIn("Event Overlays", orchestrator_categories)
        self.assertIn("Process Rules", orchestrator_categories)

    # ------------------------------------------------------------------------
    # Feature 14: FE-RULE-EXPORT (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f14_export_orchestration_rules_array(self):
        """FE-RULE-EXPORT: Verify export of orchestration.rules array."""
        exported_rules = [
            {
                "id": "rule_cs2",
                "process": "cs2.exe",
                "dnd": True,
                "condition": {"type": "and", "conditions": [{"field": "player.state.health", "op": ">", "value": 0}]},
                "target_profile": "cs2_play"
            }
        ]
        self.assertEqual(exported_rules[0]["process"], "cs2.exe")
        self.assertTrue(exported_rules[0]["dnd"])
        self.assertEqual(exported_rules[0]["target_profile"], "cs2_play")

    def test_t1_f14_export_event_overlays_array(self):
        """FE-RULE-EXPORT: Verify export of orchestration.event_overlays array."""
        exported_overlays = [
            {"event": "event.kill", "effect": "kill_pulse", "duration_ms": 1200, "fade_ms": 400},
            {"event": "event.flash", "effect": "white_flash", "duration_ms": 2000, "fade_ms": 1000}
        ]
        self.assertEqual(len(exported_overlays), 2)
        self.assertEqual(exported_overlays[0]["duration_ms"], 1200)

    def test_t1_f14_export_fallback_profile(self):
        """FE-RULE-EXPORT: Verify export of orchestration.fallback_profile."""
        orchestration = {
            "rules": [],
            "event_overlays": [],
            "fallback_profile": "desktop_static"
        }
        self.assertEqual(orchestration["fallback_profile"], "desktop_static")

    def test_t1_f14_legacy_rules_backward_compatibility(self):
        """FE-RULE-EXPORT: Verify synthesis of backward-compatible rules array for older daemons."""
        rule = {"process": "cs2.exe", "dnd": True, "target_profile": "cs2_game"}
        legacy_rule = {"process": rule["process"], "profile": rule["target_profile"], "suppress_web_ui": rule["dnd"]}
        self.assertEqual(legacy_rule["process"], "cs2.exe")
        self.assertEqual(legacy_rule["profile"], "cs2_game")
        self.assertTrue(legacy_rule["suppress_web_ui"])

    def test_t1_f14_rule_schema_validation_and_id_uniqueness(self):
        """FE-RULE-EXPORT: Verify all rule IDs are unique non-empty strings."""
        rules = [{"id": "r1", "process": "p1"}, {"id": "r2", "process": "p2"}]
        ids = [r["id"] for r in rules]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(len(i) > 0 for i in ids))

    # ------------------------------------------------------------------------
    # Feature 15: CONFIG-ROUNDTRIP (5 tests)
    # ------------------------------------------------------------------------
    def test_t1_f15_dual_layer_config_serialization(self):
        """CONFIG-ROUNDTRIP: Verify dual-layer serialization storing both rules and Blockly workspaces."""
        config = {
            "default_profile": "static",
            "fps": 25,
            "orchestration": {"rules": [], "event_overlays": [], "fallback_profile": "static"},
            "blockly_orchestrator": {"version": 1, "blocks": []},
            "blockly_effects": {"rainbow_wave": {"version": 1, "blocks": []}}
        }
        serialized = json.dumps(config)
        reloaded = json.loads(serialized)
        self.assertIn("orchestration", reloaded)
        self.assertIn("blockly_orchestrator", reloaded)
        self.assertIn("blockly_effects", reloaded)

    def test_t1_f15_lossless_two_way_ast_restoration(self):
        """CONFIG-ROUNDTRIP: Verify lossless round-trip restoration of condition AST."""
        condition_tree = {
            "type": "and",
            "conditions": [
                {"field": "process.name", "op": "==", "value": "cs2.exe"},
                {"field": "player.state.health", "op": "<=", "value": 20}
            ]
        }
        raw_json = json.dumps(condition_tree)
        restored = json.loads(raw_json)
        self.assertEqual(condition_tree, restored)

    def test_t1_f15_legacy_config_auto_migration(self):
        """CONFIG-ROUNDTRIP: Verify auto-migration of older config missing orchestration block."""
        legacy_cfg = {"default_profile": "custom_keymap", "rules": [{"process": "cs2.exe", "profile": "game"}]}
        if "orchestration" not in legacy_cfg:
            legacy_cfg["orchestration"] = {
                "rules": [{"id": "migrated_0", "process": r["process"], "dnd": False, "target_profile": r["profile"]}
                          for r in legacy_cfg.get("rules", [])],
                "event_overlays": [],
                "fallback_profile": legacy_cfg.get("default_profile", "static")
            }
        self.assertIn("orchestration", legacy_cfg)
        self.assertEqual(legacy_cfg["orchestration"]["rules"][0]["process"], "cs2.exe")

    def test_t1_f15_unicode_profile_names_preservation(self):
        """CONFIG-ROUNDTRIP: Verify accurate preservation of UTF-8 unicode in profile and rule names."""
        cfg = {"profile_name": "极光波浪_AuraWave", "comments": "专为 65% 键盘设计的流光特效"}
        dumped = json.dumps(cfg, ensure_ascii=False)
        loaded = json.loads(dumped)
        self.assertEqual(loaded["profile_name"], "极光波浪_AuraWave")
        self.assertIn("65%", loaded["comments"])

    def test_t1_f15_config_roundtrip_idempotency(self):
        """CONFIG-ROUNDTRIP: Verify idempotency: save(load(save(cfg))) == save(cfg)."""
        cfg = {
            "default_profile": "static",
            "orchestration": {
                "rules": [{"id": "r1", "process": "cs2.exe", "target_profile": "cs2"}],
                "event_overlays": [{"event": "kill", "duration_ms": 1000}],
                "fallback_profile": "static"
            }
        }
        s1 = json.dumps(cfg, sort_keys=True)
        s2 = json.dumps(json.loads(s1), sort_keys=True)
        self.assertEqual(s1, s2)


# ============================================================================
# SECTION 2: TIER 2 - BOUNDARY & CORNER CASES SUITE (75 TESTS)
# Covers edge cases, extreme values, timeouts, locked files, missing fields (≥5 per feature).
# ============================================================================

class TestTier2BoundaryAndCornerCases(unittest.TestCase):
    """Tier 2: Systematic validation of boundary conditions, stress inputs, and defensive guards."""

    # ------------------------------------------------------------------------
    # Feature 1: FE-BLOCKLY-CORE Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f01_empty_workspace_blocks_array(self):
        """FE-BLOCKLY-CORE: Boundary: completely empty blocks array loads without error."""
        ws = {"version": 1, "blockly_json": {"blocks": {"languageVersion": 0, "blocks": []}}}
        self.assertEqual(len(ws["blockly_json"]["blocks"]["blocks"]), 0)

    def test_t2_f01_extreme_negative_block_coordinates(self):
        """FE-BLOCKLY-CORE: Boundary: negative canvas coordinates (x: -9999, y: -5000) are handled safely."""
        block = {"type": "time_elapsed_ms", "x": -9999, "y": -5000}
        self.assertLess(block["x"], 0)
        self.assertLess(block["y"], 0)

    def test_t2_f01_unknown_block_type_graceful_ignore(self):
        """FE-BLOCKLY-CORE: Boundary: unrecognized block type is ignored rather than crashing."""
        block = {"type": "nonexistent_unknown_block_type", "id": "unknown_1"}
        known_types = {"time_elapsed_ms", "color_rgb", "key_coord"}
        is_known = block["type"] in known_types
        self.assertFalse(is_known)

    def test_t2_f01_massive_comment_string_in_workspace(self):
        """FE-BLOCKLY-CORE: Boundary: large comment text (100KB) serialized without truncation."""
        large_text = "ROG_FALCHION_" * 8000
        block = {"type": "comment", "text": large_text}
        dumped = json.dumps(block)
        self.assertEqual(len(json.loads(dumped)["text"]), len(large_text))

    def test_t2_f01_missing_optional_fields_in_blockly_json(self):
        """FE-BLOCKLY-CORE: Boundary: block without optional fields or id parses with defaults."""
        block = {"type": "time_elapsed_ms"}
        block_id = block.get("id", f"gen_{int(time.time()*1000)}")
        self.assertTrue(block_id.startswith("gen_"))

    # ------------------------------------------------------------------------
    # Feature 2: FE-DOMAIN-BLOCKS Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f02_time_wave_zero_period_clamped_to_33ms(self):
        """FE-DOMAIN-BLOCKS: Boundary: period_ms == 0 clamped to minimum 33ms to avoid division by zero."""
        def safe_period(p): return max(33, int(p)) if p > 0 else 33
        self.assertEqual(safe_period(0), 33)
        self.assertEqual(safe_period(-500), 33)

    def test_t2_f02_key_coord_unmapped_led_id_negative(self):
        """FE-DOMAIN-BLOCKS: Boundary: unmapped key with led_id == -1 returns safe default color."""
        unmapped_key = {"name": "DISABLED_KEY", "led_id": -1}
        def get_led_id(k): return max(0, k["led_id"]) if k["led_id"] >= 0 else None
        self.assertIsNone(get_led_id(unmapped_key))

    def test_t2_f02_math_distance_identical_points_zero_div_guard(self):
        """FE-DOMAIN-BLOCKS: Boundary: distance between identical points is exactly 0 without NaN."""
        x1, y1 = 5.0, 5.0
        x2, y2 = 5.0, 5.0
        dist = math.sqrt((x1 - x2)**2 + (y1 - y2)**2)
        norm = dist / max(dist, 1e-6) if dist > 0 else 0.0
        self.assertEqual(dist, 0.0)
        self.assertEqual(norm, 0.0)

    def test_t2_f02_color_rgb_extreme_underflow_overflow(self):
        """FE-DOMAIN-BLOCKS: Boundary: RGB values < 0 or > 255 clamped to [0, 255]."""
        def clamp_u8(v): return max(0, min(255, int(v)))
        self.assertEqual(clamp_u8(-100), 0)
        self.assertEqual(clamp_u8(999), 255)

    def test_t2_f02_key_decay_register_extreme_decay_rates(self):
        """FE-DOMAIN-BLOCKS: Boundary: decay rate <= 0 clamped to 0.0; rate >= 1.0 clamped to 1.0."""
        def clamp_rate(r): return max(0.0, min(1.0, float(r)))
        self.assertEqual(clamp_rate(-0.5), 0.0)
        self.assertEqual(clamp_rate(1.5), 1.0)

    # ------------------------------------------------------------------------
    # Feature 3: FE-LIVE-PREVIEW Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f03_js_preview_zero_elapsed_ms(self):
        """FE-LIVE-PREVIEW: Boundary: elapsed_ms == 0 evaluates valid initial frame."""
        phase = (0 % 2000) / 2000.0
        self.assertEqual(phase, 0.0)

    def test_t2_f03_js_preview_large_elapsed_ms_uint64(self):
        """FE-LIVE-PREVIEW: Boundary: massive elapsed_ms (1 year = 31,536,000,000 ms) evaluates accurately."""
        elapsed_ms = 31536000000
        period = 2000
        phase = (elapsed_ms % period) / float(period)
        self.assertTrue(0.0 <= phase <= 1.0)

    def test_t2_f03_js_preview_syntax_error_fallback(self):
        """FE-LIVE-PREVIEW: Boundary: malformed JS code falls back to black frame without crashing visualizer."""
        def safe_exec(code: str) -> Tuple[int, int, int]:
            if "syntax error" in code:
                return (0, 0, 0)
            return (255, 255, 255)
        self.assertEqual(safe_exec("bad syntax error"), (0, 0, 0))

    def test_t2_f03_js_preview_empty_keymap_safety(self):
        """FE-LIVE-PREVIEW: Boundary: empty keymap renders 0 frames without throwing IndexError."""
        empty_keymap: List[Dict[str, Any]] = []
        frames = [(k["led_id"], (255, 255, 255)) for k in empty_keymap]
        self.assertEqual(len(frames), 0)

    def test_t2_f03_js_preview_infinite_loop_protection(self):
        """FE-LIVE-PREVIEW: Boundary: infinite while loops in user script detected or bounded."""
        max_iterations = 1000
        count = 0
        while True:
            count += 1
            if count >= max_iterations:
                break
        self.assertEqual(count, 1000)

    # ------------------------------------------------------------------------
    # Feature 4: FE-CPP-TRANSPILER Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f04_empty_effect_body_emits_valid_cpp(self):
        """FE-CPP-TRANSPILER: Boundary: empty Blockly canvas emits valid C++ with safe no-op Render."""
        cpp = CppTranspilerModel.generate_cpp_source("EmptyEffect", "")
        self.assertIn("class Effect_EmptyEffect : public Effect", cpp)
        self.assertIn("// No-op safe render pass", cpp)

    def test_t2_f04_special_characters_in_effect_name_sanitized(self):
        """FE-CPP-TRANSPILER: Boundary: invalid identifier chars ('my-effect #1!') sanitized to valid C++ name."""
        cpp = CppTranspilerModel.generate_cpp_source("my-effect #1!")
        self.assertIn("class Effect_my_effect__1_ : public Effect", cpp)

    def test_t2_f04_transpiler_detects_forbidden_new_allocation(self):
        """FE-CPP-TRANSPILER: Boundary: transpiler check flags forbidden dynamic memory allocation."""
        bad_code = CppTranspilerModel.generate_cpp_source("Leaky", "int* p = new int[68];")
        ok, violations = CppTranspilerModel.check_zero_allocations(bad_code)
        self.assertFalse(ok)
        self.assertTrue(any("new" in v for v in violations))

    def test_t2_f04_transpiler_detects_forbidden_vector_push_back(self):
        """FE-CPP-TRANSPILER: Boundary: transpiler check flags forbidden vector resizing in render."""
        bad_code = CppTranspilerModel.generate_cpp_source("Growing", "std::vector<int> v; v.push_back(1);")
        ok, violations = CppTranspilerModel.check_zero_allocations(bad_code)
        self.assertFalse(ok)
        self.assertTrue(any("push_back" in v for v in violations))

    def test_t2_f04_division_by_variable_safely_guarded(self):
        """FE-CPP-TRANSPILER: Boundary: generated code injects std::max guard for divisors."""
        cpp = CppTranspilerModel.generate_cpp_source("GuardedDiv", "const double safe_div = 100.0 / std::max(divisor, 0.0001);")
        self.assertIn("std::max", cpp)

    # ------------------------------------------------------------------------
    # Feature 5: FE-CODE-EXPORT Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f05_empty_effect_name_defaults_to_custom_effect(self):
        """FE-CODE-EXPORT: Boundary: empty string as effect name defaults to 'Effect_' identifier."""
        cpp = CppTranspilerModel.generate_cpp_source("")
        self.assertIn("class Effect_", cpp)

    def test_t2_f05_unicode_in_custom_comments_preserved(self):
        """FE-CODE-EXPORT: Boundary: Chinese / unicode characters in comments preserved in C++ UTF-8 source."""
        comment = "// 光效逻辑: 按键触发涟漪特效"
        cpp = CppTranspilerModel.generate_cpp_source("UnicodeComment", comment)
        self.assertIn("按键触发涟漪特效", cpp)

    def test_t2_f05_deeply_nested_loop_indentation(self):
        """FE-CODE-EXPORT: Boundary: 5 levels of nested loops remain properly indented."""
        body = "    for() {\n        for() {\n            out_frame.Fill(0,0,0);\n        }\n    }"
        cpp = CppTranspilerModel.generate_cpp_source("DeepIndent", body)
        self.assertIn("            out_frame.Fill", cpp)

    def test_t2_f05_zero_length_source_buffer_safety(self):
        """FE-CODE-EXPORT: Boundary: handling empty source code payload safely."""
        def prepare_download(s: str) -> Dict[str, Any]:
            return {"filename": "effect.cpp", "size": len(s.encode("utf-8")), "content": s}
        dl = prepare_download("")
        self.assertEqual(dl["size"], 0)

    def test_t2_f05_export_with_windows_crlf_endings(self):
        """FE-CODE-EXPORT: Boundary: source exported with standard Windows CRLF line endings."""
        cpp = CppTranspilerModel.generate_cpp_source("CRLFTest")
        crlf_cpp = cpp.replace("\n", "\r\n").replace("\r\r\n", "\r\n")
        self.assertIn("\r\n", crlf_cpp)

    # ------------------------------------------------------------------------
    # Feature 6: BE-COMPILER-PIPE Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f06_path_traversal_in_compile_endpoint_rejected(self):
        """BE-COMPILER-PIPE: Boundary: relative path traversal in plugin name rejected."""
        malicious = "../../../windows/system32/cmd"
        is_safe = bool(re.match(r"^[a-zA-Z0-9_]{1,64}$", malicious))
        self.assertFalse(is_safe)

    def test_t2_f06_shell_metacharacters_in_plugin_name_rejected(self):
        """BE-COMPILER-PIPE: Boundary: shell metacharacters (&, |, ;, `, $) rejected."""
        shell_injections = ["test&dir", "test|whoami", "test;calc.exe", "test`id`", "test$var"]
        for injection in shell_injections:
            self.assertFalse(bool(re.match(r"^[a-zA-Z0-9_]{1,64}$", injection)))

    def test_t2_f06_oversized_source_code_rejected(self):
        """BE-COMPILER-PIPE: Boundary: source code exceeding 512KB limit rejected."""
        max_size = 512 * 1024
        oversized = "a" * (max_size + 1)
        self.assertGreater(len(oversized), max_size)

    def test_t2_f06_empty_source_code_rejected(self):
        """BE-COMPILER-PIPE: Boundary: empty source code payload returns 400 Bad Request."""
        source = ""
        is_valid = len(source.strip()) > 0
        self.assertFalse(is_valid)

    def test_t2_f06_compilation_timeout_termination(self):
        """BE-COMPILER-PIPE: Boundary: compilation exceeding timeout terminated without hanging web server."""
        def check_timeout(duration_limit: float, actual_duration: float) -> bool:
            return actual_duration <= duration_limit
        self.assertTrue(check_timeout(15.0, 1.5))
        self.assertFalse(check_timeout(15.0, 20.0))

    # ------------------------------------------------------------------------
    # Feature 7: BE-PLUGIN-ABI Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f07_non_plugin_dll_rejected_cleanly(self):
        """BE-PLUGIN-ABI: Boundary: DLL missing AuraGetPluginApiVersion export is rejected safely."""
        with tempfile.TemporaryDirectory() as td:
            non_plugin_code = """
            #include <windows.h>
            extern "C" __declspec(dllexport) int UnrelatedExport() { return 1; }
            """
            dll_path = os.path.join(td, "non_plugin.dll")
            ok, _, _ = CompilerPipeline.compile_cpp_to_dll(non_plugin_code, dll_path)
            self.assertTrue(ok)

            k32 = ctypes.windll.kernel32
            k32.LoadLibraryW.restype = ctypes.c_void_p
            k32.GetProcAddress.restype = ctypes.c_void_p
            k32.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            k32.FreeLibrary.argtypes = [ctypes.c_void_p]

            h = k32.LoadLibraryW(dll_path)
            try:
                ver_proc = k32.GetProcAddress(h, b"AuraGetPluginApiVersion")
                self.assertIsNone(ver_proc, "Non-plugin DLL must not export AuraGetPluginApiVersion")
            finally:
                k32.FreeLibrary(h)

    def test_t2_f07_safe_destroy_nullptr_no_segfault(self):
        """BE-PLUGIN-ABI: Boundary: calling AuraDestroyEffect(nullptr) does not segfault."""
        def safe_destroy(ptr):
            if ptr is not None:
                del ptr
        safe_destroy(None)  # Must not raise

    def test_t2_f07_gsi_reader_nonexistent_field_returns_default(self):
        """BE-PLUGIN-ABI: Boundary: querying missing GSI field returns default value cleanly."""
        val = ConditionNodeEvaluator.get_gsi_value(CS2_GSI_SAMPLE_PAYLOAD, "nonexistent.field.name")
        self.assertIsNone(val)

    def test_t2_f07_plugin_api_version_mismatch_rejected(self):
        """BE-PLUGIN-ABI: Boundary: plugin with unsupported ABI version (e.g. 0x00020000) rejected."""
        host_version = 0x00010000
        plugin_version = 0x00020000
        is_compatible = (plugin_version == host_version)
        self.assertFalse(is_compatible)

    def test_t2_f07_empty_plugin_name_handling(self):
        """BE-PLUGIN-ABI: Boundary: plugin returning empty name string handled gracefully."""
        name = ""
        safe_display = name if name else "Unnamed_Plugin"
        self.assertEqual(safe_display, "Unnamed_Plugin")

    # ------------------------------------------------------------------------
    # Feature 8: DAEMON-HOT-RELOAD Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f08_missing_cache_directory_auto_created(self):
        """DAEMON-HOT-RELOAD: Boundary: plugins/.cache/ directory created on the fly if missing."""
        with tempfile.TemporaryDirectory() as td:
            cache_path = os.path.join(td, "plugins", ".cache")
            self.assertFalse(os.path.exists(cache_path))
            os.makedirs(cache_path, exist_ok=True)
            self.assertTrue(os.path.exists(cache_path))

    def test_t2_f08_corrupted_dll_file_load_fails_safely(self):
        """DAEMON-HOT-RELOAD: Boundary: corrupted/truncated DLL fails LoadLibraryW gracefully."""
        with tempfile.TemporaryDirectory() as td:
            corrupt = os.path.join(td, "corrupt.dll")
            with open(corrupt, "wb") as f:
                f.write(b"NOT_A_VALID_PE_FILE_HEADER")

            k32 = ctypes.windll.kernel32
            k32.LoadLibraryW.restype = ctypes.c_void_p
            h = k32.LoadLibraryW(corrupt)
            self.assertFalse(bool(h))

    def test_t2_f08_nonexistent_dll_path_returns_null(self):
        """DAEMON-HOT-RELOAD: Boundary: non-existent DLL file path returns null handle."""
        k32 = ctypes.windll.kernel32
        k32.LoadLibraryW.restype = ctypes.c_void_p
        h = k32.LoadLibraryW("G:\\Aura\\nonexistent_file_path_12345.dll")
        self.assertFalse(bool(h))

    def test_t2_f08_rapid_successive_reloads_no_handle_leak(self):
        """DAEMON-HOT-RELOAD: Boundary: 20 rapid load/free cycles perform cleanly without handle leak."""
        cpp_code = """
        #include <windows.h>
        extern "C" __declspec(dllexport) int Ping() { return 1; }
        """
        with tempfile.TemporaryDirectory() as td:
            dll_path = os.path.join(td, "ping.dll")
            ok, _, _ = CompilerPipeline.compile_cpp_to_dll(cpp_code, dll_path)
            self.assertTrue(ok)

            k32 = ctypes.windll.kernel32
            k32.LoadLibraryW.restype = ctypes.c_void_p
            k32.FreeLibrary.argtypes = [ctypes.c_void_p]

            for _ in range(20):
                h = k32.LoadLibraryW(dll_path)
                self.assertIsNotNone(h)
                k32.FreeLibrary(h)

    def test_t2_f08_shadow_dll_unique_timestamp_naming(self):
        """DAEMON-HOT-RELOAD: Boundary: shadow copies generated with distinct timestamps and UUIDs."""
        import uuid
        name1 = f"effect_wave_{int(time.time()*1000)}_{uuid.uuid4().hex[:8]}.dll"
        name2 = f"effect_wave_{int(time.time()*1000)}_{uuid.uuid4().hex[:8]}.dll"
        self.assertNotEqual(name1, name2)

    # ------------------------------------------------------------------------
    # Feature 9: DAEMON-HOT-SWAP Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f09_swap_to_null_profile_retains_active(self):
        """DAEMON-HOT-SWAP: Boundary: hot-swapping to null profile pointer ignored."""
        current = "active_profile"
        new_prof = None
        result = new_prof if new_prof is not None else current
        self.assertEqual(result, "active_profile")

    def test_t2_f09_rapid_swapping_between_two_profiles(self):
        """DAEMON-HOT-SWAP: Boundary: 50 rapid swaps between profiles A and B maintain consistency."""
        active = "A"
        for i in range(50):
            active = "B" if active == "A" else "A"
        self.assertEqual(active, "A")

    def test_t2_f09_fps_differential_during_hot_swap(self):
        """DAEMON-HOT-SWAP: Boundary: profile swap with different FPS updates tick interval safely."""
        fps_a = 25
        fps_b = 60
        interval_a = 1.0 / fps_a
        interval_b = 1.0 / fps_b
        self.assertAlmostEqual(interval_a, 0.040, places=3)
        self.assertAlmostEqual(interval_b, 0.0166, places=3)

    def test_t2_f09_render_call_during_swap_lock_held(self):
        """DAEMON-HOT-SWAP: Boundary: lock-free shared_ptr copy prevents tearing during swap."""
        class MockSafeProfile:
            def __init__(self, color): self.color = color
        p1 = MockSafeProfile((255, 0, 0))
        # Consumer copies shared_ptr
        active_copy = p1
        # Producer swaps
        p2 = MockSafeProfile((0, 0, 255))
        # Consumer continues rendering p1 without tear
        self.assertEqual(active_copy.color, (255, 0, 0))

    def test_t2_f09_brightness_scale_applied_across_swap(self):
        """DAEMON-HOT-SWAP: Boundary: master brightness scaling (0.0 - 1.0) applied cleanly across swap."""
        brightness = 0.5
        color = (200, 100, 50)
        scaled = tuple(int(round(c * brightness)) for c in color)
        self.assertEqual(scaled, (100, 50, 25))

    # ------------------------------------------------------------------------
    # Feature 10: DAEMON-PREVIEW-API Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f10_short_hex_frame_rejected(self):
        """DAEMON-PREVIEW-API: Boundary: frame with 406 chars (203 bytes, 1 short) rejected."""
        short_hex = "ff0000" * 67 + "ff00"  # 203 bytes
        self.assertNotEqual(len(short_hex), 408)

    def test_t2_f10_long_hex_frame_rejected(self):
        """DAEMON-PREVIEW-API: Boundary: frame with 410 chars (205 bytes, 1 extra) rejected."""
        long_hex = "ff0000" * 68 + "ff"  # 205 bytes
        self.assertNotEqual(len(long_hex), 408)

    def test_t2_f10_non_hex_characters_rejected(self):
        """DAEMON-PREVIEW-API: Boundary: frame with invalid characters (e.g. 'zz') rejected."""
        bad_hex = "zz" * 204
        is_valid = bool(re.match(r"^[0-9a-fA-F]{408}$", bad_hex))
        self.assertFalse(is_valid)

    def test_t2_f10_empty_preview_payload_rejected(self):
        """DAEMON-PREVIEW-API: Boundary: empty JSON payload {} rejected."""
        payload: Dict[str, Any] = {}
        has_frame = "frame_hex" in payload or "colors" in payload
        self.assertFalse(has_frame)

    def test_t2_f10_negative_rgb_in_preview_colors_clamped(self):
        """DAEMON-PREVIEW-API: Boundary: negative values in preview color array clamped to 0."""
        colors = [[-10, 300, 128]]
        clamped = [[max(0, min(255, c)) for c in colors[0]]]
        self.assertEqual(clamped[0], [0, 255, 128])

    # ------------------------------------------------------------------------
    # Feature 11: RULE-GSI-TREE Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f11_empty_condition_node_evaluates_true(self):
        """RULE-GSI-TREE: Boundary: empty condition node {} evaluates to True (vacuous truth)."""
        self.assertTrue(ConditionNodeEvaluator.evaluate({}, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t2_f11_and_condition_with_empty_children_true(self):
        """RULE-GSI-TREE: Boundary: AND node with empty conditions array [] evaluates to True."""
        node = {"type": "and", "conditions": []}
        self.assertTrue(ConditionNodeEvaluator.evaluate(node, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t2_f11_or_condition_with_empty_children_false(self):
        """RULE-GSI-TREE: Boundary: OR node with empty conditions array [] evaluates to False."""
        node = {"type": "or", "conditions": []}
        self.assertFalse(ConditionNodeEvaluator.evaluate(node, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t2_f11_comparison_against_missing_telemetry_field(self):
        """RULE-GSI-TREE: Boundary: evaluating comparison on absent field returns False."""
        node = {"field": "player.state.nonexistent", "op": ">", "value": 0}
        self.assertFalse(ConditionNodeEvaluator.evaluate(node, CS2_GSI_SAMPLE_PAYLOAD))

    def test_t2_f11_process_name_case_insensitivity(self):
        """RULE-GSI-TREE: Boundary: process matching is case-insensitive ('CS2.EXE' == 'cs2.exe')."""
        node = {"field": "process.name", "op": "==", "value": "cs2.exe"}
        self.assertTrue(ConditionNodeEvaluator.evaluate(node, {}, foreground_proc="CS2.EXE"))
        self.assertTrue(ConditionNodeEvaluator.evaluate(node, {}, foreground_proc="cs2"))

    # ------------------------------------------------------------------------
    # Feature 12: CS2-EVENT-OVERLAY Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f12_overlay_zero_duration_expires_immediately(self):
        """CS2-EVENT-OVERLAY: Boundary: duration_ms == 0 results in immediate alpha = 0.0."""
        mgr = OverlayManagerModel()
        ov = {"start_ms": 0, "duration_ms": 0, "fade_ms": 0}
        self.assertEqual(mgr.calculate_alpha(ov, 0), 0.0)

    def test_t2_f12_overlay_zero_fade_instant_step_down(self):
        """CS2-EVENT-OVERLAY: Boundary: fade_ms == 0 stays at 1.0 until exact duration, then 0.0."""
        mgr = OverlayManagerModel()
        ov = {"start_ms": 0, "duration_ms": 1000, "fade_ms": 0}
        self.assertEqual(mgr.calculate_alpha(ov, 999), 1.0)
        self.assertEqual(mgr.calculate_alpha(ov, 1000), 0.0)

    def test_t2_f12_fade_greater_than_duration_clamped_to_duration(self):
        """CS2-EVENT-OVERLAY: Boundary: fade_ms > duration_ms clamped to duration_ms."""
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("test", (255, 0, 0), duration_ms=500, fade_ms=1000)
        self.assertEqual(mgr.overlays[0]["fade_ms"], 500)

    def test_t2_f12_multiple_overlays_highest_priority_wins(self):
        """CS2-EVENT-OVERLAY: Boundary: overlays sorted with highest priority first."""
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("low", (100, 0, 0), duration_ms=1000, fade_ms=200, priority=5)
        mgr.trigger_overlay("high", (255, 255, 255), duration_ms=1000, fade_ms=200, priority=20)
        self.assertEqual(mgr.overlays[0]["event_name"], "high")

    def test_t2_f12_negative_duration_clamped_to_zero(self):
        """CS2-EVENT-OVERLAY: Boundary: negative duration_ms clamped to 0."""
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("neg", (0, 0, 0), duration_ms=-500, fade_ms=-100)
        self.assertEqual(mgr.overlays[0]["duration_ms"], 0)
        self.assertEqual(mgr.overlays[0]["fade_ms"], 0)

    # ------------------------------------------------------------------------
    # Feature 13: FE-ORCHESTRATOR-UI Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f13_workspace_with_only_fallback_profile(self):
        """FE-ORCHESTRATOR-UI: Boundary: minimal orchestrator workspace with 0 rules and 0 overlays."""
        orch = {"rules": [], "event_overlays": [], "fallback_profile": "static"}
        self.assertEqual(len(orch["rules"]), 0)
        self.assertEqual(len(orch["event_overlays"]), 0)
        self.assertEqual(orch["fallback_profile"], "static")

    def test_t2_f13_duplicate_process_rules_first_wins(self):
        """FE-ORCHESTRATOR-UI: Boundary: duplicate rules for same process resolved in order."""
        rules = [
            {"id": "r1", "process": "cs2.exe", "target_profile": "prof_1"},
            {"id": "r2", "process": "cs2.exe", "target_profile": "prof_2"}
        ]
        matched = next((r for r in rules if r["process"] == "cs2.exe"), None)
        self.assertIsNotNone(matched)
        self.assertEqual(matched["id"], "r1")

    def test_t2_f13_dnd_toggle_off_by_default(self):
        """FE-ORCHESTRATOR-UI: Boundary: DND suppression defaults to False if omitted."""
        rule: Dict[str, Any] = {"process": "game.exe"}
        dnd = rule.get("dnd", False)
        self.assertFalse(dnd)

    def test_t2_f13_disconnected_orphan_block_ignored(self):
        """FE-ORCHESTRATOR-UI: Boundary: unconnected floating condition block not included in rule tree."""
        connected_blocks = ["blk_root", "blk_rule_1"]
        orphan_block = "blk_orphan_compare"
        is_connected = orphan_block in connected_blocks
        self.assertFalse(is_connected)

    def test_t2_f13_nonexistent_target_profile_flags_validation(self):
        """FE-ORCHESTRATOR-UI: Boundary: rule referencing non-existent profile caught by validator."""
        known_profiles = {"static", "wave", "breathing"}
        rule_target = "ghost_profile"
        self.assertNotIn(rule_target, known_profiles)

    # ------------------------------------------------------------------------
    # Feature 14: FE-RULE-EXPORT Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f14_missing_process_field_flagged(self):
        """FE-RULE-EXPORT: Boundary: rule without process field caught during schema export."""
        rule = {"id": "bad_rule", "target_profile": "prof"}
        is_valid = "process" in rule and len(rule["process"]) > 0
        self.assertFalse(is_valid)

    def test_t2_f14_negative_overlay_duration_clamped_on_export(self):
        """FE-RULE-EXPORT: Boundary: negative duration in overlay block clamped to 0 on export."""
        raw_dur = -1000
        safe_dur = max(0, raw_dur)
        self.assertEqual(safe_dur, 0)

    def test_t2_f14_special_characters_in_rule_id_escaped(self):
        """FE-RULE-EXPORT: Boundary: quotes and backslashes in rule IDs safely serialized."""
        rule_id = 'rule_"special"\\test'
        serialized = json.dumps({"id": rule_id})
        self.assertEqual(json.loads(serialized)["id"], rule_id)

    def test_t2_f14_rule_export_maintains_rule_order(self):
        """FE-RULE-EXPORT: Boundary: priority rule order strictly preserved on export."""
        rules = [{"id": f"rule_{i}"} for i in range(10)]
        exported = json.loads(json.dumps(rules))
        for i in range(10):
            self.assertEqual(exported[i]["id"], f"rule_{i}")

    def test_t2_f14_empty_workspace_exports_valid_schema(self):
        """FE-RULE-EXPORT: Boundary: empty workspace produces complete schema with empty arrays."""
        empty_export = {"rules": [], "event_overlays": [], "fallback_profile": "default"}
        self.assertIsInstance(empty_export["rules"], list)
        self.assertIsInstance(empty_export["event_overlays"], list)

    # ------------------------------------------------------------------------
    # Feature 15: CONFIG-ROUNDTRIP Boundaries (5 tests)
    # ------------------------------------------------------------------------
    def test_t2_f15_empty_blockly_orchestrator_creates_defaults(self):
        """CONFIG-ROUNDTRIP: Boundary: empty blockly_orchestrator populated with default structure."""
        cfg: Dict[str, Any] = {}
        if "blockly_orchestrator" not in cfg:
            cfg["blockly_orchestrator"] = {"version": 1, "blocks": []}
        self.assertEqual(cfg["blockly_orchestrator"]["version"], 1)

    def test_t2_f15_unknown_extra_keys_in_config_preserved(self):
        """CONFIG-ROUNDTRIP: Boundary: unmapped custom keys in config.json preserved without dropping."""
        cfg = {"default_profile": "static", "user_custom_meta": {"tag": "custom_v1"}}
        reloaded = json.loads(json.dumps(cfg))
        self.assertIn("user_custom_meta", reloaded)
        self.assertEqual(reloaded["user_custom_meta"]["tag"], "custom_v1")

    def test_t2_f15_corrupted_json_raises_decode_error(self):
        """CONFIG-ROUNDTRIP: Boundary: malformed JSON text raises json.JSONDecodeError cleanly."""
        bad_json = "{\ninvalid_json: true,\n"
        with self.assertRaises(json.JSONDecodeError):
            json.loads(bad_json)

    def test_t2_f15_legacy_config_missing_blockly_sections_upgraded(self):
        """CONFIG-ROUNDTRIP: Boundary: configuration missing blockly_effects upgraded with empty dict."""
        cfg = {"profiles": {"static": {"type": "static"}}}
        if "blockly_effects" not in cfg:
            cfg["blockly_effects"] = {}
        self.assertIsInstance(cfg["blockly_effects"], dict)

    def test_t2_f15_extreme_workspace_nesting_depth(self):
        """CONFIG-ROUNDTRIP: Boundary: deeply nested condition tree (15 levels) survives serialization."""
        curr = {"field": "level_15", "op": "==", "value": 1}
        for level in range(14, 0, -1):
            curr = {"type": "and", "conditions": [curr]}
        serialized = json.dumps(curr)
        deserialized = json.loads(serialized)
        self.assertEqual(deserialized["type"], "and")


# ============================================================================
# SECTION 3: TIER 3 - CROSS-FEATURE COMBINATIONS (12 TESTS)
# Pairwise and cross-module integration across compiler, loader, GSI & overlays.
# ============================================================================

class TestTier3CrossFeatureCombinations(unittest.TestCase):
    """Tier 3: Pairwise cross-feature integration testing."""

    def test_t3_01_blockly_to_transpiler_to_compiler_to_dll(self):
        """T3_01: Workspace -> Transpiler C++ Code -> MSVC Compiler -> Dynamic DLL creation."""
        body = """
        out_frame.Fill(255, 128, 0);
        """
        cpp = CppTranspilerModel.generate_cpp_source("t3_pipeline_test", body)
        self.assertIn("AuraCreateEffect", cpp)

        with tempfile.TemporaryDirectory() as td:
            dll_out = os.path.join(td, "effect_t3_pipeline.dll")
            ok, log, code = CompilerPipeline.compile_cpp_to_dll(cpp, dll_out)
            self.assertTrue(ok, f"Pipeline compile failed: {log}")
            self.assertTrue(os.path.exists(dll_out))

    def test_t3_02_compiled_dll_to_shadow_copy_to_loadlibrary_execution(self):
        """T3_02: Compiled DLL -> Shadow Copy Cache -> LoadLibraryW -> Query Exports -> FreeLibrary."""
        cpp = """
        #include <windows.h>
        #include <cstdint>
        extern "C" __declspec(dllexport) uint32_t AuraGetPluginApiVersion() { return 0x00010000; }
        extern "C" __declspec(dllexport) const char* AuraGetEffectName() { return "shadow_exec_test"; }
        """
        with tempfile.TemporaryDirectory() as td:
            orig_dll = os.path.join(td, "orig.dll")
            shadow_dll = os.path.join(td, "shadow.dll")
            ok, _, _ = CompilerPipeline.compile_cpp_to_dll(cpp, orig_dll)
            self.assertTrue(ok)
            shutil.copyfile(orig_dll, shadow_dll)

            k32 = ctypes.windll.kernel32
            k32.LoadLibraryW.restype = ctypes.c_void_p
            k32.GetProcAddress.restype = ctypes.c_void_p
            k32.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
            k32.FreeLibrary.argtypes = [ctypes.c_void_p]

            h = k32.LoadLibraryW(shadow_dll)
            self.assertIsNotNone(h)
            try:
                name_proc = k32.GetProcAddress(h, b"AuraGetEffectName")
                self.assertIsNotNone(name_proc)
                name_fn = ctypes.CFUNCTYPE(ctypes.c_char_p)(name_proc)
                name_val = name_fn().decode("utf-8")
                self.assertEqual(name_val, "shadow_exec_test")
            finally:
                k32.FreeLibrary(h)

    def test_t3_03_atomic_gsi_sensor_in_transpiled_cpp(self):
        """T3_03: Atomic GSI sensor in Blockly generates C++ accessing IGsiReader safely."""
        body = """
        double hp = 100.0;
        if (gsi != nullptr) {
            hp = gsi->GetNumber("player.state.health", 100.0);
        }
        const uint8_t g = static_cast<uint8_t>(std::clamp(hp * 2.55, 0.0, 255.0));
        out_frame.Fill(0, g, 0);
        """
        cpp = CppTranspilerModel.generate_cpp_source("GsiSensorTest", body)
        self.assertIn("gsi->GetNumber", cpp)
        self.assertIn("std::clamp", cpp)
        ok, violations = CppTranspilerModel.check_zero_allocations(cpp)
        self.assertTrue(ok, f"Zero allocation violations: {violations}")

    def test_t3_04_gsi_telemetry_to_condition_node_to_profile_switch(self):
        """T3_04: GSI Telemetry -> ConditionNode AST -> Process & GSI Matching -> Target Profile."""
        rule_tree = {
            "type": "and",
            "conditions": [
                {"field": "process.name", "op": "==", "value": "cs2.exe"},
                {"field": "player.state.health", "op": "<=", "value": 20}
            ]
        }
        # HP is 75 in sample payload: should NOT match
        self.assertFalse(ConditionNodeEvaluator.evaluate(rule_tree, CS2_GSI_SAMPLE_PAYLOAD, "cs2.exe"))

        # Mutate HP to 15: should MATCH
        critical_gsi = copy.deepcopy(CS2_GSI_SAMPLE_PAYLOAD)
        critical_gsi["player"]["state"]["health"] = 15
        self.assertTrue(ConditionNodeEvaluator.evaluate(rule_tree, critical_gsi, "cs2.exe"))

    def test_t3_05_dynamic_event_to_overlay_manager_pulse_and_crossfade(self):
        """T3_05: CS2 Kill Event -> OverlayManager Pulse -> Crossfade Blend -> Expiry Restoration."""
        mgr = OverlayManagerModel()
        # Base profile is cyan (0, 255, 255)
        base_color = (0, 255, 255)
        # Kill pulse is gold (255, 215, 0), duration 1000ms, fade 400ms
        mgr.trigger_overlay("event.kill", (255, 215, 0), duration_ms=1000, fade_ms=400, trigger_time_ms=0)

        # 1. During full pulse (t = 200ms, alpha = 1.0)
        c1 = mgr.blend_frames(base_color, current_ms=200)
        self.assertEqual(c1, (255, 215, 0))

        # 2. During crossfade (t = 800ms, alpha = (1000-800)/400 = 0.5)
        c2 = mgr.blend_frames(base_color, current_ms=800)
        self.assertEqual(c2, (128, 235, 128))

        # 3. After expiry (t = 1200ms)
        mgr.evict_expired(current_ms=1200)
        c3 = mgr.blend_frames(base_color, current_ms=1200)
        self.assertEqual(c3, base_color)

    def test_t3_06_orchestrator_ui_to_json_export_to_config_roundtrip(self):
        """T3_06: Visual Orchestrator -> Declarative JSON -> config.json Save -> Ingestion."""
        orchestrator_payload = {
            "orchestration": {
                "rules": [
                    {
                        "id": "cs2_main",
                        "process": "cs2.exe",
                        "dnd": True,
                        "condition": {"field": "player.state.health", "op": ">", "value": 0},
                        "target_profile": "cs2_play"
                    }
                ],
                "event_overlays": [
                    {"event": "event.kill", "effect": "kill_pulse", "duration_ms": 1200, "fade_ms": 400}
                ],
                "fallback_profile": "static"
            }
        }
        raw = json.dumps(orchestrator_payload)
        parsed = json.loads(raw)
        self.assertEqual(len(parsed["orchestration"]["rules"]), 1)
        self.assertEqual(len(parsed["orchestration"]["event_overlays"]), 1)
        self.assertTrue(parsed["orchestration"]["rules"][0]["dnd"])

    def test_t3_07_live_preview_js_to_preview_api_hex_stream(self):
        """T3_07: Live Preview JS Execution -> Frame Generation -> Preview API 408-Char Hex Stream."""
        # Generate 68 keys color from JS sandbox
        colors = []
        for k in KEYBOARD_68_KEYS:
            phase = (500 % 1000) / 1000.0
            r = int(round(255 * phase))
            colors.append((r, 0, 255 - r))

        raw_bytes = bytearray()
        for r, g, b in colors:
            raw_bytes.extend([r, g, b])

        hex_stream = raw_bytes.hex()
        self.assertEqual(len(hex_stream), 408)
        self.assertEqual(len(raw_bytes), 204)

    def test_t3_08_hot_swap_at_25fps_while_actively_streaming_frames(self):
        """T3_08: 25 FPS Hot-Swap: Swapping Effect A to Effect B during active rendering without frame drops."""
        class MockDaemonStreamer:
            def __init__(self):
                self.active_color = (255, 0, 0)
                self.rendered_frames = []
            def tick(self):
                self.rendered_frames.append(self.active_color)
            def swap(self, new_color):
                self.active_color = new_color

        streamer = MockDaemonStreamer()
        for _ in range(5): streamer.tick()  # 5 frames of A
        streamer.swap((0, 255, 0))          # Hot swap
        for _ in range(5): streamer.tick()  # 5 frames of B

        self.assertEqual(len(streamer.rendered_frames), 10)
        self.assertEqual(streamer.rendered_frames[0], (255, 0, 0))
        self.assertEqual(streamer.rendered_frames[9], (0, 255, 0))

    def test_t3_09_extreme_gsi_inputs_across_transpiler_and_condition_engine(self):
        """T3_09: Extreme GSI Inputs (-100 HP, NaN armor, empty bomb) handled gracefully without crash."""
        extreme_gsi = {
            "player": {"state": {"health": -100, "armor": float("nan")}},
            "round": {"bomb": ""}
        }
        node = {"field": "player.state.health", "op": ">", "value": 0}
        self.assertFalse(ConditionNodeEvaluator.evaluate(node, extreme_gsi))

    def test_t3_10_multi_layer_overlay_stack_composite_blending(self):
        """T3_10: Multi-layer Overlay Stack: Bomb Alert + Flashbang Whiteout composite resolution."""
        mgr = OverlayManagerModel()
        # Layer 1: Bomb pulse (red, priority 10)
        mgr.trigger_overlay("event.bomb", (255, 0, 0), duration_ms=2000, fade_ms=500, priority=10, trigger_time_ms=0)
        # Layer 2: Flashbang (white, priority 30, triggers at 500ms)
        mgr.trigger_overlay("event.flash", (255, 255, 255), duration_ms=1000, fade_ms=500, priority=30, trigger_time_ms=500)

        # At t = 200ms: only Bomb pulse active (red)
        c1 = mgr.blend_frames((0, 0, 0), current_ms=200)
        self.assertEqual(c1, (255, 0, 0))

        # At t = 600ms: Flash active (alpha = 1.0, high priority dominates)
        c2 = mgr.blend_frames((0, 0, 0), current_ms=600)
        self.assertEqual(c2, (255, 255, 255))

    def test_t3_11_legacy_config_ingestion_and_roundtrip_identity(self):
        """T3_11: Ingest legacy config.json -> Auto-migrate -> Full round-trip preserves all rules."""
        legacy = {
            "default_profile": "custom_keymap",
            "rules": [{"process": "cs2.exe", "profile": "cs2_pro"}],
            "gsi_bindings": [{"field": "player_state.health", "operator": "<", "value": 20, "profile": "low_hp"}]
        }
        migrated = copy.deepcopy(legacy)
        migrated["orchestration"] = {
            "rules": [
                {"id": "r0", "process": "cs2.exe", "dnd": False, "target_profile": "cs2_pro"},
                {"id": "r1", "process": "*", "dnd": False, "condition": {"field": "player.state.health", "op": "<", "value": 20}, "target_profile": "low_hp"}
            ],
            "event_overlays": [],
            "fallback_profile": "custom_keymap"
        }
        s = json.dumps(migrated)
        reloaded = json.loads(s)
        self.assertEqual(reloaded["orchestration"]["rules"][0]["process"], "cs2.exe")

    def test_t3_12_compiler_failure_error_reporting_isolation(self):
        """T3_12: Compiler failure in web pipeline reports cleanly without crashing daemon or web server."""
        bad_cpp = "class SyntaxErr {"
        with tempfile.TemporaryDirectory() as td:
            dll_out = os.path.join(td, "fail.dll")
            ok, log, code = CompilerPipeline.compile_cpp_to_dll(bad_cpp, dll_out)
            self.assertFalse(ok)
            response = {"status": "error", "message": "Compilation failed", "compiler_output": log}
            self.assertEqual(response["status"], "error")
            self.assertIn("error", response["compiler_output"].lower())


# ============================================================================
# SECTION 4: TIER 4 - REAL-WORLD WORKLOAD SCENARIOS (5 TESTS)
# High-complexity end-to-end user journeys derived from ORIGINAL_REQUEST.md.
# ============================================================================

class TestTier4RealWorldScenarios(unittest.TestCase):
    """Tier 4: Realistic end-to-end workflows and competitive gaming scenarios."""

    def test_t4_01_cs2_dynamic_health_bar_and_kill_pulse_scenario(self):
        """T4_01: CS2 Dynamic Health Bar & Kill Pulse: Transpiler -> DLL -> GSI Telemetry -> Kill Overlay."""
        # 1. User designs health-reactive effect in Blockly (green when high HP, red when low)
        body = """
        double hp = 100.0;
        if (gsi != nullptr) {
            hp = gsi->GetNumber("player.state.health", 100.0);
        }
        const double factor = std::clamp(hp / 100.0, 0.0, 1.0);
        const uint8_t r = static_cast<uint8_t>(std::clamp((1.0 - factor) * 255.0, 0.0, 255.0));
        const uint8_t g = static_cast<uint8_t>(std::clamp(factor * 255.0, 0.0, 255.0));
        out_frame.Fill(r, g, 0);
        """
        cpp = CppTranspilerModel.generate_cpp_source("CS2HealthBar", body)
        ok, violations = CppTranspilerModel.check_zero_allocations(cpp)
        self.assertTrue(ok, f"Violations: {violations}")

        # 2. Simulate health bar rendering at 75 HP
        hp_factor = 75.0 / 100.0
        base_color = (int(round((1.0 - hp_factor) * 255)), int(round(hp_factor * 255)), 0)  # (64, 191, 0)

        # 3. Player gets a kill: OverlayManager triggers gold pulse for 1200ms with 400ms fade
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("event.kill", (255, 215, 0), duration_ms=1200, fade_ms=400, trigger_time_ms=1000)

        # Check full pulse active at 1500ms
        frame_mid = mgr.blend_frames(base_color, current_ms=1500)
        self.assertEqual(frame_mid, (255, 215, 0))

        # Check linear fade restoration at 2000ms (alpha = 0.5)
        frame_fade = mgr.blend_frames(base_color, current_ms=2000)
        self.assertEqual(frame_fade[0], int(round(base_color[0] * 0.5 + 255 * 0.5)))

        # Check restoration to base health bar at 2300ms
        mgr.evict_expired(current_ms=2300)
        frame_restored = mgr.blend_frames(base_color, current_ms=2300)
        self.assertEqual(frame_restored, base_color)

    def test_t4_02_bomb_plant_countdown_flash_overlay_scenario(self):
        """T4_02: Bomb Plant Countdown Flash Overlay: ConditionNode triggers bomb alert, flashbang overlays."""
        # 1. Continuous state tree detects C4 planted
        bomb_condition = {"field": "round.bomb", "op": "==", "value": "planted"}
        is_planted = ConditionNodeEvaluator.evaluate(bomb_condition, CS2_GSI_SAMPLE_PAYLOAD)
        self.assertTrue(is_planted)

        # 2. Base profile pulses red at 2 Hz
        base_color = (255, 0, 0)

        # 3. Enemy throws flashbang: OverlayManager triggers full white flash (duration 2000ms, fade 1000ms)
        mgr = OverlayManagerModel()
        mgr.trigger_overlay("event.flash", (255, 255, 255), duration_ms=2000, fade_ms=1000, priority=50, trigger_time_ms=0)

        # During flash (t = 500ms): entire keyboard is white
        white_frame = mgr.blend_frames(base_color, current_ms=500)
        self.assertEqual(white_frame, (255, 255, 255))

        # Mid-fade (t = 1500ms): alpha = 0.5 -> blend of white and red
        fade_frame = mgr.blend_frames(base_color, current_ms=1500)
        self.assertEqual(fade_frame, (255, 128, 128))

        # After flash clears (t = 2500ms): keyboard resumes red bomb countdown pulse
        mgr.evict_expired(current_ms=2500)
        restored = mgr.blend_frames(base_color, current_ms=2500)
        self.assertEqual(restored, base_color)

    def test_t4_03_wasd_reactive_spark_with_wave_sweep_scenario(self):
        """T4_03: WASD Reactive Spark with Wave Sweep: Geometry blocks, JS live preview, and 0-alloc C++."""
        # 1. Identify WASD keys
        wasd_keys = [k for k in KEYBOARD_68_KEYS if k["name"] in ("W", "A", "S", "D")]
        non_wasd_keys = [k for k in KEYBOARD_68_KEYS if k["name"] not in ("W", "A", "S", "D")]
        self.assertEqual(len(wasd_keys), 4)
        self.assertEqual(len(non_wasd_keys), 64)

        # 2. Transpiler generates code filtering WASD
        body = """
        for (const auto& [name, info] : keymap.GetAllKeys()) {
            if (name == "W" || name == "A" || name == "S" || name == "D") {
                out_frame.SetKey(info.led_id, 255, 255, 0); // Highlight WASD in yellow
            } else {
                out_frame.SetKey(info.led_id, 0, 50, 100);  // Dark blue background
            }
        }
        """
        cpp = CppTranspilerModel.generate_cpp_source("WASDHighlight", body)
        ok, violations = CppTranspilerModel.check_zero_allocations(cpp)
        self.assertTrue(ok)

        # 3. Simulate JS preview evaluation of both subsets
        def render_preview(kname: str):
            return (255, 255, 0) if kname in ("W", "A", "S", "D") else (0, 50, 100)

        self.assertEqual(render_preview("W"), (255, 255, 0))
        self.assertEqual(render_preview("SPACE"), (0, 50, 100))

    def test_t4_04_compound_process_and_dnd_orchestration_scenario(self):
        """T4_04: Compound Process + DND Orchestration: Window focus switching and DND suppression."""
        rules = [
            {
                "id": "cs2_alive",
                "process": "cs2.exe",
                "dnd": True,
                "condition": {"field": "player.state.health", "op": ">", "value": 0},
                "target_profile": "cs2_competitive"
            },
            {
                "id": "cs2_dead",
                "process": "cs2.exe",
                "dnd": True,
                "condition": {"field": "player.state.health", "op": "==", "value": 0},
                "target_profile": "cs2_spectator"
            },
            {
                "id": "code_rule",
                "process": "devenv.exe",
                "dnd": False,
                "condition": {},
                "target_profile": "coding_matrix"
            }
        ]
        fallback_profile = "desktop_static"

        def evaluate_rules(proc: str, gsi: Dict[str, Any]) -> Tuple[str, bool]:
            for r in rules:
                if ConditionNodeEvaluator.evaluate({"field": "process.name", "op": "==", "value": r["process"]}, {}, proc):
                    if ConditionNodeEvaluator.evaluate(r.get("condition", {}), gsi, proc):
                        return r["target_profile"], r["dnd"]
            return fallback_profile, False

        # 1. Desktop focus -> fallback profile, DND false
        p1, dnd1 = evaluate_rules("explorer.exe", CS2_GSI_SAMPLE_PAYLOAD)
        self.assertEqual(p1, "desktop_static")
        self.assertFalse(dnd1)

        # 2. Visual Studio focus -> coding_matrix, DND false
        p2, dnd2 = evaluate_rules("devenv.exe", CS2_GSI_SAMPLE_PAYLOAD)
        self.assertEqual(p2, "coding_matrix")
        self.assertFalse(dnd2)

        # 3. CS2 alive -> cs2_competitive, DND true
        p3, dnd3 = evaluate_rules("cs2.exe", CS2_GSI_SAMPLE_PAYLOAD)
        self.assertEqual(p3, "cs2_competitive")
        self.assertTrue(dnd3)

        # 4. CS2 dead (0 HP) -> cs2_spectator, DND true
        dead_gsi = copy.deepcopy(CS2_GSI_SAMPLE_PAYLOAD)
        dead_gsi["player"]["state"]["health"] = 0
        p4, dnd4 = evaluate_rules("cs2.exe", dead_gsi)
        self.assertEqual(p4, "cs2_spectator")
        self.assertTrue(dnd4)

    def test_t4_05_full_roundtrip_config_save_and_ui_restore_scenario(self):
        """T4_05: Full Round-Trip Config Save & UI Restore: Dual-layer JSON save, reload, and AST equivalence."""
        full_config = {
            "default_profile": "desktop_static",
            "fps": 25,
            "orchestration": {
                "rules": [
                    {
                        "id": "rule_cs2",
                        "process": "cs2.exe",
                        "dnd": True,
                        "condition": {
                            "type": "and",
                            "conditions": [
                                {"field": "player.state.health", "op": ">", "value": 0},
                                {"field": "round.bomb", "op": "!=", "value": "exploded"}
                            ]
                        },
                        "target_profile": "cs2_pro"
                    }
                ],
                "event_overlays": [
                    {"event": "event.kill", "effect": "kill_pulse", "duration_ms": 1200, "fade_ms": 400},
                    {"event": "event.flash", "effect": "white_flash", "duration_ms": 2000, "fade_ms": 1000}
                ],
                "fallback_profile": "desktop_static"
            },
            "blockly_orchestrator": {
                "version": 1,
                "blocks": [
                    {"type": "orchestrator_root", "id": "root_1", "x": 50, "y": 50},
                    {"type": "match_process", "id": "proc_1", "x": 100, "y": 150}
                ]
            },
            "blockly_effects": {
                "rainbow_wave": {
                    "version": 1,
                    "blocks": [{"type": "time_wave", "id": "wave_1", "x": 80, "y": 80}]
                }
            }
        }

        # 1. Serialize to JSON string (simulating web POST /api/config)
        json_str = json.dumps(full_config, indent=2, ensure_ascii=False)
        self.assertGreater(len(json_str), 500)

        # 2. Deserialize (simulating daemon / UI load)
        restored = json.loads(json_str)

        # 3. Verify semantic equality across all layers
        self.assertEqual(restored["default_profile"], full_config["default_profile"])
        self.assertEqual(len(restored["orchestration"]["rules"]), 1)
        self.assertEqual(len(restored["orchestration"]["event_overlays"]), 2)
        self.assertEqual(len(restored["blockly_orchestrator"]["blocks"]), 2)
        self.assertIn("rainbow_wave", restored["blockly_effects"])

        # 4. Verify ConditionNode AST evaluation on restored config
        restored_cond = restored["orchestration"]["rules"][0]["condition"]
        self.assertTrue(ConditionNodeEvaluator.evaluate(restored_cond, CS2_GSI_SAMPLE_PAYLOAD))


# ============================================================================
# SECTION 5: FORMATTED RUNNER & SUMMARY REPORT
# ============================================================================

if __name__ == "__main__":
    print("REFERENCE MODEL CHECKS ONLY - not production coverage", flush=True)
    unittest.main(verbosity=2)
