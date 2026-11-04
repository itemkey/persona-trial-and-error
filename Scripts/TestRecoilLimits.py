"""Recoil during aim/input/weapon changes and at the pitch limits."""
from phase4_checks import *
checks=[]

def check(name, predicate, seconds=10):
    try: wait(predicate, seconds)
    except Exception:
        checks.append(dict(check=name,passed=False,state=state())); raise
    checks.append(dict(check=name,passed=True)); print('PASS',name,flush=True)

def pitch(): return float(me()['view'].split('P=')[1].split()[0])

def chamber():
    if held()['chamber']==1: return
    send(op='reload',held=True); send(op='reload',held=False)
    wait(lambda:held()['tube']>0)
    send(op='pump'); wait(lambda:held()['chamber']==1)

try:
    send(op='npcs',value=0); send(op='clear_inventory')
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',definition='DA_Ammo12GA',quantity=24,store=True)
    wait(lambda:held()['definition']=='DA_Shotgun')
    place(0,-2200,-1400,100); place(1,-1700,-1000,100)
    send(op='sample_interval',seconds=.02)
    chamber(); send(op='aim',held=True); time.sleep(.6)
    send(op='look',yaw=0,pitch=0); time.sleep(.15); send(op='fire')
    samples=[]; end=time.monotonic()+.8
    while time.monotonic()<end: samples.append(pitch()); time.sleep(.02)
    check('ADS scales impulse and residual to 75 percent',lambda:max(samples)>5.5 and 2.7<samples[-1]<3.3)
    send(op='aim',held=False); time.sleep(.5)
    chamber(); send(op='look',yaw=0,pitch=0); time.sleep(.15); send(op='fire')
    time.sleep(.12); send(op='look',yaw=12,pitch=pitch()-4)
    time.sleep(.65)
    check('Aim compensation and lateral input survive recovery',lambda:abs(pitch())<.8 and float(me()['view'].split('Y=')[1].split()[0])>11)
    chamber(); send(op='look',yaw=0,pitch=87); time.sleep(.15); send(op='fire'); time.sleep(.8)
    check('Clipped recoil does not pull aim down from upper limit',lambda:87.8<pitch()<88.2)
    chamber(); send(op='look',yaw=0,pitch=-87); time.sleep(.15); send(op='fire'); time.sleep(.8)
    check('Lower limit preserves the upward residual',lambda:-83.3<pitch()<-82.7)
    chamber(); send(op='look',yaw=0,pitch=0); time.sleep(.15); send(op='fire'); send(op='slot',value=2); time.sleep(.8)
    check('Changing weapon during recovery preserves aim displacement',lambda:3.7<pitch()<4.3 and me()['held']=='None')
    send(op='slot',value=0); wait(lambda:held()['definition']=='DA_Shotgun'); chamber()
    place(0,0,0,30000,pitch=80); time.sleep(.2)
    before=(me()['wallImpacts'],me()['bodyImpacts']); send(op='fire'); time.sleep(.8)
    check('Empty sky produces no impact event',lambda:(me()['wallImpacts'],me()['bodyImpacts'])==before)
    last=me()['shotSound']
    for _ in range(3): send(op='fire'); time.sleep(.3)
    chamber(); send(op='fire'); time.sleep(.4)
    check('Empty trigger pulls cannot cause adjacent fire-sound repetition',lambda:me()['shotSound']!=last)
finally:
    send(op='aim',held=False); send(op='sample_interval',seconds=.1)
    place(0,-150,1100,100)
    report('phase6-recoil-limits',checks)
