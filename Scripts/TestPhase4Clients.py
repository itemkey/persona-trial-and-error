"""Remote shotgun authority, rejoin and a new spectator during Shooting.
Start four diagnostic processes in menus; host and Player1 connected beforehand.
"""
import math
from phase4_checks import *
checks=[]
def check(name,test,seconds=25):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def target():return next(c for c in state()['characters'] if c['npc'])
try:
    identity=me(1)['persistentId'];pid=state(1)['localPlayer']
    send(op='fixture',player=pid,definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',player=pid,definition='DA_Ammo12GA',quantity=20,store=True)
    wait(lambda:held(1)['definition']=='DA_Shotgun' and ammo(1)==20)
    send(1,op='reload',held=True);send(1,op='reload',held=False);wait(lambda:held(1)['tube']==1)
    send(1,op='pump');wait(lambda:held(1)['chamber']==1)
    send(op='npcs',value=0);time.sleep(.6);send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
    place(0,-800,1100,100);place(1,-150,1100,100,yaw=0)
    send(op='place_npc',actor=target()['actor'],x=200,y=1100,z=100,yaw=180);time.sleep(.6)
    p=vec(target()['position']);eye=vec(me(1)['position']);eye[2]+=68
    d=[p[i]-eye[i] for i in range(3)];d[2]+=20
    send(1,op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2]))));send(1,op='fire')
    check('Remote client shot damages authoritative NPC and consumes chamber once',lambda:target()['health']<100 and held(1)['chamber']==2 and ammo(1)==19)
    send(op='npcs',value=0);time.sleep(1);send(op='decision',accept=True)
    wait(lambda:state()['run']['loaded'] and state(1)['run']['loaded'],60)
    send(op='board');time.sleep(2);send(op='lift');wait(lambda:state()['run']['phase']==2,35)
    gun=held(1)['itemId'];send(2,op='connect')
    check('New participant enters ongoing trial as spectator',lambda:state(2)['map']=='L_Hub' and me(2)['spectator'] and not me(2)['armsVisible'],60)
    send(1,op='leave');wait(lambda:state(1)['map']=='L_MainMenu')
    check('Disconnected participant retains one abandoned body',lambda:len([c for c in state()['characters'] if c['persistentId']==identity and c['offline']])==1)
    send(1,op='connect')
    check('Reconnect restores the same living subject and spent shotgun',lambda:state(1)['map']=='L_Hub' and me(1)['persistentId']==identity and not me(1)['spectator'] and held(1)['itemId']==gun and held(1)['chamber']==2,60)
    send(op='kill_player',player=state(1)['localPlayer'])
    check('Killed participant remains dead without respawning into wave',lambda:me(1)['dead'] and not me(1)['armsVisible'])
    send(op='kill_player',player=state()['localPlayer'])
    check('Wipe restores players and new spectator to hub',lambda:state()['run']['phase']==0 and all(me(i)['health']>0 and not me(i)['spectator'] for i in range(3)),20)
finally:report('phase4-clients',checks)
