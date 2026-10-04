"""Bounded, sequential orchestration and recomputable read-only result aggregation."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from datetime import datetime, timezone

CATEGORIES = (0, 0x10000, 0x20000, 0x30000, 0x40000, 0x50000, 0x60000, 0x70000, 0x80000, 0x120000, 0x2F0000)
APARTMENTS = ("STA", "MTA")
MAX_DEVICES = 16
MAX_LIGHT_SAMPLES = 16

def matrix_plan(repetitions=3,winrt=False):
    if repetitions not in (3, 4, 5):
        raise ValueError("Repetitions bounded to 3..5")
    # Reverse apartment order on alternate repetitions to reduce time/order confounding.
    return [dict(apartment=apartment, category=category, repetition=repetition,initializationVariant="COM_plus_WinRT" if winrt else "COM_only",
                 arguments=["--single-winrt" if winrt else "--single", apartment, hex(category)])
            for repetition in range(1, repetitions+1)
            for category in CATEGORIES
            for apartment in (APARTMENTS if repetition % 2 else APARTMENTS[::-1])]

def validate_arguments(arguments):
    if len(arguments) not in (3, 4) or arguments[1] not in APARTMENTS:
        raise ValueError("Invalid child mode/apartment")
    mode=arguments[0].removesuffix('-winrt')
    if (mode in ("--single","--single-base","--single-version2") and len(arguments)!=3) or mode not in ("--single", "--single-base", "--single-version2", "--same-object", "--fresh-objects", "--fresh-objects-strict"):
        raise ValueError("Single category child cannot receive multiple categories")
    if mode not in ("--single","--single-base","--single-version2") and len(arguments)!=4:
        raise ValueError("Cache child requires two approved categories")
    values=[int(v,0) for v in arguments[2:]]
    if any(v not in CATEGORIES for v in values) or (len(values)==2 and values[0]==values[1]):
        raise ValueError("Category outside approved fixed set")
    return values

def bounded_count(row):
    if row.get("enumeration",{}).get("execution")!="Succeeded":
        return None
    count=row.get("count",{})
    value=count.get("value")
    if count.get("execution")!="Succeeded" or type(value) is not int or not 0<=value<=MAX_DEVICES:
        return None
    return value

def sampling_plan(light_count, enabled=False):
    if type(light_count) is not int or not 0<=light_count<=4096:
        return dict(execution="Failed", samples=0)
    # Native read-only projection intentionally has no per-light accessors. Optional colors are omitted.
    if enabled:
        raise ValueError("Color sampling is not compiled into this characterizer")
    return dict(execution="NotAttempted", samples=0, maximum=MAX_LIGHT_SAMPLES)

def value(observation):
    return observation.get("value") if observation.get("execution")=="Succeeded" else None

def fingerprint(row):
    return [(value(d.get("type",{})),value(d.get("name",{})),value(d.get("width",{})),value(d.get("height",{})),value(d.get("lightCount",{}))) for d in row.get("devices",[])]

def compare_cache(result):
    rows=result.get("observations",[])
    if len(rows)!=2 or any(bounded_count(r) is None for r in rows):
        return dict(execution="Unknown",sameCollection=None,sameMetadata=None)
    identities=[value(r.get("collectionIdentity",{})) for r in rows]
    strict=result.get("mode","").startswith("--fresh-objects-strict")
    return dict(execution="Succeeded",sameCollection=None if strict or None in identities else identities[0]==identities[1],
                identityComparable=not strict and None not in identities,sameMetadata=fingerprint(rows[0])==fingerprint(rows[1]),
                counts=[bounded_count(r) for r in rows],categories=[r["category"] for r in rows])

def aggregate(observations):
    groups=[]
    for apartment in APARTMENTS:
        for category in CATEGORIES:
            entries=[o for o in observations if o["apartment"]==apartment and o["category"]==category]
            counts=[bounded_count((o.get("result") or {}).get("observations",[{}])[0]) if (o.get("result") or {}).get("observations") else None for o in entries]
            groups.append(dict(apartment=apartment,category=category,categoryHex=hex(category),
                repetitions=[o["repetition"] for o in entries],pids=[o.get("process",{}).get("pid") for o in entries],
                counts=counts,allSucceeded=bool(counts) and all(c is not None for c in counts),
                reproducible=bool(counts) and None not in counts and len(set(counts))==1))
    return groups

def classify_identity(device, topology, hardware):
    name=value(device.get("name",{}));kind=value(device.get("type",{}));lights=value(device.get("lightCount",{}))
    candidates=[d for d in topology if d.get("name")==name and d.get("lightCount")==lights]
    # Exact cached entry is not an exact physical device identity.
    if len(candidates)==1:
        entry=candidates[0];key=entry.get("key","")
        domain={"Mainboard_Master":"motherboard", "Vga":"GPU", "GSkillDram":"DRAM", "AddressableStrip":"ARGB", "WaterCooler":"water cooler", "WDL_Keyboard":"keyboard/LampArray"}.get(key,"other/unknown")
        hardware_names=[str(h.get("Product",h.get("Name",h.get("Manufacturer","")))) for group in ("motherboard","gpu","dram") for h in hardware.get(group,[])]
        exact_model=bool(name) and any(name.casefold()==h.casefold() for h in hardware_names)
        return dict(topologyConfidence="EXACT",physicalConfidence="STRONG" if domain in ("motherboard","GPU","DRAM") else "PROBABLE",
            category=domain,topologyKey=key,name=name,type=kind,lights=lights,physicalModelExact=exact_model,
            reason="Exact cached name/light-count entry; physical device lacks unique runtime PnP/serial ID")
    typed=[entry for entry in topology if entry.get('type')==kind and kind is not None]
    if len(typed)==1:
        entry=typed[0];domain={"Mainboard_Master":"motherboard","Vga":"GPU","GSkillDram":"DRAM","AddressableStrip":"ARGB","WaterCooler":"water cooler","WDL_Keyboard":"keyboard/LampArray"}.get(entry['key'],'other/unknown')
        matching_lights=entry.get('lightCount')==lights and lights is not None
        return dict(topologyConfidence='STRONG',physicalConfidence='STRONG' if domain=='GPU' and matching_lights else 'PROBABLE',
                    category=domain,topologyKey=entry['key'],name=name,type=kind,lights=lights,physicalModelExact=False,
                    cachedLightRecords=entry.get('lightCount'),cachedLightCountMatches=matching_lights,
                    reason='Exact raw Type matches a unique cached LastProfile device group; counts are cached records, not hardware proof')
    if name=='WindowsLighting_LED':
        return dict(topologyConfidence='UNKNOWN',physicalConfidence='UNKNOWN',category='WindowsLighting adapter / physical device unknown',
                    name=name,type=kind,lights=lights,physicalModelExact=False,
                    keyboardCandidateConfidence='PROBABLE' if len(hardware.get('lampArrayPnp',[]))==1 else 'UNKNOWN',
                    reason='Opaque adapter metadata; no shared PnP/ContainerId with Aura object; raw Type alone does not identify ARGB or keyboard')
    return dict(topologyConfidence="UNKNOWN",physicalConfidence="UNKNOWN",category="unknown",name=name,type=kind,lights=lights,
                reason="No unique cached name/light-count match; no LampArray identity from similar names")

def run_child(executable, plan, raw_dir, index, timeout_seconds=20):
    if timeout_seconds not in (20,45):
        raise ValueError("Fixed read-only observation budgets only")
    validate_arguments(plan["arguments"]);started=time.time();command=[str(executable),*plan["arguments"]]
    stdout=b"";stderr=b"";exit_code=None;timed_out=False;pid=None
    with subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE) as child:
        pid=child.pid
        try:stdout,stderr=child.communicate(timeout=timeout_seconds);exit_code=child.returncode
        except subprocess.TimeoutExpired:
            # Read-only child only. No ownership state exists; record termination as incomplete evidence.
            timed_out=True;child.kill();stdout,stderr=child.communicate();exit_code=child.returncode
    if len(stdout)>1024*1024 or len(stderr)>256*1024:raise RuntimeError("Child diagnostic output limit exceeded")
    stem=f"{index:03d}";raw_dir.mkdir(parents=True,exist_ok=True)
    (raw_dir/(stem+".stdout.log")).write_bytes(stdout);(raw_dir/(stem+".stderr.log")).write_bytes(stderr)
    marker="@@AURA_READONLY_RESULT@@";results=[line[len(marker):] for line in stdout.decode('utf-8',errors='replace').splitlines() if line.startswith(marker)]
    result=json.loads(results[-1]) if len(results)==1 else None
    progress_marker="@@AURA_READONLY_PROGRESS@@"
    progress=[json.loads(line[len(progress_marker):]) for line in stdout.decode('utf-8',errors='replace').splitlines() if line.startswith(progress_marker)]
    partial=next((p['snapshot'] for p in progress if p.get('stage')=='InitializationCaptured'),None)
    if partial and not result:
        partial['execution']='IncompleteProcessExit';partial['observations']=[p['observation'] for p in progress if p.get('stage')=='EnumerationCaptured']
        partial['activations']=[p['result'] for p in progress if p.get('stage')=='Activated']
    if result:
        expected_categories=validate_arguments(plan["arguments"])
        rows=result.get("observations",[])
        if len(rows)>len(expected_categories) or [r['category'] for r in rows]!=expected_categories[:len(rows)]:
            raise RuntimeError("Child violated category sequence")
        if result['identity']['pid']!=pid or result['apartmentRequested']!=plan['apartment']:
            raise RuntimeError("OS PID/apartment receipt mismatch")
        for row in rows:
            if len(row.get('devices',[]))>MAX_DEVICES:raise RuntimeError("Device bound violated")
            for device in row.get('devices',[]):
                if device.get('sampledLightCount',0)!=0 or device.get('lights',[]):raise RuntimeError("Unexpected per-light sampling surface")
    observation=dict(plan,process=dict(pid=pid,exitCode=exit_code,timedOut=timed_out,elapsedMs=(time.time()-started)*1000,
        startedAtUtc=datetime.fromtimestamp(started,timezone.utc).isoformat(),endedAtUtc=datetime.now(timezone.utc).isoformat(),launchOrdinal=index,timeoutSeconds=timeout_seconds),result=result,
        rawStdout="raw/"+stem+".stdout.log",rawStderr="raw/"+stem+".stderr.log")
    observation['partialResult']=partial if not result else None
    observation['lastRecordedStage']=progress[-1]['stage'] if progress else None
    if plan['arguments'][0].removesuffix('-winrt') not in ('--single','--single-base','--single-version2') and result:observation['cacheComparison']=compare_cache(result)
    return observation

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--executable',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--winrt',action='store_true')
    args=parser.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
    matrix=out/'aura-enumeration-matrix.json'
    if matrix.exists():raise SystemExit("Preserve prior matrix; choose a fresh evidence folder")
    observations=[];payload=dict(schemaVersion=1,executableSha256=hashlib.sha256(args.executable.read_bytes()).hexdigest(),repetitions=3,observations=observations)
    for index,plan in enumerate(matrix_plan(winrt=args.winrt),1):
        row=run_child(args.executable,plan,out/'raw',index);observations.append(row)
        payload['aggregate']=aggregate(observations);matrix.write_text(json.dumps(payload,indent=2),encoding='utf-8')
        counts=[bounded_count(r) for r in (row.get('result') or {}).get('observations',[])];print(index,plan['apartment'],hex(plan['category']),counts,flush=True)
        time.sleep(0.5)
    # Separate cache experiments; empty/nonempty order distinguishes per-object filtering from global state.
    cache=[];index=len(observations)
    for apartment in APARTMENTS:
        for mode in ('--same-object','--fresh-objects','--fresh-objects-strict'):
            for a,b in ((0x10000,0x20000),(0x20000,0x10000),(0,0x2f0000),(0x2f0000,0)):
                index+=1;child_mode=mode+'-winrt' if args.winrt else mode
                plan=dict(apartment=apartment,test=mode,initializationVariant="COM_plus_WinRT" if args.winrt else "COM_only",arguments=[child_mode,apartment,hex(a),hex(b)])
                row=run_child(args.executable,plan,out/'raw',index);cache.append(row)
                (out/'aura-cache-tests.json').write_text(json.dumps(dict(schemaVersion=1,observations=cache),indent=2),encoding='utf-8')
                print(index,apartment,mode,hex(a),hex(b),row.get('cacheComparison'),flush=True)
                time.sleep(0.5)

if __name__=='__main__':main()
