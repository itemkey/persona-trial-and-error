"""Death in different stances/locations, frame continuity and authoritative poses."""
import math
from phase4_checks import *
checks=[]; evidence={}

def check(name,predicate,seconds=30):
    try: wait(predicate,seconds)
    except Exception:
        checks.append(dict(check=name,passed=False,state=state())); raise
    checks.append(dict(check=name,passed=True)); print('PASS',name,flush=True)

def npc(identity,role=0): return next(c for c in state(role)['characters'] if c['persistentId']==identity)

try:
    check('Two peers ready for death cases',lambda:me(1).get('id',-1)>=0,300)
    send(op='clear_inventory'); send(op='vitals',health=10000); send(op='sample_interval',seconds=.04)
    for scenario,bone in [('standing','head'),('standing','spine_02'),('crouched','thigh_l'),('running','upperarm_r'),('wall','spine_02'),('air','head')]:
        send(op='npcs',value=0); send(op='npcs',value=1)
        wait(lambda:any(c['npc'] and not c['dead'] for c in state()['characters']))
        c=next(c for c in state()['characters'] if c['npc']); identity=c['persistentId']; name=c['actor']
        send(op='ai_pause',actor=name,paused=True)
        place(0,-250,1100,100,yaw=0); place(1,-600,1300,100,yaw=-20)
        send(op='place_npc',actor=name,x=100,y=1100,z=30000 if scenario=='air' else 100,yaw=180)
        if scenario=='crouched': send(op='npc_crouch',actor=name,value=True)
        if scenario=='wall': send(op='obstacle',x=155,y=1100,z=200,sx=20,sy=400,sz=400)
        if scenario=='running':
            send(op='ai_pause',actor=name,paused=False); send(op='noise')
            wait(lambda:npc(identity)['speed']>100,12)
        else: time.sleep(.5)
        send(op='hit',actor=name,bone=bone,damage=1000)
        check(f'{scenario}/{bone}: authoritative death',lambda:npc(identity)['dead'],4)
        if scenario=='air':
            time.sleep(3.2)
            check('Unsupported corpse is still simulated after three seconds',lambda:npc(identity)['ragdoll'] and npc(identity)['poseBones']==0 and vec(npc(identity)['pelvis'])[2]>3000,3)
        last=None; jump=0; end=time.monotonic()+40
        while time.monotonic()<end:
            c=npc(identity); head=vec(c['head'])
            if c['poseBones']:
                if last is not None: jump=math.dist(last,head)
                break
            last=head; time.sleep(.04)
        check(f'{scenario}/{bone}: freezes only in supported rest',lambda:npc(identity)['poseBones']>0 and not npc(identity)['ragdoll'])
        evidence[scenario+'/'+bone]=dict(freezeJumpCm=jump,head=npc(identity)['head'],support=npc(identity).get('bodySupport'))
        check(f'{scenario}/{bone}: freezing preserves the final pose',lambda:jump<8)
        check(f'{scenario}/{bone}: peer reproduces final head position',lambda:npc(identity,1)['poseBones']>0 and math.dist(vec(npc(identity)['head']),vec(npc(identity,1)['head']))<6)
        send(op='clear_obstacles')
finally:
    send(op='clear_obstacles'); send(op='sample_interval',seconds=.1)
    report('phase6-ragdoll-cases',checks,evidence=evidence)
