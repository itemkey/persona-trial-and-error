"""Rendered solo purge and failure path; diagnostic damage advances the waves."""
import shutil
from phase4_checks import *
checks=[];shots=DIAG.parent.parent/'Artifacts/Previews'
def shot(name):
    src=shots/'Player0_game.png';before=src.stat().st_mtime_ns if src.exists() else 0
    send(op='shot');wait(lambda:src.exists() and src.stat().st_mtime_ns>before,15)
    out=ROOT/'Artifacts/Previews';out.mkdir(parents=True,exist_ok=True);shutil.copy2(src,out/name)
def check(name,test,seconds=30):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
try:
    send(op='slot',value=2);send(op='gesture',seconds=.05);time.sleep(.8)
    send(op='npcs',value=0);send(op='decision',accept=True);wait(lambda:state()['run']['loaded'],60)
    send(op='board');time.sleep(1);send(op='lift')
    for wave in range(4):
        wait(lambda:state()['run']['phase']==2 and state()['run']['wave']==wave,35)
        if wave==0:
            place(0,2200,-5500,-12500,yaw=-90);send(op='move',x=1,y=0);time.sleep(1.5);send(op='move',x=0,y=0)
            check('Open gate still blocks players from the NPC service area',lambda:-5882<vec(me()['position'])[1]<-5750,5)
            place(0,0,0,-12500)
        send(op='finish_wave');wait(lambda:state()['run']['phase'] in (3,4))
    place(0,1800,-3000,-12500,yaw=-83,pitch=0)
    shot('PERSONA_04_arena_before_smoke.png')
    check('Evacuation expires with subject still below',lambda:state()['run']['phase']==5,40)
    wait(lambda:state()['run']['remaining']<7)
    shot('PERSONA_04_arena_smoke.png')
    check('Smoke damages the subject',lambda:0<me()['health']<100)
    check('No survivor means failure and an empty return to hub',lambda:state()['run']['phase']==0 and me()['health']==100 and not state()['run']['rewarded'],20)
finally:send(op='move',x=0,y=0);report('phase4-smoke',checks)
