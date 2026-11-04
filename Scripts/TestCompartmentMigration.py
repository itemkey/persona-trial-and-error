"""Round-trip actual saves, then migrate a flattened legacy cabinet inventory."""
import math, shutil, subprocess
from phase4_checks import *
checks=[]

def check(name, predicate, seconds=20):
    try: wait(predicate,seconds)
    except Exception:
        checks.append(dict(check=name,passed=False,state=state())); raise
    checks.append(dict(check=name,passed=True)); print('PASS',name,flush=True)

def fridge(role=0): return next(h for h in state(role)['hub'] if h['id']=='HUB_Refrigerator')

def open_part(index):
    send(op='close_loot'); time.sleep(.7)
    place(0,730,540,100); time.sleep(.4)
    key=fridge()['compartments'][index]['id']
    at=vec(next(p for p in fridge()['parts'] if p['mesh']==key)['center'])
    eye=vec(me()['camera']); d=[at[i]-eye[i] for i in range(3)]
    send(op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2]))))
    time.sleep(.25); send(op='interact'); wait(lambda:me()['lootPart']==key)

def restart(before, fixture=None):
    identities=[me(i)['persistentId'] for i in (0,1)]
    send(op='quit'); time.sleep(3)
    shell=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File']
    subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')],check=True)
    packaged=os.environ.get('PERSONA_PACKAGED')=='1'
    base=ROOT/'Artifacts/Windows/PersonaTrial' if packaged else ROOT
    save=base/'Saved/SaveGames/Validation/Sectors'/(before['sector']+'.sav')
    if fixture:
        for suffix in ('.pre05','.pre06'):
            backup=save.with_name(save.name+suffix)
            if backup.exists(): backup.rename(backup.with_name(backup.name+'.fixture-'+str(time.time_ns())))
        shutil.copy2(save,save.with_name(save.name+'.phase6-roundtrip'))
        shutil.copy2(fixture,save)
    launcher='RunPackagedLocal.ps1' if packaged else 'RunLocal.ps1'
    subprocess.run(shell+[str(ROOT/'Scripts'/launcher),'-Players','2','-Menu','-Diagnostics','-NetworkEmulation','-RenderHost']+([] if packaged else ['-Headless']),check=True)
    wait(lambda:all(state(i)['map']=='L_MainMenu' for i in (0,1)),300)
    send(op='menu',action='continue'); send(op='menu',action='load',index=0)
    wait(lambda:state()['map']=='L_Hub' and me().get('persistentId')==identities[0],180)
    send(1,op='connect')
    check('Peers reconnect with their original identities',lambda:me(1).get('persistentId')==identities[1],180)
    return save

try:
    send(op='npcs',value=0); send(op='clear_inventory'); send(op='close_loot')
    place(1,-600,1100,100)
    for index in (0,1):
        open_part(index)
        while fridge()['compartments'][index]['items']:
            entries=fridge()['compartments'][index]['items']
            send(op='loot',actor=fridge()['actor'],index=0,deposit=False,expectedId=entries[0]['id'])
            wait(lambda:len(fridge()['compartments'][index]['items'])<len(entries))
            send(op='clear_inventory')
    send(op='close_loot')
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',definition='DA_Ammo12GA',quantity=17,store=True)
    wait(lambda:held()['definition']=='DA_Shotgun')
    for _ in range(3):
        before=held()['tube']; send(op='reload',held=True); send(op='reload',held=False); wait(lambda:held()['tube']==before+1)
    send(op='pump'); wait(lambda:held()['chamber']==1); send(op='safety'); wait(lambda:held()['safety'])
    gun={k:held()[k] for k in ('itemId','tube','chamber','safety')}
    open_part(1)
    send(op='loot',actor=fridge()['actor'],index=0,deposit=True,expectedId=gun['itemId'])
    wait(lambda:any(x['id']==gun['itemId'] for x in fridge()['compartments'][1]['items']))
    open_part(0)
    shell=next((i,s) for i,s in enumerate(me()['slots']) if s['definition']=='DA_Ammo12GA')
    identity=next(p['itemId'] for p in state()['props'] if p['actor']==shell[1]['actor'])
    send(op='loot',actor=fridge()['actor'],index=shell[0],deposit=True,expectedId=identity)
    wait(lambda:any(x['id']==identity for x in fridge()['compartments'][0]['items']))
    send(op='close_loot'); time.sleep(.8)
    send(op='npcs',value=1)
    wait(lambda:any(c['npc'] for c in state()['characters']))
    falling=next(c for c in state()['characters'] if c['npc'])
    send(op='ai_pause',actor=falling['actor'],paused=True)
    send(op='place_npc',actor=falling['actor'],x=100,y=1100,z=30000,yaw=180)
    time.sleep(.3); send(op='hit',actor=falling['actor'],bone='spine_02',damage=1000)
    wait(lambda:next(c for c in state()['characters'] if c['persistentId']==falling['persistentId'])['ragdoll'])
    send(op='save'); time.sleep(.4)
    original=fridge()['compartments']; before=state()
    restart(before)
    check('Current format preserves separate contents and closed doors on both peers',lambda:fridge()['compartments']==original and fridge(1)['compartments']==original)
    check('Unfinished death resumes physics after reload',lambda:any(c['persistentId']==falling['persistentId'] and c['ragdoll'] and c['poseBones']==0 for c in state()['characters']))
    send(op='npcs',value=0)
    send(op='legacy_save'); time.sleep(.5)
    fixture=DIAG/'Player0_legacy.sav'; legacy=fixture.read_bytes(); before=state()
    save=restart(before,fixture)
    check('Legacy file has a byte-exact pre06 backup',lambda:save.with_name(save.name+'.pre06').read_bytes()==legacy and state()['saveVersion']==6)
    expected=[x for p in original for x in p['items']]
    check('Legacy inventory migrates wholly to first compartment without duplication',lambda:fridge()['compartments'][0]['items']==expected and all(not p['items'] for p in fridge()['compartments'][1:]))
    open_part(0)
    send(op='loot',actor=fridge()['actor'],index=0,deposit=False,expectedId=gun['itemId'])
    check('Migrated shotgun retains identity, tube, chamber and safety',lambda:{k:held()[k] for k in gun}==gun)
    check('Loose ammunition survives migration separately from loaded shells',lambda:sum(x['quantity'] for x in fridge()['items'] if 'Ammo12GA' in x['definition'])>=14)
    send(op='close_loot'); send(op='save')
finally:
    report('phase6-compartment-migration',checks)
