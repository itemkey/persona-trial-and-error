"""Focused checks on a fresh two-peer diagnostic hub, including a cold v5 migration.
Run: Scripts/RunLocal.ps1 -Players 2 -Diagnostics -Headless -RenderHost
Then the engine's bundled Python Scripts/TestInteractionFixes.py.
"""
import math
import shutil
import subprocess
from phase4_checks import *

checks = []
TOOLS = ('DA_HUB_Hammer', 'DA_HUB_Wrench', 'DA_HUB_Toolbox')
BURNERS = [(243, -25, 97.75), (243, -65, 97.75), (287, -25, 97.75), (287, -65, 97.75)]


def check(name, predicate, seconds=10):
    try:
        wait(predicate, seconds)
    except Exception:
        checks.append(dict(check=name, passed=False, state=state()))
        raise
    checks.append(dict(check=name, passed=True))
    print('PASS', name, flush=True)


def aim(role, target):
    eye = vec(me(role)['camera'])
    d = [target[i]-eye[i] for i in range(3)]
    send(role, op='look', yaw=math.degrees(math.atan2(d[1], d[0])),
         pitch=math.degrees(math.atan2(d[2], math.hypot(*d[:2]))))
    time.sleep(.35)


def prop(identity, role=0):
    return next(p for p in state(role)['props'] if p['itemId'] == identity)


def hub(identity, role=0):
    return next(h for h in state(role)['hub'] if h['id'] == identity)


def supplies():
    return {h['id']: h['items'] for h in state()['hub'] if 'Container_' in h['id']}


def take_pot(role, identity):
    at = vec(prop(identity)['position'])
    aim(role, [at[0], at[1], at[2]+12])
    send(role, op='interact')
    wait(lambda: held(role)['itemId'] == identity)


def open_door(name, index, role=0):
    send(role, op='close_loot')
    time.sleep(.8)
    h = hub(name)
    # Both leaves are on the short end, 600 cm from the container origin.
    centers = [vec(next(p for p in h['parts'] if p['mesh'] == part['id'])['center']) for part in h['compartments']]
    origin = vec(h['position'])
    middle = [(centers[0][i]+centers[1][i])/2 for i in range(3)]
    normal = [middle[i]-origin[i] for i in range(2)]
    length = math.hypot(*normal)
    at = centers[index]
    place(role, at[0]+normal[0]/length*175, at[1]+normal[1]/length*175, origin[2]+100)
    time.sleep(.5)
    aim(role, at)
    send(role, op='interact')
    wait(lambda: me(role)['lootPart'] == h['compartments'][index]['id'])


def restart(fixture=None):
    send(op='save')
    time.sleep(.7)
    before = state()
    identities = [me(i)['persistentId'] for i in (0, 1)]
    send(op='quit')
    time.sleep(3)
    shell = ['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File']
    subprocess.run(shell+[str(ROOT/'Scripts/StopLocal.ps1')], check=True, capture_output=True)
    save = ROOT/'Saved/SaveGames/Validation/Sectors'/(before['sector']+'.sav')
    if fixture:
        shutil.copy2(save, save.with_name(save.name+'.before-interaction-test'))
        shutil.copy2(fixture, save)
    subprocess.run(shell+[str(ROOT/'Scripts/RunLocal.ps1'), '-Players', '2', '-Menu',
                          '-Diagnostics', '-Headless', '-RenderHost'], check=True, capture_output=True)
    wait(lambda: all(state(i)['map'] == 'L_MainMenu' for i in (0, 1)), 180)
    send(op='menu', action='continue')
    send(op='menu', action='load', index=0)
    wait(lambda: state()['map'] == 'L_Hub' and me()['persistentId'] == identities[0], 180)
    send(1, op='connect')
    wait(lambda: me(1)['persistentId'] == identities[1] and state()['run']['loaded'], 180)
    for npc in [c for c in state()['characters'] if c['npc'] and not c['dead']]:
        send(op='ai_pause', actor=npc['actor'], paused=True)
    return save


try:
    check('Two peers in fresh hub', lambda: me(1).get('id', -1) >= 0 and state()['run']['phase'] == 0, 150)
    check('Workbench objects are absent from gameplay items', lambda: not any(p['definition'] in TOOLS for p in state()['props']))
    place(1, -600, 1100, 100)
    for x, y, z in [(-320, -27, 116), (-275, -40, 110), (-230, -45, 120)]:
        place(0, x, 100, 100)
        time.sleep(.4)
        aim(0, (x, y, z))
        send(op='interact')
        time.sleep(.35)
        check('E cannot pick up workbench decoration at '+str(x), lambda: all(s['quantity'] == 0 for s in me()['slots']))

    pot = next(p['itemId'] for p in state()['props'] if p['definition'] == 'DA_HUB_CookingPot')
    for role in (0, 1):
        place(1-role, -600, 1100, 100)
        place(role, 265, 105, 100)
        time.sleep(.6)
        for index, burner in enumerate(BURNERS):
            take_pot(role, pot)
            aim(role, burner)
            check(f'Peer {role} preview on burner {index}', lambda role=role: me(role)['placementPreview'])
            if role == 0 and index == 0:
                send(op='slot', value=2)
                check('Slot change hides preview', lambda: not me()['placementPreview'])
                send(op='slot', value=0)
                check('Equipping cookware restores preview', lambda: me()['placementPreview'])
                send(op='panel')
                check('Menu hides preview', lambda: not me()['placementPreview'])
                send(op='panel')
                check('Closing menu restores preview', lambda: me()['placementPreview'])
                send(op='shot')
                time.sleep(.8)
            send(role, op='key', key='G')
            check(f'Peer {role} G places original pot on burner {index}',
                  lambda role=role, burner=burner: prop(pot)['holder'] == -1 and
                  math.dist(vec(prop(pot)['position'])[:2], burner[:2]) < 1.5 and not me(role)['placementPreview'])
            time.sleep(.7)
            check('Placed pot replicated to peer', lambda: math.dist(vec(prop(pot)['position']), vec(prop(pot, 1)['position'])) < 2)

    send(op='fixture', player=state(1)['localPlayer'], definition='DA_HUB_CookingPot', quantity=1, store=True)
    wait(lambda: held(1)['definition'] == 'DA_HUB_CookingPot')
    extra = held(1)['itemId']
    aim(1, BURNERS[-1])
    check('Occupied burner blocks preview', lambda: me(1)['burnerTarget'] and not me(1)['placementValid'] and not me(1)['placementPreview'])
    send(1, op='key', key='G')
    time.sleep(.5)
    check('Occupied burner retains cookware', lambda: held(1)['itemId'] == extra)
    place(1, 265, 500, 100)
    time.sleep(.4)
    aim(1, BURNERS[0])
    check('Out of reach hides preview', lambda: not me(1)['placementPreview'] and not me(1)['burnerTarget'])
    send(1, op='look', yaw=0, pitch=0)
    send(1, op='gesture', seconds=.45)
    check('G away from burners still throws', lambda: prop(extra)['holder'] == -1 and me(1)['held'] == 'None')
    pot_position = vec(prop(pot)['position'])

    accept_trial()
    check('Arena loaded on both peers', lambda: all(state(i)['run']['loaded'] for i in (0, 1)), 90)
    send(op='board')
    time.sleep(2)
    send(op='lift')
    check('Two crates stocked once', lambda: state()['run']['lift'] == 1 and len(supplies()) == 2)
    expected = supplies()
    check('Supply balance unchanged', lambda: sum(r['quantity'] for rows in supplies().values() for r in rows if 'Shotgun' in r['definition']) == 2 and sum(r['quantity'] for rows in supplies().values() for r in rows if 'Ammo12GA' in r['definition']) == 72)
    check('Both doors expose the same stock on both peers', lambda: all(h['sharedInventory'] and all(p['items'] == h['items'] for p in h['compartments']) for role in (0, 1) for h in state(role)['hub'] if 'Container_' in h['id']))
    wait(lambda: state()['run']['phase'] == 2, 35)
    # Pause hostiles for an interaction check, without changing the run or loot.
    for npc in [c for c in state()['characters'] if c['npc']]:
        send(op='ai_pause', actor=npc['actor'], paused=True)
    names = sorted(supplies())
    for name in names:
        for index in (0, 1):
            open_door(name, index)
            check(name+' door '+str(index)+' opens independently', lambda name=name, index=index: sum(p['open'] for p in hub(name)['compartments']) == 1)
    name = names[0]
    open_door(name, 1)
    gun = next(r for r in hub(name)['items'] if 'Shotgun' in r['definition'])
    time.sleep(.4)
    send(op='click_loot', expectedId=gun['id'])
    check('Shotgun taken through the actual loot UI from right door', lambda: any(s['definition'] == 'DA_Shotgun' for s in me()['slots']))
    send(op='close_loot')
    check('Other container inventory unchanged', lambda: hub(names[1])['items'] == expected[names[1]])
    open_door(names[1], 0, 0)
    open_door(names[1], 1, 1)
    gun2 = next(r for r in hub(names[1])['items'] if 'Shotgun' in r['definition'])
    for role in (0, 1, 1):
        send(role, op='loot', actor=hub(names[1])['actor'], index=0, deposit=False, expectedId=gun2['id'])
    check('Concurrent doors transfer a shared item only once', lambda: sum(p['itemId'] == gun2['id'] for p in state()['props']) == 1 and not any(r['id'] == gun2['id'] for r in hub(names[1])['items']))
    for role in (0, 1):
        send(role, op='close_loot')
    remaining = supplies()
    send(op='legacy_interactions_save')
    fixture = DIAG/'Player0_legacy_interactions.sav'
    wait(lambda: fixture.exists())
    legacy = fixture.read_bytes()
    save = restart(fixture)
    check('Legacy migration keeps byte-exact backup', lambda: save.with_name(save.name+'.pre-interaction-fixes').read_bytes() == legacy)
    check('Version 6 merges supplies without replenishing taken guns', lambda: state()['saveVersion'] == 6 and supplies() == remaining)
    check('Legacy tools removed from world, hotbars and storage', lambda: not any(p['definition'] in TOOLS for p in state()['props']) and all(not any(t in r['definition'] for t in TOOLS) for h in state()['hub'] for r in h['items']))
    check('Placed pot survives save/load', lambda: math.dist(vec(prop(pot)['position']), pot_position) < 2)
    restart()
    check('Second load does not duplicate or refill supplies', lambda: supplies() == remaining)
    for role in (0, 1):
        send(op='kill_player', player=state(role)['localPlayer'])
    check('Wipe returns both players to hub', lambda: state()['run']['phase'] == 0 and all(me(i)['health'] > 0 for i in (0, 1)), 20)
    send(op='fixture', definition='DA_HUB_CookingPot', quantity=1, store=True)
    wait(lambda: held()['definition'] == 'DA_HUB_CookingPot')
    blocked_pot = held()['itemId']
    place(0, 265, -170, 100)
    time.sleep(.4)
    aim(0, BURNERS[0])
    check('Wall blocks placement preview', lambda: me()['burnerTarget'] and not me()['placementValid'] and not me()['placementPreview'])
    send(op='key', key='G')
    time.sleep(.4)
    check('G cannot place through a wall', lambda: held()['itemId'] == blocked_pot)
    send(op='clear_inventory')
    accept_trial()
    wait(lambda: all(state(i)['run']['loaded'] for i in (0, 1)), 90)
    send(op='board')
    time.sleep(2)
    send(op='lift')
    check('Next trial refills each container exactly once', lambda: state()['run']['lift'] == 1 and sum(r['quantity'] for rows in supplies().values() for r in rows if 'Shotgun' in r['definition']) == 2 and sum(r['quantity'] for rows in supplies().values() for r in rows if 'Ammo12GA' in r['definition']) == 72)
finally:
    report('interaction-fixes', checks)
