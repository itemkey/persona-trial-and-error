"""Actual NPC stance clips from three views, eight movement directions and a shot."""
import math
from phase4_checks import *
captures=[];checks=[]
send(op='npcs',value=0);wait(lambda:not any(c['npc'] for c in state()['characters']))
send(op='clear_obstacles');send(op='npcs',value=1)
wait(lambda:any(c['npc'] and not c['dead'] for c in state()['characters']))
n=next(c for c in state()['characters'] if c['npc']);name=n['actor']
def npc():return next(c for c in state()['characters'] if c['actor']==name)
send(op='ai_pause',actor=name,paused=True);send(op='place_npc',actor=name,x=-1600,y=-1400,z=100,yaw=0)
send(op='slot',value=2);place(1,-2000,-2000,100)
for angle in (0,90,180):
    a=math.radians(angle);place(0,-1600+310*math.cos(a),-1400+310*math.sin(a),100,yaw=angle+180,pitch=-10)
    time.sleep(.4);send(op='record',seconds=5,fps=30)
    time.sleep(.4);send(op='npc_crouch',actor=name,value=True);time.sleep(1.8)
    send(op='npc_crouch',actor=name,value=False);wait(lambda:not state()['recording'],8)
    captures.append(dict(kind='stance',angle=angle,path=state()['captureDirectory']))
for angle in range(-180,180,45):
    send(op='place_npc',actor=name,x=-1600,y=-1400,z=100,yaw=0)
    place(0,-1230,-1400,100,yaw=180,pitch=-10);send(op='npc_crouch',actor=name,value=True);time.sleep(.5)
    send(op='record',seconds=2.5,fps=30);a=math.radians(angle)
    send(op='npc_move',actor=name,x=-1600+180*math.cos(a),y=-1400+180*math.sin(a),speed=120)
    wait(lambda:not state()['recording'],6)
    checks.append(dict(check=f'Crouch movement direction {angle}',passed=math.dist(vec(npc()['position'])[:2],[-1600,-1400])>80))
    captures.append(dict(kind='crouch_walk',angle=angle,path=state()['captureDirectory']))
report('phase7-npc-poses',checks,captures=captures)
assert all(c['passed'] for c in checks)
