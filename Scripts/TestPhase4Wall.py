"""The visible barrel crosses a solid hub wall, but pellets must not."""
from phase4_checks import *
checks=[]
def check(name,test):
    try:wait(test,15)
    except Exception:checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
try:
    send(op='npcs',value=0);time.sleep(.6);send(op='npcs',value=1)
    wait(lambda:any(c['npc'] for c in state()['characters']))
    if held()['chamber']!=1:
        send(op='reload',held=True);send(op='reload',held=False);wait(lambda:held()['tube']>0)
        send(op='pump');wait(lambda:held()['chamber']==1)
    target=next(c for c in state()['characters'] if c['npc']);actor=target['actor']
    # Exported building wall is at X=139, Y=150; capsule fits at X=100,
    # while the roughly 94 cm camera-space barrel extends through the wall.
    place(0,100,150,100,yaw=0,pitch=-8)
    send(op='place_npc',actor=actor,x=450,y=150,z=100,yaw=180);time.sleep(.6)
    send(op='fire');check('Obstructed barrel still consumes its loaded shell',lambda:held()['chamber']==2)
    time.sleep(1)
    check('NPC behind solid wall receives no pellet damage',lambda:next(c for c in state()['characters'] if c['actor']==actor)['health']==100)
finally:report('phase4-wall-obstruction',checks)
