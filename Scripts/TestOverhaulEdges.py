"""Clearance, steep throws, wall shots, corpse Tab, and ultrawide evidence."""
import math,shutil
from phase4_checks import *
checks=[]
def check(name,test,seconds=8):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False,state=state()));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def capture(name):
    send(op='shot');time.sleep(.4)
    base=ROOT/'Artifacts/Windows/PersonaTrial' if os.environ.get('PERSONA_PACKAGED')=='1' else ROOT
    shutil.copy2(base/'Artifacts/Previews/Player0_game.png',ROOT/f'Artifacts/Previews/PERSONA_05_{name}.png')
try:
    send(op='clear_inventory');send(op='npcs',value=0);send(op='vitals',health=100);send(op='clear_obstacles')
    send(op='crouch',value=True);place(0,-500,1100,65);time.sleep(.5)
    send(op='obstacle',x=-500,y=1100,z=155,sx=350,sy=350,sz=20)
    send(op='crouch',value=False);time.sleep(.6)
    check('Ceiling blocks standing without a camera pop',lambda:me()['crouched'] and vec(me()['camera'])[2]<130)
    send(op='clear_obstacles')
    check('Standing resumes smoothly after ceiling clears',lambda:not me()['crouched'] and vec(me()['camera'])[2]>150)
    for fps in (30,60,120):
        send(op='console',value=f't.MaxFPS {fps}')
        for pitch,name in ((75,'ceiling'),(-75,'floor')):
            send(op='clear_inventory');place(0,-150,1100,100,pitch=pitch)
            if pitch>0:send(op='obstacle',x=-150,y=1100,z=310,sx=1400,sy=1400,sz=2)
            send(op='fixture',definition='DA_HUB_Hammer',quantity=1,store=True);wait(lambda:held()['definition']=='DA_HUB_Hammer')
            identity=held()['itemId'];send(op='charge',held=True);time.sleep(1.25);send(op='charge',held=False);time.sleep(.6)
            check(f'{fps} FPS: steep charged throw respects {name}',lambda:all(-5<vec(p['position'])[2]<312 for p in state()['props'] if p['itemId']==identity))
            send(op='clear_obstacles')
    send(op='console',value='t.MaxFPS 60')
    send(op='clear_inventory');place(0,-150,1100,100)
    send(op='fixture',definition='DA_Ammo12GA',quantity=1,store=True);wait(lambda:held()['definition']=='DA_Ammo12GA')
    identity=held()['itemId'];send(op='sprint',value=True);send(op='move',x=1,y=0)
    wait(lambda:me()['speed']>900);send(op='charge',held=True);time.sleep(.35);send(op='charge',held=False);time.sleep(.2)
    check('Release during sprint retains forward momentum',lambda:any(p['itemId']==identity and p['holder']==-1 and vec(p['velocity'])[0]>500 for p in state()['props']))
    send(op='move',x=0,y=0);send(op='sprint',value=False)
    send(op='clear_inventory');send(op='fixture',definition='DA_Shotgun',quantity=1,store=True);send(op='fixture',definition='DA_Ammo12GA',quantity=10,store=True)
    send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
    enemy=next(c for c in state()['characters'] if c['npc'])['actor'];send(op='ai_pause',actor=enemy,paused=True)
    for pitch in (-65,65):
        place(0,-150,1100,100,pitch=pitch);send(op='place_npc',actor=enemy,x=250,y=1100,z=100,yaw=180)
        send(op='obstacle',x=-50,y=1100,z=160,sx=10,sy=700,sz=900)
        send(op='reload',held=True);send(op='reload',held=False);wait(lambda:held()['tube']>0)
        send(op='pump');wait(lambda:held()['chamber']==1);send(op='fire');wait(lambda:held()['chamber']==2)
        check(f'{pitch} degree wall shot cannot damage through obstruction',lambda:next(c for c in state()['characters'] if c['actor']==enemy)['health']==100)
        send(op='clear_obstacles')
    place(0,-150,1100,100);send(op='hit',actor=enemy,bone='spine_02',damage=1000)
    wait(lambda:next(c for c in state()['characters'] if c['actor']==enemy)['poseBones']>0)
    body=next(c for c in state()['characters'] if c['actor']==enemy);at=vec(body['position'])
    place(0,at[0]-120,at[1],100,yaw=0,pitch=-40);time.sleep(.4)
    # Aim at the saved pelvis rather than a now-disabled living capsule.
    eye=vec(me()['camera']);delta=[at[i]-eye[i] for i in range(3)]
    send(op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))));time.sleep(.3)
    send(op='interact');check('Dead body opens loot',lambda:me()['lootTarget']==enemy)
    send(op='key',key='Tab',ui=True);check('Tab closes dead-body loot and restores game input',lambda:me()['lootTarget']=='None' and not state()['panelOpen'])
    send(op='console',value='r.SetRes 2560x1080w');time.sleep(1);place(0,-150,1100,100);send(op='aim',held=True);time.sleep(.6)
    check('Ultrawide ADS keeps 85-degree camera and raw aiming line',lambda:state()['viewportWidth']==2560 and abs(me()['fov']-85)<.2 and abs(float(me()['view'].split('Y=')[1].split()[0]))<.01)
    capture('ads_ultrawide')
finally:
    send(op='move',x=0,y=0);send(op='sprint',value=False);send(op='aim',held=False);send(op='clear_obstacles');send(op='console',value='t.MaxFPS 60');send(op='console',value='r.SetRes 1920x1080w')
    report('phase5-edge-cases',checks)
