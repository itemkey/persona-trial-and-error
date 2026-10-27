"""Round-trip a legacy default-omitted version through the real sector loader.
Only opt-in diagnostic saves under SaveGames/Validation are replaced; retain backup.
"""
import argparse,subprocess,shutil
from phase4_checks import *
p=argparse.ArgumentParser();p.add_argument('--players',type=int,default=4);args=p.parse_args()
checks=[]
def check(name,test,seconds=30):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False,state=state()));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
try:
    identities=[me(i)['persistentId'] for i in range(args.players)]
    send(op='npcs',value=0);send(op='clear_inventory');send(op='vitals',health=100)
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True);send(op='fixture',definition='DA_Ammo12GA',quantity=17,store=True)
    check('Migration fixture inventory is ready',lambda:ammo()==17 and held()['definition']=='DA_Shotgun')
    send(op='decision',accept=True);wait(lambda:state()['run']['loaded'],60);send(op='board');time.sleep(1);send(op='lift');wait(lambda:state()['run']['phase']==2,35)
    for role in range(args.players):place(role,-2000+role*120,2500,100)
    enemies=[c for c in state()['characters'] if c['npc']]
    send(op='hit',actor=enemies[0]['actor'],bone='spine_02',damage=1000)
    wait(lambda:any(c['npc'] and c['poseBones']>0 for c in state()['characters']))
    send(op='legacy_save');time.sleep(.4);before=state();identity=me()['persistentId'];gun=held()['itemId']
    fixture=DIAG/'Player0_legacy.sav';assert fixture.is_file()
    # Unreal deliberately omits the legacy default; this is the compatibility case.
    check('Legacy default version is omitted from serialized tags',lambda:b'Version\x00' not in fixture.read_bytes())
    send(op='quit');time.sleep(4)
    shell=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File'];subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')],check=True)
    packaged=os.environ.get('PERSONA_PACKAGED')=='1';base=ROOT/'Artifacts/Windows/PersonaTrial' if packaged else ROOT
    save=base/'Saved/SaveGames/Validation/Sectors'/(before['sector']+'.sav')
    assert save.is_file(),save
    # Repeating this fixture in the same validation sector must retain the older
    # migration backup while allowing the next migration to create its own.
    prior=save.with_suffix('.sav.pre05')
    if prior.exists():prior.rename(prior.with_name(prior.name+'.fixture-'+str(time.time_ns())))
    shutil.copy2(save,save.with_suffix('.sav.v4-fixture-backup'));shutil.copy2(fixture,save)
    launcher='RunPackagedLocal.ps1' if packaged else 'RunLocal.ps1'
    subprocess.run(shell+[str(ROOT/'Scripts'/launcher),'-Players',str(args.players),'-Menu','-Diagnostics','-NetworkEmulation','-RenderHost']+([] if packaged else ['-Headless','-ResX','1920','-ResY','1080']),check=True)
    wait(lambda:all(state(i)['map']=='L_MainMenu' for i in range(args.players)),180)
    send(op='menu',action='continue');send(op='menu',action='load',index=0)
    wait(lambda:state()['map']=='L_Hub' and state()['run']['loaded'],120)
    for role in range(1,args.players):send(role,op='connect')
    check('Every peer restores its own subject after reconnect',lambda:all(state(i)['map']=='L_Hub' and me(i).get('persistentId')==identities[i] for i in range(args.players)),120)
    check('Version 3 migrates with a byte-exact pre05 backup',lambda:save.with_suffix('.sav.pre05').is_file() and save.with_suffix('.sav.pre05').read_bytes()==fixture.read_bytes() and state()['saveVersion']==4)
    check('Migration recovers original participant scaling without adding enemies',lambda:state()['run']['difficultyPlayers']==args.players and state()['run']['pendingNPC']==0 and state()['run']['remainingNPC']==before['run']['remainingNPC'])
    expected={c['persistentId']:c for c in before['characters'] if c['npc']}
    check('Migration preserves living enemies, corpses and exact loot',lambda:len([c for c in state()['characters'] if c['npc']])==len(expected) and all(c['persistentId'] in expected and c['health']==expected[c['persistentId']]['health'] and c['lootItems']==expected[c['persistentId']]['lootItems'] for c in state()['characters'] if c['npc']))
    check('Migration preserves participant and weapon identities',lambda:me()['persistentId']==identity and held()['itemId']==gun and ammo()==17)
    send(op='save');time.sleep(.4)
    check('New saves explicitly serialize the current version',lambda:b'Version\x00' in save.read_bytes())
    for role in range(args.players):send(op='kill_player',player=state(role)['localPlayer'])
    check('Post-migration wipe cleans up and revives the group',lambda:state()['run']['phase']==0 and all(not me(i)['dead'] for i in range(args.players)),20)
finally:report('phase5-save-migration',checks)
