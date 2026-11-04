"""Real shotgun recoil/near miss, crouch and replicated death behaviour."""
import math
from phase4_checks import *

checks, captures = [], []


def check(name, predicate, seconds=10):
    try:
        wait(predicate, seconds)
    except Exception:
        checks.append(dict(check=name, passed=False, state=state()))
        raise
    checks.append(dict(check=name, passed=True))
    print('PASS', name, flush=True)


def load():
    if held()['chamber'] == 1:
        return
    if held()['tube'] == 0:
        send(op='reload', held=True)
        send(op='reload', held=False)
        wait(lambda: held()['tube'] > 0)
    send(op='pump')
    wait(lambda: held()['chamber'] == 1)


def npc(role=0):
    return next(c for c in state(role)['characters'] if c['npc'])


try:
    check('Two peers ready for combat presentation',lambda:me(1).get('id',-1)>=0,300)
    send(op='npcs', value=0)
    send(op='clear_inventory')
    send(op='fixture', definition='DA_Shotgun', quantity=1, store=True)
    send(op='fixture', definition='DA_Ammo12GA', quantity=24, store=True)
    wait(lambda: held()['definition'] == 'DA_Shotgun')
    send(op='vitals', health=1000)
    send(op='sample_interval', seconds=.02)
    variants = []
    for index in range(4):
        place(0, -2000, -2100, 100, yaw=0)
        # Outside the entire pellet cone plus the observer's arm hitboxes,
        # while remaining inside the near-miss radius.
        place(1, -1500, -2000, 100, yaw=180)
        time.sleep(.6)
        wait(lambda: math.dist(vec(me()['position'])[:2],[-2000,-2100])<2 and
             math.dist(vec(me(1)['position'])[:2],[-1500,-2000])<2,3)
        load()
        send(op='look', yaw=0, pitch=0)
        before = me(1)['nearMisses']
        health = me(1)['health']
        time.sleep(.25)
        send(op='fire')
        readings = []
        end = time.monotonic() + .9
        while time.monotonic() < end:
            readings.append(float(me()['view'].split('P=')[1].split()[0]))
            time.sleep(.02)
        variants.append(me()['shotSound'])
        check('Shot %d: strong impulse and residual aim displacement' % index,
              lambda: max(readings) > 6 and 3.7 < readings[-1] < 4.3)
        check('Shot %d: one near-miss sound without damage' % index,
              lambda: me(1)['nearMisses'] == before + 1 and me(1)['health'] == health)
    check('All four fire recordings used without adjacent repeat', lambda: len(set(variants)) == 4)

    send(op='npcs', value=1)
    wait(lambda: any(c['npc'] for c in state()['characters']))
    name = npc()['actor']
    send(op='ai_pause', actor=name, paused=True)
    send(op='place_npc', actor=name, x=100, y=1100, z=100, yaw=180)
    send(op='slot', value=2)
    place(0, -220, 1000, 100, yaw=20)
    place(1, -600, 1200, 100)
    time.sleep(.8)
    standing_head = vec(npc()['head'])[2]
    standing_foot = vec(npc()['footL'])[2]
    send(op='record', seconds=6)
    send(op='npc_crouch', actor=name, value=True)
    check('Crouch lowers head while maintaining the planted foot', lambda: npc()['crouched'] and vec(npc()['head'])[2] < standing_head - 25 and abs(vec(npc()['footL'])[2] - standing_foot) < 6)
    time.sleep(1.3)
    send(op='npc_crouch', actor=name, value=False)
    check('Standing pose restored smoothly', lambda: not npc()['crouched'] and abs(vec(npc()['head'])[2] - standing_head) < 4)
    wait(lambda: not state()['recording'], 10)
    captures.append(state()['captureDirectory'])
    send(op='slot', value=0)
    wait(lambda: held()['definition'] == 'DA_Shotgun')
    load()
    eye = vec(me()['camera'])
    target = vec(npc()['head'])
    delta = [target[i] - eye[i] for i in range(3)]
    send(op='look', yaw=math.degrees(math.atan2(delta[1], delta[0])), pitch=math.degrees(math.atan2(delta[2], math.hypot(*delta[:2]))))
    send(op='record', seconds=10)
    send(op='fire')
    check('Actual pellets kill the NPC', lambda: npc()['dead'])
    check('Remote receives authoritative falling pose', lambda: npc(1)['corpseFrameBones'] > 0)
    check('Corpse reaches supported rest on both peers', lambda: npc()['poseBones'] > 0 and npc(1)['poseBones'] > 0, 35)
    check('Corpse remains above the floor', lambda: vec(npc()['pelvis'])[2] > 3 and vec(npc()['pelvis'])[2] < 70)
    check('Final corpse pose agrees between peers', lambda: math.dist(vec(npc()['head']), vec(npc(1)['head'])) < 5)
    wait(lambda: not state()['recording'], 15)
    captures.append(state()['captureDirectory'])
    send(op='save')
finally:
    send(op='sample_interval', seconds=.1)
    report('phase6-combat-presentation', checks, captures=captures)
