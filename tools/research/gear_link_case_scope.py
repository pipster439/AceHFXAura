"""Offline bank-scope checks for Computer Use audit evidence, never a writer.

USBPcap does not identify the sending process. A visible browser slot alone
cannot authorize attributing captured writes to that slot or to Gear Link.
"""
import argparse
import json
from pathlib import Path


def inspect_scope(timeline, expected_slot):
    unexpected = []
    observations = []
    first_expected_selection = None
    for record in timeline:
        if record.get('interface') != 1 or record.get('status') != 0:
            continue
        payload = bytes.fromhex(record['payload_hex'])
        if len(payload) != 64:
            continue
        if record['direction'] == 'OUT' and payload[:2] == b'\x51\x00':
            if payload[4] != expected_slot:
                unexpected.append({'frame': record['frame'], 'timestamp_utc': record['timestamp_utc'],
                                   'slot': payload[4]})
            elif first_expected_selection is None:
                first_expected_selection = record['frame']
        elif record['direction'] == 'IN' and record.get('completed') and payload[:2] == b'\x12\x00' and payload[8]:
            # Official shared BasicInfo decoder: byte8 count,9 effect,10 active.
            observations.append({'frame': record['frame'], 'timestamp_utc': record['timestamp_utc'],
                                 'profile_count': payload[8], 'active_slot': payload[10],
                                 'lighting_effect': payload[9]})
    # A preflight may read the initial bank before explicitly selecting the test
    # bank. Ignore that baseline only when no expected-bank observation precedes
    # the selection. Later reselecting the test bank cannot erase observed drift.
    baseline_end = first_expected_selection or 0
    if any(o['active_slot'] == expected_slot and o['frame'] < baseline_end for o in observations):
        baseline_end = 0
    wrong_bank = any(o['active_slot'] != expected_slot and o['frame'] >= baseline_end for o in observations)
    return {'schema_version': 1, 'expected_test_slot': expected_slot,
            'unexpected_bank_selections': unexpected, 'device_bank_observations': observations,
            'scope': 'contaminated' if unexpected or wrong_bank
                     else 'no_unexpected_selection_observed',
            'caveats': ['Absence of a selection is not proof of bank ownership.',
                        'USBPcap does not attribute a command to a host process.',
                        'BasicInfo reads active bank, not every bank setting.']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('timeline', type=Path)
    parser.add_argument('--slot', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(inspect_scope(json.loads(args.timeline.read_text(encoding='utf8')),
                                                  args.slot), indent=2)+'\n', encoding='utf8')
