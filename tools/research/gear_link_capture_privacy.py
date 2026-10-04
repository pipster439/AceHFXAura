"""Passive USBPcap named-pipe sink. Never opens or writes a HID device.

Only target descriptors and reviewed configuration/status packets reach disk.
String descriptors, serial queries, ordinary key input and unknown bodies are
discarded in memory. The saved pcap is explicitly privacy-filtered, not a full
controller capture. Run before USBPcapCMD -o <printed named pipe>.
"""
import argparse, collections, ctypes, hashlib, json, struct, sys
from ctypes import wintypes
from pathlib import Path

class PrivacyFilter:
    def __init__(self):
        self.targets=set(); self.stats=collections.Counter(); self.epmaps={}
    def accept(self, raw):
        self.stats['seen']+=1
        if len(raw)<27: return self.drop('malformed')
        h,irp,status,fn,info,bus,dev,ep,transfer,n=struct.unpack_from('<HQIHBHHBBI',raw)
        if h<27 or h>len(raw) or n!=len(raw)-h: return self.drop('malformed')
        p=raw[h:]; k=(bus,dev)
        if transfer==2 and h>=28 and raw[27]==3 and len(p)==18 and p[:2]==b'\x12\x01':
            if p[8:12]==bytes.fromhex('05 0b 7e 1b'):
                self.targets.add(k); self.stats['target_descriptor']+=1; return True
            self.targets.discard(k); self.epmaps.pop(k,None)
        if k not in self.targets:return self.drop('other_or_unidentified_device')
        if transfer==2:
            if h>=28 and raw[27]==3 and len(p)>=9 and p[:2]==b'\x09\x02' and int.from_bytes(p[2:4],'little')==len(p):
                pos=0; mi=None; eps={}
                while pos<len(p):
                    size=p[pos]
                    if size<2 or pos+size>len(p):return self.drop('malformed')
                    d=p[pos:pos+size];pos+=size
                    if d[1]==4 and len(d)>=9:mi=d[2]
                    if d[1]==5 and len(d)>=7:eps[d[2]]=mi
                self.epmaps[k]=eps;self.stats['configuration_descriptor']+=1;return True
            return self.drop('control_removed_including_strings')
        mi=self.epmaps.get(k,{}).get(ep)
        if transfer!=1 or not p:return self.drop('no_reviewed_payload')
        if mi==2 and len(p)>=5 and p[0]==3 and p[1] in (0x76,0x72,0x81):
            if any(p[5:]):return self.drop('unreviewed_event_tail')
            self.stats['status']+=1;return True
        if mi!=1 or len(p)!=64:return self.drop('ordinary_input_or_other_interface')
        if p[:2]==b'\xc0\x81':
            # Count actual target OUT submissions without persisting RGB/key
            # contents. This is passive ownership evidence, never a sender.
            if not(ep&128) and not(info&1):
                self.stats['direct_rgb_out_submissions']+=1
            return self.drop('direct_rgb_body_removed')
        # Official shared WebHID SW-mode query/set: header zeros, one boolean.
        # Retention only, never submission; do not retain unrelated 27/74 bodies.
        if p[0] in (0x27,0x74) and p[1:4]==b'\0\0\0' and p[4] in (0,1) and not any(p[5:]):
            self.stats['reviewed_lighting_ownership']+=1;return True
        # Readable device serial lives in 12 14 (index 1/2): never retain it.
        allowed={0x12:{0,3,0x12,0x15},0x25:{0,1,2,3,4,5,6,8,9,10,14,0x22,0x23,0x24,0xa4,0xa6,0xa9},
                 0x50:{0x40,0x55,0x60,0x61},0x51:{0,0x21,0x23,0x2c,0x2d,0x31,0x4f,0x50,0x52,0x53,0x54,0x55,0x56,0x58,0x59}}
        if p[1] not in allowed.get(p[0],set()):return self.drop('unreviewed_vendor_body')
        self.stats['reviewed_vendor']+=1;return True
    def drop(self,reason):self.stats[reason]+=1;return False

def exact(reader,n):
    data=bytearray()
    while len(data)<n:
        block=reader(n-len(data))
        if not block:
            if not data:return None
            raise EOFError('partial stream record')
        data.extend(block)
    return bytes(data)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('output',type=Path);ap.add_argument('--pipe',required=True);a=ap.parse_args()
    if a.output.exists():raise FileExistsError('Refusing to overwrite capture')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateNamedPipeW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p];kernel.CreateNamedPipeW.restype=wintypes.HANDLE
    kernel.ConnectNamedPipe.argtypes=[wintypes.HANDLE,ctypes.c_void_p];kernel.ConnectNamedPipe.restype=wintypes.BOOL
    kernel.ReadFile.argtypes=[wintypes.HANDLE,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),ctypes.c_void_p];kernel.ReadFile.restype=wintypes.BOOL
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    pipe=kernel.CreateNamedPipeW(a.pipe,1,0,1,2**20,2**20,0,None)
    if pipe==wintypes.HANDLE(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    print('READY '+a.pipe,flush=True)
    f=PrivacyFilter();outcome='complete'
    def read(n):
        buf=ctypes.create_string_buffer(n);received=wintypes.DWORD()
        if not kernel.ReadFile(pipe,buf,n,ctypes.byref(received),None):
            error=ctypes.get_last_error()
            if error==109:return b''
            raise ctypes.WinError(error)
        return buf.raw[:received.value]
    try:
        if not kernel.ConnectNamedPipe(pipe,None) and ctypes.get_last_error()!=535:raise ctypes.WinError(ctypes.get_last_error())
        header=exact(read,24)
        if not header or header[:4]!=b'\xd4\xc3\xb2\xa1' or struct.unpack_from('<I',header,20)[0]!=249:raise ValueError('Expected little-endian USBPcap')
        with a.output.open('xb') as out:
            out.write(header);out.flush()
            while True:
                rh=exact(read,16)
                if rh is None:break
                sec,frac,size,original=struct.unpack('<IIII',rh)
                if size>2**20:raise ValueError('Unbounded capture record')
                packet=exact(read,size)
                if packet is None:raise EOFError('missing packet')
                if f.accept(packet):out.write(rh);out.write(packet);out.flush();f.stats['retained']+=1
    except EOFError:
        outcome='stream_tail_partial_discarded'
    finally:
        kernel.CloseHandle(pipe)
        report={'schema_version':1,'outcome':outcome,'privacy_filtered':True,'not_full_controller_capture':True,'counts':dict(f.stats),'sha256':hashlib.sha256(a.output.read_bytes()).hexdigest() if a.output.exists() else None}
        a.output.with_suffix('.privacy.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(json.dumps(report),flush=True)

if __name__=='__main__':main()
