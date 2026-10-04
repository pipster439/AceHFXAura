"""Isolate the fixed read-only probe. No arbitrary vendor invocation or child profile."""
import json
import hashlib
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from model import mediator_result

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'audit_artifacts/phase3-m2.8'
EXE = OUT / 'build/Release/LightingBackendProbe.exe'
MAX_OUTPUT = 4 * 1024 * 1024


def supervise(command, output, budget=20):
    # Caller is this fixed research runner or software fixtures, never an IPC client.
    start = time.monotonic(); timeout = False; excessive = False
    with output.open('wb') as log:
        child = subprocess.Popen(command, cwd=ROOT, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        try:
            while child.poll() is None:
                if output.stat().st_size > MAX_OUTPUT:
                    excessive = True; child.kill(); break
                if time.monotonic() - start > budget:
                    timeout = True; child.kill(); break
                time.sleep(.05)
            child.wait(timeout=5)
        finally:
            if child.poll() is None:
                child.kill(); child.wait(timeout=5)
    with output.open('rb') as log:
        raw = log.read(MAX_OUTPUT + 1)
    if len(raw) > MAX_OUTPUT:
        excessive = True
    records = []
    if len(raw) <= MAX_OUTPUT and not excessive:
        try:
            records = [json.loads(line) for line in raw.decode('utf-8-sig').splitlines() if line]
            if any(not isinstance(record, dict) for record in records):
                excessive = True; records = []
        except (ValueError, UnicodeError, RecursionError):
            excessive = True
    return dict(pid=child.pid, exitCode=child.returncode, timeout=timeout, malformedOrExcessiveOutput=excessive,
                elapsedMs=(time.monotonic() - start)*1000, records=records)


def main():
    OUT.joinpath('raw').mkdir(exist_ok=True)
    trials = []
    for i in range(3):
        start = datetime.now(timezone.utc).isoformat()
        trial = supervise([str(EXE), '--mediator'], OUT / f'raw/mediator-{i+1}.jsonl')
        trial.update(repetition=i+1, startedAt=start, endedAt=datetime.now(timezone.utc).isoformat(),
                     executableSha256=hashlib.sha256(EXE.read_bytes()).hexdigest())
        trial['result'] = mediator_result(trial['records'], trial['exitCode'], trial['timeout'])
        if trial['malformedOrExcessiveOutput']:
            trial['result']['outcome'] = 'MalformedOutput'
        trials.append(trial)
        # Do not repeatedly hammer a hung/crashed vendor path.
        if trial['result']['outcome'] != 'Completed' or trial['result']['activation']['state'] != 'Succeeded' or any(q['state'] != 'Succeeded' for q in trial['result']['queries']):
            break
        time.sleep(2)
    (OUT / 'service-mediator-probe.json').write_text(json.dumps(dict(trials=trials, maxWorkerSeconds=20,
        fixedQueryDispids=[3,4,63], notAttempted=['GetProfile(deviceId)', 'AuraExclusiveStatus', 'QueryMatrixControlStatus',
        'OLED', 'Hue', 'all state-changing members']), indent=2, ensure_ascii=False), encoding='utf-8')
    wdl = supervise([str(EXE), '--wdl'], OUT / 'raw/wdl.jsonl')
    wdl['executableSha256'] = hashlib.sha256(EXE.read_bytes()).hexdigest()
    (OUT / 'wdl-observation.json').write_text(json.dumps(wdl, indent=2, ensure_ascii=False), encoding='utf-8')
    print('Saved isolated ServiceMediator and DeviceInformation observations')


if __name__ == '__main__':
    main()
