"""Empty viewmodel stays hidden throughout player movement."""
from phase4_checks import *
checks=[]
def check(name,test):
    try:wait(test,10)
    except Exception:checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def hidden():return not me()['armsVisible'] and not me()['armsRendered']
try:
    send(op='slot',value=2);place(0,0,1100,100,yaw=0)
    check('Empty hands hidden at rest',hidden)
    send(op='move',x=1,y=0);check('Empty hands hidden while walking',lambda:hidden() and me()['speed']>100)
    send(op='sprint',value=True);check('Empty hands hidden while sprinting',lambda:hidden() and me()['speed']>500)
    send(op='move',x=0,y=0);send(op='sprint',value=False)
    send(op='jump');check('Empty hands hidden during jump',lambda:hidden() and me()['falling'])
    wait(lambda:not me()['falling']);send(op='crouch',value=True)
    check('Empty hands hidden while crouched',lambda:hidden() and me()['crouched'])
finally:
    send(op='move',x=0,y=0);send(op='crouch',value=False);send(op='slot',value=0)
    report('phase4-empty-hands',checks)
