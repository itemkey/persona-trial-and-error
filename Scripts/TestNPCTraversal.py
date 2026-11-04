"""Pursuit onto actual arena cover, interrupted traversal and 30-NPC load."""
from phase4_checks import *
import argparse
parser=argparse.ArgumentParser(); parser.add_argument('--quick',action='store_true'); args=parser.parse_args()

checks, captures = [], []


def check(name, predicate, seconds=20):
    try:
        wait(predicate, seconds)
    except Exception:
        checks.append(dict(check=name, passed=False, state=state()))
        raise
    checks.append(dict(check=name, passed=True))
    print('PASS', name, flush=True)


def npc(name, role=0):
    identity = next(c['persistentId'] for c in state()['characters'] if c['actor'] == name)
    return next(c for c in state(role)['characters'] if c['persistentId'] == identity)


try:
    check('Two peers ready for traversal', lambda: me(1).get('id', -1) >= 0, 300)
    send(op='close_loot')
    send(op='vitals', health=10000)
    if state()['run']['phase'] == 0:
        accept_trial()
        wait(lambda: state()['run']['loaded'], 60)
        send(op='board')
        time.sleep(.6)
        send(op='lift')
        wait(lambda: state()['run']['phase'] == 2, 40)
    check('Arena supplies navigation links', lambda: state()['climbLinks'] > 0, 30)
    enemies = [c['actor'] for c in state()['characters'] if c['npc'] and not c['dead']]
    for name in enemies:
        send(op='ai_pause', actor=name, paused=True)
    target = enemies[0]
    fixtures = [(height, x + dx, y + dy) for x,y in [(-900,-3300),(900,-3300),(-900,-1200),(900,-1200),(-900,1500),(900,1500)]
                for height,dx,dy in [(140,0,0),(220,160,160)]]
    if args.quick: fixtures=fixtures[:2]
    for index, (height, x, y) in enumerate(fixtures):
        send(op='ai_pause', actor=target, paused=True)
        send(op='place_npc', actor=target, x=x, y=y - 350, z=-12508, yaw=90)
        place(0, x, y, -12600 + height + 92, yaw=-90, pitch=-25)
        # Keep the observer away from the approach: a reachable second player
        # on the floor is a legitimate alternative target, not a climb failure.
        place(1, 0, 1100, 100, yaw=35)
        send(op='vitals', health=10000)
        time.sleep(.7)
        if index < 2 and not args.quick:
            send(op='record', seconds=12)
        health = me()['health']
        send(op='ai_pause', actor=target, paused=False)
        send(op='noise')
        label = f'cover {index // 2 + 1}, {height} cm'
        check(f'NPC starts traversal toward {label}', lambda: npc(target)['climbing'], 25)
        check(f'Peer receives traversal state at {label}', lambda: npc(target, 1)['climbing'], 3)
        check(f'NPC reaches {label}', lambda: vec(npc(target)['position'])[2] > -12600 + height + 70, 15)
        check(f'NPC damages player on {label}', lambda: me()['health'] < health, 20)
        if index < 2 and not args.quick:
            wait(lambda: not state()['recording'], 20)
            captures.append(state()['captureDirectory'])
    for role in (0,1):
        send(op='kill_player', player=state(role)['localPlayer'])
    wait(lambda:state()['run']['phase']==0,30)
    send(op='vitals',health=10000)
    send(op='npcs', value=30)
    check('Thirty NPCs remain responsive', lambda: sum(c['npc'] and not c['dead'] for c in state()['characters']) == 30, 20)
    start = state()['utc']
    time.sleep(6)
    check('Diagnostics and simulation advance under 30 NPC load', lambda: state()['utc'] != start)
finally:
    report('phase6-npc-traversal', checks, captures=captures)
