"""Offline official-reference fixtures; never connects to keyboard or service."""
import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/research'))
from gear_link_capture_privacy import PrivacyFilter
from gear_link_reference import decode, records, analyze


def usb(payload, ep=0x0d, transfer=1, dev=10, control_stage=3):
    h=28 if transfer==2 else 27
    head=struct.pack('<HQIHBHHBBI',h,1,0,9,1 if ep&128 else 0,4,dev,ep,transfer,len(payload))
    return head+(bytes([control_stage]) if transfer==2 else b'')+payload


DEVICE=bytes.fromhex('12 01 00 02 00 00 00 40 05 0b 7e 1b 59 01 01 02 03 01')
DESCRIPTORS=bytes.fromhex('09 04 01 00 02 03 00 00 00 07 05 0d 03 40 00 04 07 05 85 03 40 00 01 09 04 02 00 01 03 00 00 00 07 05 8c 03 15 00 01')
CONFIG=b'\x09\x02'+struct.pack('<H',9+len(DESCRIPTORS))+bytes.fromhex('02 01 00 a0 32')+DESCRIPTORS


class GearReferenceTests(unittest.TestCase):
    def test_actual_retained_capture_fixtures(self):
        fixture=json.loads((ROOT/'tests/fixtures/gear_link/captured_reference.json').read_text(encoding='utf8'))
        self.assertEqual(fixture['firmware'],'1.00.59')
        for r in fixture['records']:
            d=decode(bytes.fromhex(r['payload_hex']),r['interface'])
            self.assertEqual(d,r['decoded'])
            self.assertNotEqual(d.get('opcode'),'51 53')

    def target(self):
        f=PrivacyFilter()
        self.assertTrue(f.accept(usb(DEVICE,ep=0x80,transfer=2)))
        self.assertTrue(f.accept(usb(CONFIG,ep=0x80,transfer=2)))
        return f

    def test_reviewed_packet_retains_original_bytes(self):
        f=self.target();p=bytes.fromhex('51 54 01 00 12 00 05 00 01')+bytes(55)
        self.assertTrue(f.accept(usb(p)))
        self.assertEqual(decode(p,1)['sensitivity_raw'],5)

    def test_serial_and_string_descriptors_never_exported(self):
        f=self.target()
        self.assertFalse(f.accept(usb(bytes.fromhex('12 14')+bytes(62))))
        self.assertFalse(f.accept(usb(b'\x06\x03s\x00n\x00',ep=0x80,transfer=2)))

    def test_typing_and_unreviewed_interface_never_exported(self):
        f=self.target()
        self.assertFalse(f.accept(usb(b'\x00\x00\x04'+bytes(5),ep=0x81)))
        self.assertFalse(f.accept(usb(bytes(64),ep=0x8e)))

    def test_status_unknown_tail_and_unknown_opcodes_fail_closed(self):
        f=self.target()
        self.assertTrue(f.accept(usb(bytes.fromhex('03 76 00 00 01')+bytes(16),ep=0x8c)))
        self.assertFalse(f.accept(usb(bytes.fromhex('03 76 00 00 01 ff'),ep=0x8c)))
        self.assertFalse(f.accept(usb(bytes.fromhex('51 2c')+bytes(61))))
        self.assertFalse(f.accept(usb(bytes.fromhex('51 ff')+bytes(62))))

    def test_reconnect_requires_descriptor_identification(self):
        f=self.target();p=bytes.fromhex('51 54')+bytes(62)
        self.assertFalse(f.accept(usb(p,dev=11)))
        self.assertTrue(f.accept(usb(DEVICE,ep=0x80,transfer=2,dev=11)))
        self.assertTrue(f.accept(usb(CONFIG,ep=0x80,transfer=2,dev=11)))
        self.assertTrue(f.accept(usb(p,dev=11)))
        other=bytearray(DEVICE);other[10:12]=bytes.fromhex('01 00')
        self.assertFalse(f.accept(usb(bytes(other),ep=0x80,transfer=2,dev=11)))
        self.assertFalse(f.accept(usb(p,dev=11)))

    def test_rt_packet_fixtures_and_disable(self):
        for selector,raw in [(0,10),(1,5),(2,15),(1,15),(2,5)]:
            for enabled in (0,1):
                p=b'\x51\x54'+struct.pack('<HHBBB',selector,0x12,raw,0,enabled)+bytes(55)
                d=decode(p,1)
                self.assertTrue(d['verified_shape']);self.assertEqual(d['enabled'],enabled)
                self.assertEqual(d['selector'],selector);self.assertEqual(d['wire'],0x12)
        p=bytearray(p);p[9]=1;self.assertFalse(decode(bytes(p),1)['verified_shape'])
        p[9]=0;p[2]=3;self.assertFalse(decode(bytes(p),1)['verified_shape'])

    def test_gate_exact_on_off_unknown(self):
        for b,state in [(0,'off'),(1,'on')]:
            self.assertEqual(decode(bytes([3,0x76,0,0,b])+bytes(16),2)['state'],state)
        for p in [bytes.fromhex('03 76 00'),bytes.fromhex('03 76 00 00 02'),bytes.fromhex('03 76 01 00 01')]:
            self.assertNotIn('state',decode(p,2))

    def test_unsupported_bulk_rt_is_evidence_only(self):
        self.assertFalse(decode(bytes.fromhex('51 53')+bytes(62),1)['production_allowed'])

    def test_truncated_capture_rejected(self):
        header=bytes.fromhex('d4 c3 b2 a1')+struct.pack('<HHiiII',2,4,0,0,65535,249)
        self.assertEqual(records(header)[1],[])
        with self.assertRaises(ValueError):records(header+b'\x00')
        with self.assertRaises(ValueError):records(header+struct.pack('<IIII',1,0,20,20)+bytes(2))

    def test_export_requires_privacy_provenance(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'input.pcap';p.write_bytes(bytes(24));p.with_suffix('.privacy.json').write_text(json.dumps({'privacy_filtered':False}))
            with self.assertRaises(ValueError):analyze(p,Path(t)/'out')
            self.assertFalse((Path(t)/'out').exists())

    def test_export_interleaved_commands_and_reconnect_echo_identity(self):
        a=bytes.fromhex('51 54 01 00 12 00 05 00 01')+bytes(55)
        b=bytes.fromhex('50 55')+bytes(62)
        packets=[usb(DEVICE,ep=0x80,transfer=2),usb(CONFIG,ep=0x80,transfer=2),
                 usb(a),usb(b),usb(a,ep=0x85),usb(b,ep=0x85),
                 usb(DEVICE,ep=0x80,transfer=2,dev=11),usb(CONFIG,ep=0x80,transfer=2,dev=11),
                 usb(a,dev=11),usb(a,ep=0x85),usb(a,ep=0x85,dev=11)]
        header=bytes.fromhex('d4 c3 b2 a1')+struct.pack('<HHiiII',2,4,0,0,65535,249)
        data=header+b''.join(struct.pack('<IIII',1,i*1000,len(p),len(p))+p for i,p in enumerate(packets))
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'input.pcap';p.write_bytes(data)
            p.with_suffix('.privacy.json').write_text(json.dumps({'privacy_filtered':True,'sha256':hashlib.sha256(data).hexdigest()}))
            result=analyze(p,Path(t)/'out')
            outs=json.loads((Path(t)/'out/all_target_out.json').read_text())
            self.assertEqual(result['out_count'],3)
            self.assertEqual([r['identical_echo']['frame'] for r in outs],[5,6,11])
            self.assertEqual((Path(t)/'out/target_interfaces.pcap').read_bytes(),data)
            self.assertTrue((Path(t)/'out/payload_sequence.txt').is_file())

    def test_export_refuses_unreviewed_traffic_even_with_matching_hash(self):
        packets=[usb(DEVICE,ep=0x80,transfer=2),usb(CONFIG,ep=0x80,transfer=2),
                 usb(bytes.fromhex('12 14')+bytes(62))]
        header=bytes.fromhex('d4 c3 b2 a1')+struct.pack('<HHiiII',2,4,0,0,65535,249)
        data=header+b''.join(struct.pack('<IIII',1,i*1000,len(p),len(p))+p for i,p in enumerate(packets))
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'input.pcap';p.write_bytes(data)
            p.with_suffix('.privacy.json').write_text(json.dumps({'privacy_filtered':True,'sha256':hashlib.sha256(data).hexdigest()}))
            with self.assertRaisesRegex(ValueError,'Unreviewed record'):
                analyze(p,Path(t)/'out')
            self.assertFalse((Path(t)/'out').exists())


if __name__=='__main__':unittest.main()
