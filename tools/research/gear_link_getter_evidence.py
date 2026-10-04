"""Offline evidence checks only. Never opens HID or invokes a browser getter.

An official SDK return is not device readback unless a matching completed
64-byte USB response exists. Some official queues resolve a timeout sentinel.
"""
from datetime import datetime


def _stamp(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def validate_device_response(payload, opcode):
    """A synthetic queue fallback or unrelated packet cannot authorize decoding."""
    data = bytes(payload)
    if data[:2] == b"\xff\xaa":
        return False
    return len(data) == 64 and data[:2] == bytes(opcode)


def classify_getter(timeline, sdk_case, opcode):
    start, end = _stamp(sdk_case["query_start"]), _stamp(sdk_case["end"])
    window = [r for r in timeline if start <= _stamp(r["timestamp_utc"]) <= end
              and r.get("interface") == 1 and r.get("status") == 0]
    # Millisecond SDK receipt stamps may truncate a sub-millisecond final IN.
    # This check is specifically about query responses, not timing attribution
    # of the final BasicInfo. It does not establish decoded field semantics.
    requests = [r for r in window if r["direction"] == "OUT"
                and bytes.fromhex(r["payload_hex"])[:2] == bytes(opcode)]
    responses = [r for r in window if r["direction"] == "IN" and r.get("completed")
                 and validate_device_response(bytes.fromhex(r["payload_hex"]), opcode)
                 and any(q["frame"] < r["frame"] for q in requests)]
    authority = all(len(sdk_case.get(name, [])) == 64
                    and sdk_case[name][:2] == [0x12, 0]
                    and sdk_case[name][10] == 5 for name in ("pre", "post"))
    safety = safety_findings(timeline, case_start=sdk_case.get("start", sdk_case["query_start"]),
                             case_end=sdk_case["end"])
    outcome = ("READ_ONLY_GATE_FAIL" if safety["stop_required"] else
               "INVALID_AUTHORITY" if not authority else
               "NOT_TRANSMITTED" if not requests else
               "NO_MATCHING_RESPONSE" if not responses else "RESPONSE_CAPTURED")
    return {"schema_version": 1, "authority_passed": authority,
            "outcome": outcome, "request_frames": [r["frame"] for r in requests],
            "response_frames": [r["frame"] for r in responses],
            "sdk_decoded": sdk_case.get("result"),
            "readback_valid": authority and bool(responses) and not safety["stop_required"],
            "read_only_gate": safety,
            "field_meaning_verified": False}


def safety_findings(timeline, authorized_selection_frames=(), *, case_start=None, case_end=None):
    """Profile UI selection authorization does not authorize companion setters."""
    authorized = set(authorized_selection_frames)
    setters, selectors, apply_gates = [], [], []
    for row in timeline:
        if case_start is not None and _stamp(row["timestamp_utc"]) < _stamp(case_start):
            continue
        if case_end is not None and _stamp(row["timestamp_utc"]) > _stamp(case_end):
            continue
        if row.get("interface") != 1 or row.get("status") != 0 or row["direction"] != "OUT":
            continue
        payload = bytes.fromhex(row["payload_hex"])
        if payload[:2] == b"\x51\x00":
            if row["frame"] not in authorized:
                selectors.append(row["frame"])
        elif payload[:1] == b"\x51":
            setters.append(row["frame"])
        if payload[:2] == b"\x50\x55":
            apply_gates.append(row["frame"])
    return {"setter_frames": setters, "unauthorized_selection_frames": selectors,
            "apply_frames": apply_gates,
            "stop_required": bool(setters or selectors or apply_gates)}
