"""Corner throws, refused releases, melee dodging and bounded interruption."""
from phase4_checks import *
checks=[]

def check(name,test,seconds=6):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False,state=state()));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)

def enemy():return next(c for c in state()['characters'] if c['npc'])

try:
    send(op='npcs',value=0);send(op='close_loot');send(op='vitals',health=100)
    for fps in (30,60,120):
        send(op='console',value=f't.MaxFPS {fps}')
        for definition in ('DA_Shotgun','DA_Ammo12GA','DA_HUB_Hammer'):
            send(op='clear_inventory');send(op='clear_obstacles');place(0,-150,1100,100,yaw=40)
            send(op='obstacle',x=200,y=1100,z=150,sx=2,sy=900,sz=300)
            send(op='obstacle',x=0,y=1400,z=150,sx=700,sy=2,sz=300)
            send(op='fixture',definition=definition,quantity=1,store=True)
            wait(lambda:held()['definition']==definition);identity=held()['itemId']
            send(op='charge',held=True);time.sleep(1.3);send(op='charge',held=False);time.sleep(.8)
            check(f'{fps} FPS: {definition} cannot cross either corner plane',lambda:any(p['itemId']==identity and p['holder']==-1 and vec(p['position'])[0]<202 and vec(p['position'])[1]<1402 and vec(p['position'])[2]>-5 for p in state()['props']))
    send(op='console',value='t.MaxFPS 60');send(op='clear_inventory');send(op='clear_obstacles')
    place(0,-150,1100,100);send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    wait(lambda:held()['definition']=='DA_Shotgun');identity=held()['itemId']
    send(op='charge',held=True);time.sleep(1.3)
    send(op='obstacle',x=-150,y=1100,z=160,sx=500,sy=500,sz=500)
    send(op='charge',held=False);time.sleep(.4)
    check('Fully obstructed charged release retains the original item',lambda:held()['itemId']==identity)
    send(op='gesture',seconds=.08);time.sleep(.4)
    check('Fully obstructed ordinary drop retains the original item',lambda:held()['itemId']==identity)
    send(op='clear_obstacles');send(op='clear_inventory');place(0,-150,1100,100,yaw=0)
    send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
    actor=enemy()['actor'];send(op='ai_pause',actor=actor,paused=True)
    send(op='place_npc',actor=actor,x=-10,y=1100,z=100,yaw=180)
    send(op='attack',actor=actor,variant=3);wait(lambda:enemy()['attacking'])
    send(op='move',x=-1,y=0);time.sleep(.65);send(op='move',x=0,y=0);time.sleep(.8)
    check('Moving out during the heavy windup dodges the strike',lambda:me()['health']==100 and vec(me()['position'])[0]<-350)
    place(0,-150,1100,100,yaw=0);send(op='vitals',actor=actor,health=1000)
    send(op='attack',actor=actor,variant=3);wait(lambda:enemy()['attacking'])
    send(op='hit',actor=actor,bone='upperarm_l',damage=10)
    check('Arm hit interrupts the committed attack',lambda:not enemy()['attacking'] and enemy()['hitRegion']==2)
    time.sleep(1.6)
    check('Interrupted strike never applies delayed damage',lambda:me()['health']==100)
    send(op='hit',actor=actor,bone='thigh_r',damage=10);wait(lambda:enemy()['legScale']<1)
    start=time.monotonic()
    for _ in range(4):time.sleep(.2);send(op='hit',actor=actor,bone='thigh_r',damage=1)
    check('Repeated leg hits cannot extend impairment indefinitely',lambda:enemy()['legScale']==1,1)
    check('Leg impairment expires within its bounded interval',lambda:time.monotonic()-start<1.65)
finally:
    send(op='move',x=0,y=0);send(op='clear_obstacles');send(op='console',value='t.MaxFPS 60');send(op='npcs',value=0)
    report('phase5-adversarial',checks)
