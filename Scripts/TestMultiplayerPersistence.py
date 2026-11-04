"""Run after TestMultiplayerFixes: preserve extracted loot through a cold restart.
Only opt-in Validation saves and the launcher's managed processes are used.
"""
import math
import subprocess
import shutil
from phase4_checks import *

checks=[]


def check(name, test, seconds=15):
    try: wait(test,seconds)
    except Exception:
        checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)


def item(role,slot):
    return next(p for p in state(role)['props'] if p['actor']==me(role)['slots'][slot]['actor'])


def stored(s):
    return {h['id']:{part['id']:part['items'] for part in h['compartments']}
            for h in s['hub'] if h['compartments'] and 'TRIAL' not in h['id']}


try:
    check('Extracted shotgun and ammo available in completed hub',lambda:state()['run']['phase']==0
          and any(s['definition']=='DA_Shotgun' for s in me(1)['slots']) and ammo(1)>20)
    gun_slot=next(i for i,s in enumerate(me(1)['slots']) if s['definition']=='DA_Shotgun')
    send(1,op='slot',value=gun_slot)
    check('Client equips extracted shotgun',lambda:held(1)['definition']=='DA_Shotgun')
    send(1,op='reload',held=True);send(1,op='reload',held=False)
    check('Extracted weapon loads on client',lambda:held(1)['tube']==1)
    send(1,op='pump')
    check('One extracted shell is chambered',lambda:held(1)['chamber']==1)
    ammo_slot=next(i for i,s in enumerate(me(1)['slots']) if s['definition']=='DA_Ammo12GA')
    deposit_id=item(1,ammo_slot)['itemId'];deposit_count=item(1,ammo_slot)['quantity']
    place(1,-480,-480,100,yaw=-90,pitch=-35);time.sleep(.8)
    eye=vec(me(1)['camera']);target=(-480,-650,60);d=[target[i]-eye[i] for i in range(3)]
    send(1,op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2]))))
    time.sleep(.3);send(1,op='interact')
    check('Client opens real hub chest',lambda:me(1)['lootPart']!='None')
    chest=next(h for h in state(1)['hub'] if h['id']=='HUB_Chest_01')
    send(1,op='loot',actor=chest['actor'],index=ammo_slot,deposit=True,expectedId=deposit_id)
    check('Extracted ammo moves to base storage',lambda:any(i['id']==deposit_id and i['quantity']==deposit_count
          for h in state()['hub'] if h['id']=='HUB_Chest_01' for part in h['compartments'] for i in part['items']))
    check('Client observes deposited stack leave inventory',lambda:all(p['itemId']!=deposit_id for p in state(1)['props']))
    send(1,op='close_loot')
    ammo_slot=next(i for i,s in enumerate(me(1)['slots']) if s['definition']=='DA_Ammo12GA')
    loose_id=item(1,ammo_slot)['itemId'];send(1,op='slot',value=ammo_slot)
    check('Client equips remaining extracted stack',lambda:held(1)['itemId']==loose_id)
    place(1,-480,-250,100,yaw=0);time.sleep(.6)
    send(1,op='gesture',seconds=.08)
    check('Second extracted stack rests in hub world',lambda:any(p['itemId']==loose_id and p['holder']==-1
          and p['physicsSpeed']<5 for p in state()['props']))
    send(1,op='slot',value=gun_slot);time.sleep(.6)
    send(op='save');time.sleep(.6)
    before=state();identities=[me(i)['persistentId'] for i in (0,1)]
    fields=('itemId','definition','quantity','tube','chamber','safety','stowed')
    expected={p['itemId']:{k:p[k] for k in fields} for p in before['props']}
    loose_position=vec(next(p for p in before['props'] if p['itemId']==loose_id)['position'])
    send(op='quit');time.sleep(3)
    shell=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File']
    subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')],check=True,capture_output=True)
    subprocess.run(shell+[str(ROOT/'Scripts/RunLocal.ps1'),'-Players','2','-Menu','-Diagnostics','-Headless'],check=True,capture_output=True)
    wait(lambda:all(state(i)['map']=='L_MainMenu' for i in (0,1)),150)
    send(op='menu',action='continue');send(op='menu',action='load',index=0)
    check('Host loads same sector',lambda:state()['map']=='L_Hub' and state()['sector']==before['sector'],150)
    send(1,op='connect')
    check('Client rejoins original living character',lambda:me(1).get('persistentId')==identities[1]
          and not me(1)['dead'] and me(1)['meshAttached'] and not me(1)['meshSimulating'],90)
    check('Inventory/world item identities and weapon state survive exactly',lambda:
          {p['itemId']:{k:p[k] for k in fields} for p in state()['props']}==expected)
    check('Storage contents survive exactly',lambda:stored(state())==stored(before))
    check('Loose stack remains at base',lambda:math.dist(vec(next(p for p in state()['props'] if p['itemId']==loose_id)['position']),loose_position)<10)
    check('No item duplication on either peer',lambda:all(len([p['itemId'] for p in state(i)['props']])
          ==len({p['itemId'] for p in state(i)['props']}) for i in (0,1)))
    check('No trial reward or penalty reappears on reload',lambda:state()['run']['trialId']=='Shooting'
          and all(state()['run'][k]==0 for k in ('phase','respect','penalty','bonus')))
    # Old saves lack a selected trial and can contain obsolete economy values.
    accept_trial()
    check('Unfinished trial is accepted before legacy save',lambda:state()['run']['phase']==1 and state()['run']['loaded'])
    unfinished_id=state()['run']['id']
    fixture=DIAG/'Player0_legacy_trial.sav';started=time.time()
    send(op='legacy_trial_save')
    wait(lambda:fixture.exists() and fixture.stat().st_mtime>=started-1)
    send(op='quit');time.sleep(3)
    subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')],check=True,capture_output=True)
    save=ROOT/'Saved/SaveGames/Validation/Sectors'/(before['sector']+'.sav')
    backup=save.with_suffix('.before-multiplayer-legacy-test')
    shutil.copy2(save,backup);shutil.copy2(fixture,save)
    subprocess.run(shell+[str(ROOT/'Scripts/RunLocal.ps1'),'-Players','1','-Menu','-Diagnostics','-Headless'],check=True,capture_output=True)
    wait(lambda:state()['map']=='L_MainMenu',150)
    send(op='load_sector',slot=before['sector'])
    check('Legacy selected-trial default and removed economy migrate',lambda:state()['map']=='L_Hub'
          and state()['sector']==before['sector'] and state()['run']['trialId']=='Shooting'
          and all(state()['run'][k]==0 for k in ('respect','penalty','bonus')),90)
    check('Legacy migration preserves unfinished trial',lambda:state()['run']['phase']==1
          and state()['run']['id']==unfinished_id and state()['run']['loaded'],60)
    check('Legacy migration keeps all extracted item identities and weapon state',lambda:
          {p['itemId']:{k:p[k] for k in fields} for p in state()['props']}==expected)
    check('Legacy migration preserves base storage',lambda:stored(state())==stored(before))
    send(op='quit');time.sleep(3)
    subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')],check=True,capture_output=True)
    shutil.copy2(backup,save)
finally:
    report('multiplayer-fixes-persistence',checks)
