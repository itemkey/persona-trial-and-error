"""Packaged regression: real Slate routing, gravity/landing and per-frame body motion."""
import argparse,json,math,time,statistics
from phase4_checks import ROOT,DIAG,state,me,vec,send,place,wait,report

p=argparse.ArgumentParser();p.add_argument('--scenario',choices=['ui','fall','crouch','lift'],required=True)
p.add_argument('--players',type=int,default=2);args=p.parse_args();checks=[];metrics={}
def check(label,condition,seconds=15):
    try:wait(condition,seconds)
    except Exception:checks.append(dict(check=label,passed=False));raise AssertionError(label) from None
    checks.append(dict(check=label,passed=True));print('PASS',label,flush=True)
def key(name,role=0,**kw):send(role,op='key',key=name,ui=True,**kw)
def raw(role=0,**kw):key('Invalid',role,code=192,character=1105,**kw)

def ui():
    key('Tab');time.sleep(.3);check('Idle Tab opens no overlay',lambda:not state()['panelOpen'] and not state()['terminalOpen'])
    raw(held=True);check('RU physical OEM key opens host panel',lambda:state()['panelOpen'])
    raw(held=True,repeat=True);time.sleep(.3);check('Repeat does not toggle admin',lambda:state()['panelOpen']);raw(held=False)
    send(op='ui_text',field='console',text='stat fps');time.sleep(.3);raw();check('OEM key closes focused console',lambda:not state()['panelOpen'])
    key('Tilde');check('English backtick opens same panel',lambda:state()['panelOpen']);key('Tilde');check('English backtick closes panel',lambda:not state()['panelOpen'])
    raw(1);time.sleep(.4);check('Client has no admin panel',lambda:not state(1)['panelOpen'])
    send(op='terminal');check('Terminal opens',lambda:state()['terminalOpen'])
    for query,count in [('дробовик',1),('патрон',2),('вода',1),('шприц',2),('яблоко',1),('несуществующее',0),('',2)]:
        send(op='ui_text',field='trial',text=query)
        check('Loot search '+repr(query),lambda count=count:len(state()['ui']['trials'])==count)
    key('Tab');check('Tab closes search-focused terminal',lambda:not state()['terminalOpen'])
    key('Escape');check('Esc menu opens',lambda:state()['pauseOpen']);raw();check('OEM key works from pause menu',lambda:state()['panelOpen'] and not state()['pauseOpen']);raw()

def fall():
    for height,expected in [(490,100),(500,100),(800,70),(1500,0)]:
        send(op='vitals',health=100);time.sleep(.25);place(0,-900,1100,height+92)
        time.sleep(2.5)
        check(f'Fall {height/100:g}m => {expected} HP',lambda expected=expected:abs(me()['health']-expected)<3)
    check('Lethal hub fall can be recovered',lambda:me()['dead'] and me()['dry'])

def crouch():
    for role in (0,1):place(role,-1000,700+role*400,100,yaw=0,pitch=0);send(role,op='crouch',value=True)
    time.sleep(1)
    for role in (0,1):send(role,op='motion_trace',players=True,seconds=14)
    send(op='record',seconds=14,fps=30)
    for role in (0,1):send(role,op='move',x=1,y=0)
    time.sleep(5)
    for role in (0,1):send(role,op='move',x=-1,y=0)
    time.sleep(5)
    for role in (0,1):send(role,op='move',x=0,y=1)
    time.sleep(2)
    for role in (0,1):send(role,op='move',x=0,y=0)
    time.sleep(3)
    for peer in (0,1):
        frames=json.loads((DIAG/f'Player{peer}_motion.json').read_text(encoding='utf-8-sig'))['frames']
        other=me(1-peer)['persistentId'];rows=[f for f in frames if f['id']==other and f['crouched']]
        relative=[[a-b for a,b in zip(vec(f['pelvis']),vec(f['mesh']))] for f in rows]
        distances=[math.hypot(v[0],v[1]) for v in relative]
        steps=[math.dist(a,b) for a,b in zip(relative,relative[1:])]
        metrics[f'observer{peer}']=dict(frames=len(rows),maxPelvisHorizontalCm=max(distances),maxPelvisFrameStepCm=max(steps))
        check(f'Observer {peer}: body remains inside capsule',lambda:max(distances)<40 and len(rows)>150)
        check(f'Observer {peer}: no loop displacement snap',lambda:max(steps)<12)
    # A stationary observer must actually see the remote body throughout.
    place(0,-700,-700,-400,yaw=45,pitch=-8);send(0,op='crouch',value=False)
    place(1,-200,-70,-400,yaw=0,pitch=0);time.sleep(.7)
    send(op='record',seconds=12,fps=30)
    for _ in range(6):
        send(1,op='move',x=1,y=0);time.sleep(.8)
        send(1,op='move',x=-1,y=0);time.sleep(.8)
    send(1,op='move',x=0,y=0);time.sleep(3)
    metrics['capture']=state()['captureDirectory']

def lift():
    for role in range(args.players):send(role,op='crouch',value=False)
    place(0,-300,-393,-400,yaw=180,pitch=-15);time.sleep(.7);send(op='decision',accept=True,trialId='SurvivalAgility')
    check('Forest ready',lambda:all(state(i)['run']['loaded'] and state(i)['run']['phase']==1 for i in range(args.players)),60)
    send(op='board');time.sleep(.8);send(op='lift');check('Descent starts',lambda:state()['run']['lift']==1)
    def relative(role):
        s=state(role);base=next(h for h in s['hub'] if h['kind']==7)
        return vec(me(role)['position'])[2]-vec(base['position'])[2]
    def jump(label,n):
        before=[relative(i) for i in range(args.players)]
        for role in range(args.players):send(role,op='key',key='SpaceBar',held=True)
        time.sleep(.25)
        check(label+' jump '+str(n)+' actually leaves platform',lambda:any(relative(i)>before[i]+20 for i in range(args.players)),1)
        for role in range(args.players):send(role,op='key',key='SpaceBar',held=False)
        time.sleep(1.25)
        check(label+' jump '+str(n)+' causes no damage',lambda:all(me(i)['health']==100 for i in range(args.players)),2)
    for n in range(8):
        jump('Descent',n)
    check('All arrive healthy',lambda:all(not me(i)['transit'] and me(i)['health']==100 for i in range(args.players)),30)
    send(op='forest_fixture',quiet=True)
    send(op='board');time.sleep(.5);send(op='lift');check('Ascent starts',lambda:state()['run']['lift']==3,8)
    for n in range(8):
        jump('Ascent',n)
    check('Ascent arrives healthy',lambda:all(not me(i)['transit'] and me(i)['health']==100 for i in range(args.players)),30)

try:
    check('Peers connected',lambda:len([c for c in state()['characters'] if not c['npc'] and c['id']>=0])==args.players,60)
    globals()[args.scenario]()
finally:report('v0102-'+args.scenario,checks,metrics=metrics)
