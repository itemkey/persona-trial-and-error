"""Exercise E traces against authored hub props (fresh hub, no active trial)."""
import math
from phase4_checks import *
checks=[]
def hub(name):return next(h for h in state()['hub'] if h['id']==name)
def use(name,view,target,field):
    send(op='close_loot');place(0,*view)
    delta=[target[i]-view[i] for i in range(3)];delta[2]-=68
    send(op='look',yaw=math.degrees(math.atan2(delta[1],delta[0])),pitch=math.degrees(math.atan2(delta[2],math.hypot(*delta[:2]))))
    time.sleep(.6);before=hub(name)[field];send(op='interact')
    try:wait(lambda:hub(name)[field]!=before,5);ok=True
    except AssertionError:ok=False
    checks.append(dict(check=name+' E '+field,passed=ok));print('PASS' if ok else 'FAIL',name,field,flush=True)
try:
    send(op='slot',value=2)
    for i,x in enumerate([-480,-250,-20,210],1):use(f'HUB_Chest_0{i}',(x,-480,100),(x,-650,60),'open')
    use('HUB_Refrigerator',(915,340,100),(915,500,140),'open')
    use('HUB_Kitchen_WallCabinet',(515,120,100),(515,-55,210),'open')
    use('HUB_Kitchen_SinkUnit',(505,140,100),(505,-45,105),'powered')
    use('HUB_Kitchen_Extractor',(265,110,100),(265,-50,230),'powered')
    use('HUB_Stove_Oven',(265,130,100),(265,-20,55),'open')
    use('HUB_Stove_Oven',(265,130,100),(265,-45,103),'powered')
    use('HUB_BenchVice',(-470,130,100),(-470,-12,130),'open')
finally:send(op='close_loot');report('phase4-hub-interactions',checks)
