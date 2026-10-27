"""Actual pellet damage and corpse loot; run after TestShotgun on rendered host."""
import math
from phase4_checks import *
checks=[]
def check(name,test,seconds=20):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False,state=state()));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def target():return next(c for c in state()['characters'] if c['npc'])
def aim_at_target():
    c=target();p=vec(c['position']);eye=vec(me()['position']);eye[2]+=68
    delta=[p[i]-eye[i] for i in range(3)];delta[2]+=20
    send(op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))))
try:
    check('A loaded shotgun is equipped',lambda:held()['chamber']==1 and held()['definition']=='DA_Shotgun')
    send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
    place(0,-150,1100,100,yaw=0)
    send(op='place_npc',actor=target()['actor'],x=250,y=1100,z=100,yaw=180)
    time.sleep(.6);aim_at_target();send(op='record',seconds=12);send(op='fire')
    check('Server pellets damage the NPC',lambda:target()['health']<100)
    for attempt in range(4):
        if target()['dead']:break
        send(op='reload',held=True);send(op='reload',held=False);wait(lambda:held()['tube']>0)
        send(op='pump');wait(lambda:held()['chamber']==1)
        aim_at_target();send(op='fire');time.sleep(.7)
    check('NPC dies from shotgun fire and remains in world',lambda:target()['dead'] and target()['health']==0)
    corpse_id=target()['persistentId']
    check('Body settles with a saved pose and bounded sparse loot',lambda:target()['poseBones']>0 and sum(x['quantity'] for x in target()['lootItems']) in (0,1,2))
    # Sparse corpse loot may be empty. Replayed requests must never create ammo.
    before=ammo();p=vec(target()['position']);place(0,p[0]-120,p[1],p[2]+90)
    loot=target()['lootItems'][0] if target()['lootItems'] else dict(id='no-corpse-ammo',quantity=0)
    send(op='loot',actor=target()['actor'],index=0,deposit=False,expectedId=loot['id'])
    check('Available corpse ammunition transfers without removing the body',lambda:ammo()==before+loot['quantity'] and target()['persistentId']==corpse_id and not target()['lootItems'])
    send(op='loot',actor=target()['actor'],index=0,deposit=False,expectedId=loot['id']);time.sleep(1)
    check('Replayed corpse-loot request cannot duplicate rounds',lambda:ammo()==before+loot['quantity'])
    wait(lambda:not state()['recording'],20);send(op='shot')
finally:report('phase4-combat',checks,capture=state().get('captureDirectory'))
