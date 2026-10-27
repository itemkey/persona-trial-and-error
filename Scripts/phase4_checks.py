"""Shared black-box transport for the opt-in Development diagnostics."""
import json,time,datetime,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DIAG=Path(os.environ.get('PERSONA_DIAGNOSTICS',str(ROOT/'.work/Diagnostics')))

def read_snapshot(path):
    # The game atomically replaces snapshots. Windows' normal Python file open
    # denies delete sharing, which can stall Unreal's rename retry for 500 ms.
    # Share deletion so intensive sampling cannot hitch the simulation itself.
    if os.name!='nt':return path.read_bytes()
    import ctypes,msvcrt
    from ctypes import wintypes
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    create=kernel.CreateFileW
    create.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    create.restype=wintypes.HANDLE
    handle=create(str(path),0x80000000,7,None,3,0x80,None)
    if handle==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
    with os.fdopen(msvcrt.open_osfhandle(handle,os.O_RDONLY|os.O_BINARY),'rb') as stream:return stream.read()
def state(role=0):
    for attempt in range(40):
        try:
            data=json.loads(read_snapshot(DIAG/f'Player{role}.json'));break
        except (OSError,ValueError):
            if attempt==39:raise
            time.sleep(.005)
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
    s=state(role)
    return next((c for c in s['characters'] if not c['npc'] and c['id']>=0 and c['id']==s['localPlayer']),{})
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
