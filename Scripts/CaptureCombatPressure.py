"""Short, timestamped 0.8 NPC stance and live-combat recordings."""
import math
from phase4_checks import *
captures=[]
wait(lambda:me(1).get('id',-1)>=0,180)
send(op='clear_obstacles');send(op='npcs',value=0);send(op='slot',value=2);place(1,-2000,2000,100)
send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
name=next(c['actor'] for c in state()['characters'] if c['npc'])
send(op='ai_pause',actor=name,paused=True);send(op='place_npc',actor=name,x=100,y=1100,z=100,yaw=180)
for angle in (0,90,180):
    a=math.radians(angle);place(0,100+300*math.cos(a),1100+300*math.sin(a),100,yaw=angle+180,pitch=-12)
    send(op='npc_crouch',actor=name,value=False);time.sleep(.4);send(op='record',seconds=4,fps=30)
    time.sleep(.4);send(op='npc_crouch',actor=name,value=True);time.sleep(1.9);send(op='npc_crouch',actor=name,value=False)
    wait(lambda:not state()['recording'],7);captures.append(dict(kind='stance',angle=angle,path=state()['captureDirectory']))
send(op='place_npc',actor=name,x=100,y=1100,z=100,yaw=180)
place(0,430,1100,100,yaw=180,pitch=-12);send(op='npc_crouch',actor=name,value=True);time.sleep(.4)
send(op='record',seconds=3,fps=30);send(op='npc_move',actor=name,x=100,y=1320,speed=180)
wait(lambda:not state()['recording'],6);captures.append(dict(kind='stance',angle=90,moving=True,path=state()['captureDirectory']))
send(op='clear_inventory');send(op='fixture',definition='DA_Shotgun',quantity=1,store=True);send(op='fixture',definition='DA_Ammo12GA',quantity=24,store=True)
wait(lambda:held()['definition']=='DA_Shotgun');send(op='reload',held=True);wait(lambda:held()['tube']>=4,6);send(op='reload',held=False)
wait(lambda:me()['weaponAction']==0);send(op='pump');wait(lambda:held()['chamber']==1)
send(op='npcs',value=0);wait(lambda:not any(c['npc'] for c in state()['characters']))
place(0,-1700,-1800,100,yaw=-90,pitch=-5);place(1,0,1100,100);send(op='vitals',health=10000)
send(op='npcs',value=3);wait(lambda:sum(c['npc'] for c in state()['characters'])==3)
names=[c['actor'] for c in state()['characters'] if c['npc']]
for i,n in enumerate(names):
    send(op='ai_pause',actor=n,paused=True);send(op='place_npc',actor=n,x=-1750+i*70,y=-2900-i*150,z=100,yaw=90)
send(op='record',seconds=16,fps=30)
for n in names:send(op='ai_pause',actor=n,paused=False)
send(op='noise');time.sleep(.65)
for i in range(3):
    live=[c for c in state()['characters'] if c['npc'] and not c['dead']]
    if not live:break
    c=min(live,key=lambda c:math.dist(vec(c['position']),vec(me()['position'])))
    eye=vec(me()['camera']);target=vec(c['pelvis']);target[2]+=25;d=[target[j]-eye[j] for j in range(3)]
    send(op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2]))))
    send(op='fire');time.sleep(.4);send(op='pump');wait(lambda:held()['chamber']==1,4);time.sleep(.3)
wait(lambda:not state()['recording'],20);captures.append(dict(kind='combat',path=state()['captureDirectory']))
report('phase8-captures',[],captures=captures)
