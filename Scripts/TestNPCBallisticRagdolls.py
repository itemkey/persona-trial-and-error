"""Repeated actual shotgun kills; checks flight, stretch, rest and peer poses."""
import argparse,math
from phase4_checks import *
p=argparse.ArgumentParser();p.add_argument('--count',type=int,default=100);p.add_argument('--start',type=int,default=0);args=p.parse_args()
checks=[];cases=[];completed=False
def npc(identity,role=0):return next(c for c in state(role)['characters'] if c['persistentId']==identity)
def chamber():
    if ammo()<6:send(op='fixture',definition='DA_Ammo12GA',quantity=24,store=True)
    if held()['chamber']==1:return
    if held()['tube']==0:
        send(op='reload',held=True);send(op='reload',held=False);wait(lambda:held()['tube']>0)
    send(op='pump');wait(lambda:held()['chamber']==1)
def aim(n,region):
    target=vec(n['head'] if region=='head' else n['footL'] if region=='leg' else n['pelvis'])
    target[2]+=8 if region=='head' else 48 if region=='leg' else 30
    velocity=vec(n.get('velocity','X=0 Y=0 Z=0'));target=[target[i]+velocity[i]*.04 for i in range(3)]
    eye=vec(me()['camera']);d=[target[i]-eye[i] for i in range(3)]
    send(op='look',yaw=math.degrees(math.atan2(d[1],d[0])),pitch=math.degrees(math.atan2(d[2],math.hypot(d[0],d[1]))))
try:
    wait(lambda:me(1).get('id',-1)>=0,120)
    send(op='close_loot');send(op='clear_inventory');send(op='npcs',value=0)
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',definition='DA_Ammo12GA',quantity=24,store=True)
    wait(lambda:held()['definition']=='DA_Shotgun');send(op='sample_interval',seconds=.02)
    for offset in range(args.count):
        index=args.start+offset
        scenario,region=[('standing','head'),('standing','torso'),('crouched','leg'),('running','torso'),('wall','torso'),('air','head'),('ledge','leg'),('mantle','head')][index%8]
        send(op='clear_obstacles');send(op='npcs',value=0);wait(lambda:not any(c['npc'] for c in state()['characters']))
        place(0,-220,1000,100,yaw=18);place(1,-650,1300,100);send(op='vitals',health=10000)
        chamber();send(op='npcs',value=1);wait(lambda:any(c['npc'] and not c['dead'] for c in state()['characters']))
        n=next(c for c in state()['characters'] if c['npc']);name=n['actor'];identity=n['persistentId']
        send(op='ai_pause',actor=name,paused=True)
        if scenario in ('ledge','mantle'):
            height=220 if scenario=='ledge' else 140
            send(op='obstacle',x=100,y=1100,z=height/2,sx=400,sy=400,sz=height)
            if scenario=='ledge':place(0,-50,1000,312,yaw=18)
        if scenario=='wall':send(op='obstacle',x=180,y=1100,z=180,sx=20,sy=400,sz=360)
        send(op='place_npc',actor=name,x=270 if scenario=='ledge' else 100,y=700 if scenario=='mantle' else 1100,z=1700 if scenario=='air' else 312 if scenario=='ledge' else 100,yaw=180)
        wait(lambda:abs(vec(npc(identity)['position'])[1]-(700 if scenario=='mantle' else 1100))<5,3)
        if scenario=='crouched':send(op='npc_crouch',actor=name,value=True)
        if scenario=='mantle':
            send(op='clear_obstacles');send(op='obstacle',x=-1700,y=-1400,z=70,sx=400,sy=400,sz=140)
            send(op='place_npc',actor=name,x=-1700,y=-1800,z=100,yaw=90)
            place(0,-1700,-1400,232,yaw=-90);place(1,-2200,-1400,100)
            time.sleep(1);send(op='ai_pause',actor=name,paused=False);send(op='noise')
            wait(lambda:npc(identity)['climbing'],25)
        elif scenario=='running':
            place(0,-500,1100,100);send(op='ai_pause',actor=name,paused=False);send(op='noise')
            wait(lambda:npc(identity)['speed']>150,15)
        elif scenario!='air':time.sleep(.5)
        before=npc(identity);initial_z=vec(before['pelvis'])[2];shots=0;early=[]
        while not npc(identity)['dead'] and shots<4:
            chamber();aim(npc(identity),region);time.sleep(.04);send(op='fire');shots+=1
            end=time.monotonic()+.2
            while time.monotonic()<end:
                early.append(npc(identity));time.sleep(.02)
        assert npc(identity)['dead'],f'{index}/{scenario}: shots failed to kill'
        max_z=initial_z;max_stretch=0;stretch_bone='';max_spin=0;last_head=None;freeze_jump=0
        for c in early:
            max_z=max(max_z,vec(c['pelvis'])[2])
            if c.get('boneStretch',0)>max_stretch:max_stretch=c['boneStretch'];stretch_bone=c.get('stretchBone')
            max_spin=max(max_spin,max((b['spin'] for b in c.get('bodyMotion',[])),default=0))
        rest_started=time.monotonic()
        end=time.monotonic()+35
        while time.monotonic()<end:
            c=npc(identity);head=vec(c['head']);max_z=max(max_z,vec(c['pelvis'])[2])
            if c.get('boneStretch',0)>max_stretch:max_stretch=c['boneStretch'];stretch_bone=c.get('stretchBone')
            max_spin=max(max_spin,max((b['spin'] for b in c.get('bodyMotion',[])),default=0))
            if c['poseBones']:
                freeze_jump=math.dist(last_head,head) if last_head else 0;break
            last_head=head;time.sleep(.03)
        c=npc(identity)
        assert c['poseBones']>0,dict(index=index,scenario=scenario,error='No supported rest',state=c)
        wait(lambda:npc(identity,1)['poseBones']>0,8)
        peer_error=math.dist(vec(c['head']),vec(npc(identity,1)['head']))
        supported=c['poseBones']>0 and not c['ragdoll'] and c.get('bodySupportNormal',0)>.45
        rise=max_z-initial_z
        handoff=c['ragdollStartPoseErrorCm'];handoff_angle=c['ragdollStartAngleErrorDegrees']
        ok=supported and abs(c.get('bodyMass',0)-80)<.1 and max_stretch<5 and freeze_jump<5 and peer_error<5 and handoff<.1 and handoff_angle<.1 and (scenario in ('air','mantle') or rise<45)
        item=dict(index=index,scenario=scenario,region=region,deathBone=c.get('deathBone'),shots=shots,
                  startedClimbing=before['climbing'],startedFalling=before['falling'],mass=c.get('bodyMass'),
                  restSeconds=time.monotonic()-rest_started,
                  handoffErrorCm=handoff,handoffAngleDegrees=handoff_angle,
                  maxPelvisRiseCm=rise,maxBoneStretchCm=max_stretch,stretchBone=stretch_bone,maxSpin=max_spin,freezeJumpCm=freeze_jump,peerErrorCm=peer_error,passed=ok)
        cases.append(item);checks.append(dict(check=f'{index+1}: {scenario}/{region} real shotgun death',passed=ok))
        report('phase7-ballistic-ragdolls',checks,cases=cases)
        print(('PASS' if ok else 'FAIL'),json.dumps(item),flush=True)
        assert ok,item
    completed=True
finally:
    send(op='clear_obstacles');send(op='sample_interval',seconds=.1)
    checks.append(dict(check=f'All {args.count} real-shot deaths completed',passed=completed))
    report('phase7-ballistic-ragdolls',checks,cases=cases,expected=args.count,completed=completed)
