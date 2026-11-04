"""Expanded corpse targeting, range and occlusion on a rendered host + client."""
import math
import shutil
from phase4_checks import *

checks=[]

def check(name, predicate, seconds=12):
    try: wait(predicate, seconds)
    except Exception:
        checks.append(dict(check=name,passed=False,state=state())); raise
    checks.append(dict(check=name,passed=True)); print('PASS',name,flush=True)

def npc(role=0):
    return next(c for c in state(role)['characters'] if c['persistentId']==identity)

def aim(role, point):
    eye=vec(me(role)['camera']); d=[point[i]-eye[i] for i in range(3)]
    send(role,op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2]))))
    time.sleep(.35)

try:
    check('Both peers ready',lambda:me(1).get('id',-1)>=0,150)
    send(op='npcs',value=1)
    wait(lambda:any(c['npc'] for c in state()['characters']))
    c=next(c for c in state()['characters'] if c['npc']); identity=c['persistentId']; name=c['actor']
    send(op='ai_pause',actor=name,paused=True)
    place(0,-250,1100,100);place(1,-600,1400,100)
    send(op='place_npc',actor=name,x=100,y=1100,z=100,yaw=90)
    time.sleep(.5)
    aim(0,vec(npc()['pelvis']))
    send(op='interact');time.sleep(.4)
    check('Living NPC cannot be looted',lambda:me()['lootTarget']=='None')
    send(op='hit',actor=name,bone='spine_02',damage=1000)
    check('Corpse settles on both peers',lambda:all(npc(i)['poseBones']>0 for i in (0,1)),40)
    body=vec(npc()['pelvis'])
    for role in (0,1):
        place(1-role,-600,1500,100)
        place(role,body[0]-220,body[1],100)
        time.sleep(.5)
        for side in (-30,0,30):
            send(role,op='close_loot')
            aim(role,(body[0],body[1]+side,body[2]+10))
            check(f'Peer {role} targets body with {side} cm aim offset',lambda role=role:me(role)['interactionActor']==npc(role)['actor'])
            send(role,op='interact')
            check(f'Peer {role} opens corpse with {side} cm aim offset',lambda role=role:me(role)['lootTarget']==npc(role)['actor'])
        send(role,op='close_loot')
        place(role,body[0]-335,body[1],100)
        time.sleep(.5)
        aim(role,body)
        check(f'Peer {role} acquires corpse beyond old 280 cm reach',lambda role=role:math.dist(vec(me(role)['camera']),body)>300 and me(role)['interactionActor']==npc(role)['actor'])
        send(role,op='interact')
        check(f'Peer {role} opens and keeps the extended-range loot window',lambda role=role:me(role)['lootTarget']==npc(role)['actor'])
        time.sleep(1)
        assert me(role)['lootTarget']==npc(role)['actor']
        place(role,body[0]-650,body[1],100)
        time.sleep(.4)
        aim(role,body)
        check(f'Peer {role} loses corpse target and closes loot out of range',lambda role=role:me(role)['interactionActor']!=npc(role)['actor'] and me(role)['lootTarget']=='None')

    place(0,body[0]-220,body[1],100)
    place(1,-600,1500,100)
    send(op='obstacle',x=body[0]-110,y=body[1],z=100,sx=25,sy=500,sz=300)
    time.sleep(.6)
    aim(0,body)
    send(op='interact')
    check('Wall prevents broad targeting and opening corpse',lambda:me()['interactionActor']!=name and me()['lootTarget']=='None')
    send(op='clear_obstacles')
    check('Removing wall restores corpse target',lambda:me()['interactionActor']==name)
    # A precisely aimed regular interactable must keep priority over nearby bodies.
    chest=next(h for h in state()['hub'] if h['id']=='HUB_Chest_01')
    place(0,-480,-480,100)
    time.sleep(.4)
    aim(0,(-480,-650,60))
    send(op='interact')
    check('Regular storage still opens via its exact target',lambda:me()['lootTarget']==chest['actor'])
    send(op='close_loot')
    send(op='shot');time.sleep(1)
    shutil.copy2(ROOT/'Artifacts/Previews/Player0_game.png',ROOT/'Artifacts/Previews/loot-reach-clean-hud.png')
finally:
    send(op='clear_obstacles')
    report('corpse-loot-reach',checks)
