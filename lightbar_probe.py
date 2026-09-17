#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
=============================================================================
ASUS ROG Falchion Ace HFX Light Bar Probe 入口
=============================================================================
直接路由至 tools/lightbar_probe.py，保证根目录入口与 tools 目录探针完全同步。
支持 GUI 交互探针与 CLI 参数 (--diagnostics, --test-a, --test-b, --test-c)。
=============================================================================
"""
import os
import sys

_DIR = os.path.dirname(os.path.abspath(__file__))
_TOOLS = os.path.join(_DIR, "tools")
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from lightbar_probe import (
    LightingEngine,
    LightBarProbeGUI,
    check_exclusive_mutex,
    get_service_status,
    get_process_status,
    set_service_state,
    load_existing_keymap_keys,
    load_lightbar_mapping,
    export_lightbar_mapping,
    TOTAL_LEDS,
    RGB_CHANNELS,
    TEST_A_CONTROL_PIN,
    TEST_B_CANDIDATE_PIN,
    ROW0_CANDIDATES,
    main,
    run_cli,
)

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1].startswith("-"):
        run_cli(sys.argv[1:])
    else:
        main()
