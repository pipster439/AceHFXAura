"""Inspect the owned, privacy-filtered live capture. Never opens a device."""
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path
from gear_link_reference import records, utc
from gear_link_capture_privacy import PrivacyFilter


def inspect(path, slot=5, now=None):
    _, rows = records(path.read_bytes())
    guard = PrivacyFilter()
    observations, drift = [], []
    for r in rows:
        if not guard.accept(r['raw']):
            raise ValueError('Unreviewed live capture record; no authority granted')
        p = r['payload']
        mi = guard.epmaps.get((r['bus'], r['device']), {}).get(r['endpoint'])
        if mi != 1 or r['transfer'] != 1 or len(p) != 64 or r['status']:
            continue
        item = {'frame': r['frame'], 'timestamp_utc': utc(r['ns'])}
        if r['endpoint'] == 0x85 and r['info'] & 1 and p[:2] == b'\x12\x00' and p[8]:
            item.update(active_slot=p[10], profile_count=p[8], lighting_effect=p[9])
            observations.append(item)
            if p[10] != slot:
                drift.append(item)
        if r['endpoint'] == 0x0d and not r['info'] & 1 and p[:2] == b'\x51\x00' and p[4] != slot:
            drift.append({**item, 'unexpected_selected_slot': p[4]})
    current = now or datetime.now(timezone.utc)
    age = ((current - datetime.fromisoformat(observations[-1]['timestamp_utc'].replace('Z', '+00:00'))).total_seconds()
           if observations else None)
    fresh = age is not None and 0 <= age <= 90
    return {'expected_slot': slot, 'device_observations': observations, 'drift': drift,
            'authority': 'drift' if drift else ('observed' if observations else 'unknown'),
            'observation_age_seconds': age, 'fresh': fresh,
            'gate_passed': fresh and bool(observations) and not drift,
            'last_record_utc': utc(rows[-1]['ns']) if rows else None,
            'caveat': 'Discrete BasicInfo observations; unreported bank changes are not observable.'}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', type=Path)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    result = inspect(args.capture)
    value = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(value, encoding='utf8')
    print(value)
    raise SystemExit(2 if result['drift'] else (0 if result['gate_passed'] else 3))
