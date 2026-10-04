"""Offline Gear Link USB reference analysis. No device, network, or write API.

Consumes an explicitly privacy-filtered classic USBPcap file. Rejects a file
without the collector's manifest rather than exporting unrestricted traffic.
Frame numbers refer to retained capture records, not discarded bus traffic.
"""
import argparse
from collections import Counter
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct


EFFECTS = {0: 'Static', 1: 'Breathing', 2: 'ColorCycle', 3: 'Reactive',
           4: 'Rainbow', 5: 'Ripple', 6: 'StarryNight', 7: 'Quicksand',
           8: 'Current', 9: 'RainDrop'}


def decode(payload, interface):
    """Describe captured bytes; no firmware readback/production validation claim."""
    p = payload
    if interface == 2:
        if len(p) >= 5 and p[:4] == bytes.fromhex('03 76 00 00') and not any(p[5:]) and p[4] in (0, 1):
            return {'kind': 'HardwareRtGate', 'report_id': 3,
                    'state': 'on' if p[4] else 'off'}
        return {'kind': 'OtherReviewedEvent'}
    if interface != 1 or len(p) != 64:
        return {'kind': 'DescriptorOrUnclassified'}
    prefix = p[:2].hex(' ')
    sel = int.from_bytes(p[2:4], 'little')
    base = {'opcode': prefix, 'selector': sel}
    if prefix == '51 54':
        return {**base, 'kind': 'PerKeyRt', 'wire': int.from_bytes(p[4:6], 'little'),
                'sensitivity_raw': p[6], 'continuous': p[7], 'enabled': p[8],
                'verified_shape': sel in (0, 1, 2) and p[7] in (0, 1) and p[8] in (0, 1) and not any(p[9:])}
    if prefix == '51 00':
        return {**base, 'kind': 'BankSelection', 'slot': p[4]}
    if prefix == '50 55':
        return {**base, 'kind': 'Apply', 'reserved_zero': not any(p[2:])}
    if prefix == '51 53':
        return {**base, 'kind': 'UnverifiedBulkRt', 'production_allowed': False}
    if prefix == '51 2c':
        return {**base, 'kind': 'LightingParameters', 'effect_id': p[2],
                'effect': EFFECTS.get(p[2], 'UNKNOWN'), 'chunk_countdown': p[3]}
    if prefix == '51 2d':
        return {**base, 'kind': 'AnalogLighting', 'effect_id': p[4], 'enabled': p[5]}
    if prefix in ('51 50', '51 4f'):
        key = prefix == '51 4f'
        off = 6 if key else 4
        return {**base, 'kind': 'KeyActuation' if key else 'CommonActuation',
                'wire': int.from_bytes(p[4:6], 'little') if key else None,
                'raw': int.from_bytes(p[off:off+2], 'little')}
    if prefix in ('51 58', '51 59'):
        key = prefix == '51 59'
        off = 6 if key else 4
        return {**base, 'kind': 'KeyDeadzone' if key else 'CommonDeadzone',
                'wire': int.from_bytes(p[4:6], 'little') if key else None,
                'bottom_raw': p[off], 'top_raw': p[off+1]}
    if prefix == '51 23':
        return {**base, 'kind': 'DksSlot', 'wire': sel, 'start_raw': p[4],
                'end_raw': p[5], 'target_wire': int.from_bytes(p[6:8], 'little'),
                'positions_packed': p[8], 'slot': p[9]}
    if prefix == '51 21':
        return {**base, 'kind': 'StandardKey', 'wire': sel,
                'target_wire': int.from_bytes(p[4:6], 'little'), 'actuation_raw': p[6]}
    if prefix == '51 52':
        return {**base, 'kind': 'ResetDispatcherEvidence', 'reset_type': sel,
                'data_prefix': list(p[4:10]), 'production_permission': 'not granted by this audit'}
    if prefix == '51 31':
        return {**base, 'kind': 'PollingRateWrite', 'rate_index': p[4]}
    return {**base, 'kind': 'QueryOrOtherReviewedCommand'}


def records(data):
    if len(data) < 24 or data[:4] != b'\xd4\xc3\xb2\xa1' or struct.unpack_from('<I', data, 20)[0] != 249:
        raise ValueError('Requires little-endian classic USBPcap')
    pos = 24
    result = []
    while pos < len(data):
        begin = pos
        if pos+16 > len(data):
            raise ValueError('Truncated record header')
        sec, frac, size, original = struct.unpack_from('<IIII', data, pos)
        pos += 16
        if size > 2**20 or size != original or frac >= 1_000_000 or pos+size > len(data):
            raise ValueError('Invalid capture record')
        raw = data[pos:pos+size]
        pos += size
        if len(raw) < 27:
            raise ValueError('Truncated USB header')
        h, _, status, _, info, bus, dev, ep, transfer, n = struct.unpack_from('<HQIHBHHBBI', raw)
        if h < 27 or h > len(raw) or n != len(raw)-h:
            raise ValueError('Invalid USB payload length')
        result.append(dict(frame=len(result)+1, ns=sec*1_000_000_000+frac*1000,
                           bus=bus, device=dev, endpoint=ep, transfer=transfer,
                           status=status, info=info, payload=raw[h:], raw=raw,
                           control_stage=raw[27] if h >= 28 and transfer == 2 else None,
                           record=data[begin:pos]))
    return data[:24], result


def utc(ns):
    s, sub = divmod(ns, 1_000_000_000)
    return datetime.fromtimestamp(s, timezone.utc).strftime('%Y-%m-%dT%H:%M:%S')+f'.{sub//1000:06d}Z'


def analyze(path, output):
    # Import the same allowlist that protected acquisition. Never export serial.
    from gear_link_capture_privacy import PrivacyFilter
    data = path.read_bytes()
    meta = json.loads(path.with_suffix('.privacy.json').read_text(encoding='utf8'))
    if meta.get('privacy_filtered') is not True or meta.get('sha256') != hashlib.sha256(data).hexdigest():
        raise ValueError('Missing/mismatched privacy provenance')
    header, rows = records(data)
    guard = PrivacyFilter()
    for r in rows:
        if not guard.accept(r['raw']):
            raise ValueError(f"Unreviewed record {r['frame']}; export refused")
    timeline = []
    for r in rows:
        mi = guard.epmaps.get((r['bus'], r['device']), {}).get(r['endpoint']) if r['transfer'] == 1 else None
        if mi not in (1, 2):
            continue
        isout = not r['endpoint'] & 0x80 and not r['info'] & 1
        direction = 'OUT' if isout else 'IN'
        timeline.append(dict(frame=r['frame'], timestamp_utc=utc(r['ns']), timestamp_ns=r['ns'],
                             usb_bus=r['bus'], usb_address=r['device'],
                             interface=mi, endpoint=f"0x{r['endpoint']:02x}", direction=direction,
                             completed=bool(r['info'] & 1), status=r['status'],
                             payload_length=len(r['payload']), payload_hex=r['payload'].hex(' '),
                             decoded=decode(r['payload'], mi)))
    outs = [r for r in timeline if r['direction'] == 'OUT']
    consumed = set()
    for i, r in enumerate(outs):
        # Other commands may be concurrently in flight. The next *identical*
        # submission, not the next arbitrary OUT, bounds an unambiguous echo.
        end = next((x['timestamp_ns'] for x in outs[i+1:] if x['interface']==r['interface'] and
                    x['usb_bus']==r['usb_bus'] and x['usb_address']==r['usb_address'] and
                    x['payload_hex']==r['payload_hex']), r['timestamp_ns']+5_000_000_000)
        end = min(end, r['timestamp_ns']+5_000_000_000)
        echoes = [x for x in timeline if x['direction'] == 'IN' and x['completed'] and not x['status'] and
                  x['frame'] not in consumed and x['interface'] == r['interface'] and r['timestamp_ns'] <= x['timestamp_ns'] < end and
                  x['usb_bus']==r['usb_bus'] and x['usb_address']==r['usb_address'] and
                  x['payload_hex'] == r['payload_hex']]
        r['identical_echo'] = ({'frame': echoes[0]['frame'], 'delay_ms': (echoes[0]['timestamp_ns']-r['timestamp_ns'])/1e6}
                              if echoes else None)
        if echoes:
            consumed.add(echoes[0]['frame'])
        r['delta_previous_out_ms'] = (r['timestamp_ns']-outs[i-1]['timestamp_ns'])/1e6 if i else None
    batches, stages = [], []
    for r in outs:
        op = r['decoded'].get('opcode', '')
        if r['decoded']['kind'] == 'Apply':
            batches.append(dict(apply_frame=r['frame'], stage_frames=[s['frame'] for s in stages],
                                stage_count=len(stages), stage_to_apply_ms=(r['timestamp_ns']-stages[0]['timestamp_ns'])/1e6 if stages else None))
            stages = []
        elif op.startswith('51 ') or op.startswith('50 '):
            stages.append(r)
    bank_windows = []
    selections = [r for r in outs if r['decoded']['kind'] == 'BankSelection']
    for i, selection in enumerate(selections):
        end_frame = selections[i+1]['frame'] if i+1 < len(selections) else len(rows)+1
        window = [r for r in outs if selection['frame'] <= r['frame'] < end_frame]
        bank_windows.append(dict(selection_frame=selection['frame'], slot=selection['decoded']['slot'],
                                 end_frame_exclusive=end_frame,
                                 out_opcode_counts=dict(Counter(r['decoded']['opcode'] for r in window))))
    result = dict(schema_version=1, source_file=path.name, sha256=meta['sha256'],
                  privacy_filtered=True, frame_namespace='retained capture frames',
                  record_count=len(rows), out_count=len(outs),
                  out_opcode_counts=dict(Counter(r['decoded']['opcode'] for r in outs)),
                  lighting_effect_counts=dict(Counter(r['decoded']['effect'] for r in outs if r['decoded']['kind']=='LightingParameters')),
                  profile_selections=[{'frame': r['frame'], 'timestamp_utc': r['timestamp_utc'], 'slot': r['decoded']['slot']} for r in outs if r['decoded']['kind']=='BankSelection'],
                  gate_events=[r for r in timeline if r['decoded']['kind']=='HardwareRtGate' and r['completed']],
                  apply_groups=batches, uncommitted_stage_frames=[s['frame'] for s in stages],
                  caveats=['Privacy-filtered acquisition, not full bus capture; removed categories cannot be counted as absent.',
                           'Echo proves host submission, not physical effect or NVM persistence.',
                           'Human click times and physical results require user notes.',
                           'Observed USB timing does not authorize Aura settle/retry changes.'])
    output.mkdir(parents=True, exist_ok=True)
    for name, value in [('capture_analysis.json', result), ('timeline.json', timeline),
                        ('all_target_out.json', outs), ('bank-windows.json', bank_windows)]:
        (output/name).write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    with (output/'timeline.csv').open('w', newline='', encoding='utf8') as f:
        fields=['frame','timestamp_utc','interface','endpoint','direction','payload_length','payload_hex']
        w=csv.DictWriter(f, fieldnames=fields, extrasaction='ignore');w.writeheader();w.writerows(timeline)
    (output/'payload_sequence.txt').write_text('\n\n'.join(f"frame {r['frame']} {r['timestamp_utc']} MI_{r['interface']:02d} {r['endpoint']} {r['direction']} len={r['payload_length']} {r['decoded']['kind']}\n{r['payload_hex']}" for r in timeline)+'\n', encoding='utf8')
    with (output/'target_interfaces.pcap').open('wb') as f:
        f.write(header)
        for r in rows:
            f.write(r['record'])
    return result


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('pcap',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();r=analyze(a.pcap,a.output)
    print(json.dumps({k:r[k] for k in ('record_count','out_count','out_opcode_counts')},indent=2))
