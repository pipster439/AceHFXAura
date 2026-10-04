"""Offline classic USBPcap audit: all target interfaces, no HID or network access.

The original capture is never modified. Device identity comes from descriptors.
Unknown OUT commands and non-identical IN responses remain in the timeline.
"""
import argparse
import csv
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

MAGICS={b'\xd4\xc3\xb2\xa1':('<',1_000_000), b'\xa1\xb2\xc3\xd4':('>',1_000_000),
        b'\x4d\x3c\xb2\xa1':('<',1_000_000_000),b'\xa1\xb2\x3c\x4d':('>',1_000_000_000)}
COUNTERS=['51 00','51 50','51 4f','51 58','51 59','51 52','51 53','51 54','51 23','50 55']
def records(data):
    if len(data)<24 or data[:4] not in MAGICS:raise ValueError('Invalid classic pcap header')
    endian,scale=MAGICS[data[:4]]
    major,minor,_,_,snaplen,link=struct.unpack_from(endian+'HHiiII',data,4)
    if (major,minor)!=(2,4) or link!=249:raise ValueError('Requires USBPcap linktype 249, version 2.4')
    pos=24;result=[]
    while pos<len(data):
        begin=pos
        if len(data)-pos<16:raise ValueError('Truncated record header')
        sec,frac,size,original=struct.unpack_from(endian+'IIII',data,pos);pos+=16
        if frac>=scale or size>snaplen or size>original or pos+size>len(data):raise ValueError('Invalid/truncated pcap record')
        raw=data[pos:pos+size];pos+=size
        if len(raw)<27:raise ValueError('Truncated USBPcap header')
        hlen,irp,status,function,info,bus,device,ep,transfer,length=struct.unpack_from('<HQIHBHHBBI',raw)
        if hlen<27 or hlen>len(raw) or len(raw)-hlen!=length:raise ValueError('Invalid USBPcap header/data length')
        result.append(dict(frame=len(result)+1,ns=sec*1_000_000_000+frac*(1_000_000_000//scale),
            bus=bus,device=device,endpoint=ep,transfer=transfer,info=info,status=status,function=function,
            irp_id=irp,control_stage=raw[27] if transfer==2 and hlen>=28 else None,
            payload=raw[hlen:],record=data[begin:pos],snapshot_truncated=size!=original))
    return data[:24],result

def utc(ns):
    sec,nano=divmod(ns,1_000_000_000)
    return datetime.fromtimestamp(sec,timezone.utc).strftime('%Y-%m-%dT%H:%M:%S')+f'.{nano//1000:06d}Z'

def interfaces(payload):
    if len(payload)<9 or payload[:2]!=b'\x09\x02' or int.from_bytes(payload[2:4],'little')!=len(payload):
        raise ValueError('Incomplete configuration descriptor')
    pos=0;current=None;result=[]
    while pos<len(payload):
        if len(payload)-pos<2 or payload[pos]<2 or pos+payload[pos]>len(payload):raise ValueError('Malformed config descriptor')
        item=payload[pos:pos+payload[pos]];pos+=len(item)
        if item[1]==4 and len(item)>=9:
            current=dict(interface=item[2],alternate=item[3],interface_class=item[5],endpoints=[]);result.append(current)
        elif item[1]==5 and current is not None and len(item)>=7:
            current['endpoints'].append(dict(address=item[2],attributes=item[3],max_packet_size=int.from_bytes(item[4:6],'little'),interval=item[6]))
    return result

def semantic(payload):
    if len(payload)!=64:return dict(kind='UnclassifiedReport',evidence='NOT VERIFIED')
    prefix=payload[:2].hex(' ')
    if prefix=='51 00':return dict(kind='ProfileSelectionCandidate',profile_selector=payload[4],reserved_zero=not any(payload[2:4]+payload[5:]),evidence='USB bytes only; role requires static correlation')
    if prefix=='50 55':return dict(kind='ApplyGate',reserved_zero=not any(payload[2:]))
    if prefix=='51 52':return dict(kind='Reset',reset_type=int.from_bytes(payload[2:4],'little'),common_raw=payload[4],layer=payload[5])
    if prefix=='51 54':return dict(kind='PerKeyRapidTrigger',selector=int.from_bytes(payload[2:4],'little'),wire=int.from_bytes(payload[4:6],'little'),sensitivity_raw=payload[6],continuous=payload[7],enable=payload[8],reserved_zero=not any(payload[9:]))
    if prefix in ['51 50','51 4f','51 58','51 59','51 23','51 53']:
        return dict(kind={'51 50':'AllKeyActuation','51 4f':'PerKeyActuation','51 58':'AllKeyDeadzone','51 59':'PerKeyDeadzone','51 23':'PerKeyDksSlot','51 53':'UnverifiedBulkRt'}[prefix])
    return dict(kind='UnclassifiedVendorCommand',opcode=prefix,evidence='NOT VERIFIED')

def analyze(path,output):
    data=path.read_bytes();header,rows=records(data)
    identities=[];targets=set();maps={};descriptors=[]
    for r in rows:
        p=r['payload'];key=(r['bus'],r['device'])
        if r['transfer']==2 and r['control_stage']==3 and len(p)==18 and p[:2]==b'\x12\x01' and p[8:12]==bytes.fromhex('05 0b 7e 1b'):
            targets.add(key);identities.append(dict(frame=r['frame'],bus=key[0],address=key[1],bcd_device=f'0x{int.from_bytes(p[12:14],"little"):04x}',payload_hex=p.hex(' ')))
    if not targets:raise ValueError('No captured target descriptor; refusing to guess device address')
    for r in rows:
        p=r['payload']
        if (r['bus'],r['device']) in targets and r['transfer']==2 and r['control_stage']==3 and len(p)==18 and p[:2]==b'\x12\x01' and p[8:12]!=bytes.fromhex('05 0b 7e 1b'):
            raise ValueError('Target address reused by another device; split capture before analysis')
    for key in targets:
        candidates=[r for r in rows if (r['bus'],r['device'])==key and r['transfer']==2 and r['control_stage']==3 and r['payload'][:2]==b'\x09\x02']
        if not candidates:raise ValueError('No target configuration descriptor')
        configs=[interfaces(r['payload']) for r in candidates]
        if any(c!=configs[0] for c in configs):raise ValueError('Configuration changed; cannot silently use a stale interface mapping')
        maps[key]=configs[0]
    target=[r for r in rows if (r['bus'],r['device']) in targets]
    timeline=[];outrows=[]
    for r in target:
        if not r['payload']:continue
        ep=r['endpoint'];isout=not ep&128 and not r['info']&1 and bool(r['irp_id'])
        direction='OUT' if isout else ('IN' if ep&128 or r['transfer']==2 and r['control_stage']==3 else 'Control/Completion')
        match=[i['interface'] for i in maps[(r['bus'],r['device'])] if any(e['address']==ep for e in i['endpoints'])]
        v={k:r[k] for k in ['frame','bus','device','endpoint','transfer','info','status','function','control_stage','snapshot_truncated']}
        v.update(timestamp_utc=utc(r['ns']),timestamp_ns=r['ns'],interface=match[0] if len(match)==1 else None,
            direction=direction,payload_length=len(r['payload']),payload_hex=r['payload'].hex(' '),irp_id=f'0x{r["irp_id"]:016x}',
            decoded=semantic(r['payload']) if r['transfer']==1 else dict(kind='ControlTransfer'))
        timeline.append(v)
        if isout:outrows.append(v)
    # Identical completed IN payload, same bus/address/interface, before next OUT.
    # Query responses may differ and are retained rather than falsely called echo.
    for index,r in enumerate(outrows):
        nextns=outrows[index+1]['timestamp_ns'] if index+1<len(outrows) else float('inf')
        echoes=[x for x in timeline if x['direction']=='IN' and x['info']&1 and x['status']==0 and
                x['bus']==r['bus'] and x['device']==r['device'] and x['interface']==r['interface'] and
                r['timestamp_ns']<=x['timestamp_ns']<nextns and x['payload_hex']==r['payload_hex']]
        r['identical_echo']=dict(frame=echoes[0]['frame'],timestamp_utc=echoes[0]['timestamp_utc'],delay_ms=(echoes[0]['timestamp_ns']-r['timestamp_ns'])/1e6) if echoes else None
        r['delta_from_previous_out_ms']=(r['timestamp_ns']-outrows[index-1]['timestamp_ns'])/1e6 if index else None
    groups=[];current=None
    for r in outrows:
        if r['payload_hex'].startswith('51 00 ') and r['payload_length']==64:
            current=dict(group=len(groups)+1,profile_selector=r['decoded']['profile_selector'],commands=[],apply_observed=False,start_timestamp_utc=r['timestamp_utc']);groups.append(current)
        if current is not None:
            current['commands'].append(r)
            if r['decoded']['kind']=='ApplyGate':
                current['apply_observed']=True
                current['select_to_apply_out_ms']=(r['timestamp_ns']-current['commands'][0]['timestamp_ns'])/1e6
                current['select_to_apply_echo_ms']=current['select_to_apply_out_ms']+r['identical_echo']['delay_ms'] if r['identical_echo'] else None
                current=None
    for index,g in enumerate(groups):
        start=g['commands'][0]['timestamp_ns']
        end=groups[index+1]['commands'][0]['timestamp_ns'] if index+1<len(groups) else float('inf')
        g['opcode_counts']=dict(Counter(x['payload_hex'][:5] for x in g['commands']))
        g['command_span_ms']=(g['commands'][-1]['timestamp_ns']-start)/1e6
        for x in timeline:
            if start<=x['timestamp_ns']<end:x['profile_transition_phase']=f"selection_{g['group']}_to_{g['profile_selector']}"
    for x in timeline:x.setdefault('profile_transition_phase','before_first_selection')
    counts=Counter(x['payload_hex'][:5] for x in outrows)
    result=dict(schema_version=1,source=str(path),sha256=hashlib.sha256(data).hexdigest(),size_bytes=len(data),record_count=len(rows),
        target_record_count=len(target),tail_complete=True,snapshot_truncated_records=sum(r['snapshot_truncated'] for r in rows),identities=identities,
        interfaces=[dict(bus=k[0],device=k[1],interfaces=v) for k,v in sorted(maps.items())],
        actual_target_out_count=len(outrows),opcode_counts={**{k:counts[k] for k in COUNTERS},**dict(counts)},
        transition_groups=groups,target_status_errors=[x for x in timeline if x['status']],
        caveats=['No kernel-drop statistics in classic pcap','Echo is not firmware readback','Click and physical-settled times are not captured','Transition direction requires user operation notes'])
    output.mkdir(parents=True,exist_ok=True)
    def save(name,value):(output/name).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    save('capture_analysis.json',result);save('timeline.json',timeline);save('all_target_out.json',outrows);save('opcode_counts.json',result['opcode_counts'])
    with (output/'timeline.csv').open('w',encoding='utf8',newline='') as stream:
        fields=['frame','timestamp_utc','interface','endpoint','direction','payload_length','payload_hex','profile_transition_phase']
        writer=csv.DictWriter(stream,fieldnames=fields,extrasaction='ignore');writer.writeheader();writer.writerows(timeline)
    with (output/'ace_hfx_all_interfaces.pcap').open('wb') as stream:
        stream.write(header)
        for r in target:stream.write(r['record'])
    text=[]
    for r in timeline:
        text.append(f"frame {r['frame']} {r['timestamp_utc']} MI_{r['interface']} EP {r['endpoint']:02x} {r['direction']} len={r['payload_length']} {r['decoded']['kind']}\n{r['payload_hex']}\n")
    (output/'payload_sequence.txt').write_text('\n'.join(text),encoding='utf8')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('pcap',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    r=analyze(a.pcap,a.output)
    print(json.dumps({k:r[k] for k in ['sha256','record_count','target_record_count','opcode_counts']},indent=2))
