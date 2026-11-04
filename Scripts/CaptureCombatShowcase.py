"""Record the packaged shotgun's mechanical sounds, recoil and real impacts."""
import math
from phase4_checks import *

send(op='close_loot'); send(op='clear_inventory'); send(op='npcs',value=0)
send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
send(op='fixture',definition='DA_Ammo12GA',quantity=12,store=True)
wait(lambda:held()['definition']=='DA_Shotgun')
send(op='npcs',value=1); wait(lambda:any(c['npc'] for c in state()['characters']))
enemy=next(c for c in state()['characters'] if c['npc'])
send(op='ai_pause',actor=enemy['actor'],paused=True)
send(op='place_npc',actor=enemy['actor'],x=100,y=1100,z=100,yaw=180)
place(0,-220,1000,100,yaw=18,pitch=-6); place(1,-600,1300,100)
time.sleep(.8)
# Keep silent mixer buffers in the recording, otherwise UE's submix auto-disable
# removes quiet intervals and the resulting WAV drifts against wall-clock frames.
send(op='console',value='au.NeverDisableSubmixes 1')
time.sleep(.2)
send(op='record',seconds=20)
for _ in range(3):
    before=held()['tube']; send(op='reload',held=True); send(op='reload',held=False)
    wait(lambda:held()['tube']==before+1)
send(op='pump'); wait(lambda:held()['chamber']==1)
head=vec(next(c for c in state()['characters'] if c['actor']==enemy['actor'])['head'])
eye=vec(me()['camera']); d=[head[i]-eye[i] for i in range(3)]
yaw=math.degrees(math.atan2(d[1],d[0])); pitch=math.degrees(math.atan2(d[2],math.hypot(*d[:2])))
send(op='look',yaw=yaw,pitch=pitch); time.sleep(.3); send(op='fire')
time.sleep(.8); send(op='look',yaw=yaw,pitch=-18); time.sleep(2)
send(op='pump'); wait(lambda:held()['chamber']==1)
# The nearby concrete floor gives a clearly located surface impact.
send(op='look',yaw=70,pitch=-35); time.sleep(.3); send(op='fire')
time.sleep(1); send(op='pump'); wait(lambda:held()['chamber']==1)
send(op='aim',held=True); time.sleep(.6); send(op='look',yaw=70,pitch=-30)
time.sleep(.3); send(op='fire'); time.sleep(1); send(op='aim',held=False)
send(op='look',yaw=yaw,pitch=-18)
wait(lambda:not state()['recording'],25)
send(op='console',value='au.NeverDisableSubmixes 0')
report('phase6-showcase',[],capture=state()['captureDirectory'])
