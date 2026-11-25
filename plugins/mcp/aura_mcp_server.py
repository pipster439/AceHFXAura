#!/usr/bin/env python3
"""Aura MCP Server (stdio JSON-RPC 2.0, zero external dependencies).

Replaces the removed Studio built-in AI assistant by exposing Aura's local
daemon (127.0.0.1:19897) and web/compiler server (127.0.0.1:19898) to
external MCP clients such as Antigravity, Claude Desktop, and Cursor.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from typing import Any

PROTOCOL_DEFAULT = "2024-11-05"
SUPPORTED_PROTOCOLS = ("2024-11-05", "2025-03-26", "2025-06-18")
SERVER_INFO = {"name": "aura-mcp", "version": "1.1.0"}

HEX32_RE = re.compile(r"^[a-f0-9]{32}$")
PROJECT_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{0,47}$")
CPP_NAME_RE = re.compile(r"^[A-Za-z0-9_]{1,64}$")

# Authoritative 68-key ROG Falchion Ace HFX table aligned 1:1 with
# kHallKeyTable in include/aura/hardware/m605_hall_telemetry.h:
#   (canonical_name, logical_id, hid_usage)
HALL_KEY_TABLE: tuple[tuple[str, int, int], ...] = (
    # Row 0 (15 keys)
    ("ESC", 0x0100, 41), ("1", 0x0600, 30), ("2", 0x0700, 31), ("3", 0x0101, 32),
    ("4", 0x0102, 33), ("5", 0x0103, 34), ("6", 0x0104, 35), ("7", 0x0105, 36),
    ("8", 0x0106, 37), ("9", 0x0107, 38), ("0", 0x0108, 39), ("-", 0x0109, 45),
    ("=", 0x010A, 46), ("BACKSPACE", 0x0706, 42), ("INS", 0x0608, 73),
    # Row 1 (15 keys)
    ("TAB", 0x0200, 43), ("Q", 0x0601, 20), ("W", 0x0701, 26), ("E", 0x0201, 8),
    ("R", 0x0202, 21), ("T", 0x0203, 23), ("Y", 0x0204, 28), ("U", 0x0205, 24),
    ("I", 0x0206, 12), ("O", 0x0207, 18), ("P", 0x0208, 19), ("[", 0x0209, 47),
    ("]", 0x020A, 48), ("\\", 0x0607, 49), ("DEL", 0x0708, 76),
    # Row 2 (14 keys)
    ("CAPS", 0x0300, 57), ("A", 0x0602, 4), ("S", 0x0702, 22), ("D", 0x0301, 7),
    ("F", 0x0302, 9), ("G", 0x0303, 10), ("H", 0x0304, 11), ("J", 0x0305, 13),
    ("K", 0x0306, 14), ("L", 0x0307, 15), (";", 0x0308, 51), ("'", 0x0309, 52),
    ("ENTER", 0x0707, 40), ("PGUP", 0x060B, 75),
    # Row 3 (14 keys)
    ("L_SHIFT", 0x0400, 225), ("Z", 0x0703, 29), ("X", 0x0401, 27), ("C", 0x0501, 6),
    ("V", 0x0402, 25), ("B", 0x0403, 5), ("N", 0x0404, 17), ("M", 0x0405, 16),
    (",", 0x0406, 54), (".", 0x0407, 55), ("/", 0x0408, 56), ("R_SHIFT", 0x040A, 229),
    ("UP", 0x070A, 82), ("PGDN", 0x070B, 78),
    # Row 4 (10 keys)
    ("L_CTRL", 0x0500, 224), ("L_WIN", 0x0604, 227), ("L_ALT", 0x0704, 226),
    ("SPACE", 0x0503, 44), ("R_ALT", 0x0507, 230), ("FN", 0x0508, 232),
    ("COPILOT", 0x050A, 228), ("LEFT", 0x060A, 80), ("DOWN", 0x050B, 81),
    ("RIGHT", 0x040B, 79),
)

KEY_ALIASES: dict[str, str] = {
    "ESCAPE": "ESC",
    "BACK": "BACKSPACE",
    "INSERT": "INS",
    "DELETE": "DEL",
    "CAPSLOCK": "CAPS",
    "CAPS_LOCK": "CAPS",
    "RETURN": "ENTER",
    "LSHIFT": "L_SHIFT",
    "LEFT_SHIFT": "L_SHIFT",
    "RSHIFT": "R_SHIFT",
    "RIGHT_SHIFT": "R_SHIFT",
    "LCTRL": "L_CTRL",
    "LEFT_CTRL": "L_CTRL",
    "LWIN": "L_WIN",
    "LEFT_WIN": "L_WIN",
    "WIN": "L_WIN",
    "LALT": "L_ALT",
    "LEFT_ALT": "L_ALT",
    "RALT": "R_ALT",
    "RIGHT_ALT": "R_ALT",
    "SPACEBAR": "SPACE",
    "MINUS": "-",
    "EQUAL": "=",
    "PLUS": "=",
    "LBRACKET": "[",
    "RBRACKET": "]",
    "BACKSLASH": "\\",
    "SEMICOLON": ";",
    "APOSTROPHE": "'",
    "QUOTE": "'",
    "COMMA": ",",
    "PERIOD": ".",
    "DOT": ".",
    "SLASH": "/",
    "ARROW_UP": "UP",
    "ARROW_DOWN": "DOWN",
    "ARROW_LEFT": "LEFT",
    "ARROW_RIGHT": "RIGHT",
}

KEY_NAME_TO_SPEC: dict[str, tuple[str, int, int]] = {name: (name, lid, hid) for name, lid, hid in HALL_KEY_TABLE}
LOGICAL_ID_TO_SPEC: dict[int, tuple[str, int, int]] = {lid: (name, lid, hid) for name, lid, hid in HALL_KEY_TABLE}
FN_LOGICAL_ID = 0x0508

EFFECT_IR_V1_GUIDE = """# Aura EffectProgram IR v1 Specification

Aura's Studio execution engine compiles restricted EffectProgram IR v1 JSON into safe, real-time native lighting programs (`POST /api/programs`).

## Top-Level Fields (all required, no unknown fields)
- `schema_version`: `1` (integer)
- `entry`: non-negative integer index into `nodes` (usually `0`)
- `variables`: integer `0..64` (numeric registers accessed via `{"op":"var","index":i}`)
- `slots`: integer `0..64` (loop counter slots accessed by `loopInit`/`loopTest`/`increment`)
- `publication`: `{"mode": "continuous", "fade_out_ms": 0}` or `{"mode": "one_shot", "fade_out_ms": 0..60000}`
- `nodes`: non-empty array of instruction objects (`<= 2048` nodes)

## Instructions (`kind`)
- `{"kind": "end"}`
- `{"kind": "jump", "next": pc}`
- `{"kind": "if", "condition": <bool_expr>, "yes": pc, "no": pc}`
- `{"kind": "wait", "duration": <num_expr_ms>, "next": pc}`
- `{"kind": "loopInit", "slot": s, "mode": "repeat", "count": <num_expr>, "next": pc}`
- `{"kind": "loopInit", "slot": s, "mode": "keys", "zone": "all|alpha|wasd|arrows|numeric|navigation|modifier", "next": pc}`
- `{"kind": "loopTest", "slot": s, "mode": "repeat|keys", "body": pc, "after": pc}`
- `{"kind": "loopTest", "slot": s, "mode": "while", "condition": <bool_expr>, "until": false, "body": pc, "after": pc}`
- `{"kind": "increment", "slot": s, "next": pc}`
- `{"kind": "action", "op": "fill", "color": <color_expr>, "next": pc}`
- `{"kind": "action", "op": "setKey", "led": <num_expr>, "color": <color_expr>, "next": pc}`
- `{"kind": "action", "op": "setVar", "index": i, "value": <num_expr>, "next": pc}`
- `{"kind": "action", "op": "changeVar", "index": i, "value": <num_expr>, "next": pc}`
- `{"kind": "action", "op": "ripple", "key": "ESC", "speed": <num_expr>, "color": <color_expr>, "next": pc}`

## Expressions (`op`)
- Literals/Inputs:
  - `{"op": "number", "value": 1.0}`, `{"op": "boolean", "value": true}`, `{"op": "text", "value": "x"}`
  - `{"op": "var", "index": 0}`, `{"op": "elapsed"}`, `{"op": "pressed"}`
  - `{"op": "coords", "field": "physical_x|physical_y|row|col|led_id"}`
  - `{"op": "gsi_number|gsi_bool|gsi_text", "path": "player.state.health", "default": <expr>}`
  - `{"op": "audio_feature", "feature": "rms|peak|centroid", "default": <num_expr>}`
  - `{"op": "screen_color", "default": <color_expr>}`
- Math & Logic (`args: [...]`):
  - Unary num: `sqrt, abs, neg, ln, log10, exp, pow10, sin, cos, tan, asin, acos, atan, round, ceil, floor, decay` (`decay` takes 1 num ms arg and reads key release decay!)
  - Binary num: `add, sub, mul, div, mod, pow, min, max`
  - Binary bool: `eq, ne, lt, le, gt, ge, and, or`; Unary bool: `not`
  - Lighting helpers:
    - `{"op": "time_phase", "args": [<period_ms>]}` -> `0..1`
    - `{"op": "waveform", "wave": "sine|triangle|square|saw", "args": [<phase_0_1>, <min>, <max>]}`
    - `{"op": "distance", "args": [<x1>, <y1>, <x2>, <y2>]}`
    - `{"op": "radial_phase", "args": [<cx>, <cy>, <scale>, <phase>]}`
    - `{"op": "rgb", "args": [<r_0_255>, <g_0_255>, <b_0_255>]}`
    - `{"op": "hsv", "args": [<h_0_360>, <s_0_1>, <v_0_1>]}`
    - `{"op": "lerp", "args": [<color_a>, <color_b>, <t_0_1>]}`
    - `{"op": "brightness", "args": [<color>, <factor_0_1>]}`
    - `{"op": "cycle", "args": [<color_a>, <color_b>, <phase_0_1>]}`

## Minimal Working Rainbow Wave Example
```json
{
  "schema_version": 1,
  "entry": 0,
  "variables": 0,
  "slots": 1,
  "publication": {"mode": "continuous", "fade_out_ms": 0},
  "nodes": [
    {"kind": "loopInit", "slot": 0, "mode": "keys", "zone": "all", "next": 1},
    {"kind": "loopTest", "slot": 0, "mode": "keys", "body": 2, "after": 4},
    {
      "kind": "action",
      "op": "setKey",
      "led": {"op": "coords", "field": "led_id"},
      "color": {
        "op": "hsv",
        "args": [
          {
            "op": "mod",
            "args": [
              {
                "op": "add",
                "args": [
                  {"op": "mul", "args": [{"op": "coords", "field": "physical_x"}, {"op": "number", "value": 24}]},
                  {"op": "mul", "args": [{"op": "time_phase", "args": [{"op": "number", "value": 2500}]}, {"op": "number", "value": 360}]}
                ]
              },
              {"op": "number", "value": 360}
            ]
          },
          {"op": "number", "value": 1},
          {"op": "number", "value": 1}
        ]
      },
      "next": 3
    },
    {"kind": "increment", "slot": 0, "next": 1},
    {"kind": "wait", "duration": {"op": "number", "value": 33}, "next": 0}
  ]
}
```
"""

NATIVE_CPP_GUIDE = r"""# Aura Native C++ Plugin ABI v1 + Hall Analog v1 Template

Compile with `studio_compile_cpp_effect` (`name`: `^[A-Za-z0-9_]{1,64}$`).
Optional `AuraGetHallAnalogVersion` / `AuraBindHallAnalog` lets plugins read 68-key ROG Falchion Ace HFX magnetic switch depth in real time.

```cpp
#include "engine/effect.h"
#include "engine/plugin_interface.h"
#include <algorithm>
#include <array>
#include <cmath>

class CustomHallEffect : public aura::Effect {
public:
    CustomHallEffect() : aura::Effect("custom_hall") {}

    void BindHallAnalog(const AuraHallAnalogHostV1* host) {
        if (host && host->struct_size == sizeof(AuraHallAnalogHostV1) &&
            host->version == AURA_HALL_ANALOG_VERSION && host->read_snapshot) {
            host_ = *host;
            bound_ = true;
        }
    }

    void Render(uint64_t elapsed_ms, aura::FrameBuffer& out_frame, const aura::Keymap& keymap) override {
        out_frame.Clear();
        // Base ambient glow using keymap.GetAllKeys(), then layer Hall depth if bound_:
        if (bound_) {
            AuraHallAnalogSnapshotV1 snap{};
            if (host_.read_snapshot(host_.context, &snap, sizeof(snap)) == 1 && snap.active) {
                for (uint32_t i = 0; i < std::min<uint32_t>(snap.count, AURA_HALL_ANALOG_MAX_KEYS); ++i) {
                    const auto& k = snap.keys[i];
                    if (k.depth_mm < 0.08f) continue;
                    // Map hid_usage or key name -> keymap.FindLedId(...) -> out_frame.SetKey(...)
                }
            }
        }
    }

private:
    AuraHallAnalogHostV1 host_{};
    bool bound_{false};
};

extern "C" {
AURA_PLUGIN_EXPORT uint32_t AuraGetPluginApiVersion() { return AURA_PLUGIN_API_VERSION; }
AURA_PLUGIN_EXPORT const char* AuraGetEffectName() { return "custom_hall"; }
AURA_PLUGIN_EXPORT aura::Effect* AuraCreateEffect() { return new CustomHallEffect(); }
AURA_PLUGIN_EXPORT void AuraDestroyEffect(aura::Effect* effect) { delete effect; }
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraGetHallAnalogVersion() { return AURA_HALL_ANALOG_VERSION; }
AURA_PLUGIN_EXPORT uint32_t AURA_PLUGIN_CALL AuraBindHallAnalog(aura::Effect* e, const AuraHallAnalogHostV1* host) {
    if (auto* p = dynamic_cast<CustomHallEffect*>(e); p && host && host->version == AURA_HALL_ANALOG_VERSION) {
        p->BindHallAnalog(host);
        return 1;
    }
    return 0;
}
}
```
"""


def _ensure_loopback_base(raw_url: str, label: str) -> str:
    parsed = urllib.parse.urlparse(raw_url.strip())
    if parsed.scheme != "http" or parsed.hostname not in ("127.0.0.1", "localhost", "::1"):
        raise ValueError(f"{label} must be an http loopback URL (127.0.0.1 / localhost): {raw_url}")
    return f"{parsed.scheme}://{parsed.netloc}"


def resolve_logical_id(key: Any) -> int:
    """Resolve key name (e.g. 'W', 'L_SHIFT', 'SPACE'), hex string ('0x0701'), or int to logical_id."""
    if isinstance(key, bool):
        raise ValueError("Key specifier cannot be boolean")
    if isinstance(key, int):
        if key not in LOGICAL_ID_TO_SPEC:
            raise ValueError(f"Unknown logical_id 0x{key:04X}; must be one of the 68 verified M605 keys")
        return key
    if isinstance(key, str):
        raw = key.strip()
        if not raw:
            raise ValueError("Empty key specifier")
        if raw.lower().startswith("0x"):
            try:
                val = int(raw, 16)
            except ValueError as exc:
                raise ValueError(f"Invalid hex logical_id: {key}") from exc
            if val not in LOGICAL_ID_TO_SPEC:
                raise ValueError(f"Unknown logical_id {raw}; must be one of the 68 verified M605 keys")
            return val
        if raw.isdigit() and raw not in KEY_NAME_TO_SPEC:
            val = int(raw)
            if val in LOGICAL_ID_TO_SPEC:
                return val
        upper = raw.upper().replace("-", "_") if len(raw) > 1 else raw
        canonical = KEY_ALIASES.get(upper, upper)
        spec = KEY_NAME_TO_SPEC.get(canonical)
        if spec is None and raw in KEY_NAME_TO_SPEC:
            spec = KEY_NAME_TO_SPEC[raw]
        if spec is None:
            raise ValueError(f"Unknown M605 key name '{key}'; inspect resource aura://keymap/m605-68 for valid keys")
        return spec[1]
    raise ValueError(f"Unsupported key specifier type: {type(key).__name__}")


def _resolve_logical_ids(keys: Any) -> list[int]:
    if not isinstance(keys, list) or not keys:
        raise ValueError("'keys' must be a non-empty list of key names or logical_ids")
    seen: set[int] = set()
    resolved: list[int] = []
    for k in keys:
        lid = resolve_logical_id(k)
        if lid not in seen:
            seen.add(lid)
            resolved.append(lid)
    return resolved


class AuraHttpError(RuntimeError):
    def __init__(self, status: int | None, url: str, message: str, body: Any = None) -> None:
        super().__init__(message)
        self.status = status
        self.url = url
        self.body = body


class AuraHttpClient:
    """Thin loopback HTTP/JSON client wrapping daemon and web/studio endpoints."""

    def __init__(
        self,
        daemon_url: str | None = None,
        studio_url: str | None = None,
        root_dir: Path | None = None,
    ) -> None:
        self.daemon_url = _ensure_loopback_base(
            daemon_url or os.environ.get("AURA_DAEMON_URL", "http://127.0.0.1:19897"),
            "AURA_DAEMON_URL",
        )
        self.studio_url = _ensure_loopback_base(
            studio_url or os.environ.get("AURA_STUDIO_URL", "http://127.0.0.1:19898"),
            "AURA_STUDIO_URL",
        )
        default_root = Path(__file__).resolve().parents[2]
        env_root = os.environ.get("AURA_ROOT")
        self.root_dir = Path(env_root).resolve() if env_root else (root_dir or default_root)

    def request(
        self,
        base_url: str,
        method: str,
        path: str,
        payload: Any = None,
        extra_headers: dict[str, str] | None = None,
        timeout: float = 5.0,
    ) -> tuple[int, dict[str, str], Any]:
        url = base_url + path
        headers: dict[str, str] = {"Accept": "application/json"}
        data: bytes | None = None
        if payload is not None:
            data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json"
        if extra_headers:
            headers.update(extra_headers)

        req = urllib.request.Request(url, data=data, headers=headers, method=method.upper())
        try:
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                raw = resp.read().decode("utf-8", errors="replace")
                resp_headers = {k.lower(): v for k, v in resp.headers.items()}
                return resp.status, resp_headers, _parse_json_or_text(raw)
        except urllib.error.HTTPError as exc:
            raw = exc.read().decode("utf-8", errors="replace")
            parsed = _parse_json_or_text(raw)
            msg = parsed.get("message") or parsed.get("error") or parsed.get("last_error") if isinstance(parsed, dict) else raw
            raise AuraHttpError(exc.code, url, f"HTTP {exc.code} from {url}: {msg}", parsed) from exc
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            raise AuraHttpError(None, url, f"Connection failed to {url}: {exc}") from exc

    def daemon_get(self, path: str, timeout: float = 5.0) -> Any:
        _, _, body = self.request(self.daemon_url, "GET", path, timeout=timeout)
        return body

    def daemon_post(self, path: str, payload: Any, timeout: float = 15.0) -> Any:
        _, _, body = self.request(self.daemon_url, "POST", path, payload=payload, timeout=timeout)
        return body

    def daemon_patch(self, path: str, payload: Any, timeout: float = 5.0) -> Any:
        _, _, body = self.request(self.daemon_url, "PATCH", path, payload=payload, timeout=timeout)
        return body

    def daemon_put(self, path: str, payload: Any, timeout: float = 5.0) -> Any:
        _, _, body = self.request(self.daemon_url, "PUT", path, payload=payload, timeout=timeout)
        return body

    def daemon_delete(self, path: str, payload: Any, timeout: float = 5.0) -> Any:
        _, _, body = self.request(self.daemon_url, "DELETE", path, payload=payload, timeout=timeout)
        return body

    def studio_get_raw(self, path: str, timeout: float = 5.0) -> tuple[int, dict[str, str], Any]:
        return self.request(self.studio_url, "GET", path, timeout=timeout)

    def studio_post(
        self,
        path: str,
        payload: Any,
        extra_headers: dict[str, str] | None = None,
        timeout: float = 10.0,
    ) -> Any:
        _, _, body = self.request(
            self.studio_url, "POST", path, payload=payload, extra_headers=extra_headers, timeout=timeout
        )
        return body


def _parse_json_or_text(raw: str) -> Any:
    text = raw.strip()
    if not text:
        return {}
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return {"raw_text": raw}


def _new_hex32() -> str:
    return uuid.uuid4().hex


def _derive_program_id(name: str) -> str:
    return hashlib.sha256(f"aura-mcp:{name}".encode("utf-8")).hexdigest()[:32]


def _normalize_etag(etag: str) -> str:
    cleaned = etag.strip()
    if len(cleaned) >= 2 and cleaned.startswith('"') and cleaned.endswith('"'):
        cleaned = cleaned[1:-1]
    return cleaned


def _safe_probe(fn: Any) -> dict[str, Any]:
    try:
        return {"ok": True, "data": fn()}
    except AuraHttpError as exc:
        return {"ok": False, "status": exc.status, "error": str(exc), "body": exc.body}


class AuraMcpServer:
    """Stateless JSON-RPC dispatcher for MCP stdio transport."""

    def __init__(self, client: AuraHttpClient | None = None) -> None:
        self.client = client or AuraHttpClient()
        self.tools = self._build_tools()
        self.resources = self._build_resources()
        self.prompts = self._build_prompts()

    def _build_tools(self) -> list[dict[str, Any]]:
        return [
            {
                "name": "aura_status",
                "description": "Probe Aura daemon (19897) and Studio web server (19898) health, active plugins, IR programs, global FPS, and magnetic/Hall state.",
                "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
            },
            {
                "name": "aura_list_plugins",
                "description": "List loaded native C++/IR plugins and external bridges (Wooting Analog SDK & Aura MCP). Set rescan=true to hot-scan plugins/.",
                "inputSchema": {
                    "type": "object",
                    "properties": {"rescan": {"type": "boolean", "default": False}},
                    "additionalProperties": False,
                },
            },
            {
                "name": "studio_list_programs",
                "description": "List Studio restricted EffectProgram IR (.aurair) entries and catalog revision from daemon GET /api/programs.",
                "inputSchema": {
                    "type": "object",
                    "properties": {"operation_id": {"type": "string", "description": "Optional 32-hex operation ID to inspect a receipt."}},
                    "additionalProperties": False,
                },
            },
            {
                "name": "studio_publish_ir_program",
                "description": "Install or update an EffectProgram IR v1 JSON lighting program via POST /api/programs and optionally enable it immediately.",
                "inputSchema": {
                    "type": "object",
                    "required": ["name", "program"],
                    "properties": {
                        "name": {"type": "string", "description": "Display name (1-128 chars)."},
                        "program": {"type": "object", "description": "Valid EffectProgram IR v1 JSON (see resource aura://schemas/effect-ir-v1)."},
                        "id": {"type": "string", "description": "Optional 32 lowercase hex program ID; auto-derived from name if omitted."},
                        "version": {"type": "string", "description": "Optional version label (^[A-Za-z0-9][A-Za-z0-9._-]{0,47}$)."},
                        "operation": {"type": "string", "enum": ["install", "update"]},
                        "source": {"type": "object", "description": "Optional Blockly source project metadata {name, json, publication}."},
                        "enable": {"type": "boolean", "default": True},
                        "expected_revision": {"type": "integer", "minimum": 0},
                        "operation_id": {"type": "string"},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "studio_manage_program",
                "description": "Manage an installed IR program lifecycle (enable, disable, uninstall, or rollback) via POST /api/programs.",
                "inputSchema": {
                    "type": "object",
                    "required": ["id", "operation"],
                    "properties": {
                        "id": {"type": "string", "description": "32 lowercase hex program ID."},
                        "operation": {"type": "string", "enum": ["enable", "disable", "uninstall", "rollback"]},
                        "hash": {"type": "string", "description": "64-hex version hash required when operation is 'rollback'."},
                        "expected_revision": {"type": "integer", "minimum": 0},
                        "operation_id": {"type": "string"},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "studio_compile_cpp_effect",
                "description": "Compile native C++ ABI-v1 plugin code into plugins/effect_<name>.dll via Studio POST /api/compile_effect, then rescan daemon plugins.",
                "inputSchema": {
                    "type": "object",
                    "required": ["name", "code"],
                    "properties": {
                        "name": {"type": "string", "description": "Effect identifier (1-64 alphanumeric/underscore chars)."},
                        "code": {"type": "string", "description": "Complete C++ plugin source code."},
                        "rescan": {"type": "boolean", "default": True},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "studio_list_blockly_projects",
                "description": "Read Studio Blockly workspace projects from config via GET /api/config (list names or inspect a specific project).",
                "inputSchema": {
                    "type": "object",
                    "properties": {"project_name": {"type": "string"}},
                    "additionalProperties": False,
                },
            },
            {
                "name": "studio_save_blockly_project",
                "description": "Safely create or update a project under blockly_effects in Studio config using GET /api/config ETag + POST /api/config If-Match.",
                "inputSchema": {
                    "type": "object",
                    "required": ["project_name", "project"],
                    "properties": {
                        "project_name": {"type": "string", "description": "Identifier matching ^[A-Za-z_][A-Za-z0-9_]{0,47}$."},
                        "project": {"type": "object", "description": "Blockly project object stored under config.blockly_effects[project_name]."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "studio_preview_frame",
                "description": "Push a live preview frame (68-element [R,G,B] list or {KEY: [R,G,B]} map) to daemon POST /api/preview.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "colors": {"description": "Either 68 [r,g,b] arrays or object mapping key names (e.g. 'W', 'ESC') to [r,g,b]."},
                        "duration_ms": {"type": "integer", "minimum": 1, "maximum": 10000, "default": 500},
                        "stop": {"type": "boolean", "default": False},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_lighting_get",
                "description": "Inspect Aura global FPS, active base lighting, saved profiles, and available presets/effects.",
                "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
            },
            {
                "name": "aura_lighting_set_base",
                "description": "Update active base lighting preset/effect/brightness/period_ms/parameters and/or global FPS using optimistic revision locks.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "preset": {"type": "string"},
                        "effect": {"type": "string"},
                        "brightness": {"type": "integer", "minimum": 0, "maximum": 255},
                        "period_ms": {"type": "integer", "minimum": 100, "maximum": 60000},
                        "parameters": {"type": "object"},
                        "fps": {"type": "integer", "minimum": 10, "maximum": 100},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_hall_telemetry",
                "description": "Inspect magnetic switch status and 68-key Hall analog snapshot, or start/stop the Hall shared-memory telemetry stream safely.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "action": {"type": "string", "enum": ["snapshot", "start", "stop"], "default": "snapshot"},
                        "unlock_fa": {"type": "boolean", "default": False, "description": "Used only when action is 'start' on unpatched factory firmware."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_hall_verify",
                "description": "Run or inspect closed-loop Hall actuation, Rapid Trigger, or Deadzone verification on a physical M605 key.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "action": {"type": "string", "enum": ["status", "start", "stop"], "default": "status"},
                        "key": {"description": "Key name (e.g. 'W', 'A', 'SPACE') or logical_id when action is 'start'."},
                        "mode": {"type": "string", "enum": ["actuation", "rt", "deadzone"]},
                        "actuation_points": {"type": "array", "items": {"type": "number"}, "description": "Up to 8 target points in mm (0.1..4.0)."},
                        "rt_points": {"type": "array", "items": {"type": "number"}, "description": "Up to 8 RT sensitivities in mm (0.1..2.5)."},
                        "dz_top_mm": {"type": "number", "minimum": 0.0, "maximum": 0.5},
                        "dz_bottom_mm": {"type": "number", "minimum": 0.0, "maximum": 0.5},
                        "dz_rt_mm": {"type": "number", "minimum": 0.1, "maximum": 2.5},
                        "restore_actuation_mm": {"type": "number", "minimum": 0.1, "maximum": 4.0},
                        "unlock_fa": {"type": "boolean"},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_magnetic_set_actuation",
                "description": "Set magnetic switch actuation travel (0.1..4.0 mm, 0.1 mm step) for a single key, a batch of keys, or globally.",
                "inputSchema": {
                    "type": "object",
                    "required": ["scope", "mm"],
                    "properties": {
                        "scope": {"type": "string", "enum": ["single", "batch", "global"]},
                        "mm": {"type": "number", "minimum": 0.1, "maximum": 4.0},
                        "key": {"description": "Target key name or logical_id for scope='single'."},
                        "keys": {"type": "array", "description": "List of key names or logical_ids for scope='batch'."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_magnetic_set_rapid_trigger",
                "description": "Enable or disable per-key Rapid Trigger (0.1..2.5 mm press/release) on a single key or batch (blocked legacy global RT is intentionally excluded).",
                "inputSchema": {
                    "type": "object",
                    "required": ["scope", "action"],
                    "properties": {
                        "scope": {"type": "string", "enum": ["single", "batch"]},
                        "action": {"type": "string", "enum": ["enable", "disable"]},
                        "key": {"description": "Key name or logical_id for scope='single'."},
                        "keys": {"type": "array", "description": "Key names or logical_ids for scope='batch'."},
                        "press_mm": {"type": "number", "minimum": 0.1, "maximum": 2.5},
                        "release_mm": {"type": "number", "minimum": 0.1, "maximum": 2.5},
                        "resolve_dks": {"type": "boolean", "default": False, "description": "Explicitly confirm restoring DKS to Standard before enabling RT."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_magnetic_set_deadzone",
                "description": "Set top/bottom deadzone (0.0..0.5 mm) for single key, batch, global baseline, or reset all per-key overrides.",
                "inputSchema": {
                    "type": "object",
                    "required": ["scope"],
                    "properties": {
                        "scope": {"type": "string", "enum": ["single", "batch", "global", "reset_all"]},
                        "key": {"description": "Key name or logical_id for scope='single'."},
                        "keys": {"type": "array", "description": "Key names or logical_ids for scope='batch'."},
                        "top_mm": {"type": "number", "minimum": 0.0, "maximum": 0.5},
                        "bottom_mm": {"type": "number", "minimum": 0.0, "maximum": 0.5},
                        "confirm_all_keys": {"type": "boolean", "description": "Required true when scope='reset_all'."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_magnetic_set_dks",
                "description": "Configure 4-slot Dynamic Key Stroke (DKS) on a key or restore a key back to Standard configuration.",
                "inputSchema": {
                    "type": "object",
                    "required": ["action", "key"],
                    "properties": {
                        "action": {"type": "string", "enum": ["set", "standard"]},
                        "key": {"description": "Source key name or logical_id."},
                        "start_mm": {"type": "number", "minimum": 0.1, "maximum": 4.0},
                        "end_mm": {"type": "number", "minimum": 0.1, "maximum": 4.0},
                        "slots": {
                            "type": "array",
                            "description": "4 slot objects with {target: {kind: 'DefaultSentinel'|'LogicalKey', key|logical_id}, down_start, down_end, up_start, up_end} ('Inactive'|'Tap'|'Release'|'Hold').",
                        },
                        "resolve_rt": {"type": "boolean", "default": False},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_magnetic_set_speedtap",
                "description": "Manage SpeedTap SOCD pairs, master engine toggle, or reset SpeedTap runtime back to baseline.",
                "inputSchema": {
                    "type": "object",
                    "required": ["action"],
                    "properties": {
                        "action": {"type": "string", "enum": ["pair_enable", "pair_disable", "master", "profile_reset"]},
                        "key1": {"description": "First key name or logical_id for pair_enable/pair_disable."},
                        "key2": {"description": "Second key name or logical_id for pair_enable/pair_disable."},
                        "enabled": {"type": "boolean", "description": "Master switch state when action='master'."},
                        "confirm_profile_baseline": {"type": "boolean", "description": "Required true when action='profile_reset'."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_magnetic_maintenance",
                "description": "Toggle firmware static analog lighting effect or acknowledge persistent magnetic safety quarantine.",
                "inputSchema": {
                    "type": "object",
                    "required": ["action"],
                    "properties": {
                        "action": {"type": "string", "enum": ["static_analog", "acknowledge_quarantine"]},
                        "enabled": {"type": "boolean", "description": "Required when action='static_analog'."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_device_profiles",
                "description": "Inspect, activate, mutate, or refresh hardware slot on magnetic device profiles (/api/device-profiles*).",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "action": {
                            "type": "string",
                            "enum": ["list", "runtime", "get", "activate", "refresh_hardware_slot", "mutate"],
                            "default": "list",
                        },
                        "profile_id": {"type": "string", "description": "Profile GUID for 'get' or 'activate'."},
                        "reason": {"type": "string", "enum": ["Manual", "Automation", "Startup", "Reconnect", "Restore"], "default": "Manual"},
                        "expected_revision": {"type": "integer", "description": "Document revision; auto-fetched when omitted."},
                        "temporary_override": {"type": "object", "description": "Optional temporary magnetic override for host_managed 'activate'."},
                        "mutation": {
                            "type": "string",
                            "enum": ["create", "duplicate", "rename", "update", "delete", "defaults", "automation", "migrate-legacy-rt"],
                        },
                        "payload": {"type": "object", "description": "Body forwarded to /api/device-profiles/<mutation> (expected_revision auto-filled)."},
                    },
                    "additionalProperties": False,
                },
            },
            {
                "name": "aura_automation_rules",
                "description": "List Automation v2 capabilities and configured rules, or validate/create/update/delete/replace rules with optimistic revision checks.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "action": {
                            "type": "string",
                            "enum": ["list", "validate", "create", "update", "delete", "replace"],
                            "default": "list",
                        },
                        "expected_revision": {"type": "string", "description": "Revision hash; auto-fetched from /api/automation/v2/records when omitted."},
                        "id": {"type": "string", "description": "Target rule ID for 'update', 'delete', or patch 'validate'."},
                        "rule": {"type": "object", "description": "Automation v2 rule object for 'create' or 'validate'."},
                        "patch": {"type": "object", "description": "Sparse JSON Merge Patch object for 'update' or 'validate'."},
                        "rules": {"type": "array", "description": "Complete ordered rule array when action is 'replace'."},
                        "position": {"type": "integer", "minimum": 0},
                    },
                    "additionalProperties": False,
                },
            },
        ]

    def _build_resources(self) -> list[dict[str, Any]]:
        return [
            {
                "uri": "aura://schemas/effect-ir-v1",
                "name": "Aura EffectProgram IR v1 Schema & Examples",
                "description": "Instructions, expression ops, limits, and working IR example for studio_publish_ir_program.",
                "mimeType": "text/markdown",
            },
            {
                "uri": "aura://schemas/native-plugin-cpp",
                "name": "Aura Native C++ Plugin ABI + Hall Analog v1 Template",
                "description": "Reference C++ template for studio_compile_cpp_effect with optional Hall Analog depth hook.",
                "mimeType": "text/markdown",
            },
            {
                "uri": "aura://keymap/m605-68",
                "name": "ROG Falchion Ace HFX 68-Key Map & Logical IDs",
                "description": "Calibrated LED IDs, physical coordinates, verified logical_id, and USB HID usages for all 68 keys.",
                "mimeType": "application/json",
            },
            {
                "uri": "aura://overview",
                "name": "Live Aura System Overview",
                "description": "Real-time snapshot of Aura plugins, bridges, IR programs, lighting, and magnetic/Hall status.",
                "mimeType": "application/json",
            },
        ]

    def _build_prompts(self) -> list[dict[str, Any]]:
        return [
            {
                "name": "create_ir_effect",
                "description": "Guide an external AI to author and publish a safe Aura EffectProgram IR v1 effect.",
                "arguments": [
                    {"name": "description", "description": "Desired lighting behavior", "required": False}
                ],
            },
            {
                "name": "create_hall_reactive_cpp_plugin",
                "description": "Guide an external AI to write and compile a native C++ plugin using Hall Analog travel.",
                "arguments": [
                    {"name": "description", "description": "Desired Hall-depth reactive effect", "required": False}
                ],
            },
            {
                "name": "tune_magnetic_profile",
                "description": "Guide an external AI to inspect M605 magnetic state and configure AP/RT/Deadzone/SpeedTap safely.",
                "arguments": [
                    {"name": "game_or_style", "description": "Target game or typing style (e.g. CS2/Valorant FPS, typing)", "required": False}
                ],
            },
        ]

    def handle_message(self, msg: dict[str, Any]) -> dict[str, Any] | None:
        if not isinstance(msg, dict):
            return self._error(None, -32600, "Invalid Request: expected JSON object")

        req_id = msg.get("id")
        method = msg.get("method")
        params = msg.get("params") or {}

        # Notifications have no 'id' and expect no reply.
        if req_id is None:
            return None

        if not isinstance(method, str):
            return self._error(req_id, -32600, "Invalid Request: missing method")

        try:
            if method == "initialize":
                return self._ok(req_id, self._handle_initialize(params))
            if method == "ping":
                return self._ok(req_id, {})
            if method == "tools/list":
                return self._ok(req_id, {"tools": self.tools})
            if method == "tools/call":
                return self._ok(req_id, self._handle_tool_call(params))
            if method == "resources/list":
                return self._ok(req_id, {"resources": self.resources})
            if method == "resources/read":
                return self._ok(req_id, self._handle_resource_read(params))
            if method == "prompts/list":
                return self._ok(req_id, {"prompts": self.prompts})
            if method == "prompts/get":
                return self._ok(req_id, self._handle_prompt_get(params))
            return self._error(req_id, -32601, f"Method not found: {method}")
        except ValueError as exc:
            return self._error(req_id, -32602, str(exc))
        except Exception as exc:  # pragma: no cover - defensive boundary
            return self._error(req_id, -32603, f"Internal error: {exc}")

    def _handle_initialize(self, params: dict[str, Any]) -> dict[str, Any]:
        requested = params.get("protocolVersion") if isinstance(params, dict) else None
        protocol = requested if requested in SUPPORTED_PROTOCOLS else PROTOCOL_DEFAULT
        return {
            "protocolVersion": protocol,
            "capabilities": {
                "tools": {"listChanged": False},
                "resources": {"subscribe": False, "listChanged": False},
                "prompts": {"listChanged": False},
            },
            "serverInfo": SERVER_INFO,
            "instructions": (
                "Aura MCP server bridges Aura daemon (19897) and Studio web/compiler server (19898). "
                "Read aura://schemas/effect-ir-v1 before authoring IR programs with studio_publish_ir_program, "
                "aura://schemas/native-plugin-cpp before calling studio_compile_cpp_effect, and "
                "aura://keymap/m605-68 for key coordinates and magnetic logical IDs."
            ),
        }

    def _handle_resource_read(self, params: dict[str, Any]) -> dict[str, Any]:
        uri = params.get("uri") if isinstance(params, dict) else None
        if not isinstance(uri, str) or not uri:
            raise ValueError("resources/read requires non-empty string 'uri'")

        if uri == "aura://schemas/effect-ir-v1":
            return {"contents": [{"uri": uri, "mimeType": "text/markdown", "text": EFFECT_IR_V1_GUIDE}]}
        if uri == "aura://schemas/native-plugin-cpp":
            return {"contents": [{"uri": uri, "mimeType": "text/markdown", "text": NATIVE_CPP_GUIDE}]}
        if uri == "aura://keymap/m605-68":
            keymap_path = self.client.root_dir / "calibrated_keymap.json"
            if not keymap_path.is_file():
                raise ValueError(f"Keymap file not found: {keymap_path}")
            raw = json.loads(keymap_path.read_text(encoding="utf-8"))
            raw_keys = raw.get("keys") or {}
            compact: dict[str, Any] = {}
            for k, v in raw_keys.items():
                spec = KEY_NAME_TO_SPEC.get(k)
                entry = {
                    "led_id": v.get("led_id"),
                    "physical_x": v.get("physical_x"),
                    "physical_y": v.get("physical_y"),
                    "physical_row": v.get("physical_row"),
                    "physical_col": v.get("physical_col"),
                }
                if spec is not None:
                    entry["logical_id"] = spec[1]
                    entry["logical_id_hex"] = f"0x{spec[1]:04X}"
                    entry["hid_usage"] = spec[2]
                compact[k] = entry
            payload = {"device_profile": raw.get("device_profile", {}), "key_count": len(compact), "keys": compact}
            return {
                "contents": [
                    {
                        "uri": uri,
                        "mimeType": "application/json",
                        "text": json.dumps(payload, ensure_ascii=False, indent=2),
                    }
                ]
            }
        if uri == "aura://overview":
            status = self._collect_status()
            return {
                "contents": [
                    {
                        "uri": uri,
                        "mimeType": "application/json",
                        "text": json.dumps(status, ensure_ascii=False, indent=2),
                    }
                ]
            }
        raise ValueError(f"Unknown resource URI: {uri}")

    def _handle_prompt_get(self, params: dict[str, Any]) -> dict[str, Any]:
        name = params.get("name") if isinstance(params, dict) else None
        args = (params.get("arguments") or {}) if isinstance(params, dict) else {}
        if name == "create_ir_effect":
            desc = str(args.get("description") or "custom lighting effect")
            text = (
                f"Create an Aura EffectProgram IR v1 lighting effect for: {desc}.\n"
                "1. Read `aura://schemas/effect-ir-v1` and `aura://keymap/m605-68`.\n"
                "2. Construct valid EffectProgram IR v1 JSON.\n"
                "3. Publish & enable it via `studio_publish_ir_program`.\n"
                "4. Verify live status via `studio_list_programs` and `aura_list_plugins`."
            )
            return {
                "description": "Author and publish an EffectProgram IR v1 effect",
                "messages": [{"role": "user", "content": {"type": "text", "text": text}}],
            }
        if name == "create_hall_reactive_cpp_plugin":
            desc = str(args.get("description") or "custom Hall reactive lighting effect")
            text = (
                f"Create a native Aura C++ ABI-v1 + Hall Analog v1 lighting plugin for: {desc}.\n"
                "1. Read `aura://schemas/native-plugin-cpp` and `aura://keymap/m605-68`.\n"
                "2. Write clean C++ implementing `AuraGetPluginApiVersion`, `AuraGetEffectName`, "
                "`AuraCreateEffect`, `AuraDestroyEffect`, `AuraGetHallAnalogVersion`, and `AuraBindHallAnalog`.\n"
                "3. Compile & load it via `studio_compile_cpp_effect`.\n"
                "4. Optionally turn on Hall telemetry with `aura_hall_telemetry(action='start')`."
            )
            return {
                "description": "Author and compile a native C++ Hall-reactive Aura plugin",
                "messages": [{"role": "user", "content": {"type": "text", "text": text}}],
            }
        if name == "tune_magnetic_profile":
            style = str(args.get("game_or_style") or "competitive FPS")
            text = (
                f"Tune the ROG Falchion Ace HFX magnetic switch configuration for: {style}.\n"
                "1. Inspect current state via `aura_hall_telemetry(action='snapshot')` and `aura_device_profiles(action='list')`.\n"
                "2. Adjust AP (`aura_magnetic_set_actuation`), RT (`aura_magnetic_set_rapid_trigger`), Deadzone "
                "(`aura_magnetic_set_deadzone`), and/or SpeedTap (`aura_magnetic_set_speedtap`).\n"
                "3. Optionally run a closed-loop Hall verification pass with `aura_hall_verify(action='start', ...)`."
            )
            return {
                "description": "Inspect and tune M605 magnetic switches via MCP",
                "messages": [{"role": "user", "content": {"type": "text", "text": text}}],
            }
        raise ValueError(f"Unknown prompt: {name}")

    def _handle_tool_call(self, params: dict[str, Any]) -> dict[str, Any]:
        if not isinstance(params, dict):
            raise ValueError("tools/call requires object params")
        name = params.get("name")
        args = params.get("arguments") or {}
        if not isinstance(name, str) or not isinstance(args, dict):
            raise ValueError("tools/call requires string 'name' and object 'arguments'")

        dispatch = {
            "aura_status": self._tool_status,
            "aura_list_plugins": self._tool_list_plugins,
            "studio_list_programs": self._tool_list_programs,
            "studio_publish_ir_program": self._tool_publish_ir_program,
            "studio_manage_program": self._tool_manage_program,
            "studio_compile_cpp_effect": self._tool_compile_cpp_effect,
            "studio_list_blockly_projects": self._tool_list_blockly_projects,
            "studio_save_blockly_project": self._tool_save_blockly_project,
            "studio_preview_frame": self._tool_preview_frame,
            "aura_lighting_get": self._tool_lighting_get,
            "aura_lighting_set_base": self._tool_lighting_set_base,
            "aura_hall_telemetry": self._tool_hall_telemetry,
            "aura_hall_verify": self._tool_hall_verify,
            "aura_magnetic_set_actuation": self._tool_magnetic_set_actuation,
            "aura_magnetic_set_rapid_trigger": self._tool_magnetic_set_rapid_trigger,
            "aura_magnetic_set_deadzone": self._tool_magnetic_set_deadzone,
            "aura_magnetic_set_dks": self._tool_magnetic_set_dks,
            "aura_magnetic_set_speedtap": self._tool_magnetic_set_speedtap,
            "aura_magnetic_maintenance": self._tool_magnetic_maintenance,
            "aura_device_profiles": self._tool_device_profiles,
            "aura_automation_rules": self._tool_automation_rules,
        }
        handler = dispatch.get(name)
        if handler is None:
            return self._tool_result({"error": f"Unknown tool: {name}"}, is_error=True)

        try:
            payload = handler(args)
            return self._tool_result(payload, is_error=False)
        except (AuraHttpError, ValueError) as exc:
            err: dict[str, Any] = {"error": str(exc)}
            if isinstance(exc, AuraHttpError) and exc.body is not None:
                err["details"] = exc.body
            return self._tool_result(err, is_error=True)

    @staticmethod
    def _tool_result(payload: Any, is_error: bool) -> dict[str, Any]:
        text = json.dumps(payload, ensure_ascii=False, indent=2) if not isinstance(payload, str) else payload
        return {"content": [{"type": "text", "text": text}], "isError": is_error}

    @staticmethod
    def _ok(req_id: Any, result: Any) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": req_id, "result": result}

    @staticmethod
    def _error(req_id: Any, code: int, message: str) -> dict[str, Any]:
        return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}

    # ------------------------------------------------------------------
    # Tool implementations
    # ------------------------------------------------------------------
    def _collect_status(self) -> dict[str, Any]:
        global_probe = _safe_probe(lambda: self.client.daemon_get("/api/lighting/global"))
        plugins_probe = _safe_probe(lambda: self.client.daemon_get("/api/plugins/overview"))
        programs_probe = _safe_probe(lambda: self.client.daemon_get("/api/programs"))
        magnetic_probe = _safe_probe(lambda: self.client.daemon_get("/api/magnetic/status"))
        studio_probe = _safe_probe(lambda: self.client.studio_get_raw("/api/config")[2])

        return {
            "endpoints": {"daemon_url": self.client.daemon_url, "studio_url": self.client.studio_url},
            "daemon_reachable": global_probe["ok"] or plugins_probe["ok"] or programs_probe["ok"],
            "studio_reachable": studio_probe["ok"],
            "global_lighting": global_probe.get("data") if global_probe["ok"] else global_probe,
            "plugins_overview": plugins_probe.get("data") if plugins_probe["ok"] else plugins_probe,
            "programs_summary": (
                {
                    "revision": programs_probe["data"].get("revision"),
                    "count": len(programs_probe["data"].get("entries") or []),
                    "entries": [
                        {
                            "id": e.get("id"),
                            "name": e.get("name"),
                            "plugin_name": e.get("plugin_name"),
                            "enabled": e.get("enabled"),
                            "loaded": e.get("loaded"),
                        }
                        for e in (programs_probe["data"].get("entries") or [])
                    ],
                }
                if programs_probe["ok"]
                else programs_probe
            ),
            "magnetic_status": magnetic_probe.get("data") if magnetic_probe["ok"] else magnetic_probe,
        }

    def _tool_status(self, _args: dict[str, Any]) -> Any:
        return self._collect_status()

    def _tool_list_plugins(self, args: dict[str, Any]) -> Any:
        if args.get("rescan"):
            return self.client.daemon_post("/api/plugins/rescan", {})
        return self.client.daemon_get("/api/plugins/overview")

    def _tool_list_programs(self, args: dict[str, Any]) -> Any:
        op_id = args.get("operation_id")
        if op_id is not None:
            if not isinstance(op_id, str) or not HEX32_RE.match(op_id):
                raise ValueError("operation_id must be 32 lowercase hex characters")
            return self.client.daemon_get(f"/api/programs?operation_id={op_id}")
        return self.client.daemon_get("/api/programs")

    def _tool_publish_ir_program(self, args: dict[str, Any]) -> Any:
        name = args.get("name")
        program = args.get("program")
        if not isinstance(name, str) or not (1 <= len(name) <= 128):
            raise ValueError("'name' must be a string between 1 and 128 characters")
        if not isinstance(program, dict):
            raise ValueError("'program' must be an EffectProgram IR JSON object")

        prog_id = args.get("id") or _derive_program_id(name)
        if not isinstance(prog_id, str) or not HEX32_RE.match(prog_id):
            raise ValueError("'id' must be 32 lowercase hex characters")

        catalog = self.client.daemon_get("/api/programs")
        current_rev = catalog.get("revision", 0)
        entries = catalog.get("entries") or []
        existing = next((e for e in entries if e.get("id") == prog_id), None)

        operation = args.get("operation")
        if operation is None:
            operation = "update" if existing is not None else "install"
        elif operation not in ("install", "update"):
            raise ValueError("'operation' must be 'install' or 'update'")

        expected_revision = args.get("expected_revision")
        if expected_revision is None:
            expected_revision = current_rev

        version = args.get("version")
        if not version:
            if operation == "install":
                version = "1.0.0"
            else:
                known_versions = {v.get("version") for v in (existing or {}).get("versions", [])}
                candidate = f"1.0.{int(expected_revision) + 1}"
                while candidate in known_versions:
                    candidate += ".1"
                version = candidate

        op_id = args.get("operation_id") or _new_hex32()
        if not isinstance(op_id, str) or not HEX32_RE.match(op_id):
            raise ValueError("'operation_id' must be 32 lowercase hex characters")

        req_body: dict[str, Any] = {
            "operation": operation,
            "operation_id": op_id,
            "expected_revision": expected_revision,
            "id": prog_id,
            "name": name,
            "version": version,
            "program": program,
        }
        if args.get("source") is not None:
            req_body["source"] = args["source"]

        commit_res = self.client.daemon_post("/api/programs", req_body)
        want_enable = args.get("enable", True)
        if want_enable and not commit_res.get("enabled"):
            enable_req = {
                "operation": "enable",
                "operation_id": _new_hex32(),
                "expected_revision": commit_res["revision"],
                "id": prog_id,
            }
            enable_res = self.client.daemon_post("/api/programs", enable_req)
            return {"install_or_update": commit_res, "enable": enable_res, "plugin_name": enable_res.get("plugin_name")}
        return commit_res

    def _tool_manage_program(self, args: dict[str, Any]) -> Any:
        prog_id = args.get("id")
        operation = args.get("operation")
        if not isinstance(prog_id, str) or not HEX32_RE.match(prog_id):
            raise ValueError("'id' must be 32 lowercase hex characters")
        if operation not in ("enable", "disable", "uninstall", "rollback"):
            raise ValueError("'operation' must be one of enable, disable, uninstall, rollback")

        expected_revision = args.get("expected_revision")
        if expected_revision is None:
            expected_revision = self.client.daemon_get("/api/programs").get("revision", 0)

        req_body: dict[str, Any] = {
            "operation": operation,
            "operation_id": args.get("operation_id") or _new_hex32(),
            "expected_revision": expected_revision,
            "id": prog_id,
        }
        if operation == "rollback":
            version_hash = args.get("hash")
            if not isinstance(version_hash, str) or len(version_hash) != 64:
                raise ValueError("'hash' (64 lowercase hex) is required for rollback")
            req_body["hash"] = version_hash

        return self.client.daemon_post("/api/programs", req_body)

    def _tool_compile_cpp_effect(self, args: dict[str, Any]) -> Any:
        name = args.get("name")
        code = args.get("code")
        if not isinstance(name, str) or not CPP_NAME_RE.match(name):
            raise ValueError("'name' must match ^[A-Za-z0-9_]{1,64}$")
        if not isinstance(code, str) or not code.strip():
            raise ValueError("'code' must be non-empty C++ source")

        compile_res = self.client.studio_post("/api/compile_effect", {"name": name, "code": code}, timeout=60.0)
        rescan_res = None
        if args.get("rescan", True) and isinstance(compile_res, dict) and compile_res.get("success"):
            rescan_res = _safe_probe(lambda: self.client.daemon_post("/api/plugins/rescan", {}))
        return {"compile": compile_res, "rescan": rescan_res}

    def _tool_list_blockly_projects(self, args: dict[str, Any]) -> Any:
        _, headers, config = self.client.studio_get_raw("/api/config")
        if not isinstance(config, dict):
            raise ValueError("Unexpected /api/config response")
        effects = config.get("blockly_effects") or {}
        etag = _normalize_etag(headers.get("etag", ""))
        project_name = args.get("project_name")
        if project_name is not None:
            if not isinstance(project_name, str) or project_name not in effects:
                raise ValueError(f"Blockly project not found: {project_name}")
            return {"etag": etag, "project_name": project_name, "project": effects[project_name]}
        return {"etag": etag, "project_names": sorted(effects.keys())}

    def _tool_save_blockly_project(self, args: dict[str, Any]) -> Any:
        project_name = args.get("project_name")
        project = args.get("project")
        if not isinstance(project_name, str) or not PROJECT_NAME_RE.match(project_name):
            raise ValueError("'project_name' must match ^[A-Za-z_][A-Za-z0-9_]{0,47}$")
        if not isinstance(project, dict):
            raise ValueError("'project' must be a JSON object")

        _, headers, config = self.client.studio_get_raw("/api/config")
        if not isinstance(config, dict):
            raise ValueError("Unexpected /api/config response")
        etag = _normalize_etag(headers.get("etag", ""))
        if not etag:
            raise ValueError("Missing ETag header from GET /api/config")

        updated = dict(config)
        effects = dict(updated.get("blockly_effects") or {})
        effects[project_name] = project
        updated["blockly_effects"] = effects

        save_res = self.client.studio_post("/api/config", updated, extra_headers={"If-Match": f'"{etag}"'})
        return {"project_name": project_name, "result": save_res}

    def _tool_preview_frame(self, args: dict[str, Any]) -> Any:
        if args.get("stop"):
            payload = {"colors": {}, "duration_ms": 1}
        else:
            colors = args.get("colors")
            if colors is None or (not isinstance(colors, (list, dict))):
                raise ValueError("'colors' (68-element [r,g,b] array or {KEY: [r,g,b]} object) is required unless stop=true")
            duration_ms = int(args.get("duration_ms", 500))
            payload = {"colors": colors, "duration_ms": max(1, min(duration_ms, 10000))}
        return self.client.daemon_post("/api/preview", payload)

    def _tool_lighting_get(self, _args: dict[str, Any]) -> Any:
        return {
            "global": self.client.daemon_get("/api/lighting/global"),
            "base": self.client.daemon_get("/api/lighting/base"),
            "profiles": self.client.daemon_get("/api/lighting/profiles"),
            "effects": self.client.daemon_get("/api/lighting/effects"),
        }

    def _tool_lighting_set_base(self, args: dict[str, Any]) -> Any:
        results: dict[str, Any] = {}
        if "fps" in args:
            current_global = self.client.daemon_get("/api/lighting/global")
            fps_rev = current_global.get("revision", "")
            results["global_fps"] = self.client.daemon_patch(
                "/api/lighting/global",
                {"expected_revision": fps_rev, "fps": int(args["fps"])},
            )

        base_keys = ("preset", "effect", "brightness", "period_ms", "parameters")
        patch_fields = {k: args[k] for k in base_keys if k in args}
        if patch_fields:
            current_base = self.client.daemon_get("/api/lighting/base")
            patch_fields["expected_revision"] = current_base.get("revision", "")
            results["base_lighting"] = self.client.daemon_patch("/api/lighting/base", patch_fields)

        if not results:
            raise ValueError("Provide at least one field to update (preset, effect, brightness, period_ms, parameters, fps)")
        return results

    def _tool_hall_telemetry(self, args: dict[str, Any]) -> Any:
        action = args.get("action", "snapshot")
        if action == "start":
            body: dict[str, Any] = {}
            if "unlock_fa" in args:
                body["unlock_fa"] = bool(args["unlock_fa"])
            return self.client.daemon_post("/api/magnetic/hall/start", body)
        if action == "stop":
            return self.client.daemon_post("/api/magnetic/hall/stop", {})
        if action == "snapshot":
            return {
                "magnetic_status": _safe_probe(lambda: self.client.daemon_get("/api/magnetic/status")),
                "hall_snapshot": self.client.daemon_get("/api/magnetic/hall/snapshot"),
            }
        raise ValueError("'action' must be 'snapshot', 'start', or 'stop'")

    def _tool_hall_verify(self, args: dict[str, Any]) -> Any:
        action = args.get("action", "status")
        if action == "status":
            return self.client.daemon_get("/api/magnetic/hall/verify/status")
        if action == "stop":
            return self.client.daemon_post("/api/magnetic/hall/verify/stop", {})
        if action == "start":
            if "key" not in args:
                raise ValueError("'key' is required when action is 'start'")
            mode = args.get("mode")
            if mode not in ("actuation", "rt", "deadzone"):
                raise ValueError("'mode' must be 'actuation', 'rt', or 'deadzone'")
            body: dict[str, Any] = {
                "logical_id": resolve_logical_id(args["key"]),
                "mode": mode,
            }
            for opt in (
                "actuation_points",
                "rt_points",
                "dz_top_mm",
                "dz_bottom_mm",
                "dz_rt_mm",
                "restore_actuation_mm",
                "unlock_fa",
            ):
                if opt in args:
                    body[opt] = args[opt]
            return self.client.daemon_post("/api/magnetic/hall/verify/start", body)
        raise ValueError("'action' must be 'status', 'start', or 'stop'")

    def _tool_magnetic_set_actuation(self, args: dict[str, Any]) -> Any:
        scope = args.get("scope")
        if "mm" not in args:
            raise ValueError("'mm' is required")
        mm = float(args["mm"])
        if scope == "single":
            if "key" not in args:
                raise ValueError("'key' is required for scope='single'")
            return self.client.daemon_post("/api/magnetic/actuation", {"logical_id": resolve_logical_id(args["key"]), "mm": mm})
        if scope == "batch":
            ids = _resolve_logical_ids(args.get("keys"))
            return self.client.daemon_post("/api/magnetic/batch/actuation", {"logical_ids": ids, "millimeters": mm})
        if scope == "global":
            return self.client.daemon_post("/api/magnetic/global/actuation", {"mm": mm})
        raise ValueError("'scope' must be 'single', 'batch', or 'global'")

    def _tool_magnetic_set_rapid_trigger(self, args: dict[str, Any]) -> Any:
        scope = args.get("scope")
        action = args.get("action")
        if scope not in ("single", "batch"):
            raise ValueError("'scope' must be 'single' or 'batch'")
        if action not in ("enable", "disable"):
            raise ValueError("'action' must be 'enable' or 'disable'")

        if action == "enable":
            if "press_mm" not in args or "release_mm" not in args:
                raise ValueError("'press_mm' and 'release_mm' are required when action='enable'")
            press_mm = float(args["press_mm"])
            release_mm = float(args["release_mm"])
            resolve_dks = bool(args.get("resolve_dks", False))
            if scope == "single":
                if "key" not in args:
                    raise ValueError("'key' is required for scope='single'")
                return self.client.daemon_post(
                    "/api/magnetic/rapid-trigger",
                    {
                        "logical_id": resolve_logical_id(args["key"]),
                        "press_mm": press_mm,
                        "release_mm": release_mm,
                        "resolve_dks": resolve_dks,
                    },
                )
            return self.client.daemon_post(
                "/api/magnetic/batch/rapid-trigger",
                {
                    "logical_ids": _resolve_logical_ids(args.get("keys")),
                    "action": "enable",
                    "press_mm": press_mm,
                    "release_mm": release_mm,
                    "resolve_dks": resolve_dks,
                },
            )

        if scope == "single":
            if "key" not in args:
                raise ValueError("'key' is required for scope='single'")
            return self.client.daemon_post(
                "/api/magnetic/rapid-trigger/disable",
                {"logical_id": resolve_logical_id(args["key"])},
            )
        return self.client.daemon_post(
            "/api/magnetic/batch/rapid-trigger",
            {"logical_ids": _resolve_logical_ids(args.get("keys")), "action": "disable"},
        )

    def _tool_magnetic_set_deadzone(self, args: dict[str, Any]) -> Any:
        scope = args.get("scope")
        if scope == "reset_all":
            if args.get("confirm_all_keys") is not True:
                raise ValueError("scope='reset_all' requires confirm_all_keys=true")
            return self.client.daemon_post("/api/magnetic/deadzone/reset-all", {"confirm_all_keys": True})
        if "top_mm" not in args or "bottom_mm" not in args:
            raise ValueError("'top_mm' and 'bottom_mm' are required")
        top_mm = float(args["top_mm"])
        bottom_mm = float(args["bottom_mm"])
        if scope == "single":
            if "key" not in args:
                raise ValueError("'key' is required for scope='single'")
            return self.client.daemon_post(
                "/api/magnetic/deadzone",
                {"logical_id": resolve_logical_id(args["key"]), "top_mm": top_mm, "bottom_mm": bottom_mm},
            )
        if scope == "batch":
            return self.client.daemon_post(
                "/api/magnetic/batch/deadzone",
                {"logical_ids": _resolve_logical_ids(args.get("keys")), "top_mm": top_mm, "bottom_mm": bottom_mm},
            )
        if scope == "global":
            return self.client.daemon_post("/api/magnetic/global/deadzone", {"top_mm": top_mm, "bottom_mm": bottom_mm})
        raise ValueError("'scope' must be 'single', 'batch', 'global', or 'reset_all'")

    def _tool_magnetic_set_dks(self, args: dict[str, Any]) -> Any:
        action = args.get("action")
        if "key" not in args:
            raise ValueError("'key' is required")
        source_lid = resolve_logical_id(args["key"])
        if action == "standard":
            return self.client.daemon_post("/api/magnetic/dks/standard", {"logical_id": source_lid})
        if action != "set":
            raise ValueError("'action' must be 'set' or 'standard'")

        if "start_mm" not in args or "end_mm" not in args:
            raise ValueError("'start_mm' and 'end_mm' are required when action='set'")
        slots = args.get("slots")
        if not isinstance(slots, list) or len(slots) != 4:
            raise ValueError("'slots' must be an array of 4 slot definitions")

        normalized_slots: list[dict[str, Any]] = []
        for idx, slot in enumerate(slots):
            if not isinstance(slot, dict):
                raise ValueError(f"Slot {idx} must be an object")
            target = slot.get("target")
            if not isinstance(target, dict):
                raise ValueError(f"Slot {idx} missing object 'target'")
            kind = target.get("kind")
            if kind == "DefaultSentinel":
                norm_target: dict[str, Any] = {"kind": "DefaultSentinel"}
            elif kind == "LogicalKey":
                raw_target_key = target.get("key") if "key" in target else target.get("logical_id")
                if raw_target_key is None:
                    raise ValueError(f"Slot {idx} LogicalKey target requires 'key' or 'logical_id'")
                target_lid = resolve_logical_id(raw_target_key)
                if target_lid == FN_LOGICAL_ID:
                    raise ValueError(f"Slot {idx} cannot bind FN (0x0508) as a DKS action target")
                norm_target = {"kind": "LogicalKey", "logical_id": target_lid}
            else:
                raise ValueError(f"Slot {idx} target.kind must be 'DefaultSentinel' or 'LogicalKey'")

            norm_slot = {
                "target": norm_target,
                "down_start": slot.get("down_start"),
                "down_end": slot.get("down_end"),
                "up_start": slot.get("up_start"),
                "up_end": slot.get("up_end"),
            }
            normalized_slots.append(norm_slot)

        payload = {
            "logical_id": source_lid,
            "start_mm": float(args["start_mm"]),
            "end_mm": float(args["end_mm"]),
            "slots": normalized_slots,
            "resolve_rt": bool(args.get("resolve_rt", False)),
        }
        return self.client.daemon_post("/api/magnetic/dks", payload)

    def _tool_magnetic_set_speedtap(self, args: dict[str, Any]) -> Any:
        action = args.get("action")
        if action in ("pair_enable", "pair_disable"):
            if "key1" not in args or "key2" not in args:
                raise ValueError("'key1' and 'key2' are required")
            k1 = resolve_logical_id(args["key1"])
            k2 = resolve_logical_id(args["key2"])
            route = "/api/magnetic/speedtap/pair" if action == "pair_enable" else "/api/magnetic/speedtap/pair/disable"
            return self.client.daemon_post(route, {"key1": k1, "key2": k2})
        if action == "master":
            if "enabled" not in args or not isinstance(args["enabled"], bool):
                raise ValueError("Boolean 'enabled' is required when action='master'")
            return self.client.daemon_post("/api/magnetic/speedtap/master", {"enabled": args["enabled"]})
        if action == "profile_reset":
            if args.get("confirm_profile_baseline") is not True:
                raise ValueError("action='profile_reset' requires confirm_profile_baseline=true")
            return self.client.daemon_post("/api/magnetic/speedtap/profile-reset", {"confirm_profile_baseline": True})
        raise ValueError("'action' must be 'pair_enable', 'pair_disable', 'master', or 'profile_reset'")

    def _tool_magnetic_maintenance(self, args: dict[str, Any]) -> Any:
        action = args.get("action")
        if action == "static_analog":
            if "enabled" not in args or not isinstance(args["enabled"], bool):
                raise ValueError("Boolean 'enabled' is required when action='static_analog'")
            return self.client.daemon_post("/api/magnetic/analog-effect/static", {"enabled": args["enabled"]})
        if action == "acknowledge_quarantine":
            return self.client.daemon_post("/api/magnetic/safety/acknowledge-external-resynchronization", {})
        raise ValueError("'action' must be 'static_analog' or 'acknowledge_quarantine'")

    def _tool_device_profiles(self, args: dict[str, Any]) -> Any:
        action = args.get("action", "list")
        if action == "list":
            return self.client.daemon_get("/api/device-profiles")
        if action == "runtime":
            return self.client.daemon_get("/api/device-profiles/runtime")
        if action == "get":
            pid = args.get("profile_id")
            if not isinstance(pid, str) or not pid.strip():
                raise ValueError("'profile_id' is required when action='get'")
            return self.client.daemon_get(f"/api/device-profiles/{pid.strip()}")
        if action == "refresh_hardware_slot":
            return self.client.daemon_post("/api/device-profiles/hardware-slot/refresh", {}, timeout=10.0)
        if action == "activate":
            pid = args.get("profile_id")
            if not isinstance(pid, str) or not pid.strip():
                raise ValueError("'profile_id' is required when action='activate'")
            rev = args.get("expected_revision")
            if rev is None:
                rev = self.client.daemon_get("/api/device-profiles").get("document_revision")
            reason = args.get("reason", "Manual")
            body: dict[str, Any] = {
                "profile_id": pid.strip(),
                "reason": reason,
                "expected_revision": int(rev),
            }
            if args.get("temporary_override") is not None:
                body["temporary_override"] = args["temporary_override"]
            return self.client.daemon_post("/api/device-profiles/activate", body, timeout=30.0)
        if action == "mutate":
            mutation = args.get("mutation")
            if mutation not in (
                "create", "duplicate", "rename", "update", "delete", "defaults", "automation", "migrate-legacy-rt"
            ):
                raise ValueError("'mutation' must be one of create, duplicate, rename, update, delete, defaults, automation, migrate-legacy-rt")
            body = dict(args.get("payload") or {})
            if "expected_revision" not in body:
                rev = args.get("expected_revision")
                if rev is None:
                    rev = self.client.daemon_get("/api/device-profiles").get("document_revision")
                body["expected_revision"] = int(rev)
            return self.client.daemon_post(f"/api/device-profiles/{mutation}", body, timeout=15.0)
        raise ValueError("'action' must be one of list, runtime, get, activate, refresh_hardware_slot, mutate")

    def _tool_automation_rules(self, args: dict[str, Any]) -> Any:
        action = args.get("action", "list")
        if action == "list":
            return {
                "capabilities": self.client.daemon_get("/api/automation/v2/capabilities"),
                "records": self.client.daemon_get("/api/automation/v2/records"),
            }

        expected_rev = args.get("expected_revision")
        if expected_rev is None:
            expected_rev = self.client.daemon_get("/api/automation/v2/records").get("revision", "")

        body: dict[str, Any] = {"expected_revision": expected_rev}
        if "id" in args:
            body["id"] = args["id"]
        if "position" in args:
            body["position"] = int(args["position"])

        if action == "validate":
            if "patch" in args:
                body["patch"] = args["patch"]
            else:
                rule = args.get("rule")
                if not isinstance(rule, dict):
                    raise ValueError("'rule' or 'patch' object is required when action is 'validate'")
                body["rule"] = rule
            return self.client.daemon_post("/api/automation/v2/validate", body)
        if action == "create":
            rule = args.get("rule")
            if not isinstance(rule, dict):
                raise ValueError("'rule' object is required when action is 'create'")
            body["rule"] = rule
            return self.client.daemon_post("/api/automation/v2/rules", body)
        if action == "update":
            if "id" not in args or not isinstance(args.get("patch"), dict):
                raise ValueError("'id' and 'patch' are required when action is 'update'")
            body["patch"] = args["patch"]
            return self.client.daemon_patch("/api/automation/v2/rules", body)
        if action == "delete":
            if "id" not in args:
                raise ValueError("'id' is required when action is 'delete'")
            return self.client.daemon_delete("/api/automation/v2/rules", body)
        if action == "replace":
            rules = args.get("rules")
            if not isinstance(rules, list):
                raise ValueError("'rules' array is required when action is 'replace'")
            body["rules"] = rules
            return self.client.daemon_put("/api/automation/v2/rules", body)
        raise ValueError("'action' must be one of list, validate, create, update, delete, replace")


def serve_stdio(server: AuraMcpServer | None = None) -> int:
    srv = server or AuraMcpServer()
    stdin_buf = sys.stdin.buffer
    stdout_buf = sys.stdout.buffer

    while True:
        raw_line = stdin_buf.readline()
        if not raw_line:
            break
        line = raw_line.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError as exc:
            resp = {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": f"Parse error: {exc}"}}
        else:
            resp = srv.handle_message(msg)

        if resp is not None:
            encoded = (json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8")
            stdout_buf.write(encoded)
            stdout_buf.flush()
    return 0


if __name__ == "__main__":
    raise SystemExit(serve_stdio())
