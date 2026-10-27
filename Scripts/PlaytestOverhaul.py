"""Rendered survival exercise using ordinary movement, aim, fire, pump and shell
loading. Deterministic input driver, not a human subjective acceptance signoff.
Run on a fresh offered trial with --players 1 or 4. No diagnostic combat damage.
"""
import argparse,math,shutil
from phase4_checks import *
p=argparse.ArgumentParser();p.add_argument('--players',type=int,default=1);p.add_argument('--seconds',type=float,default=90);args=p.parse_args()
samples=[];actions=[];captures=[];checks=[]
capture_root=(ROOT/'Artifacts/Windows/PersonaTrial' if os.environ.get('PERSONA_PACKAGED')=='1' else ROOT)/'.work/Capture'
recordings_before=set(capture_root.glob('Player0_*'))
def command(role,**c):send(role,**c);actions.append(dict(time=time.monotonic(),role=role,**c))
def capture(name):
    base=ROOT/'Artifacts/Windows/PersonaTrial' if os.environ.get('PERSONA_PACKAGED')=='1' else ROOT
    src=base/'Artifacts/Previews/Player0_game.png';dest=ROOT/f'Artifacts/Previews/PERSONA_05_{args.players}p_{name}.png'
    previous=src.stat().st_mtime_ns if src.exists() else 0
    send(op='shot');wait(lambda:src.exists() and src.stat().st_mtime_ns>previous,8)
    if src.exists():shutil.copy2(src,dest);captures.append(str(dest))
try:
    wait(lambda:state()['run']['phase']==0 and len([c for c in state()['characters'] if not c['npc']])>=args.players,150)
    # Authority fixture commands reset the entire group before deployment.
    send(op='npcs',value=0);send(op='clear_inventory');send(op='vitals',health=100)
    send(op='decision',accept=True);wait(lambda:state()['run']['loaded'],60)
    send(op='board');time.sleep(1);send(op='lift');wait(lambda:state()['run']['lift']==1)
    # Equipment allocation matches the run's starting supply; use fixtures only
    # during setup so the exercise focuses on sustained combat input.
    for role in range(args.players):
        send(op='fixture',player=state(role)['localPlayer'],definition='DA_Shotgun',quantity=1,store=True)
        rounds=min(40,(48+24*(args.players-1))//args.players)
        for quantity in (min(20,rounds),max(0,rounds-20)):
            if quantity:send(op='fixture',player=state(role)['localPlayer'],definition='DA_Ammo12GA',quantity=quantity,store=True)
        wait(lambda role=role,rounds=rounds:held(role)['definition']=='DA_Shotgun' and ammo(role)==rounds)
    wait(lambda:state()['run']['phase']==2,35)
    for role in range(args.players):
        place(role,-450+role*300,-1700,-12505,yaw=-90)
        command(role,op='reload',held=True)
    start=time.monotonic();next_capture=8;next_action=[0]*args.players;last_health=[100]*args.players
    send(op='record',seconds=20)
    while time.monotonic()-start<args.seconds and state()['run']['phase'] not in (0,6):
        now=time.monotonic();s=state();enemies=[c for c in s['characters'] if c['npc'] and not c['dead']]
        samples.append(dict(elapsed=now-start,run=s['run'],characters=s['characters'],frameMs=s['frameMs'],gameThreadMs=s['gameThreadMs'],renderThreadMs=s['renderThreadMs'],gpuMs=s['gpuMs']))
        for role in range(args.players):
            player=me(role)
            if not player or player['dead']:continue
            if player['health']<last_health[role] and role==0:capture('damage')
            last_health[role]=player['health']
            try:gun=held(role)
            except StopIteration:continue
            position=vec(player['position']);target=min(enemies,key=lambda c:sum((vec(c['position'])[i]-position[i])**2 for i in (0,1))) if enemies else None
            if target:
                at=vec(target['position']);delta=[at[i]-position[i] for i in range(3)];delta[2]-=40
                distance=math.hypot(delta[0],delta[1])
                command(role,op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],distance)))
                # Back away from committed strikes, then cross to another route.
                command(role,op='move',x=-.8 if distance<600 else .4,y=.55 if int((now-start)/5+role)%2 else -.55)
                command(role,op='aim',held=350<distance<1800 and gun['chamber']==1)
                command(role,op='sprint',value=distance<240 and player['stamina']>15)
            else:command(role,op='move',x=0,y=0)
            if now<next_action[role]:continue
            if player['weaponAction']!=0:
                if gun['tube']>=3:command(role,op='reload',held=False)
                continue
            if gun['chamber']==1 and target:
                # File commands are consumed on separate simulation frames.
                # Refresh tracking immediately before the trigger, rather than
                # firing at a position sampled before four movement commands.
                fresh=state();tracked=next((c for c in fresh['characters'] if c['actor']==target['actor'] and not c['dead']),None)
                if not tracked:continue
                eye=vec(me(role)['camera']);at=vec(tracked['position']);old=vec(target['position'])
                age=(datetime.datetime.fromisoformat(fresh['utc'].replace('Z','+00:00'))-datetime.datetime.fromisoformat(s['utc'].replace('Z','+00:00'))).total_seconds()
                if age>.02:
                    at=[at[i]+max(-110,min(110,(at[i]-old[i])*.12/age)) for i in range(3)]
                at[2]+=25;delta=[at[i]-eye[i] for i in range(3)];distance=math.hypot(*delta[:2])
                if distance<1600:
                    command(role,op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],distance)))
                    command(role,op='fire');next_action[role]=now+.35
            elif gun['tube']>0:
                command(role,op='pump');next_action[role]=now+.55
            elif ammo(role)>0:
                command(role,op='reload',held=True);next_action[role]=now+.8
        if now-start>next_capture:capture(f'combat_{int(next_capture)}');next_capture+=15
        time.sleep(.1)
    tactics=sorted({c.get('tactic') for s in samples for c in s['characters'] if c['npc'] and not c['dead'] and 'tactic' in c})
    kills=max((len([c for c in s['characters'] if c['npc'] and c['dead']]) for s in samples),default=0)
    received=any(c['health']<100 for s in samples for c in s['characters'] if not c['npc'])
    checks=[dict(check='Actual shotgun combat produced persistent corpses',passed=kills>0),dict(check='Enemies applied lethal-combat pressure',passed=received),dict(check='Multiple tactical states observed',passed=len(tactics)>=4)]
    print(dict(kills=kills,receivedDamage=received,tactics=tactics,duration=samples[-1]['elapsed'] if samples else 0),flush=True)
finally:
    for role in range(args.players):
        try:send(role,op='move',x=0,y=0);send(role,op='sprint',value=False);send(role,op='aim',held=False);send(role,op='reload',held=False)
        except Exception:pass
    recordings=set(capture_root.glob('Player0_*'))-recordings_before
    report(f'phase5-playtest-{args.players}p',checks,actions=actions,samples=samples,captures=captures,recording=str(max(recordings,key=lambda p:p.stat().st_mtime)) if recordings else None,note='Automated ordinary combat inputs; no god mode, damage injection or health refill after deployment. Initial equipment uses the baseline supply; crate stock is not consumed by this driver.')
