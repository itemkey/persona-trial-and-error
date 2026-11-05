"""Run after TestSectorBattle --team-size 2 --stop-at fight."""
import json,time,subprocess,shutil,math
from pathlib import Path
from phase4_checks import ROOT,state,send,wait,me,vec,report
checks=[];ids={i:me(i)['persistentId'] for i in range(4)}
def check(label,test,seconds=30):
    wait(test,seconds);checks.append(dict(check=label,passed=True));print('PASS',label,flush=True)
def body(i):return next(c for c in state()['characters'] if c['persistentId']==ids[i])
def at(i,x,y,z):send(op='place',player=body(i)['id'],x=x,y=y,z=z,yaw=0,pitch=0)
def hit(i,n):send(op='hit',actor=body(i)['actor'],bone='spine_02',damage=n)
def process(role,action):
    shell=shutil.which('pwsh') or str(Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
    subprocess.run([shell,'-NoProfile','-File',str(ROOT/'Scripts/RestartBattleClient.ps1'),'-Role',str(role),'-Action',action],check=True)
try:
    check('Battle ready for edge scenarios',lambda:state()['battle']['phase']==7)
    original=body(2)['actor'];health=body(2)['health'];items=[(s['definition'],s['quantity']) for s in body(2)['slots']]
    process(2,'Stop')
    check('Disconnected owner leaves one vulnerable body',lambda:body(2)['offline'],60)
    hit(2,7);check('Offline body takes real damage',lambda:body(2)['health']<health-6)
    damaged=body(2)['health'];process(2,'Start')
    check('Restarted client reaches menu',lambda:state(2)['map']=='L_MainMenu',90)
    send(2,op='connect',address='127.0.0.1:7777')
    check('Restarted sector owner rejoins same body',lambda:state(2)['map']=='L_SectorBattle' and me(2).get('persistentId')==ids[2] and body(2)['actor']==original and not body(2)['offline'],60)
    check('Reconnect preserves damage and inventory',lambda:abs(me(2)['health']-damaged)<.1 and [(s['definition'],s['quantity']) for s in me(2)['slots']]==items)
    check('Reconnect restores original escrow journal',lambda:state(2)['battle']['journal']==1 and state(2)['battle']['team']==1)
    hit(1,9);check('Friendly fire damages own team',lambda:body(1)['health']<=91)
    send(op='admin_give',item='/Game/Persona/Items/DA_Syringe.DA_Syringe',quantity=1);time.sleep(.4)
    check('World-changing administrator grants blocked in battle',lambda:all(s['definition']!='DA_Syringe' for s in me()['slots']))
    points=[(3990,0,95),(-3990,0,95),(1000,3990,95),(1000,-3990,95),(1000,1000,1990),(0,2200,-250)]
    for face,p in enumerate(points):
        at(1,*p);check(f'Lethal cube face {face+1}',lambda:body(1)['dead'],10)
        if face==0:check('Dead player camera follows its own teammate',lambda:math.dist(vec(state(1)['viewPosition']),vec(me()['camera']))<100)
        at(1,-3000,500,95);send(op='vitals',actor=body(1)['actor'],health=100)
        check('Boundary fixture restored',lambda:not body(1)['dead'])
    send(op='battle',action='shrink',elapsed=899);time.sleep(.25)
    check('No shrink before fifteen minutes',lambda:state()['battle']['halfExtent']>=3999)
    for i in range(4):at(i,-200+i*120,-1800,95)
    send(op='battle',action='shrink',elapsed=1050)
    check('Half-size at midpoint of five-minute shrink',lambda:1900<state()['battle']['halfExtent']<2010)
    send(op='battle',action='shrink',elapsed=1200)
    check('Simultaneous boundary wipe resolves draw',lambda:state()['battle']['winner']==-1 and state()['battle']['phase']>=8,8)
    check('Draw returns both groups to their own hubs',lambda:all(state(i)['map']=='L_Hub' for i in range(4)),150)
    check('Draw loses dead inventories and revives all participants',lambda:all(not me(i)['dead'] and all(s['quantity']==0 for s in me(i)['slots']) for i in range(4)))
finally:report('v0110-battle-edges',checks,ids=ids)
