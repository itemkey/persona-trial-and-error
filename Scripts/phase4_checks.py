"""Shared black-box transport for the opt-in Development diagnostics."""
import json,time,datetime,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DIAG=Path(os.environ.get('PERSONA_DIAGNOSTICS',str(ROOT/'.work/Diagnostics')))
def state(role=0):
    data=json.loads((DIAG/f'Player{role}.json').read_bytes())
    if time.time()-datetime.datetime.fromisoformat(data['utc'].replace('Z','+00:00')).timestamp()>5:raise ValueError('Stale diagnostics')
    return data
def send(role=0,**command):
    path=DIAG/f'Player{role}.command';end=time.monotonic()+15
    while path.exists():
        if time.monotonic()>end:raise TimeoutError(str(path))
        time.sleep(.05)
    tmp=path.with_suffix('.tmp');tmp.write_text(json.dumps(command),encoding='utf-8');tmp.replace(path)
def wait(test,seconds=30):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        try:
            if test():return
        except (OSError,ValueError,KeyError,IndexError,StopIteration):pass
        time.sleep(.15)
    raise AssertionError('Condition timed out')
def me(role=0):
    s=state(role);return next((c for c in s['characters'] if c['id']==s['localPlayer']),{})
def held(role=0):
    s=state(role);return next(p for p in s['props'] if p['actor']==me(role)['held'])
def ammo(role=0):return sum(s['quantity'] for s in me(role)['slots'] if s['definition']=='DA_Ammo12GA')
def vec(text):return [float(text.split(a+'=')[1].split()[0]) for a in 'XYZ']
def place(role,x,y,z,yaw=0,pitch=0):send(0,op='place',player=state(role)['localPlayer'],x=x,y=y,z=z,yaw=yaw,pitch=pitch)
def report(name,checks,**extra):
    if os.environ.get('PERSONA_PACKAGED')=='1' and name.startswith('phase4-') and not name.startswith('phase4-packaged-'):
        name=name.replace('phase4-','phase4-packaged-',1)
    out=ROOT/'Artifacts/Validation';out.mkdir(parents=True,exist_ok=True)
    (out/(name+'.json')).write_text(json.dumps(dict(checks=checks,**extra),indent=2),encoding='utf-8')
