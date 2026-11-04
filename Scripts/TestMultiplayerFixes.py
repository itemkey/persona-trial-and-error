"""Two diagnostic peers; run after RunLocal.ps1 -Players 2 -Diagnostics.
Exercises real trial/loot RPCs and records observer movement, not just health.
Use -NetworkEmulation on the launcher for the lag/loss pass.
"""
import argparse
import datetime
import math
import shutil
import statistics
from phase4_checks import *

parser = argparse.ArgumentParser()
parser.add_argument('--report', default='multiplayer-fixes')
parser.add_argument('--movement-only', action='store_true')
args = parser.parse_args()
checks, metrics = [], {}


def check(name, predicate, seconds=12):
    try:
        wait(predicate, seconds)
    except Exception:
        checks.append(dict(check=name, passed=False, state=state(), client=state(1)))
        raise
    checks.append(dict(check=name, passed=True))
    print('PASS', name, flush=True)


def peers_alive():
    return all(all(not c['dead'] and c['health'] > 0 and c['meshAttached']
                   and not c['meshSimulating'] and c['corpsePoseBones'] == 0
                   and c['displayCorpseBones'] == 0
                   and abs(vec(c['position'])[2] - 92.15) < 5
                   and abs(vec(c['meshPosition'])[2] - vec(c['position'])[2] + 90) < 2
                   for c in state(peer)['characters'] if not c['npc'])
               for peer in (0, 1))


def movement(label):
    for crouch in (False, True):
        for role in (0, 1):
            place(role, -1100, 1100 + role * 300, 100)
            send(role, op='crouch', value=crouch)
        time.sleep(1)
        for role in (0, 1):
            send(role, op='move', x=1, y=0)
        expected = 280 if crouch else 600
        wait(lambda:all(me(i)['speed']>.8*expected for i in (0,1)),10)
        time.sleep(.3)
        samples = {0: {}, 1: {}}
        end = time.monotonic() + 1.5
        while time.monotonic() < end:
            for peer in (0, 1):
                s = state(peer)
                samples[peer][s['utc']] = s
            time.sleep(.02)
        for role in (0, 1):
            send(role, op='move', x=0, y=0)
        for peer in (0, 1):
            pid = me(1-peer)['persistentId']
            points = [(datetime.datetime.fromisoformat(t.replace('Z', '+00:00')).timestamp(),
                       next(c for c in s['characters'] if c['persistentId'] == pid))
                      for t, s in samples[peer].items()]
            speeds = [(vec(b[1]['meshPosition'])[0]-vec(a[1]['meshPosition'])[0])/(b[0]-a[0])
                      for a, b in zip(points, points[1:]) if b[0] > a[0]]
            expected = 280 if crouch else 600
            key = f'{label}: observer {peer}, crouch={crouch}'
            metrics[key] = dict(samples=len(points), median=statistics.median(speeds),
                                minimum=min(speeds), maximum=max(speeds),
                                capsule_displacement=vec(points[-1][1]['position'])[0]-vec(points[0][1]['position'])[0],
                                mesh_displacement=vec(points[-1][1]['meshPosition'])[0]-vec(points[0][1]['meshPosition'])[0])
            check(key+' moves continuously', lambda: len(points) > 10
                  and .65*expected < statistics.median(speeds) < 1.4*expected
                  and min(speeds) > -.4*expected and max(speeds) < 3*expected)
            height = 60 if crouch else 90
            check(key+' mesh follows capsule', lambda: all(
                abs(vec(c['meshPosition'])[2]-vec(c['position'])[2]+height) < 3
                for _, c in points))
    for role in (0, 1):
        send(role, op='crouch', value=False)


def capture_views(label):
    if not all(state(i)['gpuMs']>0 for i in (0,1)):return
    source=DIAG.parent.parent/'Artifacts/Previews'
    for peer in (0,1):
        started=time.time();send(peer,op='shot');path=source/f'Player{peer}_game.png'
        wait(lambda:path.exists() and path.stat().st_mtime>=started-1,10)
        shutil.copy2(path,ROOT/f'Artifacts/Previews/Multiplayer_{label}_{peer}.png')


def terminal(role):
    place(role, -300, -393+role*100, -400, yaw=180, pitch=-15)
    time.sleep(.7)


def accept(role=1):
    terminal(role)
    send(role, op='decision', accept=True)
    check('Client accepts Shooting', lambda: all(state(i)['run']['phase'] == 1 for i in (0, 1)))
    check('Both peers load selected arena', lambda: all(state(i)['run']['loaded']
          and state(i)['run']['trialId'] == 'Shooting' for i in (0, 1)), 60)


def ride(label, up=False):
    send(op='board')
    time.sleep(1)
    send(op='lift')
    check(label+' starts', lambda: all(me(i)['transit'] for i in (0, 1)))
    time.sleep(1)
    if label=='first descent':
        send(0,op='look',yaw=0,pitch=-15);send(1,op='look',yaw=180,pitch=-15)
        capture_views('Lift')
    offsets = {0: [], 1: []}
    end = time.monotonic()+4
    while time.monotonic() < end:
        for peer in (0, 1):
            s = state(peer)
            base = vec(next(h for h in s['hub'] if h['kind'] == 7)['position'])
            for c in s['characters']:
                if not c['npc']:
                    offsets[peer].append(vec(c['meshPosition'])[2]-base[2])
        time.sleep(.03)
    for peer, values in offsets.items():
        spread = max(values)-min(values)
        metrics[label+f' relative mesh spread peer {peer}'] = spread
        check(label+f' no lift jitter on peer {peer}', lambda: spread < 3)
    check(label+' arrives and releases players', lambda: all(not me(i)['transit'] for i in (0, 1)), 30)
    z = -407.85 if up else -12507.85
    check(label+' correct destination', lambda: all(abs(vec(me(i)['position'])[2]-z) < 8 for i in (0, 1)))


def wipe(label):
    for role in (0, 1):
        send(op='kill_player', player=state(role)['localPlayer'])
    check(label+' returns both living animated bodies', lambda: state()['run']['phase'] == 0 and peers_alive(), 25)


def open_crate(role, identity):
    """Open a physical container leaf before using its shared inventory."""
    h=next(h for h in state(role)['hub'] if h['id']==identity)
    centers=[vec(next(p for p in h['parts'] if p['mesh']==part['id'])['center']) for part in h['compartments']]
    origin=vec(h['position']);middle=[(centers[0][i]+centers[1][i])/2 for i in range(3)]
    normal=[middle[i]-origin[i] for i in range(2)];length=math.hypot(*normal);at=centers[0]
    place(role,at[0]+normal[0]/length*175,at[1]+normal[1]/length*175,origin[2]+100)
    time.sleep(.7)
    eye=vec(me(role)['camera']);delta=[at[i]-eye[i] for i in range(3)]
    send(role,op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))))
    time.sleep(.4);send(role,op='interact')
    wait(lambda:me(role)['lootPart']==h['compartments'][0]['id'])
    return next(h for h in state(role)['hub'] if h['id']==identity)


def loot(role, target, item):
    send(role, op='loot', actor=target['actor'], index=target['items'].index(item),
         deposit=False, expectedId=item['id'])


try:
    check('Two distinct connected players', lambda: me(1).get('persistentId')
          and me(1)['persistentId'] != me()['persistentId'], 180)
    for role in (0, 1):
        send(role, op='sample_interval', seconds=.02)
        send(role, op='console', value='t.MaxFPS 60')
    metrics['network']={str(i):{k:state(i).get(k) for k in ('packetLagMs','packetLossPercent')} for i in (0,1)}
    movement('fresh hub')
    if not args.movement_only:
        send(1, op='decision', accept=True)
        time.sleep(.7)
        check('Acceptance from outside terminal rejected', lambda: state()['run']['phase'] == 0)
        terminal(1)
        send(1, op='decision', accept=True, trialId='MissingTrial')
        time.sleep(.7)
        check('Unknown trial rejected', lambda: state()['run']['phase'] == 0)
        if state(1)['gpuMs']>0:
            send(1, op='terminal');time.sleep(.4);send(1, op='shot');time.sleep(1.5)
            image_path=DIAG.parent.parent/'Artifacts/Previews/Player1_game.png'
            if image_path.exists():shutil.copy2(image_path,ROOT/'Artifacts/Previews/Multiplayer_Terminal.png')
        accept()
        run_id=state()['run']['id']
        terminal(0)
        send(op='decision', accept=True)
        send(1, op='decision', accept=True)
        time.sleep(.5)
        check('Repeated acceptance does not reset run', lambda: state()['run']['phase'] == 1 and state()['run']['id']==run_id)
        check('Preview matches two-player supplies', lambda: '72' in state(1)['run']['details'])
        ride('first descent')
        crates = [h for h in state()['hub'] if 'Container_' in h['id']]
        check('Guaranteed container supply', lambda: sum(i['quantity'] for h in crates for i in h['items']
              if 'DA_Ammo12GA' in i['definition']) == 72 and sum(i['quantity'] for h in crates for i in h['items']
              if 'DA_Shotgun' in i['definition']) == 2)
        wipe('first wipe')
        time.sleep(2)  # Let streamed-level cleanup finish before measuring gait.
        place(0,-1100,1100,100,yaw=0);place(1,-750,1100,100,yaw=180)
        time.sleep(.5);capture_views('Revived')
        movement('after wipe')
        terminal(0);terminal(1)
        send(0,op='decision',accept=True);send(1,op='decision',accept=True)
        check('Simultaneous requests create one selected run',lambda:state()['run']['phase']==1
              and state()['run']['id']==state(1)['run']['id'] and state()['run']['trialId']=='Shooting')
        check('Both peers load arena after simultaneous acceptance',lambda:all(state(i)['run']['loaded'] for i in (0,1)),60)
        ride('second descent')
        # Also cover death while crouched, followed by a settled corpse.
        send(1,op='crouch',value=True);time.sleep(.5)
        send(op='kill_player', player=state(1)['localPlayer'])
        place(0, -2000, 2500, 100)
        check('Remote body reaches saved corpse pose', lambda: any(
            c['persistentId'] == me(1)['persistentId'] and c['corpsePoseBones'] > 0
            for c in state(1)['characters']), 45)
        send(op='kill_player', player=state()['localPlayer'])
        check('Settled corpse cleared on revive', lambda: state()['run']['phase'] == 0 and peers_alive(), 25)
        accept()
        ride('loot descent')
        for role in (0, 1):
            place(role, -2000+role*150, 2500, 100)
        end = time.monotonic()+150
        while state()['run']['phase'] != 4 and time.monotonic() < end:
            if state()['run']['phase'] == 2:
                # Let a wave enter the accessible arena before creating loot corpses.
                time.sleep(5)
                send(op='finish_wave')
            time.sleep(.5)
        check('All waves lead to evacuation', lambda: state()['run']['phase'] == 4)
        # Choose an isolated body; if bodies overlap, loot whichever the real trace opens.
        bodies=[c for c in state(1)['characters'] if c['npc'] and c['dead']]
        candidates=[c for c in bodies if c['lootItems'] and vec(c['pelvis'])[1]>-5400]
        candidates.sort(key=lambda c:min((math.dist(vec(c['pelvis']),vec(other['pelvis']))
                        for other in bodies if c['actor']!=other['actor']),default=1000),reverse=True)
        corpse=None
        for candidate in candidates[:4]:
            at=vec(candidate['pelvis']);place(1,at[0]-220,at[1],-12500);time.sleep(.8)
            eye=vec(me(1)['camera']);delta=[at[i]-eye[i] for i in range(3)]
            send(1,op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))))
            time.sleep(.4);send(1,op='interact');time.sleep(.7)
            opened=me(1)['lootTarget']
            corpse=next((c for c in state(1)['characters'] if c['actor']==opened and c['npc'] and c['lootItems']),None)
            if corpse:break
            send(1,op='close_loot')
        check('Client opens visible corpse containing ammunition',lambda:corpse is not None,1)
        item = corpse['lootItems'][0]
        send(1, op='loot', actor=corpse['actor'], index=0, deposit=False, expectedId=item['id'])
        check('Client takes ammunition from a real corpse', lambda: ammo(1) > 0,5)
        send(1,op='close_loot')
        corpse_ammo = ammo(1)
        crate = next(h for h in state(1)['hub'] if 'Container_' in h['id'] and any('DA_Shotgun' in i['definition'] for i in h['items']))
        crate = open_crate(1,crate['id'])
        loot(1, crate, next(i for i in crate['items'] if 'DA_Shotgun' in i['definition']))
        check('Client takes guaranteed shotgun', lambda: any(s['definition'] == 'DA_Shotgun' for s in me(1)['slots']))
        crate = next(h for h in state(1)['hub'] if h['id'] == crate['id'])
        loot(1, crate, next(i for i in crate['items'] if 'DA_Ammo12GA' in i['definition']))
        check('Client takes container ammunition', lambda: ammo(1) > corpse_ammo)
        inventory = [(s['definition'], s['quantity']) for s in me(1)['slots']]
        ride('evacuation', up=True)
        check('Extracted inventory retained without bonus', lambda: [(s['definition'], s['quantity']) for s in me(1)['slots']] == inventory
              and all(state()['run'][key] == 0 for key in ('respect', 'penalty', 'bonus')))
        check('Completed run closes', lambda: state()['run']['phase'] == 0, 60)
        send(op='save')
        time.sleep(.5)
        (ROOT/'.work/MultiplayerFix/expected_save.json').write_text(json.dumps(dict(
            sector=state()['sector'], player=me(1)['persistentId'], slots=me(1)['slots'],
            props=[p for p in state()['props'] if p['holder'] == state(1)['localPlayer']])))
finally:
    report(args.report, checks, metrics=metrics)
