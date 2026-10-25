"""Real inventory/loot RPCs on a fresh host and at least one local client."""
from phase4_checks import *
checks=[]
def check(name,test,seconds=20):
    try:wait(test,seconds)
    except Exception:checks.append(dict(check=name,passed=False));raise
    checks.append(dict(check=name,passed=True));print('PASS',name,flush=True)
def chest():return next(h for h in state()['hub'] if h['id']=='HUB_Chest_01')
def prop(role,slot):return next(p for p in state(role)['props'] if p['actor']==me(role)['slots'][slot]['actor'])
def transfer(role,index,deposit=False,item=None):
    send(role,op='loot',actor=chest()['actor'],index=index,deposit=deposit,
         expectedId=item or (prop(role,index)['itemId'] if deposit else chest()['items'][index]['id']))
try:
    check('Two peers in fresh hub',lambda:state(1)['map']=='L_Hub' and ammo()==0 and ammo(1)==0,150)
    place(0,-520,-490,100,-90,-20);place(1,-410,-490,100,-90,-20)
    send(op='fixture',definition='DA_Ammo12GA',quantity=15,store=True)
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',definition='DA_Shotgun',quantity=1,store=True)
    send(op='fixture',player=state(1)['localPlayer'],definition='DA_Ammo12GA',quantity=10,store=True)
    check('Hotbar full: one partial ammo stack and two non-stackable guns',lambda:ammo()==15 and sum(s['definition']=='DA_Shotgun' for s in me()['slots'])==2 and ammo(1)==10)
    transfer(1,0,True)
    check('Client deposits stack into hub storage',lambda:ammo(1)==0 and sum(i['quantity'] for i in chest()['items'])==10)
    item=chest()['items'][0]['id'];transfer(0,0,item=item)
    check('Full hotbar takes only five rounds; remainder stays in chest',lambda:ammo()==20 and sum(i['quantity'] for i in chest()['items'])==5)
    # Both peers request the same remaining record. The full host cannot take it;
    # the client removes it exactly once, including a duplicate stale request.
    transfer(0,0,item=item);transfer(1,0,item=item);transfer(1,0,item=item)
    check('Concurrent/stale loot requests conserve ammunition',lambda:ammo()==20 and ammo(1)==5 and not chest()['items'])
    transfer(1,0,True);check('Storage accepts returned loot',lambda:ammo(1)==0 and len(chest()['items'])==1)
    item=chest()['items'][0]['id'];place(1,0,1800,100);time.sleep(1);transfer(1,0,item=item);time.sleep(1)
    check('Server rejects remote looting',lambda:ammo(1)==0 and chest()['items'][0]['quantity']==5)
    place(1,-410,-490,100,-90,-20);time.sleep(1);transfer(1,0,item=item)
    check('Loot can be recovered when back in range',lambda:ammo(1)==5 and not chest()['items'])
    send(1,op='charge',held=True);send(1,op='slot',value=1);time.sleep(1)
    check('Slot change cancels charged throw',lambda:me(1)['throwAlpha']==0 and ammo(1)==5)
    send(1,op='slot',value=0);time.sleep(1);item=prop(1,0)['itemId']
    send(1,op='gesture',seconds=.08)
    check('Short G drops the entire stack',lambda:ammo(1)==0 and any(p['itemId']==item and p['holder']==-1 and p['quantity']==5 for p in state()['props']))
    place(1,-410,-250,100,0,0);send(op='fixture',player=state(1)['localPlayer'],definition='DA_Ammo12GA',quantity=9,store=True)
    check('Second throw stack equipped',lambda:ammo(1)==9)
    item=prop(1,0)['itemId'];send(1,op='charge',held=True)
    check('Held G exposes charge meter',lambda:me(1)['throwAlpha']>.25)
    time.sleep(1.3);send(1,op='charge',held=False)
    check('Long G throws the whole selected stack',lambda:ammo(1)==0 and any(p['itemId']==item and p['holder']==-1 and p['quantity']==9 for p in state()['props']))
    check('Empty selected slot hides first-person hands',lambda:not me(1)['armsVisible'])
finally:report('phase4-loot',checks)
