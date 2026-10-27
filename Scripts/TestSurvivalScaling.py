"""Authoritative encounter lifecycle; diagnostic damage is not combat playtesting."""
import argparse,math,statistics,subprocess
from phase4_checks import *
p=argparse.ArgumentParser();p.add_argument('--players',type=int,default=4);p.add_argument('--cold',action='store_true');args=p.parse_args()
checks=[];performance={};populations=[]
def check(name,test,seconds=30):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False,state=state()));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def run():return state()['run']
def living():return [c for c in state()['characters'] if c['npc'] and not c['dead']]
def bodies():return [c for c in state()['characters'] if c['npc'] and c['dead']]
def crates():return [h for h in state()['hub'] if 'Container_' in h['id']]
def profile(label):
    place(0,2200,-3000,-12100,yaw=-90,pitch=-10);time.sleep(.35)
    samples=[]
    for _ in range(12):
        s=state();samples.append({k:s.get(k) for k in ('frameMs','gameThreadMs','renderThreadMs','gpuMs','packetLagMs','packetLossPercent','viewportWidth','viewportHeight')});time.sleep(.25)
    performance[label]=samples
    place(0,-2000,2500,100)
def cold_restart():
    identities=[me(i)['persistentId'] for i in range(args.players)]
    send(op='attack',actor=living()[0]['actor'],variant=3)
    wait(lambda:any(c['attacking'] for c in living()))
    send(op='save');time.sleep(.3);before=state()
    send(op='quit');time.sleep(4)
    shell=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File']
    subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')],check=True)
    packaged=os.environ.get('PERSONA_PACKAGED')=='1'
    launcher='RunPackagedLocal.ps1' if packaged else 'RunLocal.ps1'
    subprocess.run(shell+[str(ROOT/'Scripts'/launcher),'-Players',str(args.players),'-Menu','-Diagnostics','-NetworkEmulation','-RenderHost']+([] if packaged else ['-Headless','-ResX','1920','-ResY','1080']),check=True)
    wait(lambda:all(state(i)['map']=='L_MainMenu' for i in range(args.players)),180)
    send(op='menu',action='continue');send(op='menu',action='load',index=0)
    wait(lambda:state()['map']=='L_Hub' and state()['run']['loaded'],150)
    for role in range(1,args.players):send(role,op='connect')
    wait(lambda:all(state(i)['map']=='L_Hub' and me(i).get('persistentId')==identities[i] for i in range(args.players)),120)
    ids=sorted(c['persistentId'] for c in before['characters'] if c['npc'])
    check('Cold restart preserves every NPC and corpse identity',lambda:sorted(c['persistentId'] for c in state()['characters'] if c['npc'])==ids)
    check('Cold restart preserves difficulty and queued reinforcements',lambda:all(run()[k]==before['run'][k] for k in ('difficultyPlayers','pendingNPC','spawnedNPC','wave')))
    expected={c['persistentId']:c['lootItems'] for c in before['characters'] if c['npc'] and c['dead']}
    check('Cold restart preserves sparse corpse loot exactly',lambda:all(c['lootItems']==expected[c['persistentId']] for c in bodies()))
    check('Restarted melee does not replay an interrupted strike',lambda:all(not c['attacking'] for c in living()))
try:
    send(op='npcs',value=0);send(op='clear_inventory');send(op='vitals',health=100)
    check('Encounter starts from offered state',lambda:run()['phase']==0)
    send(op='decision',accept=True);check('Arena streams on every peer',lambda:all(state(i)['run']['loaded'] for i in range(args.players)),90)
    send(op='board');time.sleep(1);send(op='lift')
    check('Departure snapshots participating player count',lambda:run()['lift']==1 and run()['difficultyPlayers']==args.players)
    check('Scaled ammunition is split across two crates',lambda:len(crates())==2 and sum(r['quantity'] for h in crates() for r in h['items'] if 'DA_Ammo12GA' in r['definition'])==48+24*(args.players-1))
    check('Crates contain enough shotguns for the group',lambda:sum(r['quantity'] for h in crates() for r in h['items'] if 'DA_Shotgun' in r['definition'])==max(2,args.players))
    check('First wave starts with unchanged baseline health',lambda:run()['phase']==2 and living() and all(c['maxHealth']==100 for c in living()),35)
    # Park lifecycle-test players outside combat without changing the run snapshot.
    for role in range(args.players):place(role,-2000+role*120,2500,100)
    check('Enemies can leave the service gate',lambda:any(vec(c['position'])[1]>-5600 for c in living()),30)
    counts=[math.ceil(n*(1+.45*(args.players-1))) for n in (5,10,20,30)]
    for wave,total in enumerate(counts):
        check(f'Wave {wave+1}: total {total}, active cap 30',lambda wave=wave,total=total:run()['phase']==2 and run()['wave']==wave and run()['remainingNPC']==total and len(living())<=30 and len(living())+run()['pendingNPC']==total,35)
        if wave==3:
            check('Thirty enemies active with reinforcements queued',lambda:len(living())==30 and run()['pendingNPC']==max(0,total-30))
            profile('30_active_accumulated_corpses')
            if args.cold:cold_restart()
            send(op='kill_player',player=state(args.players-1)['localPlayer']) if args.players>1 else None
            check('Participant death does not lower current difficulty',lambda:run()['difficultyPlayers']==args.players)
        populations.append(dict(wave=wave,total=total,initial=run()))
        while run()['phase']==2 and run()['wave']==wave:
            previous=run()['pendingNPC'];send(op='finish_wave')
            if previous:
                check(f'Wave {wave+1}: queued enemies refill vacancies',lambda previous=previous:run()['pendingNPC']<previous and len(living())>0 and len(living())<=30,10)
            else:wait(lambda:run()['phase'] in (3,4))
        check(f'Wave {wave+1}: all scheduled identities spawned once',lambda total=total:run()['spawnedNPC']==total)
    check('All scaled wave corpses persist',lambda:run()['phase']==4 and len(bodies())==sum(counts))
    check('Corpse ammunition is sparse and bounded to two rounds',lambda:all(sum(x['quantity'] for x in c['lootItems'])<=2 for c in bodies()) and any(not c['lootItems'] for c in bodies()) and any(c['lootItems'] for c in bodies()))
    check('Corpses retain settled poses',lambda:all(c['poseBones']>0 for c in bodies()),15)
    profile('all_corpses')
    send(op='save')
    for role in range(1,args.players):place(role,1000+role*120,2000,-12505)
    place(0,0,0,-12505);time.sleep(1);send(op='lift')
    check('Survivor extraction still grants the existing reward',lambda:run()['rewarded'] and run()['bonus']==20,35)
    check('Purge cleans the run and revives lost participants',lambda:run()['phase']==0 and not any(c['npc'] for c in state()['characters']) and all(not me(i)['dead'] for i in range(args.players)),50)
finally:report(f'phase5-scaling-{args.players}p',checks,populations=populations,performance=performance,note='Lifecycle fixture uses diagnostic damage; separate rendered combat exercises weapon actions.')
