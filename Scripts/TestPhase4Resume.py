"""Cold-process persistence of lift/cargo, corpses and paused trial timers.
Run on a fresh diagnostic host (no other clients). Uses isolated validation saves.
"""
import subprocess
from phase4_checks import *
checks=[]
def check(name,test,seconds=25):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def restart():
    send(op='save');time.sleep(.6);before=state();send(op='quit')
    # Wait for graceful teardown/save, then stop only matching managed processes.
    time.sleep(4)
    # Scope the execution policy to these child processes; do not change the
    # user's machine or account policy when launching the repository scripts.
    shell=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File']
    subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')],check=True)
    packaged=os.environ.get('PERSONA_PACKAGED')=='1'
    launcher='RunPackagedLocal.ps1' if packaged else 'RunLocal.ps1'
    subprocess.run(shell+[str(ROOT/'Scripts'/launcher),'-Players','1','-Menu','-Diagnostics']+([] if packaged else ['-Headless']),check=True)
    wait(lambda:state()['map']=='L_MainMenu',150)
    send(op='menu',action='continue');send(op='menu',action='load',index=0)
    wait(lambda:state()['map']=='L_Hub' and state()['sector']==before['sector'] and me()['persistentId'],150)
    return before
def npc_ids():return sorted(c['persistentId'] for c in state()['characters'] if c['npc'])
try:
    check('Fresh sector before cold-save checks',lambda:state()['run']['phase']==0 and all(s['quantity']==0 for s in me()['slots']),150)
    identity=me()['persistentId']
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',definition='DA_Ammo12GA',quantity=20,store=True)
    check('Weapon fixture ready',lambda:ammo()==20 and held()['definition']=='DA_Shotgun')
    send(op='reload',held=True);send(op='reload',held=False);wait(lambda:held()['tube']==1)
    send(op='pump');wait(lambda:held()['chamber']==1)
    send(op='fire');wait(lambda:held()['chamber']==2)
    gun_id=held()['itemId']
    send(op='decision',accept=True);wait(lambda:state()['run']['loaded'],60)
    send(op='board');time.sleep(1);send(op='fixture',definition='DA_Ammo12GA',quantity=7,store=False);time.sleep(1)
    send(op='lift');wait(lambda:state()['run']['lift']==1 and state()['run']['progress']>.12)
    before=restart()
    check('Cold load resumes lift rather than completing it offline',lambda:state()['run']['lift']==1 and state()['run']['passengers']==1 and state()['run']['cargo']==1 and state()['run']['progress']<before['run']['progress']+.25)
    check('Same subject, spent chamber and inventory survive cold load',lambda:me()['persistentId']==identity and held()['itemId']==gun_id and held()['chamber']==2 and ammo()==19)
    check('Resumed lift arrives and starts five NPCs',lambda:state()['run']['phase']==2 and state()['run']['remainingNPC']==5 and not me()['transit'],30)
    check('Loose cargo restored at arena height',lambda:any(p['quantity']==7 and p['holder']==-1 and vec(p['position'])[2]<-12000 for p in state()['props']))
    living=npc_ids();before=restart()
    check('Live wave resumes with the same five NPC identities and health',lambda:state()['run']['phase']==2 and npc_ids()==living and all(not c['dead'] and c['health']==100 for c in state()['characters'] if c['npc']))
    send(op='finish_wave');wait(lambda:state()['run']['phase']==3)
    wait(lambda:all(c['poseBones']>0 for c in state()['characters'] if c['npc']))
    bodies=npc_ids();saved_loot={c['persistentId']:c['lootItems'] for c in state()['characters'] if c['npc']};before=restart()
    check('Intermission timer pauses while the application is closed',lambda:state()['run']['phase']==3 and abs(state()['run']['remaining']-before['run']['remaining'])<3)
    check('Settled corpses keep identities, poses and their exact sparse loot',lambda:npc_ids()==bodies and all(c['dead'] and c['poseBones']>0 and c['lootItems']==saved_loot[c['persistentId']] for c in state()['characters'] if c['npc']))
    for wave in range(1,4):
        wait(lambda:state()['run']['phase']==2 and state()['run']['wave']==wave,30)
        send(op='finish_wave');wait(lambda:state()['run']['phase'] in (3,4))
    before=restart()
    check('Evacuation deadline pauses across a cold restart',lambda:state()['run']['phase']==4 and abs(state()['run']['remaining']-before['run']['remaining'])<3)
    check('All 65 corpses survive evacuation continuation without a duplicate reward',lambda:len(npc_ids())==65 and not state()['run']['rewarded'] and state()['run']['bonus']==0)
    # End through the actual failure path, then verify a finished run stays gone.
    send(op='kill_player',player=state()['localPlayer'])
    check('Wipe returns subject to hub after five seconds',lambda:state()['run']['phase']==0 and me()['health']>0 and not any(c['npc'] for c in state()['characters']),15)
    before=restart()
    check('Finished trial does not resurrect after continuation',lambda:state()['run']['phase']==0 and not state()['run']['loaded'] and not npc_ids())
    check('Lost equipment remains lost after continuation',lambda:all(s['quantity']==0 for s in me()['slots']) and not any(p['itemId']==gun_id for p in state()['props']))
finally:report('phase4-packaged-cold-resume' if os.environ.get('PERSONA_PACKAGED')=='1' else 'phase4-cold-resume',checks)
