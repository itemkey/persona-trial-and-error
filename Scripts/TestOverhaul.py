"""Movement, real input, physics and combat regression on opt-in diagnostics.
Run TestShotgun.py first on a rendered host; --players 2 also tests prediction.
"""
import argparse,math,shutil
from phase4_checks import *

parser=argparse.ArgumentParser()
parser.add_argument('--players',type=int,default=1)
parser.add_argument('--report',default='phase5-gameplay')
args=parser.parse_args()
checks=[];measurements={}
def check(name,predicate,seconds=8):
    try:wait(predicate,seconds)
    except Exception:
        checks.append(dict(check=name,passed=False,state=state()));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def npc():return next(c for c in state()['characters'] if c['npc'] and not c['dead'])
def sample(seconds,role=0):
    out=[];end=time.monotonic()+seconds
    while time.monotonic()<end:
        s=state(role);out.append(dict(utc=s['utc'],character=me(role)));time.sleep(.025)
    return list({s['utc']:s for s in out}.values())
def capture(name):
    send(op='shot');time.sleep(.5)
    src=ROOT/'Artifacts/Previews/Player0_game.png'
    if os.environ.get('PERSONA_PACKAGED')=='1':src=ROOT/'Artifacts/Windows/PersonaTrial/Artifacts/Previews/Player0_game.png'
    if src.exists():shutil.copy2(src,ROOT/'Artifacts/Previews'/('PERSONA_05_'+name+'.png'))
def load_gun(role=0):
    if held(role)['chamber']!=1:
        if held(role)['tube']==0:
            send(role,op='reload',held=True);send(role,op='reload',held=False)
            wait(lambda:held(role)['tube']>0)
        send(role,op='pump');wait(lambda:held(role)['chamber']==1)

try:
    check('Version 4 save format loaded',lambda:state()['saveVersion']==4)
    send(op='npcs',value=0)
    send(op='vitals',health=100)
    for role in range(1,args.players):place(role,-2500,-2500+role*150,100)
    for role in range(min(2,args.players)):
        send(role,op='crouch',value=False);send(role,op='aim',held=False);send(role,op='sprint',value=False)
        send(role,op='sample_interval',seconds=.03)
        if role:
            send(op='fixture',player=state(role)['localPlayer'],definition='DA_Shotgun',quantity=1,store=True)
        place(role,-600,1100,100,yaw=0);time.sleep(.4)
        check(f'Player {role}: cosmetic camera motion cannot change raw aim',lambda role=role:abs(float(me(role)['view'].split('Y=')[1].split()[0]))<.01)
        send(role,op='move',x=1,y=0)
        check(f'Player {role}: normal speed reaches 600 cm/s',lambda role=role:580<me(role)['speed']<620)
        send(role,op='sprint',value=True)
        check(f'Player {role}: sprint reaches 980 cm/s',lambda role=role:950<me(role)['speed']<1010)
        send(role,op='move',x=0,y=0)
        check(f'Player {role}: braking settles without drift',lambda role=role:me(role)['speed']<1,2)
        send(role,op='aim',held=True)
        check(f'Player {role}: ADS cancels sprint and limits speed',lambda role=role:not me(role)['sprinting'] and me(role)['maxSpeed']==300)
        send(role,op='move',x=-1,y=0)
        check(f'Player {role}: moving ADS reaches 300 cm/s',lambda role=role:285<me(role)['speed']<315)
        send(role,op='move',x=0,y=0)
        check(f'Player {role}: aiming FOV settles at 85 degrees',lambda role=role:abs(me(role)['fov']-85)<.2)
        if role==0:capture('ads')
        send(role,op='aim',held=False);send(role,op='sprint',value=False)
        place(role,-150,1100,100);time.sleep(.4)
        standing=vec(me(role)['camera'])[2]
        send(role,op='crouch',value=True);samples=sample(.6,role)
        check(f'Player {role}: crouch reaches expected height and speed',lambda role=role:me(role)['crouched'] and me(role)['maxSpeed']==280 and abs(vec(me(role)['camera'])[2]-(standing-53))<3)
        heights=[vec(s['character']['camera'])[2] for s in samples]
        measurements[f'crouch_{role}']=heights
        assert len(heights)>3
        check(f'Player {role}: crouch camera has intermediate positions',lambda:any(standing-45<z<standing-5 for z in heights))
        send(role,op='aim',held=True)
        check(f'Player {role}: crouched ADS uses 180 cm/s',lambda role=role:me(role)['maxSpeed']==180)
        send(role,op='aim',held=False)
        send(role,op='crouch',value=False)
        check(f'Player {role}: standing restores camera height',lambda role=role:not me(role)['crouched'] and abs(vec(me(role)['camera'])[2]-standing)<3)
        send(role,op='jump')
        check(f'Player {role}: jump enters falling movement',lambda role=role:me(role)['falling'])
        send(role,op='move',x=0,y=1);time.sleep(.2);send(role,op='move',x=0,y=0)
        check(f'Player {role}: controlled landing returns to ground',lambda role=role:not me(role)['falling'])
        send(role,op='sample_interval',seconds=.1)
        place(role,-2500,-2500+role*150,100)

    # Real game input and Slate focus use the same Tab precedence.
    send(op='close_loot');place(0,-480,-480,100,yaw=-90,pitch=-20)
    chest=next(h for h in state()['hub'] if h['id']=='HUB_Chest_01')
    send(op='open_loot',actor=chest['actor'])
    check('Container opens its loot interface',lambda:me()['lootTarget']==chest['actor'])
    send(op='key',key='Tab')
    check('Tab closes container without opening session panel',lambda:me()['lootTarget']=='None' and not state()['panelOpen'])
    send(op='key',key='Tab')
    check('Tab opens session panel when loot is closed',lambda:state()['panelOpen'])
    send(op='key',key='Tab');wait(lambda:not state()['panelOpen'])
    # Deposit a shell stack to ensure a focusable loot row.
    slot=next(i for i,s in enumerate(me()['slots']) if s['definition']=='DA_Ammo12GA')
    prop=next(p for p in state()['props'] if p['actor']==me()['slots'][slot]['actor'])
    send(op='loot',actor=chest['actor'],index=slot,deposit=True,expectedId=prop['itemId'])
    send(op='open_loot',actor=chest['actor']);wait(lambda:me()['lootTarget']==chest['actor'])
    send(op='focus_loot');time.sleep(.3);send(op='key',key='Tab',ui=True)
    check('Tab closes loot with a button focused in Slate',lambda:me()['lootTarget']=='None' and not state()['panelOpen'])
    item=next(h for h in state()['hub'] if h['id']=='HUB_Chest_01')['items'][0]
    send(op='loot',actor=chest['actor'],index=0,deposit=False,expectedId=item['id'])
    wait(lambda:ammo()>0)

    # Collision fixture: two-centimetre wall, independent of existing architecture.
    send(op='clear_inventory')
    for fps in (30,60,120):
        send(op='console',value=f't.MaxFPS {fps}')
        for definition in ('DA_Shotgun','DA_Ammo12GA','DA_HUB_Hammer'):
            send(op='clear_obstacles');place(0,-150,1100,100,yaw=0)
            send(op='obstacle',x=200,y=1100,z=150,sx=2,sy=900,sz=300)
            send(op='fixture',definition=definition,quantity=1,store=True)
            check(f'{fps} FPS: {definition} equipped with CCD',lambda:held()['definition']==definition and held()['ccd'])
            item_id=held()['itemId'];send(op='charge',held=True);time.sleep(1.25);send(op='charge',held=False)
            check(f'{fps} FPS: charged release leaves inventory',lambda:all(s['quantity']==0 for s in me()['slots']))
            time.sleep(.65)
            prop=next(p for p in state()['props'] if p['itemId']==item_id)
            check(f'{fps} FPS: {definition} stays in front of thin wall and above floor',lambda p=prop:vec(p['position'])[0]<201 and vec(p['position'])[2]>-5)
    send(op='clear_obstacles');send(op='console',value='t.MaxFPS 60')
    # An oversized weapon near a wall must release safely or stay held.
    place(0,100,150,100,yaw=0)
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True);wait(lambda:held()['definition']=='DA_Shotgun')
    near_id=held()['itemId'];send(op='charge',held=True);time.sleep(1.25);send(op='charge',held=False);time.sleep(.5)
    check('Flush-wall release never teleports through the wall',lambda:any(p['itemId']==near_id and vec(p['position'])[0]<140 for p in state()['props']))

    # Explicit attack windows and region damage; these are regression fixtures,
    # not counted as survival playtesting.
    send(op='clear_inventory');send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',definition='DA_Ammo12GA',quantity=20,store=True)
    place(0,-150,1100,100,yaw=0);send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
    enemy=npc()['actor'];send(op='ai_pause',actor=enemy,paused=True)
    send(op='place_npc',actor=enemy,x=-10,y=1100,z=100,yaw=180)
    send(op='vitals',health=100);send(op='sample_interval',seconds=.03)
    send(op='attack',actor=enemy,variant=0);time.sleep(.12)
    check('Melee windup does not deal immediate contact damage',lambda:me()['health']==100)
    check('Normal melee strike deals exactly 35 damage',lambda:me()['health']==65)
    check('Confirmed player damage drives visible feedback',lambda:me()['damageFlash']>0)
    capture('damage');time.sleep(1)
    check('Attack cannot repeatedly damage during recovery',lambda:me()['health']==65)
    send(op='vitals',health=100)
    send(op='attack',actor=enemy,variant=3)
    check('Heavy strike has a longer telegraph',lambda:npc()['attacking'] and npc()['attackWindup']>=.7 and me()['health']==100)
    check('Heavy strike deals exactly 55 damage',lambda:me()['health']==45);time.sleep(1)
    send(op='vitals',health=100)
    send(op='obstacle',x=-80,y=1100,z=150,sx=8,sy=500,sz=300)
    send(op='attack',actor=enemy,variant=0);time.sleep(1.5)
    check('Melee cannot damage through cover',lambda:me()['health']==100)
    send(op='clear_obstacles')
    send(op='vitals',actor=enemy,health=1000)
    for region,bone in enumerate(('spine_02','head','upperarm_l','upperarm_r','thigh_l','thigh_r')):
        before=npc()['hitSequence'];send(op='hit',actor=enemy,bone=bone,damage=12)
        check(f'{bone}: correct replicated hit region',lambda region=region,before=before:npc()['hitSequence']>before and npc()['hitRegion']==region)
        if region>=4:
            check(f'{bone}: temporary leg impairment',lambda:npc()['legScale']<1)
        time.sleep(1.6)
        check(f'{bone}: movement impairment expires',lambda:npc()['legScale']==1)
    send(op='sample_interval',seconds=.1)

    # Perception must lose a hidden player even after acquiring them.
    send(op='place_npc',actor=enemy,x=600,y=1100,z=100,yaw=180)
    place(0,-300,1100,100,yaw=0);send(op='ai_pause',actor=enemy,paused=False)
    check('AI acquires a visible player',lambda:npc().get('targetVisible',False))
    send(op='obstacle',x=100,y=1100,z=250,sx=15,sy=1600,sz=500)
    check('Opaque cover breaks target visibility',lambda:not npc().get('targetVisible',True))
    check('AI searches using last-known information',lambda:npc().get('target')=='None' and npc().get('tactic') in (1,7,0))
    send(op='ai_pause',actor=enemy,paused=True);send(op='clear_obstacles')
    send(op='place_npc',actor=enemy,x=250,y=1100,z=100,yaw=180)
    place(0,-150,1100,100,yaw=0);load_gun()
    before=npc()['health'];send(op='fire')
    check('Actual pellets still damage skeletal targets',lambda:npc()['health']<before)
    send(op='save')
    capture('combat')
finally:
    for role in range(min(args.players,2)):
        try:send(role,op='move',x=0,y=0);send(role,op='sprint',value=False);send(role,op='aim',held=False)
        except Exception:pass
    report(args.report,checks,measurements=measurements)
