"""Control the owned passive capture worker and persist resumable audit cases.

Never connects to HID, invokes official builders or automates browser controls.
UI actions must be performed separately with Computer Use and current evidence.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys
import time
from gear_link_reference import analyze, records
from gear_link_case_scope import inspect_scope


def stamp():
    return datetime.now(timezone.utc).isoformat()


def read(path):
    # Windows Move-Item can briefly hide the target during status replacement.
    # Retry that absence only; malformed JSON and other errors stay fail-closed.
    for attempt in range(3):
        try:
            return json.loads(path.read_text(encoding="utf-8-sig"))
        except FileNotFoundError:
            if attempt == 2:
                raise
            time.sleep(.025)


def write(path, value):
    temp=path.with_suffix(path.suffix+".tmp")
    temp.write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    temp.replace(path)


def wait_until(check, timeout=15):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        if check(): return
        time.sleep(.1)
    raise TimeoutError("Owned capture did not reach expected state; stop this case")


def command(root, action, case):
    value={"id":action+"-"+stamp(),"action":action}
    if action=="start": value.update(case_name=case,pipe_name="aura_audit_"+case)
    write(root/"state/capture-command.json",value)


def begin(root, case, expected):
    if not re.fullmatch(r"[a-z0-9_]{1,60}",case): raise ValueError("Invalid case name")
    state=read(root/"audit_session_state.json")
    ledger=read(root/"slot_ledger.json")
    if state.get("phase") in ("BLOCKED_SCOPE","BLOCKED_EXTERNAL_WRITER","PRECHECK_FAILED"):
        raise ValueError("Audit safety review required; no new case until scope/precheck is resolved")
    if state.get("test_slot")!=5 or not ledger["slots"]["5"]["writable"]:
        raise ValueError("No authorized single test slot")
    if state.get("current_capture") or read(root/"state/capture-worker.json")["state"]!="ready":
        raise ValueError("Worker is not idle; will not start another capture")
    capture=root/"usb"/(case+".pcap")
    case_dir=root/"state"/case
    if capture.exists() or case_dir.exists(): raise FileExistsError("Case exists; resume or use a new name")
    case_dir.mkdir()
    log=root/"logs"/(case+"-collector.log")
    tool=Path(__file__).with_name("gear_link_capture_privacy.py")
    with log.open("wb") as out:
        collector=subprocess.Popen([sys.executable,"-u",str(tool),str(capture),"--pipe",r"\\.\pipe\aura_audit_"+case],
                                   stdout=out,stderr=subprocess.STDOUT,
                                   creationflags=getattr(subprocess,"CREATE_NO_WINDOW",0))
    issued_start=False
    try:
        wait_until(lambda:log.exists() and "READY" in log.read_text(encoding="utf8",errors="replace"))
        command(root,"start",case)
        issued_start=True
        wait_until(lambda:capture.exists() and capture.stat().st_size>24)
        _,rows=records(capture.read_bytes())
        descriptors=[{"bus":r["bus"],"usb_address":r["device"],"vid_le":r["payload"][8:10].hex(),
                      "pid_le":r["payload"][10:12].hex(),"bcd_device_le":r["payload"][12:14].hex()}
                     for r in rows if r["transfer"]==2 and len(r["payload"])==18]
        if not descriptors: raise ValueError("No currently identified HFX descriptor; no UI action permitted")
    except Exception as error:
        # Stop only through the worker's executable/pipe ownership check.
        # Preserve the failed case and acquisition evidence, never overwrite it.
        cleanup="no start issued"
        if issued_start:
            try:
                command(root,"stop",case)
                wait_until(lambda:read(root/"state/capture-worker.json")["state"]=="ready")
                cleanup="owned capture stopped"
            except Exception:
                cleanup="stop unconfirmed; inspect worker before resuming"
        elif collector.poll() is None:
            # This returned child is only our local pipe collector, not HID or
            # another user's capture. Without a producer it cannot reach EOF.
            try:
                collector.terminate()
                collector.wait(timeout=5)
                cleanup="owned waiting collector stopped; no capture start issued"
            except Exception:
                cleanup="waiting collector stop unconfirmed; inspect before resuming"
        value={"schema_version":1,"case":case,"phase":"PRECHECK_FAILED",
               "classification":"NOT VERIFIED", "error":str(error),"cleanup":cleanup,
               "evidence_refs":["logs/"+log.name,"usb/"+capture.name]}
        write(case_dir/"case.json",value)
        state.update(phase="PRECHECK_FAILED",reason=str(error),next_action=cleanup,
                     current_case=case,current_capture="usb/"+capture.name if issued_start else None,
                     updated_at_utc=stamp())
        write(root/"audit_session_state.json",state)
        raise
    write(case_dir/"case.json",{"schema_version":1,"case":case,"phase":"UI_ACTION",
          "started_at_utc":stamp(),"test_slot":5,"initial_profile":"配置文件 5",
          "expected":expected,"device_descriptors":descriptors,"evidence_refs":["usb/"+capture.name],
          "actions":[],"classification":"pending"})
    state.update(phase="UI_ACTION",current_capture="usb/"+capture.name,current_case=case,
                 next_action=expected,updated_at_utc=stamp())
    write(root/"audit_session_state.json",state)
    return {"capture_started":case,"descriptor_count":len(descriptors)}


def finish(root, case, observation):
    state=read(root/"audit_session_state.json")
    if state.get("current_case")!=case: raise ValueError("Active case mismatch")
    capture=root/"usb"/(case+".pcap")
    command(root,"stop",case)
    wait_until(lambda:capture.with_suffix(".privacy.json").is_file())
    wait_until(lambda:read(root/"state/capture-worker.json")["state"]=="ready")
    privacy=read(capture.with_suffix(".privacy.json"))
    if privacy["outcome"]!="complete": raise ValueError("Incomplete capture; do not classify PASS")
    output=root/"parsed"/case
    result=analyze(capture,output)
    scope=inspect_scope(read(output/"timeline.json"),state["test_slot"])
    write(output/"bank-scope.json",scope)
    value=read(root/"state"/case/"case.json")
    end=stamp()
    actions_path=root/"timelines/ui-actions.jsonl"
    actions=[json.loads(line) for line in actions_path.read_text(encoding="utf8").splitlines()] if actions_path.exists() else []
    value.update(phase="CLASSIFY",ended_at_utc=end,observed=observation,
                 out_opcode_counts=result["out_opcode_counts"],sha256=result["sha256"],
                 classification="Official UI interaction captured; query feasibility requires analysis",
                 actions=[x for x in actions if value["started_at_utc"]<=x["timestamp_utc"]<=end])
    value["bank_scope"]=scope
    if scope["scope"]=="contaminated":
        value["classification"]="CONTAMINATED: unexpected bank selection; stop this case"
    write(root/"state"/case/"case.json",value)
    state.update(phase="CASE_COMPLETE",current_capture=None,current_case=None,
                 next_action="Inspect query results and restore unchanged test state before next case",updated_at_utc=end)
    state.setdefault("completed_cases",[]).append(case)
    if scope["scope"]=="contaminated":
        state.update(phase="BLOCKED_SCOPE",next_action="Inspect external writer and verify device bank before further UI mutation")
    write(root/"audit_session_state.json",state)
    return {"case":case,"out_opcode_counts":result["out_opcode_counts"],"sha256":result["sha256"]}


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=["begin","finish"])
    parser.add_argument("case")
    parser.add_argument("--session",type=Path,default=Path("audit_artifacts/computer-use-audit"))
    parser.add_argument("--note",required=True)
    args=parser.parse_args()
    result=begin(args.session,args.case,args.note) if args.action=="begin" else finish(args.session,args.case,args.note)
    print(json.dumps(result,ensure_ascii=False,indent=2))
