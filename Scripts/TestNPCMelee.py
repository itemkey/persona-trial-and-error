"""Real limb contacts and continuous AI approach; no injected damage."""
import math
from phase4_checks import *
checks=[];measurements={}
def enemy():return next(c for c in state()['characters'] if c['npc'] and not c['dead'])
def check(name,condition):
    checks.append(dict(check=name,passed=bool(condition)));print('PASS' if condition else 'FAIL',name,flush=True)
    assert condition,name
def setup(x=100):
    send(op='npcs',value=0);wait(lambda:not any(c['npc'] for c in state()['characters']))
    send(op='npcs',value=1);wait(lambda:any(c['npc'] for c in state()['characters']))
    name=enemy()['actor'];send(op='ai_pause',actor=name,paused=True)
    send(op='place_npc',actor=name,x=x,y=1100,z=100,yaw=180)
    place(0,0,1100,100,yaw=0);place(1,-800,1600,100);send(op='vitals',health=10000)
    time.sleep(.5);return name
try:
    wait(lambda:all(me(i).get('id',-1)>=0 for i in (0,1)),180)
    send(op='close_loot');send(op='clear_obstacles');send(op='sample_interval',seconds=.02)
    for variant in range(4):
        name=setup(95);health=me()['health'];send(op='attack',actor=name,variant=variant)
        samples=[];end=time.monotonic()+1.5
        while time.monotonic()<end:
            c=enemy();samples.append(dict(t=c['attackElapsed'],health=me()['health']));time.sleep(.02)
        damage=health-me()['health'];measurements['strike_'+str(variant)]=samples
        check(f'Variant {variant}: one real limb contact, unchanged damage',damage==(55 if variant==3 else 35))
        hit=next(s['t'] for s in samples if s['health']<health)
        check(f'Variant {variant}: contact occurs inside active window',(.45 if variant==3 else .22)-.03<=hit<=(.45 if variant==3 else .22)+.23)
    name=setup(95);health=me()['health'];send(op='attack',actor=name,variant=0)
    time.sleep(.06);send(op='move',x=0,y=1);time.sleep(.8);send(op='move',x=0,y=0)
    check('Player can evade during windup',me()['health']==health)
    place(0,0,1100,100);time.sleep(.4)
    check('An expired missed strike never damages later',me()['health']==health)
    name=setup(95);send(op='obstacle',x=47,y=1100,z=100,sx=4,sy=300,sz=200)
    time.sleep(.2);health=me()['health'];send(op='attack',actor=name,variant=3);time.sleep(1.5)
    check('A wall blocks limb damage',me()['health']==health);send(op='clear_obstacles')
    for seed in range(3):
        name=setup(700);health=me()['health'];send(op='ai_pause',actor=name,paused=False,seed=seed);send(op='noise')
        samples=[];end=time.monotonic()+15;first=None
        while time.monotonic()<end and me()['health']==health:
            c=enemy();samples.append(c)
            if c['attacking'] and first is None:first=c
            time.sleep(.02)
        measurements['approach_'+str(seed)]=samples
        check(f'Seed {seed}: first approach delivers a real contact attack',me()['health']<health)
        check(f'Seed {seed}: first windup starts while approaching',first and first['speed']>30 and first['attackVariant']<3 and abs(first['attackWindup']-.22)<.01)
    # Four opponents must manoeuvre when both attack slots are occupied.
    send(op='npcs',value=4);wait(lambda:sum(c['npc'] and not c['dead'] for c in state()['characters'])==4)
    for i,c in enumerate(c for c in state()['characters'] if c['npc'] and not c['dead']):
        send(op='place_npc',actor=c['actor'],x=200+i*80,y=1000+i*100,z=100,yaw=180)
        send(op='ai_pause',actor=c['actor'],paused=False)
    health=me()['health'];time.sleep(8)
    check('Multiple NPCs continue attacking',me()['health']<health)
finally:
    send(op='move',x=0,y=0);send(op='clear_obstacles');send(op='sample_interval',seconds=.1)
    report('phase7-melee',checks,measurements=measurements)
