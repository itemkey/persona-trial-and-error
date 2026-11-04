"""Focused 0.8 combat checks using actual game input and projectile traces."""
import argparse,math
from phase4_checks import *
p=argparse.ArgumentParser();p.add_argument('group',choices=['weapon','shotgun','pursuit']);p.add_argument('--count',type=int,default=20);args=p.parse_args()
checks=[];details=[]
def check(name,ok):
    checks.append(dict(check=name,passed=bool(ok)));print('PASS' if ok else 'FAIL',name,flush=True)
    assert ok,name
def server_player(role):return next(c for c in state()['characters'] if not c['npc'] and c['id']==me(role)['id'])
def server_ammo(role=0):return sum(s['quantity'] for s in server_player(role)['slots'] if s['definition']=='DA_Ammo12GA')
def gun(role):
    name=server_player(role)['held'];return next(p for p in state()['props'] if p['actor']==name)
def idle(role=0):return server_player(role)['weaponAction']==0
def equip(role=0):
    send(op='clear_inventory');wait(lambda:all(not any(s['quantity'] for s in c['slots']) for c in state()['characters'] if not c['npc']))
    for definition,quantity in [('DA_Shotgun',1),('DA_Ammo12GA',20)]:send(op='fixture',player=me(role)['id'],definition=definition,quantity=quantity,store=True)
    wait(lambda:held(role)['definition']=='DA_Shotgun' and ammo(role)>10);time.sleep(.25)
def tap_load(role=0):
    before=gun(role)['tube'];send(role,op='reload',held=True);send(role,op='reload',held=False)
    wait(lambda:idle(role) and gun(role)['tube']==before+1,4);time.sleep(.15)
def chamber(role=0):
    if server_ammo(role)<2:
        send(op='fixture',player=me(role)['id'],definition='DA_Ammo12GA',quantity=20,store=True)
        wait(lambda:server_ammo(role)>1)
    if gun(role)['chamber']==1:return
    if gun(role)['tube']==0:tap_load(role)
    send(role,op='pump');wait(lambda:idle(role) and gun(role)['chamber']==1,4);time.sleep(.2)
def npcs():return [c for c in state()['characters'] if c['npc']]
def spawn(count,locations):
    send(op='npcs',value=0);wait(lambda:not npcs());send(op='npcs',value=count);wait(lambda:len(npcs())==count)
    for c,pos in zip(npcs(),locations):
        send(op='ai_pause',actor=c['actor'],paused=True)
        send(op='place_npc',actor=c['actor'],x=pos[0],y=pos[1],z=100,yaw=180)
    time.sleep(.35)
try:
    wait(lambda:all(me(i).get('id',-1)>=0 for i in (0,1)),180)
    send(op='close_loot');send(op='clear_obstacles');send(op='npcs',value=0);send(op='sample_interval',seconds=.02)
    place(0,0,1100,100);place(1,-1200,2000,100);send(op='vitals',health=10000)
    if args.group=='weapon':
        durations=json.loads((ROOT/'SourceArt/Audio/combat_provenance.json').read_text())['assets'];pump_time=durations['Pump']['seconds']
        for role in (0,1):
            send(role,op='sample_interval',seconds=.02)
            equip(role);before=server_ammo(role);send(role,op='reload',held=True);time.sleep(.12)
            send(role,op='pump');send(role,op='pump');send(role,op='reload',held=False)
            wait(lambda:idle(role) and gun(role)['chamber']==1,5);time.sleep(.3)
            check(f'{role}: R then repeated F inserts and pumps exactly one shell',gun(role)['tube']==0 and server_ammo(role)==before-1)
            send(role,op='fire');wait(lambda:gun(role)['chamber']==2);tap_load(role)
            send(role,op='pump');time.sleep(.1);send(role,op='reload',held=True);time.sleep(.1);send(role,op='reload',held=False)
            wait(lambda:idle(role) and gun(role)['chamber']==1,4);time.sleep(.4)
            check(f'{role}: released R during pump does not load',gun(role)['tube']==0)
            send(role,op='pump');time.sleep(.1);send(role,op='reload',held=True)
            wait(lambda:server_player(role)['weaponAction']==1,4);send(role,op='reload',held=False)
            wait(lambda:idle(role),4)
            check(f'{role}: held R starts loading after pump',gun(role)['tube']==1)
            chamber(role);before=server_ammo(role);tube=gun(role)['tube'];fired=server_player(role)['shotsFired']
            send(role,op='reload',held=True);time.sleep(.12);send(role,op='fire');send(role,op='reload',held=False)
            wait(lambda:gun(role)['chamber']==2 and idle(role),3);time.sleep(.2)
            check(f'{role}: live shot interrupts insertion without losing ammo',server_ammo(role)==before and gun(role)['tube']==tube and server_player(role)['shotsFired']==fired+1)
            equip(role);before=server_ammo(role);send(role,op='reload',held=True);send(role,op='reload',held=False);time.sleep(.1);send(role,op='fire')
            wait(lambda:idle(role) and gun(role)['tube']==1,4)
            check(f'{role}: empty trigger preserves current insertion',server_ammo(role)==before-1 and gun(role)['chamber']==0)
            # Timing originates in the same client process as F. Network delay
            # shifts both RPCs together, preserving their separation.
            fired=server_player(role)['shotsFired'];send(role,op='pump');time.sleep(.1);send(role,op='fire')
            wait(lambda:idle(role) and gun(role)['chamber']==1,4);time.sleep(.25)
            check(f'{role}: early pump trigger neither fires nor misfires',server_player(role)['shotsFired']==fired)
            send(role,op='fire');wait(lambda:gun(role)['chamber']==2);tap_load(role)
            fired=server_player(role)['shotsFired'];send(role,op='pump');time.sleep(max(.1,pump_time-.11));send(role,op='fire')
            wait(lambda:gun(role)['chamber']==2 and idle(role),4)
            check(f'{role}: trigger at pump end fires exactly once',server_player(role)['shotsFired']==fired+1)
            chamber(role);send(role,op='safety');wait(lambda:gun(role)['safety']);before=server_ammo(role);tube=gun(role)['tube']
            send(role,op='reload',held=True);send(role,op='reload',held=False);time.sleep(.1);send(role,op='fire')
            wait(lambda:idle(role) and gun(role)['tube']==tube+1,4)
            check(f'{role}: safety click preserves insertion and live chamber',server_ammo(role)==before-1 and gun(role)['chamber']==1)
    elif args.group=='shotgun':
        equip();place(1,-2000,2000,100)
        for label,distance,offset,count in [('body3m',300,0,1),('body5m',500,0,1),('offset5m',500,15,1),('pair4m',400,0,2)]:
            successes=0
            for i in range(args.count):
                chamber();spawn(count,[(distance,1100+(j-.5)*45 if count==2 else 1100) for j in range(count)])
                place(0,0,1100,100);time.sleep(.15)
                target_z=sum(vec(c['pelvis'])[2]*.55+vec(c['head'])[2]*.45 for c in npcs())/count
                eye=vec(me()['camera'])
                send(op='look',yaw=math.degrees(math.atan2(offset,distance-eye[0])),pitch=math.degrees(math.atan2(target_z-eye[2],distance-eye[0])))
                time.sleep(.1);send(op='fire');wait(lambda:gun(0)['chamber']==2,3);time.sleep(.15)
                killed=sum(c['dead'] for c in npcs());successes+=killed==count
                details.append(dict(case=label,index=i,killed=killed,health=[c['health'] for c in npcs()],pellets=me()['pellets']))
                print(label,i+1,killed,flush=True)
            check(label+': required close-range kills',successes>=math.ceil(args.count*(.7 if count==2 else .9)))
        chamber();spawn(1,[(400,1100)]);place(0,0,1100,100,pitch=-7)
        send(op='obstacle',x=200,y=1100,z=100,sx=20,sy=500,sz=220);time.sleep(.2);send(op='fire');time.sleep(.3)
        check('Solid wall blocks every pellet',npcs()[0]['health']==100)
    else:
        for walking in (False,True):
            place(0,-1900,-2100,100,yaw=0);place(1,0,1100,100)
            spawn(1,[(-2600,-2100)]);name=npcs()[0]['actor'];send(op='place_npc',actor=name,x=-2600,y=-2100,z=100,yaw=0)
            send(op='ai_pause',actor=name,paused=False);send(op='noise');send(op='move',x=1 if walking else 0,y=0)
            health=me()['health'];samples=[];end=time.monotonic()+8
            while time.monotonic()<end and me()['health']==health:
                samples.append(dict(npc=npcs()[0],player=me()));time.sleep(.04)
            send(op='move',x=0,y=0)
            details.append(dict(walking=walking,samples=samples))
            check('Chase and hit '+('walking' if walking else 'stationary')+' player',me()['health']<health)
        place(0,-1600,-2100,100,yaw=0);spawn(1,[(-2300,-2100)]);name=npcs()[0]['actor']
        send(op='place_npc',actor=name,x=-2300,y=-2100,z=100,yaw=0);send(op='ai_pause',actor=name,paused=False);send(op='noise')
        send(op='sprint',value=True);send(op='move',x=1,y=0)
        samples=[];end=time.monotonic()+1.6
        while time.monotonic()<end:samples.append(dict(npc=npcs()[0],player=me()));time.sleep(.03)
        send(op='move',x=0,y=0);send(op='sprint',value=False)
        check('Burst exceeds player sprint speed',max(s['npc']['speed'] for s in samples)>1000)
        full_speed=[s for s in samples if s['npc']['speed']>1000 and s['player']['speed']>900]
        gap=lambda s:math.dist(vec(s['npc']['position']),vec(s['player']['position']))
        check('Burst closes distance on a sprinting player',len(full_speed)>3 and gap(full_speed[-1])<gap(full_speed[0])-30)
        details.append(dict(sprint=samples))
        # Acquire first, then hide the player without issuing a noise event.
        place(0,-1200,-2100,100);spawn(1,[(-2600,-2100)]);name=npcs()[0]['actor']
        send(op='place_npc',actor=name,x=-2600,y=-2100,z=100,yaw=0)
        send(op='ai_pause',actor=name,paused=False);send(op='noise');wait(lambda:npcs()[0]['targetVisible'],5)
        send(op='obstacle',x=-2100,y=-2100,z=130,sx=25,sy=1400,sz=260)
        wait(lambda:not npcs()[0]['targetVisible'],3);known=vec(npcs()[0]['lastKnown'])
        place(0,-1100,-1900,100);time.sleep(.25);hidden=npcs()[0]
        check('Brief occlusion retains target and honest last-known position',hidden['target']!='None' and hidden['investigating'] and math.dist(vec(hidden['lastKnown']),known)<5)
        send(op='clear_obstacles');health=me()['health'];wait(lambda:me()['health']<health,10)
        check('Pursuit resumes and reaches player after obstruction',True)
finally:
    for role in (0,1):send(role,op='reload',held=False);send(role,op='move',x=0,y=0);send(role,op='sample_interval',seconds=.1)
    send(op='clear_obstacles');report('phase8-'+args.group,checks,details=details)
