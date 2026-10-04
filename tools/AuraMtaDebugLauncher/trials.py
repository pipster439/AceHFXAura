"""Serial fixed ALL-category triage scheduler and offline evidence reduction."""
import argparse, hashlib, json, subprocess, time
from datetime import datetime, timezone
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'audit_artifacts/phase3-m2.7'
VARIANTS=('COM_ONLY','RO_INITIALIZE','LAMP_PREFLIGHT','SDK2_QI','METADATA_DELAY','M2_5_EXACT_PREFLIGHT','GPU_ONLY')
def cooldown(outcome):
    return 10 if outcome=='Completed' else 30
def timing_plan():
    return [dict(variant='COM_ONLY',delayMs=d,repetition=r) for d in (0,250,1000,5000) for r in range(1,4)]
def differential_plan():
    # Minimum compatibility controls after the negative timing/preflight evidence.
    return [dict(variant=v,delayMs=0,repetition=1) for v in ('COM_ONLY','RO_INITIALIZE','SDK2_QI','GPU_ONLY')]
def exception_record(code, flags, address, parameters, tid, pid, first_chance=False):
    if len(parameters)>15: raise ValueError('Invalid Windows exception parameter count')
    return dict(code=code,flags=flags,address=address,parameters=list(parameters),numberParameters=len(parameters),tid=tid,pid=pid,firstChance=first_chance,fastFailReason=parameters[0] if code==0xc0000409 and parameters else None)
def address_module(address, modules):
    for m in reversed(modules):
        if m['base']<=address<m['base']+m['size']:
            return dict(path=m['path'],base=m['base'],rva=address-m['base'])
    return None
def outcome(exit_code, exceptions, timeout, returned):
    if any(e['code']==0xc0000409 for e in exceptions):return 'FailFast'
    if any(not e['firstChance'] for e in exceptions):return 'Failed'
    if timeout:return 'Timeout'
    return 'Completed' if exit_code==0 and returned else 'Failed'
def summary(rows):
    groups={}
    for row in rows:
        key=(row['variant'],row['delayMs']);g=groups.setdefault(key,dict(variant=key[0],delayMs=key[1],trials=0,Completed=0,FailFast=0,Timeout=0,Failed=0,counts=[]))
        g['trials']+=1;g[row['outcome']]=g.get(row['outcome'],0)+1
        if row.get('count') is not None:g['counts'].append(row['count'])
    return list(groups.values())
def dump_metadata(path):
    p=Path(path);return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest(),localOnly=True)
def reduce():
    rows=[];crashes=[];maps=[]
    for p in sorted((OUT/'raw').glob('*.json')):
        d=json.loads(p.read_text())
        if d.get('fixture') or 'variant' not in d:continue
        rows.append(dict(raw=str(p.relative_to(ROOT)),**{k:v for k,v in d.items() if k not in ('modules','preEnumerateModules','samples','exceptions')}))
        crashes.extend(dict(trial=p.stem,**e) for e in d['exceptions'])
        maps.append(dict(trial=p.stem,pid=d['pid'],preEnumerate=d['preEnumerateModules'],modules=d['modules']))
        (OUT/'stacks'/(p.stem+'.json')).write_text(json.dumps(dict(exceptions=d['exceptions'],samples=d['samples']),indent=2))
    for name,value in [('mta-trials.json',rows),('crash-events.json',crashes),('module-maps.json',maps),('differential-summary.json',summary(rows)),('dump-hashes.json',[dump_metadata(p) for p in sorted((OUT/'dumps').glob('*.dmp'))])]:
        (OUT/name).write_text(json.dumps(value,indent=2))
    return rows
def run(plan,label):
    previous=reduce()
    for i,item in enumerate(plan,1):
        if item['variant'] not in VARIANTS or item['delayMs'] not in (0,250,1000,5000):raise ValueError('Unapproved fixed trial')
        output=OUT/'raw'/f'{label}-{i:02}-{item["variant"]}-{item["delayMs"]}.json'
        if output.exists():raise ValueError('Append-only trial output already exists')
        rest=cooldown(previous[-1]['outcome']) if previous else 10
        # Across separate batches use actual most recent end time, not lexical filename order.
        if previous:
            latest=max(previous,key=lambda r:r['endedAt']);rest=cooldown(latest['outcome'])
            elapsed=(datetime.now(timezone.utc)-datetime.fromisoformat(latest['endedAt'].replace('Z','+00:00'))).total_seconds()
            wait=max(0,rest-elapsed)
        else:wait=rest
        print(f'cooldown {wait:.1f}s before {label}/{i}',flush=True);time.sleep(wait)
        command=[str(OUT/'debug-build/Release/AuraMtaDebugLauncher.exe'),item['variant'],str(item['delayMs']),output.stem]
        began=datetime.now(timezone.utc).isoformat();print('starting',began,item,flush=True)
        subprocess.run(command,check=True,cwd=ROOT,timeout=100)
        d=json.loads(output.read_text());d.update(repetition=item['repetition'],requestedCooldownSeconds=rest,actualCooldownSleepSeconds=wait,schedulerStart=began)
        output.write_text(json.dumps(d,indent=2));previous=reduce();print('finished',d['endedAt'],d['outcome'],'Count',d['count'],flush=True)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--run',choices=['timing','differential']);p.add_argument('--label',default='triage');a=p.parse_args()
    if a.run:run(timing_plan() if a.run=='timing' else differential_plan(),a.label)
    else:reduce()
