"""Unobstructed observer views of NPC deaths from actual client-fired shots."""
import math
from phase4_checks import *
captures=[];checks=[]
send(op='clear_obstacles');send(op='slot',value=2);send(1,op='clear_inventory')
send(op='fixture',player=state(1)['localPlayer'],definition='DA_Shotgun',quantity=1,store=True)
send(op='fixture',player=state(1)['localPlayer'],definition='DA_Ammo12GA',quantity=24,store=True)
wait(lambda:held(1)['definition']=='DA_Shotgun')
for angle in (0,90,180):
    send(op='npcs',value=0);wait(lambda:not any(c['npc'] for c in state()['characters']))
    send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
    c=next(c for c in state()['characters'] if c['npc']);name=c['actor']
    send(op='ai_pause',actor=name,paused=True);send(op='place_npc',actor=name,x=100,y=1100,z=100,yaw=180)
    a=math.radians(angle);b=math.radians(angle+75)
    place(1,100+330*math.cos(a),1100+330*math.sin(a),100)
    place(0,100+330*math.cos(b),1100+330*math.sin(b),100,yaw=angle+255,pitch=-17)
    if held(1)['chamber']!=1:
        if held(1)['tube']==0:
            send(1,op='reload',held=True);send(1,op='reload',held=False);wait(lambda:held(1)['tube']>0)
        send(1,op='pump');wait(lambda:held(1)['chamber']==1)
    time.sleep(.5);c=next(c for c in state()['characters'] if c['actor']==name)
    eye=vec(me(1)['camera']);target=vec(c['head']);target[2]+=8;d=[target[i]-eye[i] for i in range(3)]
    send(1,op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2]))))
    send(op='record',seconds=8,fps=30);time.sleep(1);send(1,op='fire')
    wait(lambda:next(c for c in state()['characters'] if c['actor']==name)['dead'],5)
    checks.append(dict(check=f'Client shot kills NPC, direction {angle}',passed=True))
    wait(lambda:not state()['recording'],12)
    captures.append(dict(angle=angle,path=state()['captureDirectory']))
report('phase7-death-views',checks,captures=captures)
