"""Per-door ray selection, isolated storage and concurrent viewers on two peers."""
import math
from phase4_checks import *

checks = []


def check(name, predicate, seconds=8):
    try:
        wait(predicate, seconds)
    except Exception:
        checks.append(dict(check=name, passed=False, state=state()))
        raise
    checks.append(dict(check=name, passed=True))
    print('PASS', name, flush=True)


def hub(name, role=0):
    return next(h for h in state(role)['hub'] if h['id'] == name)


def part(name, key, role=0):
    return next(p for p in hub(name, role)['compartments'] if p['id'] == key)


def aim(role, target):
    eye = vec(me(role)['camera'])
    delta = [target[i] - eye[i] for i in range(3)]
    send(role, op='look', yaw=math.degrees(math.atan2(delta[1], delta[0])),
         pitch=math.degrees(math.atan2(delta[2], math.hypot(*delta[:2]))))
    time.sleep(.25)


try:
    check('Two peers joined', lambda: me(1).get('id', -1) >= 0, 120)
    send(op='npcs', value=0)
    for role in (0, 1):
        send(role, op='close_loot')
        send(role, op='slot', value=2)
    place(1, -600, 1100, 100)
    fixtures = [('HUB_Refrigerator', (730, 540, 100)),
                ('HUB_Kitchen_WallCabinet', (515, 120, 100)),
                ('HUB_Kitchen_SinkUnit', (540, 140, 100))]
    for name, view in fixtures:
        for key in [p['id'] for p in hub(name)['compartments']]:
            place(0, *view)
            time.sleep(.4)
            target = vec(next(p for p in hub(name)['parts'] if p['mesh'] == key)['center'])
            aim(0, target)
            check(name + ' ray selects ' + key, lambda key=key: me()['interactionPart'] == key)
            send(op='interact')
            check(key + ' opens alone', lambda key=key: part(name, key)['open'] and sum(p['open'] for p in hub(name)['compartments']) == 1)
            check(key + ' selection reaches UI and peer', lambda key=key: me()['lootPart'] == key and part(name, key, 1)['open'])
            send(op='close_loot')
            check(key + ' closes independently on both peers', lambda: not any(p['open'] for p in hub(name)['compartments']) and not any(p['open'] for p in hub(name, 1)['compartments']))
            time.sleep(.65)

    name = 'HUB_Refrigerator'
    first, second = [p['id'] for p in hub(name)['compartments']]
    place(0, 730, 540, 100)
    time.sleep(.4)
    aim(0, vec(next(p for p in hub(name)['parts'] if p['mesh'] == first)['center']))
    send(op='interact')
    wait(lambda: me()['lootPart'] == first)
    send(op='fixture', definition='DA_Ammo12GA', quantity=9, store=True)
    wait(lambda: any(s['definition'] == 'DA_Ammo12GA' for s in me()['slots']))
    slot = next((i, s) for i, s in enumerate(me()['slots']) if s['definition'] == 'DA_Ammo12GA')
    before = sum(x['quantity'] for x in part(name, first)['items'])
    send(op='loot', actor=hub(name)['actor'], index=slot[0], deposit=True, expectedId=next(p['itemId'] for p in state()['props'] if p['actor']==slot[1]['actor']))
    check('Deposit changes selected inventory only', lambda: sum(x['quantity'] for x in part(name, first)['items']) > before and not part(name, second)['items'])
    time.sleep(.8)
    place(1, 730, 455, 100)
    time.sleep(.4)
    aim(1, vec(next(p for p in hub(name, 1)['parts'] if p['mesh'] == first)['center']))
    check('Second peer targets the open leaf', lambda: me(1)['interactionPart'] == first)
    send(1, op='interact')
    check('Two viewers share one selected compartment', lambda: me(1)['lootPart'] == first)
    send(op='close_loot')
    time.sleep(.7)
    check('First viewer leaving does not close second viewer door', lambda: part(name, first)['open'] and me(1)['lootPart'] == first)
    send(1, op='close_loot')
    check('Last viewer closes door', lambda: not part(name, first)['open'])
    time.sleep(.7)
    for role,key in ((0,first),(1,second)):
        aim(role, vec(next(p for p in hub(name,role)['parts'] if p['mesh']==key)['center']))
        send(role,op='interact')
        wait(lambda role=role,key=key:me(role)['lootPart']==key)
    check('Different compartments can be viewed simultaneously',lambda:part(name,first)['open'] and part(name,second)['open'])
    send(op='close_loot')
    check('Closing lower compartment preserves upper viewer',lambda:not part(name,first)['open'] and part(name,second)['open'])
    place(1,-600,1100,100)
    check('Walking away releases the last viewer and closes only its door',lambda:not part(name,second)['open'] and me(1)['lootTarget']=='None')
    place(0,730,650,100); time.sleep(.8)
    aim(0,(915,599,145))
    check('Aimed cabinet body has no compartment ID',lambda:me()['interactionPart']=='None')
    send(op='interact'); time.sleep(.4)
    check('Body interaction cannot open a random compartment',lambda:not any(p['open'] for p in hub(name)['compartments']) and me()['lootTarget']=='None')
    send(op='save')
    check('Compartment save format is version 6', lambda: state()['saveVersion'] == 6)
finally:
    for role in (0, 1):
        send(role, op='close_loot')
    report('phase6-compartments', checks)
