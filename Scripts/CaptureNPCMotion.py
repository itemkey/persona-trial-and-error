"""Observe live AI chase/turn/attack continuously; no animation replacement."""
import argparse,math
from phase4_checks import *
p=argparse.ArgumentParser();p.add_argument('--label',default='after');args=p.parse_args()
wait(lambda:me(1).get('id',-1)>=0,120)
send(op='npcs',value=0);wait(lambda:not any(c['npc'] for c in state()['characters']));send(op='npcs',value=1)
wait(lambda:any(c['npc'] and not c['dead'] for c in state()['characters']))
c=next(c for c in state()['characters'] if c['npc']);identity=c['persistentId']
send(op='ai_pause',actor=c['actor'],paused=True)
send(op='place_npc',actor=c['actor'],x=300,y=1100,z=100,yaw=180)
place(0,-700,1100,100,yaw=0,pitch=-6);place(1,-1600,2200,100)
send(op='vitals',health=10000);send(op='slot',value=2)
send(op='sample_interval',seconds=.02);time.sleep(.7)
send(op='record',seconds=20,fps=30)
trace_started=time.time()
if args.label!='before':send(op='motion_trace',seconds=20)
send(op='ai_pause',actor=c['actor'],paused=False);send(op='noise')
start=time.monotonic();samples=[];last_move=None
while time.monotonic()-start<20:
    t=time.monotonic()-start;s=state();n=next(c for c in s['characters'] if c['persistentId']==identity)
    samples.append(dict(t=t,utc=s['utc'],npc=n))
    # First approach, then moving pursuit, then close melee.
    move=0 if t<5 or t>15 else (.45 if t<10 else -.45)
    if move!=last_move:send(op='move',x=0,y=move);last_move=move
    eye=vec(me()['camera']);target=vec(n['head']);d=[target[i]-eye[i] for i in range(3)]
    send(op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2]-35,math.hypot(d[0],d[1]))))
    time.sleep(.04)
send(op='move',x=0,y=0);send(op='sample_interval',seconds=.1)
wait(lambda:not state()['recording'])
if args.label!='before':wait(lambda:(DIAG/'Player0_motion.json').stat().st_mtime>=trace_started,5)
report('phase7-motion-'+args.label,[],samples=samples,capture=state()['captureDirectory'])
