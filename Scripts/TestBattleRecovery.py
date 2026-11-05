"""Crash real isolated sector processes around the durable result protocol."""
import argparse,subprocess,sys,time,shutil
from pathlib import Path
from phase4_checks import ROOT,state,send,wait,me,report
p=argparse.ArgumentParser();p.add_argument('--phase',choices=['departure','fighting','vote','commit'],required=True);args=p.parse_args();checks=[]
shell=shutil.which('pwsh') or str(Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe')
def process(role,action):subprocess.run([shell,'-NoProfile','-File',str(ROOT/'Scripts/RestartBattleClient.ps1'),'-Role',str(role),'-Action',action],check=True)
def check(label,test,seconds=40):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=label,passed=False));raise
    checks.append(dict(check=label,passed=True));print('PASS',label,flush=True)
def qty(role):return sum(s['quantity'] for s in me(role)['slots'] if s['definition']=='DA_Scrap')
def start(role):process(role,'Start');check(f'Owner {role} restarts in menu',lambda:state(role)['map']=='L_MainMenu',100)
try:
    subprocess.run([sys.executable,str(ROOT/'Scripts/TestSectorBattle.py'),'--team-size','1','--stop-at','gathering' if args.phase=='departure' else 'fight'],check=True)
    sectors={i:state(i)['sector'] for i in (0,1)}
    if args.phase=='departure':
        # The second group has not requested departure yet: establish its normal
        # hub save before crashing both processes at the first escrow barrier.
        send(1,op='save');time.sleep(.5)
        for i in (0,1):send(i,op='battle',action='ready')
        check('Both groups ready before departure',lambda:all(state(i)['battle']['ready']==1 for i in (0,1)))
        send(op='board');time.sleep(.5);send(op='battle',action='lift')
        check('First sector durably escrows departure stakes',lambda:state()['battle']['phase']==3 and state()['battle']['journal']==0)
    elif args.phase!='fighting':
        send(op='battle',action='hold_commit' if args.phase=='vote' else 'hold_return',hold=True)
        enemy=next(c for c in state()['characters'] if c['persistentId']==me(1)['persistentId'])
        send(op='hit',actor=enemy['actor'],bone='spine_02',damage=10000)
        check('Team victory reaches loot phase',lambda:state()['battle']['phase']==8)
        send(op='place',player=me()['id'],x=-3100,y=0,z=95,yaw=0,pitch=0);time.sleep(.4);send(op='battle',action='lift')
        expected=2 if args.phase=='vote' else 3
        check('Both owners durably record '+args.phase,lambda:all(state(i)['battle']['journal']==expected for i in (0,1)),15)
    process(0,'Stop');process(1,'Stop')
    start(1);send(1,op='load_sector',slot=sectors[1])
    if args.phase=='vote':
        check('Voted owner waits for master decision',lambda:state(1)['map']=='L_MainMenu' and state(1)['battle']['phase']==11)
        time.sleep(2)
        check('Uncertain result grants no rollback or trophies',lambda:state(1)['map']=='L_MainMenu')
    else:check('Remote original sector recovers',lambda:state(1)['map']=='L_Hub',90)
    start(0);send(op='load_sector',slot=sectors[0])
    check('Both original hubs recover after crash',lambda:all(state(i)['map']=='L_Hub' and state(i)['sector']==sectors[i] for i in (0,1)),120)
    check('Recovery applies exactly the agreed inventory outcome',lambda:qty(0)==3 and qty(1)==(0 if args.phase=='commit' else 4))
    check('Stocks outside stakes remain intact',lambda:all(sum(p['quantity'] for p in state(i)['props'] if p['definition']=='DA_Scrap' and p['holder']<0)==2 for i in (0,1)))
    for i in (0,1):send(i,op='save')
    time.sleep(.5)
    for i in (0,1):process(i,'Restart');check('Repeated load starts in menu',lambda i=i:state(i)['map']=='L_MainMenu',90);send(i,op='load_sector',slot=sectors[i])
    check('Repeated load cannot duplicate or revert result',lambda:all(state(i)['map']=='L_Hub' for i in (0,1)) and qty(0)==3 and qty(1)==(0 if args.phase=='commit' else 4),120)
finally:report('v0110-recovery-'+args.phase,checks)
